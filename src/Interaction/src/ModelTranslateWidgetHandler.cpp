#include "ModelTranslateWidgetHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    ModelTranslateWidgetHandler, "model.translate", "model.remove_translate_widget")
#include "InteractionRuntime.hpp"
#include "PickService.hpp"
#include "ActionHandlerRegistry.hpp"
#include "ActionContext.hpp"
#include "ActionManager.hpp"
#include "DocumentManager.hpp"
#include "WindowDBManager.hpp"
#include "SelectionBridge.hpp"
#include "TransactionManager.hpp"
#include "WindowDB.hpp"
#include "ViewportCoordinateSystem.hpp"
#include "AIDescriptorHelper.hpp"
#include "Foundation/Log.h"
#include <ModelTranslateWidgetDB.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkRenderWindow.h>
#include <vtkMatrix4x4.h>
#include <QVariantList>
#include <QGuiApplication>
#include <QCursor>
#include <algorithm>
#include <cmath>

// Using declaration for easier access to TranslateMode enum
using TranslateMode = ModelTranslateWidgetDB::TranslateMode;

ModelTranslateWidgetHandler::ModelTranslateWidgetHandler(QObject* parent)
    : StandardActionHandler(parent) {
    auto& picks = interactionPickService();
    connect(&picks, &PickService::pickCompleted,
            this,
            [this](const PickSnapshot& snapshot) {
                if (!isActive() || m_dragState.isDragging ||
                    snapshot.channel != PickChannel::Hover ||
                    snapshot.viewId != m_interactionViewId ||
                    (snapshot.status !=
                         GPlatform::Rendering::PickStatus::Hit &&
                     snapshot.status !=
                         GPlatform::Rendering::PickStatus::Miss)) {
                    return;
                }
                updateHoverCursor(snapshot.result);
            },
            Qt::AutoConnection);
    LOG_DEBUG("ModelTranslateWidgetHandler::ModelTranslateWidgetHandler - Constructor");
}

ModelTranslateWidgetHandler::~ModelTranslateWidgetHandler() {
    LOG_DEBUG("ModelTranslateWidgetHandler::~ModelTranslateWidgetHandler - Destructor");
}

void ModelTranslateWidgetHandler::onEnter(std::shared_ptr<ActionContext> context) {
    LOG_INFO("🚀 ModelTranslateWidgetHandler::onEnter - Entering translate mode");

    const QString actionCode = context ? context->getActionCode() : QString();
    if (actionCode == "model.remove_translate_widget") {
        deleteModelTranslateWidget();
        return;
    }

    if (!context) {
        LOG_ERROR("ModelTranslateWidgetHandler::onEnter - Missing action context");
        return;
    }

    // 获取当前选中的Actor
    // 从 context 中获取模型 ID
    const QVariantMap params = context->getParams();
    const int modelId = params.contains("modelId") ? params.value("modelId").toInt()
        : SelectionBridge::instance()->getSelectedId();

    if (modelId <= 0) {
        context->setError(ActionErrorCode::InvalidParams, "model.translate requires a modelId or selected model");
        LOG_ERROR("ModelTranslateWidgetHandler::onEnter - Invalid model ID: {}", modelId);
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager || !std::dynamic_pointer_cast<ActorDB>(
                           docManager->getDBInstance(DBInstanceID(modelId)))) {
        LOG_ERROR("ModelTranslateWidgetHandler::onEnter - Actor {} is not available", modelId);
        context->setError(ActionErrorCode::TargetNotFound, "Selected model is unavailable");
        return;
    }

    StandardActionHandler::onEnter(context);
    m_activeModelId = DBInstanceID(modelId);
    LOG_DEBUG("ModelTranslateWidgetHandler::onEnter - Active model ID: {}", m_activeModelId.getValue());

    if (auto* actionManager = ActionManager::getInstance()) {
        actionManager->triggerAction(QStringLiteral("modelInteraction.suspendSelectionInput"));
    }

    // 创建或获取Widget
    if (!createOrGetWidget()) {
        if (auto* actionManager = ActionManager::getInstance();
            actionManager && actionManager->isAcceptingActions()) {
            actionManager->triggerAction(QStringLiteral("modelInteraction.resumeSelectionInput"));
        }
        m_activeModelId = DBInstanceID();
        StandardActionHandler::onExit();
        return;
    }

    LOG_DEBUG("ModelTranslateWidgetHandler::onEnter - Translation mode activated");
}

void ModelTranslateWidgetHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    bool ok = false;
    const QString actionCode = getActionCode();
    if (actionCode == "model.translate") {
        ok = executeAITranslation(context);
    } else {
        if (context) {
            context->setError(ActionErrorCode::InvalidParams,
                              QString("AI invoke not supported for action '%1'").arg(actionCode));
        }
        LOG_WARN("ModelTranslateWidgetHandler AI invoke rejected for action '{}'",
                 actionCode.toStdString());
    }
    if (!ok && context && !context->hasError()) {
        context->setError(ActionErrorCode::Internal, "AI translation failed");
    }

    // AI one-shot call should not keep this persistent handler active.
    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& ModelTranslateWidgetHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"model.translate", makeDescriptor(
            "model.translate",
            "Translate Model",
            "Translate selected or specified models by dx/dy/dz in millimeters.",
            "write",
            {
                {"dx", "number", false},
                {"dy", "number", false},
                {"dz", "number", false},
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false}
            },
            {"transform", "translate", "scene"}
        )}
    };
    return table;
}

bool ModelTranslateWidgetHandler::executeAITranslation(std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("ModelTranslateWidgetHandler::executeAITranslation - null context");
        return false;
    }

    const QVariantMap params = context->getParams();
    const double dx = params.value("dx", 0.0).toDouble();
    const double dy = params.value("dy", 0.0).toDouble();
    const double dz = params.value("dz", 0.0).toDouble();
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9 && std::abs(dz) < 1e-9) {
        context->setError(ActionErrorCode::InvalidParams, "model.translate requires non-zero dx/dy/dz");
        LOG_WARN("AI model.translate rejected: zero delta");
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        context->setError(ActionErrorCode::SystemUnavailable, "DocumentManager not available");
        LOG_ERROR("AI model.translate failed: DocumentManager unavailable");
        return false;
    }

    std::vector<DBInstanceID> targetIds;
    const auto appendId = [&targetIds](int id) {
        if (id > 0) {
            targetIds.emplace_back(id);
        }
    };
    appendId(params.value("modelId", params.value("dbId", 0)).toInt());
    for (const auto& value : params.value("modelIds").toList()) {
        appendId(value.toInt());
    }
    for (const auto& value : params.value("dbIds").toList()) {
        appendId(value.toInt());
    }

    if (targetIds.empty()) {
        if (auto* selectionBridge = SelectionBridge::instance()) {
            targetIds = selectionBridge->getSelectedIds();
        }
    }
    if (targetIds.empty()) {
        context->setError(ActionErrorCode::TargetRequired, "No target model ids and no current selection");
        LOG_WARN("AI model.translate failed: empty target ids and selection");
        return false;
    }

    std::sort(targetIds.begin(), targetIds.end(),
              [](const DBInstanceID& a, const DBInstanceID& b) {
                  return a.getValue() < b.getValue();
              });
    targetIds.erase(std::unique(targetIds.begin(), targetIds.end(),
                                [](const DBInstanceID& a, const DBInstanceID& b) {
                                    return a.getValue() == b.getValue();
                                }),
                    targetIds.end());

    int movedCount = 0;
    QVariantList movedIds;
    QVariantList targetResults;
    try {
        TransactionGuard guard("AI Translate Selected Models");
        for (const auto& targetId : targetIds) {
            auto dbObject = docManager->getDBInstance(targetId);
            auto actor = std::dynamic_pointer_cast<ActorDB>(dbObject);
            if (!actor) {
                LOG_WARN("AI model.translate skipped non-actor id={}", targetId.getValue());
                continue;
            }

            const Vector3 oldPos = actor->getPosition();
            const Vector3 newPos(oldPos.x + static_cast<float>(dx),
                                 oldPos.y + static_cast<float>(dy),
                                 oldPos.z + static_cast<float>(dz));
            actor->setPosition(newPos);
            ++movedCount;
            movedIds.push_back(targetId.getValue());
            targetResults.push_back(QVariantMap{
                {"modelId", targetId.getValue()},
                {"before", QVariantMap{
                               {"x", static_cast<double>(oldPos.x)},
                               {"y", static_cast<double>(oldPos.y)},
                               {"z", static_cast<double>(oldPos.z)}
                           }},
                {"after", QVariantMap{
                              {"x", static_cast<double>(newPos.x)},
                              {"y", static_cast<double>(newPos.y)},
                              {"z", static_cast<double>(newPos.z)}
                          }},
                {"delta", QVariantMap{
                              {"x", static_cast<double>(newPos.x - oldPos.x)},
                              {"y", static_cast<double>(newPos.y - oldPos.y)},
                              {"z", static_cast<double>(newPos.z - oldPos.z)}
                          }}
            });
        }
        guard.commit();
    } catch (const std::exception& e) {
        context->setError(ActionErrorCode::Internal, QString("Translation transaction failed: %1").arg(e.what()));
        LOG_ERROR("AI model.translate exception: {}", e.what());
        return false;
    }

    if (movedCount <= 0) {
        context->setError(ActionErrorCode::TargetNotFound, "No valid ActorDB targets moved");
        return false;
    }

    context->setResult(QVariantMap{
        {"movedCount", movedCount},
        {"movedIds", movedIds},
        {"targetResults", targetResults},
        {"dx", dx},
        {"dy", dy},
        {"dz", dz}
    });
    LOG_INFO("AI model.translate completed: moved={} delta=({:.3f},{:.3f},{:.3f})",
             movedCount, dx, dy, dz);
    return true;
}

