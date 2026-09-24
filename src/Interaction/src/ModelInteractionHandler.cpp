#include "ModelInteractionHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_PERSISTENT_INTERACTION_ACTION(
    ModelInteractionHandler,
    "interaction.model",
    "selection.pick",
    "selection.clear",
    "modelInteraction.suspendSelectionInput",
    "modelInteraction.resumeSelectionInput")

#include "AIDescriptorHelper.hpp"
#include "InteractionRuntime.hpp"
#include "PickService.hpp"
#include "SelectionBridge.hpp"

#include <ActorDB.hpp>
#include <DocumentManager.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <SelectionBoxWidgetDB.hpp>
#include <TransactionManager.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <WindowDB.hpp>

#include "Foundation/Log.h"

#include <QGuiApplication>
#include <QStyleHints>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kRayEpsilon = 1e-6f;
constexpr float kPositionEpsilon = 1e-5f;
constexpr float kSnapStepMillimeters = 1.0f;
constexpr int kNewSelectionDragThreshold = 5;

bool nearlyEqual(float lhs, float rhs) {
    return std::abs(lhs - rhs) <= kPositionEpsilon;
}

bool directDragModifierAllowed(Qt::KeyboardModifiers modifiers) {
    constexpr auto disallowed = Qt::ControlModifier |
        Qt::AltModifier | Qt::MetaModifier;
    return !(modifiers & disallowed);
}

int dragThreshold() {
    return QGuiApplication::styleHints()
        ? QGuiApplication::styleHints()->startDragDistance()
        : 10;
}

bool containsId(const std::vector<DBInstanceID>& ids, DBInstanceID id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::vector<DBInstanceID> parseModelIds(const QVariantMap& params) {
    std::vector<DBInstanceID> ids;
    const auto append = [&ids](int rawId) {
        if (rawId > 0) {
            ids.emplace_back(rawId);
        }
    };
    append(params.value("modelId", params.value("dbId", 0)).toInt());
    const auto appendList = [&append](const QVariantList& values) {
        for (const auto& value : values) {
            append(value.toInt());
        }
    };
    appendList(params.value("modelIds").toList());
    appendList(params.value("dbIds").toList());
    std::sort(ids.begin(), ids.end(), [](DBInstanceID lhs, DBInstanceID rhs) {
        return lhs.getValue() < rhs.getValue();
    });
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

ModelInteractionHandler::ModelInteractionHandler(QObject* parent)
    : IActionHandlerBase(parent) {}

ModelInteractionHandler::~ModelInteractionHandler() {
    finishDrag(false);
}

bool ModelInteractionHandler::supportsEnvironment(
    const QString& environment) const {
    return environment == QStringLiteral("normal") ||
           environment == QStringLiteral("editing");
}

void ModelInteractionHandler::onEnter(
    std::shared_ptr<ActionContext> context) {
    setActive(true);
    restoreSelectionVisuals();
    handleAction(context);
}

void ModelInteractionHandler::onEnterForAI(
    std::shared_ptr<ActionContext> context) {
    setActive(true);
    restoreSelectionVisuals();
    handleAction(context);
}

void ModelInteractionHandler::handleAction(
    const std::shared_ptr<ActionContext>& context) {
    if (!context || context->getActionCode() == "interaction.model") {
        LOG_INFO("ModelInteractionHandler activated");
        return;
    }

    const QString actionCode = context->getActionCode();
    if (actionCode == "modelInteraction.suspendSelectionInput") {
        m_selectionInputSuspended = true;
        finishDrag(false);
        return;
    }
    if (actionCode == "modelInteraction.resumeSelectionInput") {
        m_selectionInputSuspended = false;
        return;
    }
    if (actionCode == "selection.clear") {
        clearSelection();
        context->setResult(QVariantMap{{"cleared", true}});
        return;
    }
    if (actionCode == "selection.pick") {
        executeSelectionAction(context);
    }
}

void ModelInteractionHandler::executeSelectionAction(
    const std::shared_ptr<ActionContext>& context) {
    const auto requestedIds = parseModelIds(context->getParams());
    if (requestedIds.empty()) {
        context->setError(ActionErrorCode::TargetRequired,
                          "selection.pick requires modelId/dbId/modelIds");
        return;
    }

    auto* document = DocumentManager::instance();
    if (!document) {
        context->setError(ActionErrorCode::SystemUnavailable,
                          "DocumentManager unavailable");
        return;
    }

    std::vector<DBInstanceID> validIds;
    validIds.reserve(requestedIds.size());
    for (const auto id : requestedIds) {
        if (std::dynamic_pointer_cast<ActorDB>(document->getDBInstance(id))) {
            validIds.push_back(id);
        }
    }
    if (validIds.empty()) {
        context->setError(ActionErrorCode::TargetNotFound,
                          "No valid ActorDB model found for selection");
        return;
    }

    const QVariantMap params = context->getParams();
    const bool additive = params.value("addToSelection", false).toBool();
    const bool keepExisting = params.value("keepExisting", additive).toBool();
    std::vector<DBInstanceID> selection = keepExisting
        ? SelectionBridge::instance()->getSelectedIds()
        : std::vector<DBInstanceID>{};
    for (const auto id : validIds) {
        if (!containsId(selection, id)) {
            selection.push_back(id);
        }
    }
    applySelection(selection);

    QVariantList selectedIds;
    for (const auto id : selection) {
        selectedIds.push_back(id.getValue());
    }
    context->setResult(QVariantMap{
        {"selectedCount", static_cast<int>(selection.size())},
        {"selectedIds", selectedIds}});
}

const QHash<QString, QVariantMap>&
ModelInteractionHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"selection.pick", makeDescriptor(
            "selection.pick", "Select Model",
            "Select one or more models by db id. Supports replace and additive modes.",
            "write",
            {{"modelId", "number", false},
             {"dbId", "number", false},
             {"modelIds", "array", false},
             {"dbIds", "array", false},
             {"addToSelection", "bool", false},
             {"keepExisting", "bool", false}},
            {"selection", "scene"}, false)},
        {"selection.clear", makeDescriptor(
            "selection.clear", "Clear Selection",
            "Clear all currently selected models.", "write", {},
            {"selection", "scene"})}};
    return table;
}