void ModelTranslateWidgetHandler::onExit() {
    LOG_INFO("🚪 ModelTranslateWidgetHandler::onExit - Exiting translate mode");

    // 如果正在拖拽，结束拖拽
    if (m_dragState.isDragging) {
        endDrag();
    }
    clearTranslateCursor();

    // 删除Widget
    deleteModelTranslateWidget();

    if (auto* actionManager = ActionManager::getInstance(); actionManager && actionManager->isAcceptingActions()) {
        actionManager->triggerAction(QStringLiteral("modelInteraction.resumeSelectionInput"));
    }

    // 重置状态
    m_activeModelId = DBInstanceID();
    m_widgetId = DBInstanceID();

    StandardActionHandler::onExit();

    LOG_DEBUG("ModelTranslateWidgetHandler::onExit - Translation mode deactivated");
}

bool ModelTranslateWidgetHandler::onMousePressEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!interactionPickService().supportsFeature(
            m_interactionViewId,
            GPlatform::Rendering::PickFeature::SubTarget)) {
        return false;
    }
    if (event->button() != Qt::LeftButton) {
        return false;
    }

    LOG_DEBUG("🖱️ ModelTranslateWidgetHandler::onMousePressEvent - Mouse press at ({}, {})",
              event->pos().x(), event->pos().y());

    const auto blockingPick = interactionPickService().pickRendererForViewBlocking(
        m_interactionViewId, event->position().toPoint(),
        GPlatform::Rendering::PickDetail::Object);
    const PickResult pickResult = blockingPick.status ==
            GPlatform::Rendering::PickStatus::Hit
        ? blockingPick.payload
        : PickResult{};
    if (!pickResult.objectId.isValid()) {
        LOG_DEBUG("ModelTranslateWidgetHandler::onMousePressEvent - No valid pick result");
        return false;
    }

    // 检查是否Pick到了我们的Widget
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("ModelTranslateWidgetHandler::onMousePressEvent - DocumentManager not available");
        return false;
    }
    auto pickedInstance = docManager->getDBInstance(pickResult.objectId);
    auto pickedWidget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(pickedInstance);
    if (!pickedWidget || pickedWidget->getDBInstanceID() != m_widgetId) {
        LOG_DEBUG("ModelTranslateWidgetHandler::onMousePressEvent - Not our widget");
        return false;
    }

    // 识别被pick的部件
    const int partId = pickResult.subTarget
        ? static_cast<int>(*pickResult.subTarget)
        : NONE;
    if (partId == NONE) {
        LOG_DEBUG("ModelTranslateWidgetHandler::onMousePressEvent - No widget part identified");
        return false;
    }

    LOG_DEBUG("ModelTranslateWidgetHandler::onMousePressEvent - Picked widget part: {}", partId);

    // 开始拖拽
    if (!startDrag(m_widgetId, partId, event->pos())) {
        return false;
    }

    applyTranslateCursor(Qt::ClosedHandCursor);
    return true;
}

bool ModelTranslateWidgetHandler::onMouseMoveEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!m_dragState.isDragging) {
        if (event->buttons() != Qt::NoButton ||
            !interactionPickService().supportsFeature(
                m_interactionViewId,
                GPlatform::Rendering::PickFeature::SubTarget)) {
            return false;
        }
        interactionPickService().requestForView(
            m_interactionViewId, event->position().toPoint(),
            PickChannel::Hover,
            GPlatform::Rendering::PickDetail::Object,
            PickDelivery::LatestOnly);
        return false;
    }

    updateDrag(event->pos());
    return true;
}

bool ModelTranslateWidgetHandler::onMouseReleaseEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!m_dragState.isDragging || event->button() != Qt::LeftButton) {
        return false;
    }

    LOG_INFO("🖱️ ModelTranslateWidgetHandler::onMouseReleaseEvent - Ending drag");
    endDrag();
    updateHoverCursor(getCurrentPickResult());
    return true;
}

bool ModelTranslateWidgetHandler::createOrGetWidget() {
    if (m_activeModelId.getValue() == 0) {
        LOG_ERROR("❌ ModelTranslateWidgetHandler::createOrGetWidget - No active model");
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("❌ ModelTranslateWidgetHandler::createOrGetWidget - DocumentManager not available");
        return false;
    }

    // 获取Actor信息
    auto actor = std::dynamic_pointer_cast<ActorDB>(docManager->getDBInstance(m_activeModelId));
    if (!actor) {
        LOG_ERROR("❌ ModelTranslateWidgetHandler::createOrGetWidget - Failed to get actor");
        return false;
    }

    Vector3 actorPosition = actor->getPosition();
    LOG_DEBUG("ModelTranslateWidgetHandler::createOrGetWidget - Actor position: ({:.3f}, {:.3f}, {:.3f})",
             actorPosition.x, actorPosition.y, actorPosition.z);

    if (m_widgetId.getValue() != 0) {
        auto widget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(docManager->getDBInstance(m_widgetId));
        if (widget) {
            widget->setLinkedActor(m_activeModelId);
            widget->setVisible(true);
            LOG_DEBUG("ModelTranslateWidgetHandler::createOrGetWidget - Reused widget {} for model {}",
                      m_widgetId.getValue(), m_activeModelId.getValue());

            auto selectionBridge = SelectionBridge::instance();
            if (selectionBridge) {
                selectionBridge->updateTranslateWidgetStatus();
            }
            return true;
        }

        LOG_WARN("ModelTranslateWidgetHandler::createOrGetWidget - Cached widget {} no longer exists, creating a new one",
                 m_widgetId.getValue());
        m_widgetId = DBInstanceID();
    }

    TransactionGuard guard("Create ModelTranslateWidget");
    auto widget = trans::TransDB::create<ModelTranslateWidgetDB>(m_activeModelId, actorPosition);
    if (!widget) {
        LOG_ERROR("❌ ModelTranslateWidgetHandler::createOrGetWidget - Failed to create widget");
        return false;
    }

    m_widgetId = widget->getDBInstanceID();
    LOG_DEBUG("ModelTranslateWidgetHandler::createOrGetWidget - Widget created with ID: {}", m_widgetId.getValue());

    // ✅ 关键修复：通知SelectionBridge更新Translate按钮状态
    auto selectionBridge = SelectionBridge::instance();
    if (selectionBridge) {
        selectionBridge->updateTranslateWidgetStatus();
        LOG_DEBUG("Translate按钮状态已更新：通知SelectionBridge translate widget已创建");
    }
    return true;
}