std::optional<QVariantMap> ModelInteractionHandler::getAIDescriptor(
    const QString& actionCode) const {
    const auto descriptor = aiDescriptorTable().find(actionCode);
    return descriptor != aiDescriptorTable().end()
        ? std::make_optional(*descriptor)
        : std::nullopt;
}

void ModelInteractionHandler::onExit() {
    finishDrag(false);
    m_blankClick.reset();
    hideSelectionVisuals();
    m_selectionInputSuspended = false;
    setActive(false);
    LOG_INFO("ModelInteractionHandler deactivated");
}

void ModelInteractionHandler::onSuspend() {
    finishDrag(false);
    m_blankClick.reset();
    hideSelectionVisuals();
    LOG_DEBUG("ModelInteractionHandler suspended by environment");
}

void ModelInteractionHandler::onResume() {
    m_blankClick.reset();
    restoreSelectionVisuals();
    LOG_DEBUG("ModelInteractionHandler resumed by environment");
}

bool ModelInteractionHandler::onMousePressEvent(QMouseEvent* event) {
    if (!isActive() || !event || event->button() != Qt::LeftButton ||
        m_drag.armed || m_selectionInputSuspended) {
        return false;
    }

    m_blankClick.reset();
    const DBInstanceID viewId(inputViewId());
    if (viewId == INVALID_DB_ID) {
        return false;
    }

    const QPoint pressPosition = event->position().toPoint();
    const auto pick = interactionPickService().pickCpuForView(
        viewId, pressPosition, GPlatform::Rendering::PickDetail::Object);
    if (pick.status == GPlatform::Rendering::PickStatus::Miss) {
        if (!SelectionBridge::instance()->getSelectedIds().empty() &&
            event->modifiers() == Qt::NoModifier) {
            m_blankClick = BlankClickCandidate{viewId, pressPosition};
        }
        return false;
    }
    if (pick.status != GPlatform::Rendering::PickStatus::Hit ||
        pick.payload.subTarget.has_value() ||
        !pick.payload.objectId.isValid()) {
        return false;
    }

    auto* document = DocumentManager::instance();
    auto actor = document
        ? std::dynamic_pointer_cast<ActorDB>(
              document->getDBInstance(pick.payload.objectId))
        : nullptr;
    if (!actor || !actor->isVisible() || !actor->isPickable()) {
        return false;
    }

    return beginModelGesture(
        actor, viewId, pressPosition, event->modifiers());
}