void ModelTranslateWidgetHandler::deleteModelTranslateWidget() {
    if (m_widgetId.getValue() == 0) {
        return;
    }

    LOG_INFO("🗑️ ModelTranslateWidgetHandler::deleteModelTranslateWidget - Deleting widget ID: {}", m_widgetId.getValue());

    auto* docManager = DocumentManager::instance();
    if (docManager) {
        // 使用事务删除Widget
        TransactionGuard guard("Delete ModelTranslateWidget");
        docManager->unregisterDBInstance(m_widgetId);
    }

    m_widgetId = DBInstanceID();

    // ✅ 关键修复：通知SelectionBridge更新Translate按钮状态
    auto selectionBridge = SelectionBridge::instance();
    if (selectionBridge) {
        selectionBridge->updateTranslateWidgetStatus();
        LOG_DEBUG("Translate按钮状态已更新：通知SelectionBridge translate widget已删除");
    }
}

bool ModelTranslateWidgetHandler::startDrag(const DBInstanceID& widgetId, int partId, const QPoint& mousePos) {
    LOG_DEBUG("ModelTranslateWidgetHandler::startDrag - Starting drag on part {} at ({}, {})",
             partId, mousePos.x(), mousePos.y());

    // 重置拖拽状态
    m_dragState = DragState();

    // 获取Widget
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("ModelTranslateWidgetHandler::startDrag - DocumentManager not available");
        return false;
    }
    auto widget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(docManager->getDBInstance(widgetId));
    if (!widget) {
        LOG_ERROR("❌ ModelTranslateWidgetHandler::startDrag - Failed to get widget");
        return false;
    }

    // 设置拖拽状态；所有依赖项验证通过后才进入 dragging 状态并开启事务。
    m_dragState.widgetId = widgetId;
    m_dragState.linkedActorId = widget->getLinkedActorID();
    m_dragState.draggedPart = partId;
    m_dragState.translateMode = getTranslateModeFromPart(partId);
    if (m_dragState.translateMode == TranslateMode::None) {
        LOG_WARN("ModelTranslateWidgetHandler::startDrag - Invalid widget part: {}", partId);
        m_dragState = DragState();
        return false;
    }
    m_dragState.initialBoundsMin = widget->getModelBoundsMin();
    m_dragState.initialBoundsMax = widget->getModelBoundsMax();
    m_dragState.startingBoxCenter = widget->getWidgetCenter();

    switch (m_dragState.translateMode) {
        case TranslateMode::XAxis:
            m_dragState.startingGrabberPosition = widget->getXArrowPosition();
            break;
        case TranslateMode::YAxis:
            m_dragState.startingGrabberPosition = widget->getYArrowPosition();
            break;
        case TranslateMode::ZAxis:
            m_dragState.startingGrabberPosition = widget->getZArrowPosition();
            break;
        default:
            m_dragState.startingGrabberPosition = widget->getWidgetCenter();
            break;
    }

    if (auto actor = std::dynamic_pointer_cast<ActorDB>(
            docManager->getDBInstance(m_dragState.linkedActorId))) {
        m_dragState.initialActorPosition = actor->getPosition();
    } else {
        LOG_ERROR("❌ ModelTranslateWidgetHandler::startDrag - Failed to get linked actor");
        m_dragState = DragState();
        return false;
    }

    const auto startProjection = calculateAxisParameterFromMouseRay(mousePos);
    if (!startProjection) {
        LOG_ERROR("ModelTranslateWidgetHandler::startDrag - Failed to project mouse ray onto axis");
        m_dragState = DragState();
        return false;
    }
    m_dragState.startProjection = *startProjection;
    m_dragState.currentDisplacement = Vector3(0.0f, 0.0f, 0.0f);
    m_dragState.isDragging = true;

    // 🔑 锁定 Pick - 拖动期间不再执行 Pick 操作

    // 开始事务
    if (!m_dragState.hasActiveTransaction) {
        TransactionManager::instance().beginTransaction("Model Translation");
        m_dragState.hasActiveTransaction = true;
    }

    LOG_DEBUG("ModelTranslateWidgetHandler::startDrag - Drag started in mode: {}",
             static_cast<int>(m_dragState.translateMode));
    return true;
}

void ModelTranslateWidgetHandler::updateDrag(const QPoint& currentPos) {
    if (!m_dragState.isDragging) {
        return;
    }

    // 🚨 关键修复：使用 ImmediateNotifyGuard 确保拖动过程中的通知立即发送
    // 这样可以实现实时视觉反馈，而不是等到事务结束
    ImmediateNotifyGuard immediateGuard;

    const auto currentProjection = calculateAxisParameterFromMouseRay(currentPos);
    if (!currentProjection) {
        return;
    }

    float projection = *currentProjection - m_dragState.startProjection;
    if (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
        constexpr float kSnapStep = 1.0f;
        projection = kSnapStep * std::round(projection / kSnapStep);
    }
    const Vector3 worldDelta = displacementFromProjection(projection);
    m_dragState.currentDisplacement = worldDelta;

    // 计算新位置
    Vector3 newPosition = m_dragState.initialActorPosition + worldDelta;
    Vector3 newWidgetCenter = m_dragState.startingBoxCenter + worldDelta;

    applyTranslationToLinkedActor(newPosition);
    updateWidgetAfterTranslation(newWidgetCenter);

    LOG_DEBUG("ModelTranslateWidgetHandler::updateDrag - New position: ({:.3f}, {:.3f}, {:.3f})",
             newPosition.x, newPosition.y, newPosition.z);
}

void ModelTranslateWidgetHandler::endDrag() {
    if (!m_dragState.isDragging) {
        return;
    }

    LOG_DEBUG("ModelTranslateWidgetHandler::endDrag - Ending drag operation");

    // 获取Widget并更新其当前位置
    auto* docManager = DocumentManager::instance();
    auto widget = docManager
        ? std::dynamic_pointer_cast<ModelTranslateWidgetDB>(
              docManager->getDBInstance(m_dragState.widgetId))
        : nullptr;
    if (widget) {
        // 🔧 关键修复：更新Widget的当前位置为最终位置
        Vector3 finalPosition = m_dragState.startingBoxCenter + m_dragState.currentDisplacement;
        widget->setWidgetCenter(finalPosition);
        widget->setModelBoundsMin(m_dragState.initialBoundsMin + m_dragState.currentDisplacement);
        widget->setModelBoundsMax(m_dragState.initialBoundsMax + m_dragState.currentDisplacement);
        widget->updateHandlePositions();
        LOG_DEBUG("ModelTranslateWidgetHandler::endDrag - Updated widget position to: ({:.3f}, {:.3f}, {:.3f})",
                 finalPosition.x, finalPosition.y, finalPosition.z);
    }

    // 提交事务
    if (m_dragState.hasActiveTransaction) {
        if (docManager) {
            TransactionManager::instance().commitTransaction();
        } else {
            LOG_ERROR("ModelTranslateWidgetHandler::endDrag - DocumentManager unavailable; rolling back drag");
            TransactionManager::instance().rollbackTransaction();
        }
        m_dragState.hasActiveTransaction = false;
    }


    // 重置拖拽状态
    m_dragState.isDragging = false;

    LOG_DEBUG("ModelTranslateWidgetHandler::endDrag - Drag operation completed");
}