bool ModelInteractionHandler::beginModelGesture(
    const std::shared_ptr<ActorDB>& actor,
    DBInstanceID viewId,
    const QPoint& pressPosition,
    Qt::KeyboardModifiers modifiers) {
    if (!actor || !actor->isVisible() || !actor->isPickable()) {
        return false;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        selectObject(actor->getDBInstanceID(), true);
        return true;
    }
    if (!directDragModifierAllowed(modifiers)) {
        return false;
    }

    const bool alreadySelected = containsId(
        SelectionBridge::instance()->getSelectedIds(),
        actor->getDBInstanceID());
    selectObject(actor->getDBInstanceID(), false);
    if (!actor->isDragable()) {
        return true;
    }

    m_drag.armed = true;
    m_drag.viewId = viewId;
    m_drag.actorId = actor->getDBInstanceID();
    m_drag.pressPosition = pressPosition;
    m_drag.latestPosition = pressPosition;
    m_drag.latestModifiers = modifiers;
    m_drag.initialActorPosition = actor->getPosition();
    m_drag.requiresThreshold = !alreadySelected;
    const auto worldBounds = actor->worldBounds();
    m_drag.anchorWorldPosition = worldBounds.valid
        ? worldBounds.getCenter()
        : m_drag.initialActorPosition;
    const auto anchor = intersectDragPlane(pressPosition);
    if (!anchor) {
        m_drag = DragState{};
        return true;
    }
    m_drag.anchorWorldPosition = *anchor;

    LOG_DEBUG("ModelInteractionHandler armed actor={} View={} threshold={}",
              m_drag.actorId.getValue(), m_drag.viewId.getValue(),
              m_drag.requiresThreshold ? kNewSelectionDragThreshold : 0);
    return true;
}

bool ModelInteractionHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!event) {
        return false;
    }

    if (m_blankClick) {
        const bool sameView = DBInstanceID(inputViewId()) == m_blankClick->viewId;
        const int distance =
            (event->position().toPoint() - m_blankClick->pressPosition)
                .manhattanLength();
        if (!sameView || !(event->buttons() & Qt::LeftButton) ||
            distance >= dragThreshold()) {
            m_blankClick.reset();
        }
    }

    if (!m_drag.armed) {
        return false;
    }
    if (DBInstanceID(inputViewId()) != m_drag.viewId ||
        !(event->buttons() & Qt::LeftButton)) {
        finishDrag(false);
        return true;
    }

    m_drag.latestPosition = event->position().toPoint();
    m_drag.latestModifiers = event->modifiers();
    updateDrag(m_drag.latestPosition, m_drag.latestModifiers);
    return true;
}

bool ModelInteractionHandler::onMouseReleaseEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }

    if (m_drag.armed) {
        const bool sameView = DBInstanceID(inputViewId()) == m_drag.viewId;
        if (sameView) {
            m_drag.latestPosition = event->position().toPoint();
            m_drag.latestModifiers = event->modifiers();
            updateDrag(m_drag.latestPosition, m_drag.latestModifiers);
        }
        finishDrag(sameView);
        return true;
    }

    if (m_blankClick) {
        const bool clear = DBInstanceID(inputViewId()) == m_blankClick->viewId &&
            (event->position().toPoint() - m_blankClick->pressPosition)
                    .manhattanLength() < dragThreshold();
        m_blankClick.reset();
        if (clear) {
            clearSelection();
        }
    }

    return false;
}

bool ModelInteractionHandler::onKeyPressEvent(QKeyEvent* event) {
    if (!isActive() || !event || event->key() != Qt::Key_Escape) {
        return false;
    }
    if (m_drag.armed) {
        finishDrag(false);
    } else {
        clearSelection();
    }
    return true;
}

void ModelInteractionHandler::selectObject(DBInstanceID id, bool additive) {
    std::vector<DBInstanceID> selected =
        SelectionBridge::instance()->getSelectedIds();
    const auto existing = std::find(selected.begin(), selected.end(), id);
    if (additive) {
        if (existing == selected.end()) {
            selected.push_back(id);
        } else {
            selected.erase(existing);
        }
    } else if (selected.size() != 1 || existing == selected.end()) {
        selected = {id};
    }
    applySelection(selected);
}

void ModelInteractionHandler::applySelection(
    const std::vector<DBInstanceID>& selectedIds) {
    auto* bridge = SelectionBridge::instance();
    const std::vector<DBInstanceID> previous = bridge->getSelectedIds();
    if (previous == selectedIds) {
        return;
    }
    for (const auto id : previous) {
        if (!containsId(selectedIds, id)) {
            clearSelectionVisual(id);
        }
    }
    for (const auto id : selectedIds) {
        if (!containsId(previous, id)) {
            applySelectionVisual(id);
        }
    }
    bridge->setSelectedObjects(selectedIds);
}

void ModelInteractionHandler::clearSelection() {
    auto* bridge = SelectionBridge::instance();
    const std::vector<DBInstanceID> selected = bridge->getSelectedIds();
    for (const auto id : selected) {
        clearSelectionVisual(id);
    }
    bridge->clearSelection();
}

void ModelInteractionHandler::hideSelectionVisuals() {
    const auto selected = SelectionBridge::instance()->getSelectedIds();
    for (const auto id : selected) {
        clearSelectionVisual(id);
    }
}

void ModelInteractionHandler::restoreSelectionVisuals() {
    const auto selected = SelectionBridge::instance()->getSelectedIds();
    for (const auto id : selected) {
        applySelectionVisual(id);
    }
}

void ModelInteractionHandler::applySelectionVisual(DBInstanceID id) {
    createSelectionBox(id);
}