std::optional<float> ModelTranslateWidgetHandler::calculateAxisParameterFromMouseRay(
    const QPoint& currentScreenPos) const {
    auto* documentManager = DocumentManager::instance();
    if (!documentManager) {
        LOG_ERROR("DocumentManager not available for translate axis projection");
        return std::nullopt;
    }
    auto windowDB = std::dynamic_pointer_cast<WindowDB>(
        documentManager->getDBInstance(m_interactionViewId));
    if (!windowDB) {
        LOG_ERROR("No input View available for translate axis projection");
        return std::nullopt;
    }

    auto coordSystem = windowDB->getCoordinateSystem();
    if (!coordSystem) {
        LOG_ERROR("ViewportCoordinateSystem not available for translate axis projection");
        return std::nullopt;
    }

    const WorldRay ray = coordSystem->rayFromScreen(QPointF(currentScreenPos));

    const Vector3 startingVec = m_dragState.startingGrabberPosition - m_dragState.startingBoxCenter;
    const float startingLen = length(startingVec);
    const float rayLen = length(ray.direction);
    if (startingLen <= 1e-6f || rayLen <= 1e-6f) {
        return std::nullopt;
    }

    const Vector3 axisDirection = normalize(startingVec);
    const Vector3 mouseDirection = normalize(ray.direction);

    // Find the closest point between the fixed translation axis
    //   P_axis = grabber + axisParameter * axisDirection
    // and the current mouse ray
    //   P_ray  = rayOrigin + rayParameter * mouseDirection.
    //
    // Projecting only the closest ray point onto the axis misses the
    // 1 / (1 - dot(axis, ray)^2) perspective compensation. The visible
    // grabber then trails the cursor whenever the axis points partly toward
    // the camera. Solving both line parameters keeps the grabber locked to
    // the mouse ray for every non-degenerate view angle.
    const Vector3 axisToRay = m_dragState.startingGrabberPosition - ray.origin;
    const float axisRayDot = dotProduct(axisDirection, mouseDirection);
    const float axisOffset = dotProduct(axisDirection, axisToRay);
    const float rayOffset = dotProduct(mouseDirection, axisToRay);
    const float denominator = 1.0f - axisRayDot * axisRayDot;

    if (denominator > 1e-4f) {
        return (axisRayDot * rayOffset - axisOffset) / denominator;
    }

    // When the movement axis is almost parallel to the view ray its screen
    // projection collapses to a point, so the exact solution is ill-conditioned.
    // Keep the old bounded projection as a stable fallback instead of allowing
    // tiny mouse noise to produce a very large world-space jump.
    const Vector3 toGrabber = m_dragState.startingGrabberPosition - ray.origin;
    const float rayT = dotProduct(toGrabber, mouseDirection);
    const Vector3 closestRayPoint = ray.origin + mouseDirection * rayT;
    return dotProduct(closestRayPoint - m_dragState.startingGrabberPosition,
                      axisDirection);
}

Vector3 ModelTranslateWidgetHandler::displacementFromProjection(float projection) const {
    switch (m_dragState.translateMode) {
        case TranslateMode::XAxis:
            return Vector3(projection, 0.0f, 0.0f);
        case TranslateMode::YAxis:
            return Vector3(0.0f, projection, 0.0f);
        case TranslateMode::ZAxis:
            return Vector3(0.0f, 0.0f, projection);
        case TranslateMode::None:
        default:
            return Vector3(0.0f, 0.0f, 0.0f);
    }
}

void ModelTranslateWidgetHandler::applyTranslationToLinkedActor(const Vector3& newPosition) {
    if (m_dragState.linkedActorId.getValue() == 0) {
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return;
    }
    auto actor = std::dynamic_pointer_cast<ActorDB>(
        docManager->getDBInstance(m_dragState.linkedActorId));

    if (actor) {
        actor->setPosition(newPosition);
        LOG_DEBUG("ModelTranslateWidgetHandler::applyTranslationToLinkedActor - Actor position updated to: ({:.3f}, {:.3f}, {:.3f})",
                 newPosition.x, newPosition.y, newPosition.z);
    } else {
        LOG_WARN("ModelTranslateWidgetHandler::applyTranslationToLinkedActor - Actor not found for ID: {}", m_dragState.linkedActorId.getValue());
    }
}

void ModelTranslateWidgetHandler::updateWidgetAfterTranslation(const Vector3& newPosition) {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return;
    }
    auto widget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(
        docManager->getDBInstance(m_dragState.widgetId));

    if (widget) {
        // 更新 Widget 位置，触发 DBSync 更新渲染。包围盒按拖拽位移同步平移，
        // 与 Orca 在拖拽时使用 selection cache 的行为一致。
        widget->setWidgetCenter(newPosition);
        widget->setModelBoundsMin(m_dragState.initialBoundsMin + m_dragState.currentDisplacement);
        widget->setModelBoundsMax(m_dragState.initialBoundsMax + m_dragState.currentDisplacement);
        widget->updateHandlePositions();
    }
}

PickResult ModelTranslateWidgetHandler::getCurrentPickResult() const {
    if (const auto snapshot = interactionPickService().latestForView(
            m_interactionViewId,
            PickChannel::Hover,
            GPlatform::Rendering::PickDetail::Object)) {
        return snapshot->result;
    }
    PickResult empty;
    return empty;
}

bool ModelTranslateWidgetHandler::isInteractiveConePick(const PickResult& pickResult) const {
    if (!pickResult.objectId.isValid() ||
        !pickResult.subTarget ||
        pickResult.objectId != m_widgetId) {
        return false;
    }

    const int partId = static_cast<int>(*pickResult.subTarget);
    return partId == X_AXIS || partId == Y_AXIS || partId == Z_AXIS;
}

void ModelTranslateWidgetHandler::updateHoverCursor(const PickResult& pickResult) {
    if (m_dragState.isDragging) {
        applyTranslateCursor(Qt::ClosedHandCursor);
        return;
    }

    if (isInteractiveConePick(pickResult)) {
        applyTranslateCursor(Qt::OpenHandCursor);
    } else {
        clearTranslateCursor();
    }
}

void ModelTranslateWidgetHandler::applyTranslateCursor(Qt::CursorShape shape) {
    if (m_translateCursorApplied && m_translateCursorShape == shape) {
        return;
    }

    if (m_translateCursorApplied) {
        QGuiApplication::changeOverrideCursor(QCursor(shape));
    } else {
        QGuiApplication::setOverrideCursor(QCursor(shape));
        m_translateCursorApplied = true;
    }

    m_translateCursorShape = shape;
}

void ModelTranslateWidgetHandler::clearTranslateCursor() {
    if (!m_translateCursorApplied) {
        return;
    }

    QGuiApplication::restoreOverrideCursor();
    m_translateCursorApplied = false;
    m_translateCursorShape = Qt::ArrowCursor;
}

TranslateMode ModelTranslateWidgetHandler::getTranslateModeFromPart(int partId) {
    switch (partId) {
        case X_AXIS: return TranslateMode::XAxis;
        case Y_AXIS: return TranslateMode::YAxis;
        case Z_AXIS: return TranslateMode::ZAxis;
        default: return TranslateMode::None;
    }
}

// === 数学辅助方法实现 ===

float ModelTranslateWidgetHandler::dotProduct(const Vector3& a, const Vector3& b) const {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 ModelTranslateWidgetHandler::normalize(const Vector3& v) const {
    float len = length(v);
    if (len > 0.0f) {
        return Vector3(v.x / len, v.y / len, v.z / len);
    }
    return Vector3(0.0f, 0.0f, 0.0f);
}

float ModelTranslateWidgetHandler::length(const Vector3& v) const {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