void ModelInteractionHandler::clearSelectionVisual(DBInstanceID id) {
    destroySelectionBox(id);
}

void ModelInteractionHandler::createSelectionBox(DBInstanceID actorId) {
    if (!actorId.isValid() || m_selectionBoxes.count(actorId) != 0) {
        return;
    }
    TransientUpdateGuard transient;
    const auto widget = trans::TransDB::create<SelectionBoxWidgetDB>(actorId);
    if (widget) {
        m_selectionBoxes[actorId] = widget->getDBInstanceID();
    }
}

void ModelInteractionHandler::destroySelectionBox(DBInstanceID actorId) {
    const auto box = m_selectionBoxes.find(actorId);
    if (box == m_selectionBoxes.end()) {
        return;
    }
    if (auto* document = DocumentManager::instance();
        document && document->getDBInstance(box->second)) {
        TransientUpdateGuard transient;
        document->unregisterDBInstance(box->second);
    }
    m_selectionBoxes.erase(box);
}

void ModelInteractionHandler::updateDrag(
    const QPoint& screenPosition,
    Qt::KeyboardModifiers modifiers) {
    if (!m_drag.armed) {
        return;
    }
    const int distance =
        (screenPosition - m_drag.pressPosition).manhattanLength();
    const int threshold = m_drag.requiresThreshold
        ? kNewSelectionDragThreshold : 0;
    if (!m_drag.transaction && distance < threshold) {
        return;
    }

    auto actor = resolveDraggedActor();
    if (!actor || !draggedActorIsStillSelected()) {
        finishDrag(false);
        return;
    }

    const auto intersection = intersectDragPlane(screenPosition);
    if (!intersection) {
        return;
    }
    Vector3 delta = *intersection - m_drag.anchorWorldPosition;
    delta.z = 0.0f;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        delta.x = kSnapStepMillimeters *
            std::round(delta.x / kSnapStepMillimeters);
        delta.y = kSnapStepMillimeters *
            std::round(delta.y / kSnapStepMillimeters);
    }

    const Vector3 newPosition(
        m_drag.initialActorPosition.x + delta.x,
        m_drag.initialActorPosition.y + delta.y,
        m_drag.initialActorPosition.z);
    const Vector3 current = actor->getPosition();
    if (nearlyEqual(current.x, newPosition.x) &&
        nearlyEqual(current.y, newPosition.y) &&
        nearlyEqual(current.z, newPosition.z)) {
        return;
    }

    if (!m_drag.transaction) {
        auto& transactions = TransactionManager::instance();
        if (transactions.isInTransaction()) {
            LOG_WARN("ModelInteractionHandler refused to replace an active transaction");
            finishDrag(false);
            return;
        }
        m_drag.transaction =
            std::make_unique<TransactionGuard>("Direct Model Translation");
        if (!transactions.isInTransaction()) {
            m_drag.transaction.reset();
            LOG_ERROR("ModelInteractionHandler could not start a transaction");
            finishDrag(false);
            return;
        }
    }
    ImmediateNotifyGuard notifyImmediately;
    actor->setPosition(newPosition);
}

std::optional<Vector3> ModelInteractionHandler::intersectDragPlane(
    const QPoint& screenPosition) const {
    auto* document = DocumentManager::instance();
    auto window = document
        ? std::dynamic_pointer_cast<WindowDB>(
              document->getDBInstance(m_drag.viewId))
        : nullptr;
    auto coordinates = window ? window->getCoordinateSystem() : nullptr;
    if (!coordinates) {
        return std::nullopt;
    }
    const WorldRay ray = coordinates->rayFromScreen(QPointF(screenPosition));
    if (std::abs(ray.direction.z) <= kRayEpsilon) {
        return std::nullopt;
    }
    const float distance =
        (m_drag.anchorWorldPosition.z - ray.origin.z) / ray.direction.z;
    if (distance < 0.0f) {
        return std::nullopt;
    }
    return ray.origin + ray.direction * distance;
}

std::shared_ptr<ActorDB> ModelInteractionHandler::resolveDraggedActor() const {
    auto* document = DocumentManager::instance();
    return document
        ? std::dynamic_pointer_cast<ActorDB>(
              document->getDBInstance(m_drag.actorId))
        : nullptr;
}

bool ModelInteractionHandler::draggedActorIsStillSelected() const {
    const auto& selected = SelectionBridge::instance()->getSelectedIds();
    return selected.size() == 1 && selected.front() == m_drag.actorId;
}

void ModelInteractionHandler::finishDrag(bool commit) {
    if (!m_drag.armed) {
        return;
    }
    if (m_drag.transaction) {
        if (commit) {
            m_drag.transaction->commit();
        } else {
            m_drag.transaction->rollback();
        }
    }
    m_drag = DragState{};
}
