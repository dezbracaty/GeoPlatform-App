#include "ModelScaleWidgetHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(ModelScaleWidgetHandler, "model.scale", "model.remove_scale_widget")
#include "InteractionRuntime.hpp"
#include "PickService.hpp"
#include "ActionHandlerRegistry.hpp"
#include "ActionContext.hpp"
#include "ActionManager.hpp"
#include "transdb.h"
#include "ModelScaleWidgetDB.hpp"
#include "DocumentManager.hpp"
#include "WindowDBManager.hpp"
#include "Foundation/Log.h"
#include "ActorDB.hpp"
#include "SelectionBridge.hpp"
#include "TransactionManager.hpp"
#include "WindowDB.hpp"
#include "ViewportCoordinateSystem.hpp"
#include "AIDescriptorHelper.hpp"
#include <ImmediateNotifyGuard.hpp>
#include <QGuiApplication>
#include <cmath>
#include <algorithm>

namespace {

constexpr float kOrcaScaleSnapStep = 0.05f;

float dotProduct(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 crossProduct(const Vector3& a, const Vector3& b) {
    return Vector3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

float vectorLength(const Vector3& v) {
    return std::sqrt(dotProduct(v, v));
}

Vector3 normalized(const Vector3& v) {
    const float len = vectorLength(v);
    if (len <= 1e-6f) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }
    return v * (1.0f / len);
}

int axisForScalePart(int partId) {
    if (partId == 0 || partId == 1) {
        return 0;
    }
    if (partId == 2 || partId == 3) {
        return 1;
    }
    if (partId == 4 || partId == 5) {
        return 2;
    }
    return -1;
}

bool isUniformScalePart(int partId) {
    return partId >= 6 && partId <= 9;
}

int constraintId(int grabberId) {
    static constexpr int kMap[10] = {1, 0, 3, 2, 5, 4, 8, 9, 6, 7};
    return grabberId >= 0 && grabberId < 10 ? kMap[grabberId] : -1;
}

float componentAt(const Vector3& v, int axis) {
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

void setComponent(Vector3& v, int axis, float value) {
    if (axis == 0) v.x = value;
    else if (axis == 1) v.y = value;
    else v.z = value;
}

Vector3 axisVector(int axis) {
    if (axis == 0) return Vector3(1.0f, 0.0f, 0.0f);
    if (axis == 1) return Vector3(0.0f, 1.0f, 0.0f);
    return Vector3(0.0f, 0.0f, 1.0f);
}

Vector3 transformPoint(const Transform& transform, const Vector3& point) {
    const auto& matrix = transform.getMatrix();
    Eigen::Vector4f p(point.x, point.y, point.z, 1.0f);
    Eigen::Vector4f result = matrix * p;
    return Vector3(result.x(), result.y(), result.z());
}

Vector3 transformVector(const Transform& transform, const Vector3& vector) {
    const auto& matrix = transform.getMatrix();
    Eigen::Vector4f v(vector.x, vector.y, vector.z, 0.0f);
    Eigen::Vector4f result = matrix * v;
    return Vector3(result.x(), result.y(), result.z());
}

Vector3 rayPlaneIntersection(const Vector3& rayOrigin,
                             const Vector3& rayDirection,
                             const Vector3& planePoint,
                             const Vector3& planeNormal,
                             bool* ok) {
    const float denom = dotProduct(planeNormal, rayDirection);
    if (std::abs(denom) <= 1e-6f) {
        if (ok) *ok = false;
        return planePoint;
    }
    const float t = dotProduct(planeNormal, planePoint - rayOrigin) / denom;
    if (ok) *ok = true;
    return rayOrigin + rayDirection * t;
}

ActorDB::BoundingBox transformBounds(const ActorDB::BoundingBox& localBounds,
                                     const Transform::Matrix4& matrix) {
    ActorDB::BoundingBox result;
    if (!localBounds.valid || !matrix.allFinite()) {
        return result;
    }

    const Vector3& min = localBounds.min;
    const Vector3& max = localBounds.max;
    const Vector3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {min.x, max.y, min.z}, {max.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {min.x, max.y, max.z}, {max.x, max.y, max.z}
    };

    Vector3 worldMin;
    Vector3 worldMax;
    bool initialized = false;
    for (const Vector3& corner : corners) {
        const Eigen::Vector4f transformed = matrix *
            Eigen::Vector4f(corner.x, corner.y, corner.z, 1.0f);
        if (!transformed.allFinite()) {
            return ActorDB::BoundingBox{};
        }

        const Vector3 point(transformed.x(), transformed.y(), transformed.z());
        if (!initialized) {
            worldMin = point;
            worldMax = point;
            initialized = true;
            continue;
        }
        worldMin.x = std::min(worldMin.x, point.x);
        worldMin.y = std::min(worldMin.y, point.y);
        worldMin.z = std::min(worldMin.z, point.z);
        worldMax.x = std::max(worldMax.x, point.x);
        worldMax.y = std::max(worldMax.y, point.y);
        worldMax.z = std::max(worldMax.z, point.z);
    }

    result.min = worldMin;
    result.max = worldMax;
    result.valid = initialized;
    return result;
}

ActorDB::BoundingBox transformedWorldBoundsFromInitial(
    const ActorDB::BoundingBox& initialWorldBounds,
    const Vector3& initialPosition,
    const Vector3& currentPosition,
    float scaleFactor) {
    ActorDB::BoundingBox result;
    if (!initialWorldBounds.valid || scaleFactor <= 0.0f ||
        !std::isfinite(scaleFactor)) {
        return result;
    }

    const Vector3& min = initialWorldBounds.min;
    const Vector3& max = initialWorldBounds.max;
    const Vector3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {min.x, max.y, min.z}, {max.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {min.x, max.y, max.z}, {max.x, max.y, max.z}
    };

    Vector3 worldMin;
    Vector3 worldMax;
    bool initialized = false;
    for (const Vector3& corner : corners) {
        const Vector3 point = currentPosition +
            (corner - initialPosition) * scaleFactor;
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            return ActorDB::BoundingBox{};
        }

        if (!initialized) {
            worldMin = point;
            worldMax = point;
            initialized = true;
            continue;
        }
        worldMin.x = std::min(worldMin.x, point.x);
        worldMin.y = std::min(worldMin.y, point.y);
        worldMin.z = std::min(worldMin.z, point.z);
        worldMax.x = std::max(worldMax.x, point.x);
        worldMax.y = std::max(worldMax.y, point.y);
        worldMax.z = std::max(worldMax.z, point.z);
    }

    result.min = worldMin;
    result.max = worldMax;
    result.valid = initialized;
    return result;
}

} // namespace

ModelScaleWidgetHandler::ModelScaleWidgetHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

ModelScaleWidgetHandler::~ModelScaleWidgetHandler() {
}

void ModelScaleWidgetHandler::onEnter(std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("ModelScaleWidgetHandler::onEnter - Missing action context");
        return;
    }

    const QString actionCode = context->getActionCode();

    // 如果是删除操作，执行删除逻辑
    if (actionCode == "model.remove_scale_widget") {
        deleteModelScaleWidget();
        return;
    }

    const QVariantMap params = context->getParams();
    const int modelId = params.contains("modelId") ? params.value("modelId").toInt()
        : SelectionBridge::instance()->getSelectedId();

    const bool directActivation = params.value("directActivation", false).toBool();
    if (directActivation) {
        auto* docManager = DocumentManager::instance();
        const DBInstanceID widgetId(params.value("widgetId", 0).toInt());
        // Renderer Pick统一使用 partId；保留 handleId 回退以兼容旧调用方。
        const int handleId = params.value("partId", params.value("handleId", NONE)).toInt();
        auto widget = docManager
            ? std::dynamic_pointer_cast<ModelScaleWidgetDB>(docManager->getDBInstance(widgetId))
            : nullptr;
        if (!widget || handleId < HANDLE_X_MIN || handleId > HANDLE_UNIFORM_XMIN_YMAX ||
            handleId == HANDLE_Z_MIN) {
            LOG_ERROR("ModelScaleWidgetHandler::onEnter - Invalid direct activation");
            return;
        }

        StandardActionHandler::onEnter(context);
        m_activeModelId = widget->getLinkedActorID();
        m_widgetId = widgetId;

        if (auto* actionManager = ActionManager::getInstance()) {
            actionManager->triggerAction(QStringLiteral("modelInteraction.suspendSelectionInput"));
        }

        const QPoint mousePos(params.value("mouseX", 0).toInt(),
                              params.value("mouseY", 0).toInt());
        if (!startDrag(m_widgetId, handleId, mousePos)) {
            onExit();
        }
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (modelId <= 0 || !docManager ||
        !std::dynamic_pointer_cast<ActorDB>(docManager->getDBInstance(DBInstanceID(modelId)))) {
        LOG_WARN("ModelScaleWidgetHandler::onEnter - Invalid model ID: {}", modelId);
        context->setError(ActionErrorCode::InvalidParams, "model.scale requires a valid modelId or selected model");
        return;
    }

    StandardActionHandler::onEnter(context);
    m_activeModelId = DBInstanceID(modelId);

    if (auto* actionManager = ActionManager::getInstance()) {
        actionManager->triggerAction(QStringLiteral("modelInteraction.suspendSelectionInput"));
    }

    if (!createOrGetWidget()) {
        if (auto* actionManager = ActionManager::getInstance();
            actionManager && actionManager->isAcceptingActions()) {
            actionManager->triggerAction(QStringLiteral("modelInteraction.resumeSelectionInput"));
        }
        m_activeModelId = DBInstanceID();
        StandardActionHandler::onExit();
    }
}

void ModelScaleWidgetHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    bool ok = false;
    const QString actionCode = getActionCode();
    if (actionCode == "model.scale") {
        ok = executeAIScale(context);
    } else {
        if (context) {
            context->setError(ActionErrorCode::InvalidParams,
                              QString("AI invoke not supported for action '%1'").arg(actionCode));
        }
        LOG_WARN("ModelScaleWidgetHandler AI invoke rejected for action '{}'",
                 actionCode.toStdString());
    }
    if (!ok && context && !context->hasError()) {
        context->setError(ActionErrorCode::Internal, "AI scaling failed");
    }

    // AI one-shot call should not keep this persistent handler active.
    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& ModelScaleWidgetHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"model.scale", makeDescriptor(
            "model.scale",
            "Scale Model",
            "Scale selected or specified models by factor or sx/sy/sz multipliers.",
            "write",
            {
                {"factor", "number", false},
                {"scaleFactor", "number", false},
                {"sx", "number", false},
                {"sy", "number", false},
                {"sz", "number", false},
                {"absolute", "bool", false},
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false}
            },
            {"transform", "scale", "scene"}
        )}
    };
    return table;
}

bool ModelScaleWidgetHandler::executeAIScale(std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("ModelScaleWidgetHandler::executeAIScale - null context");
        return false;
    }

    const QVariantMap params = context->getParams();
    const bool absolute = params.value("absolute", false).toBool();

    const bool factorProvided = params.contains("factor") || params.contains("scaleFactor");
    const double factor = params.value("factor", params.value("scaleFactor", 1.0)).toDouble();
    const bool sxProvided = params.contains("sx");
    const bool syProvided = params.contains("sy");
    const bool szProvided = params.contains("sz");
    const double sxValue = params.value("sx", factorProvided ? factor : 1.0).toDouble();
    const double syValue = params.value("sy", factorProvided ? factor : 1.0).toDouble();
    const double szValue = params.value("sz", factorProvided ? factor : 1.0).toDouble();

    if (!factorProvided && !sxProvided && !syProvided && !szProvided) {
        context->setError(ActionErrorCode::InvalidParams,
                          "model.scale requires factor/scaleFactor or at least one of sx/sy/sz");
        return false;
    }

    if (sxValue <= 0.0 || syValue <= 0.0 || szValue <= 0.0) {
        context->setError(ActionErrorCode::InvalidParams, "model.scale values must be > 0");
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        context->setError(ActionErrorCode::SystemUnavailable, "DocumentManager not available");
        LOG_ERROR("AI model.scale failed: DocumentManager unavailable");
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

    int scaledCount = 0;
    try {
        TransactionGuard guard("AI Scale Selected Models");
        for (const auto& targetId : targetIds) {
            auto dbObject = docManager->getDBInstance(targetId);
            auto actor = std::dynamic_pointer_cast<ActorDB>(dbObject);
            if (!actor) {
                LOG_WARN("AI model.scale skipped non-actor id={}", targetId.getValue());
                continue;
            }

            const Vector3 currentScale = actor->getScale();
            Vector3 newScale = currentScale;
            if (absolute) {
                newScale.x = static_cast<float>(sxProvided ? sxValue : (factorProvided ? factor : currentScale.x));
                newScale.y = static_cast<float>(syProvided ? syValue : (factorProvided ? factor : currentScale.y));
                newScale.z = static_cast<float>(szProvided ? szValue : (factorProvided ? factor : currentScale.z));
            } else {
                newScale.x = currentScale.x * static_cast<float>(sxValue);
                newScale.y = currentScale.y * static_cast<float>(syValue);
                newScale.z = currentScale.z * static_cast<float>(szValue);
            }

            newScale.x = std::clamp(newScale.x, 0.001f, 1000.0f);
            newScale.y = std::clamp(newScale.y, 0.001f, 1000.0f);
            newScale.z = std::clamp(newScale.z, 0.001f, 1000.0f);
            actor->setScale(newScale);
            ++scaledCount;
        }
        guard.commit();
    } catch (const std::exception& e) {
        context->setError(ActionErrorCode::Internal, QString("Scale transaction failed: %1").arg(e.what()));
        LOG_ERROR("AI model.scale exception: {}", e.what());
        return false;
    }

    if (scaledCount <= 0) {
        context->setError(ActionErrorCode::TargetNotFound, "No valid ActorDB targets scaled");
        return false;
    }

    context->setResult(QVariantMap{
        {"scaledCount", scaledCount},
        {"absolute", absolute},
        {"sx", sxValue},
        {"sy", syValue},
        {"sz", szValue}
    });
    LOG_INFO("AI model.scale completed: scaled={} absolute={} scale=({:.3f},{:.3f},{:.3f})",
             scaledCount,
             absolute ? "true" : "false",
             sxValue,
             syValue,
             szValue);
    return true;
}

void ModelScaleWidgetHandler::onExit() {
    // 如果正在拖拽，先结束拖拽
    if (m_dragState.isDragging) {
        endDrag();
    }

    // 删除创建的Widget
    deleteModelScaleWidget();

    if (auto* actionManager = ActionManager::getInstance(); actionManager && actionManager->isAcceptingActions()) {
        actionManager->triggerAction(QStringLiteral("modelInteraction.resumeSelectionInput"));
    }

    m_widgetId = DBInstanceID();
    m_activeModelId = DBInstanceID();
    StandardActionHandler::onExit();
}

bool ModelScaleWidgetHandler::createOrGetWidget() {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("ModelScaleWidgetHandler::createOrGetWidget - DocumentManager not available");
        return false;
    }

    // 检查是否已经存在关联到此模型的 widget
    auto instanceIds = docManager->getAllDBInstanceIds(TypeID::MODEL_SCALE_WIDGET_DB);

    for (const auto& id : instanceIds) {
        auto instance = docManager->getDBInstance(id);
        if (auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(instance)) {
            if (widget->getLinkedActorID() == m_activeModelId) {
                m_widgetId = id;
                widget->updateFromLinkedActor();
                widget->setVisible(true);
                if (auto selectionBridge = SelectionBridge::instance()) {
                    selectionBridge->updateScaleWidgetStatus();
                }
                return true;
            }
        }
    }

    // 获取模型实例和包围盒信息
    auto modelInstance = docManager->getDBInstance(m_activeModelId);
    if (!modelInstance) {
        LOG_ERROR("Failed to get model instance for ID: {}", m_activeModelId.getValue());
        return false;
    }

    auto actor = std::dynamic_pointer_cast<ActorDB>(modelInstance);
    if (!actor) {
        LOG_ERROR("Model is not an ActorDB");
        return false;
    }

    const auto worldBounds = actor->worldBounds();
    if (!worldBounds.valid) {
        LOG_ERROR("ModelScaleWidgetHandler::createOrGetWidget - Invalid world bounds");
        return false;
    }

    LOG_DEBUG("Handler创建Widget: 使用模型实际包围盒 min({:.3f},{:.3f},{:.3f}) max({:.3f},{:.3f},{:.3f})",
             worldBounds.min.x, worldBounds.min.y, worldBounds.min.z,
             worldBounds.max.x, worldBounds.max.y, worldBounds.max.z);

    TransactionGuard guard("Create ModelScaleWidget");
    auto widget = trans::TransDB::create<ModelScaleWidgetDB>(
        m_activeModelId, worldBounds.min, worldBounds.max);
    if (!widget) {
        LOG_ERROR("ModelScaleWidgetHandler::createOrGetWidget - Failed to create widget");
        return false;
    }

    m_widgetId = widget->getDBInstanceID();
    guard.commit();

    // 🔧 修复：通知SelectionBridge更新Scale按钮状态
    auto selectionBridge = SelectionBridge::instance();
    if (selectionBridge) {
        selectionBridge->updateScaleWidgetStatus();
        LOG_DEBUG("Scale按钮状态已更新：通知SelectionBridge scale widget已创建");
    }
    return true;
}

void ModelScaleWidgetHandler::deleteModelScaleWidget() {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        return;
    }

    // 查找并删除所有的 ModelScaleWidget
    auto instanceIds = docManager->getAllDBInstanceIds(TypeID::MODEL_SCALE_WIDGET_DB);
    if (instanceIds.empty()) {
        m_widgetId = DBInstanceID();
        m_activeModelId = DBInstanceID();
        return;
    }

    TransactionGuard guard("Delete ModelScaleWidget");
    for (const auto& id : instanceIds) {
        auto instance = docManager->getDBInstance(id);
        if (auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(instance)) {
            // 删除所有的Scale Widget，不管关联的是哪个Actor
            docManager->unregisterDBInstance(id);
        }
    }
    guard.commit();

    m_widgetId = DBInstanceID();
    m_activeModelId = DBInstanceID();

    // 🔧 修复：通知SelectionBridge更新Scale按钮状态
    auto selectionBridge = SelectionBridge::instance();
    if (selectionBridge) {
        selectionBridge->updateScaleWidgetStatus();
        LOG_DEBUG("Scale按钮状态已更新：通知SelectionBridge scale widget已删除");
    }
}

bool ModelScaleWidgetHandler::onMousePressEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!interactionPickService().supportsFeature(
            m_interactionViewId,
            GPlatform::Rendering::PickFeature::SubTarget)) {
        return false;
    }
    if (!isActive()) {
        LOG_WARN("ModelScaleWidgetHandler not active, returning false");
        return false;
    }

    // 只处理左键
    if (event->button() != Qt::LeftButton) {
        return false;
    }

    const auto blockingPick = interactionPickService().pickRendererForViewBlocking(
        m_interactionViewId, event->position().toPoint(),
        GPlatform::Rendering::PickDetail::Object);
    const PickResult pickResult = blockingPick.status ==
            GPlatform::Rendering::PickStatus::Hit
        ? blockingPick.payload
        : PickResult{};


    // 检查是否点击在我们的Widget上
    if (pickResult.objectId == m_widgetId && pickResult.subTarget) {
        const int partId = static_cast<int>(*pickResult.subTarget);

        LOG_DEBUG("[MousePress] 检测到手柄点击 - Widget: {} PartId: {} ({})",
                 m_widgetId.getValue(), partId,
                 axisForScalePart(partId) == 0 ? "X轴" :
                 axisForScalePart(partId) == 1 ? "Y轴" :
                 axisForScalePart(partId) == 2 ? "Z轴" :
                 isUniformScalePart(partId) ? "统一缩放" : "未知手柄");

        return startDrag(m_widgetId, partId, event->pos());
    }

    return false;
}

bool ModelScaleWidgetHandler::onMouseMoveEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!isActive()) return false;

    if (m_dragState.isDragging) {
        QPoint currentPos(event->position().x(), event->position().y());
        updateDrag(currentPos);
        return true;  // 消费事件
    }

    return false;
}

bool ModelScaleWidgetHandler::onMouseReleaseEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!isActive()) return false;

    if (m_dragState.isDragging && event->button() == Qt::LeftButton) {
        endDrag();
        return true;  // 消费事件
    }

    return false;
}

bool ModelScaleWidgetHandler::startDrag(const DBInstanceID& widgetId, int handleId, const QPoint& mousePos) {
    m_dragState = DragState();
    if (handleId < HANDLE_X_MIN || handleId > HANDLE_UNIFORM_XMIN_YMAX ||
        handleId == HANDLE_Z_MIN) {
        LOG_WARN("ModelScaleWidgetHandler::startDrag - Invalid handle {}", handleId);
        return false;
    }
    LOG_DEBUG("[StartDrag] 开始拖拽 - Widget: {} HandleId: {} ({}) 鼠标位置: ({},{})",
             widgetId.getValue(), handleId,
             axisForScalePart(handleId) == 0 ? "X轴" :
             axisForScalePart(handleId) == 1 ? "Y轴" :
             axisForScalePart(handleId) == 2 ? "Z轴" :
             isUniformScalePart(handleId) ? "统一缩放" : "未知手柄",
             mousePos.x(), mousePos.y());

    // 首先验证所有必要的资源
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("❌ [StartDrag] DocumentManager not available");
        return false;
    }

    auto widgetInstance = docManager->getDBInstance(widgetId);
    auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(widgetInstance);
    if (!widget) {
        LOG_ERROR("❌ [StartDrag] Failed to get ModelScaleWidgetDB for ID: {}", widgetId.getValue());
        return false;
    }

    // 获取关联的Actor
    auto linkedActorId = widget->getLinkedActorID();
    auto actorInstance = docManager->getDBInstance(linkedActorId);
    auto actor = std::dynamic_pointer_cast<ActorDB>(actorInstance);
    if (!actor) {
        LOG_ERROR("❌ [StartDrag] Failed to get linked ActorDB for ID: {}", linkedActorId.getValue());
        return false;
    }

    widget->updateFromLinkedActor();

    m_dragState.widgetId = widgetId;
    m_dragState.linkedActorId = linkedActorId;
    m_dragState.draggedHandle = handleId;
    m_dragState.initialScale = actor->getScale();
    m_dragState.initialPosition = actor->getPosition();

    const auto initialBounds = actor->worldBounds();
    const auto initialLocalBounds = actor->localBounds();
    if (!initialLocalBounds.valid || !initialBounds.valid) {
        LOG_ERROR("ModelScaleWidgetHandler::startDrag - Invalid initial bounds, local={}, world={}",
                  initialLocalBounds.valid ? "true" : "false",
                  initialBounds.valid ? "true" : "false");
        m_dragState = DragState();
        return false;
    }
    m_dragState.initialLocalBounds = initialLocalBounds;
    m_dragState.initialWorldBounds = initialBounds;
    m_dragState.initialBottomZ = initialBounds.min.z;
    if (!initializeOrcaStartingData(handleId)) {
        m_dragState = DragState();
        return false;
    }

    // 更新Widget状态
    try {
        widget->setActiveHandle(handleId);
        widget->setConstrainedHandle(m_dragState.ctrlDown ? constraintId(handleId) : -1);
        widget->setIsDragging(true);
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [StartDrag] Failed to update widget state: {}", e.what());
        m_dragState = DragState();
        return false;
    }

    m_dragState.isDragging = true;

    // 🔑 锁定 Pick - 拖动期间不再执行 Pick 操作

    // 关键：使用 TransactionManager 开始事务
    TransactionManager::instance().beginTransaction("Scale Model");
    m_dragState.hasActiveTransaction = true;

    LOG_DEBUG("[StartDrag] 拖拽初始化成功 - Handle: {} ({}) 初始缩放: ({},{},{}) 事务已开始",
             handleId,
             axisForScalePart(handleId) == 0 ? "X轴" :
             axisForScalePart(handleId) == 1 ? "Y轴" :
             axisForScalePart(handleId) == 2 ? "Z轴" :
             isUniformScalePart(handleId) ? "统一缩放" : "未知手柄",
             m_dragState.initialScale.x, m_dragState.initialScale.y, m_dragState.initialScale.z);
    return true;
}

void ModelScaleWidgetHandler::updateDrag(const QPoint& currentPos) {
    if (!m_dragState.isDragging) {
        return;
    }

    // 🚨 关键修复：使用 ImmediateNotifyGuard 确保缩放过程中的通知立即发送
    // 这样可以实现实时视觉反馈，而不是等到事务结束
    ImmediateNotifyGuard immediateGuard;

    const float ratio = calculateOrcaScaleRatio(currentPos);
    if (ratio <= 0.0f || std::abs(ratio - m_dragState.currentRatio) < 0.0001f) {
        return;
    }

    Vector3 newScale = m_dragState.initialScale;
    Vector3 offset(0.0f, 0.0f, 0.0f);
    const int axis = axisForScalePart(m_dragState.draggedHandle);

    if (axis >= 0) {
        const float scaled = componentAt(m_dragState.initialScale, axis) * ratio;
        setComponent(newScale, axis, scaled);
        if (m_dragState.ctrlDown && std::abs(ratio - 1.0f) > 0.001f) {
            float localOffset = 0.5f * (scaled - componentAt(m_dragState.initialScale, axis)) *
                                componentAt(m_dragState.boxSize, axis);
            if (m_dragState.draggedHandle == 2 * axis) {
                localOffset *= -1.0f;
            }
            offset = m_dragState.referenceAxes[axis] * localOffset;
        }
    } else if (isUniformScalePart(m_dragState.draggedHandle)) {
        newScale = m_dragState.initialScale * ratio;
        if (m_dragState.ctrlDown && std::abs(ratio - 1.0f) > 0.001f) {
            newScale.z = m_dragState.initialScale.z;
            const float localOffsetX = 0.5f * (newScale.x - m_dragState.initialScale.x) * m_dragState.boxSize.x;
            const float localOffsetY = 0.5f * (newScale.y - m_dragState.initialScale.y) * m_dragState.boxSize.y;
            switch (m_dragState.draggedHandle) {
                case HANDLE_UNIFORM_XMIN_YMIN:
                    offset = m_dragState.referenceAxes[0] * -localOffsetX + m_dragState.referenceAxes[1] * -localOffsetY;
                    break;
                case HANDLE_UNIFORM_XMAX_YMIN:
                    offset = m_dragState.referenceAxes[0] * localOffsetX + m_dragState.referenceAxes[1] * -localOffsetY;
                    break;
                case HANDLE_UNIFORM_XMAX_YMAX:
                    offset = m_dragState.referenceAxes[0] * localOffsetX + m_dragState.referenceAxes[1] * localOffsetY;
                    break;
                case HANDLE_UNIFORM_XMIN_YMAX:
                    offset = m_dragState.referenceAxes[0] * -localOffsetX + m_dragState.referenceAxes[1] * localOffsetY;
                    break;
                default: break;
            }
        }
    } else {
        LOG_WARN("❌ [UpdateDrag] Unknown handle ID: {}", m_dragState.draggedHandle);
        return;
    }

    // 应用缩放约束
    Vector3 constrainedScale = newScale;
    constrainedScale.x = std::clamp(newScale.x, 0.001f, 1000.0f);
    constrainedScale.y = std::clamp(newScale.y, 0.001f, 1000.0f);
    constrainedScale.z = std::clamp(newScale.z, 0.001f, 1000.0f);

    if (applyScaleToLinkedActor(constrainedScale, offset)) {
        m_dragState.currentRatio = ratio;
        updateWidgetAfterScale(constrainedScale);
    }
}

void ModelScaleWidgetHandler::endDrag() {
    if (!m_dragState.isDragging) {
        return;
    }

    const DBInstanceID widgetId = m_dragState.widgetId;
    // 缩放和 transform-only 贴底属于同一次用户操作，应在同一事务中提交。
    const bool success = performSnapToGround();

    auto* docManager = DocumentManager::instance();
    auto widget = docManager
        ? std::dynamic_pointer_cast<ModelScaleWidgetDB>(docManager->getDBInstance(widgetId))
        : nullptr;

    if (m_dragState.hasActiveTransaction) {
        if (success && docManager) {
            TransactionManager::instance().commitTransaction();
        } else {
            TransactionManager::instance().rollbackTransaction();
        }
    }

    if (widget) {
        widget->setActiveHandle(NONE);
        widget->setConstrainedHandle(NONE);
        widget->setIsDragging(false);
        widget->updateFromLinkedActor();
    }

    m_dragState = DragState();
}

float ModelScaleWidgetHandler::calculateOrcaScaleRatio(const QPoint& currentPos) const {
    Vector3 pivot = (m_dragState.ctrlDown && m_dragState.draggedHandle < 6)
        ? m_dragState.constraintPosition
        : m_dragState.planeCenter;
    const Vector3 startingVec = m_dragState.dragPosition - pivot;
    const float lenStartingVec = vectorLength(startingVec);
    if (lenStartingVec <= 1e-6f) {
        return 1.0f;
    }

    auto* documentManager = DocumentManager::instance();
    auto windowDB = documentManager
        ? std::dynamic_pointer_cast<WindowDB>(
              documentManager->getDBInstance(m_interactionViewId))
        : nullptr;
    if (!windowDB) {
        LOG_ERROR("No input View available for Orca scale projection");
        return 1.0f;
    }

    auto coordSystem = windowDB->getCoordinateSystem();
    if (!coordSystem) {
        LOG_ERROR("ViewportCoordinateSystem not available for Orca scale projection");
        return 1.0f;
    }

    const WorldRay ray = coordSystem->rayFromScreen(QPointF(currentPos));
    const Vector3 rayOrigin = ray.origin;
    const Vector3 mouseDir = normalized(ray.direction);

    Vector3 planeNormal = m_dragState.planeNormal;
    if (m_dragState.draggedHandle == HANDLE_Z_MAX) {
        const Vector3 planeVec = crossProduct(mouseDir, m_dragState.planeNormal);
        planeNormal = crossProduct(planeVec, m_dragState.planeNormal);
    }
    planeNormal = normalized(planeNormal);
    if (vectorLength(planeNormal) <= 1e-6f) {
        return 1.0f;
    }

    const float dotValue = std::clamp(dotProduct(planeNormal, mouseDir), -1.0f, 1.0f);
    const float angle = std::acos(dotValue) * 180.0f / static_cast<float>(M_PI);
    if (std::abs(angle) < 95.0f && std::abs(angle) > 85.0f) {
        return 1.0f;
    }

    bool ok = false;
    const Vector3 intersection = rayPlaneIntersection(
        rayOrigin,
        mouseDir,
        m_dragState.dragPosition,
        planeNormal,
        &ok);
    if (!ok) {
        return 1.0f;
    }

    const Vector3 intersVec = intersection - m_dragState.dragPosition;
    const float proj = dotProduct(intersVec, normalized(startingVec));

    float ratio = (lenStartingVec + proj) / lenStartingVec;
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
        ratio = kOrcaScaleSnapStep * std::round(ratio / kOrcaScaleSnapStep);
    }
    return ratio;
}

bool ModelScaleWidgetHandler::initializeOrcaStartingData(int handleId) {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return false;
    }
    auto widgetInstance = docManager->getDBInstance(m_dragState.widgetId);
    auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(widgetInstance);
    if (!widget) {
        LOG_ERROR("Failed to get ModelScaleWidgetDB for Orca scale starting data");
        return false;
    }

    const Vector3 min = widget->getModelLocalBoundsMin();
    const Vector3 max = widget->getModelLocalBoundsMax();
    const Vector3 center = (min + max) * 0.5f;
    const Vector3 half = (max - min) * 0.5f;
    const Transform referenceTransform = widget->getModelReferenceTransform();

    m_dragState.boxSize = max - min;
    if (m_dragState.boxSize.x <= 1e-6f || m_dragState.boxSize.y <= 1e-6f ||
        m_dragState.boxSize.z <= 1e-6f) {
        LOG_ERROR("ModelScaleWidgetHandler::initializeOrcaStartingData - Degenerate bounds");
        return false;
    }
    const Vector3 localGrabbers[10] = {
        Vector3(center.x - half.x, center.y, min.z),
        Vector3(center.x + half.x, center.y, min.z),
        Vector3(center.x, center.y - half.y, min.z),
        Vector3(center.x, center.y + half.y, min.z),
        Vector3(center.x, center.y, min.z),
        Vector3(center.x, center.y, max.z),
        Vector3(min.x, min.y, min.z),
        Vector3(max.x, min.y, min.z),
        Vector3(max.x, max.y, min.z),
        Vector3(min.x, max.y, min.z)
    };

    for (int i = 0; i < 10; ++i) {
        m_dragState.grabbers[i] = transformPoint(referenceTransform, localGrabbers[i]);
    }
    m_dragState.referenceAxes[0] = normalized(transformVector(referenceTransform, axisVector(0)));
    m_dragState.referenceAxes[1] = normalized(transformVector(referenceTransform, axisVector(1)));
    m_dragState.referenceAxes[2] = normalized(transformVector(referenceTransform, axisVector(2)));

    const int constraint = constraintId(handleId);
    const Vector3 worldCenter = transformPoint(referenceTransform, center);
    m_dragState.dragPosition = handleId >= 0 && handleId < 10 ? m_dragState.grabbers[handleId] : worldCenter;
    m_dragState.constraintPosition = constraint >= 0 ? m_dragState.grabbers[constraint] : worldCenter;
    m_dragState.planeCenter = m_dragState.grabbers[4];
    m_dragState.planeNormal = normalized(m_dragState.grabbers[5] - m_dragState.grabbers[4]);
    m_dragState.ctrlDown = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    if (vectorLength(m_dragState.planeNormal) <= 1e-6f ||
        vectorLength(m_dragState.referenceAxes[0]) <= 1e-6f ||
        vectorLength(m_dragState.referenceAxes[1]) <= 1e-6f ||
        vectorLength(m_dragState.referenceAxes[2]) <= 1e-6f) {
        LOG_ERROR("ModelScaleWidgetHandler::initializeOrcaStartingData - Invalid reference frame");
        return false;
    }

    LOG_DEBUG("[InitOrcaScale] part={} drag=({:.3f},{:.3f},{:.3f}) planeCenter=({:.3f},{:.3f},{:.3f}) planeNormal=({:.3f},{:.3f},{:.3f}) ctrl={}",
              handleId,
              m_dragState.dragPosition.x, m_dragState.dragPosition.y, m_dragState.dragPosition.z,
              m_dragState.planeCenter.x, m_dragState.planeCenter.y, m_dragState.planeCenter.z,
              m_dragState.planeNormal.x, m_dragState.planeNormal.y, m_dragState.planeNormal.z,
              m_dragState.ctrlDown ? "true" : "false");
    return true;
}

bool ModelScaleWidgetHandler::applyScaleToLinkedActor(const Vector3& scale, const Vector3& offset) {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return false;
    }

    auto actorInstance = docManager->getDBInstance(m_dragState.linkedActorId);
    auto actor = std::dynamic_pointer_cast<ActorDB>(actorInstance);
    if (!actor) {
        return false;
    }

    actor->setScale(scale);
    Vector3 position = m_dragState.initialPosition + offset;
    actor->setPosition(position);

    if (std::abs(scale.z - m_dragState.initialScale.z) > 1e-6f) {
        const auto worldBounds = actor->worldBounds();
        if (worldBounds.valid) {
            const float bottomCorrection = m_dragState.initialBottomZ - worldBounds.min.z;
            if (std::abs(bottomCorrection) > 1e-5f) {
                position.z += bottomCorrection;
                actor->setPosition(position);
                LOG_DEBUG("[ScaleDragGroundAnchor] handle={} scaleZ={:.6f} bottom={:.6f} target={:.6f} correction={:.6f}",
                          m_dragState.draggedHandle,
                          scale.z,
                          worldBounds.min.z,
                          m_dragState.initialBottomZ,
                          bottomCorrection);
            }
        }
    }
    return true;
}

void ModelScaleWidgetHandler::updateWidgetAfterScale(const Vector3& newScale) {
    if (!m_dragState.isDragging || m_dragState.widgetId.getValue() == 0) {
            return;
    }

    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("DocumentManager not available");
        return;
    }

    // 获取ModelScaleWidgetDB实例
    auto widgetInstance = docManager->getDBInstance(m_dragState.widgetId);
    auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(widgetInstance);
    if (!widget) {
        LOG_ERROR("Failed to get ModelScaleWidgetDB for ID: {}", m_dragState.widgetId.getValue());
        return;
    }

    auto actor = std::dynamic_pointer_cast<ActorDB>(
        docManager->getDBInstance(m_dragState.linkedActorId));
    if (!actor) {
        LOG_ERROR("Failed to get linked ActorDB while updating scale widget");
        return;
    }

    // 拖动时局部包围盒不会变化，只同步当前变换即可让 10 个 grabber 跟随。
    // 完整 bounds 刷新留到鼠标释放，避免每个鼠标事件重复刷新 DB 属性。
    (void)newScale;
    widget->setModelReferenceTransform(actor->getTransform());
}

bool ModelScaleWidgetHandler::calculateScaleDragCandidateBounds(
    const std::shared_ptr<ActorDB>& actor,
    ActorDB::BoundingBox* worldBounds) const {
    if (!actor || !worldBounds ||
        !m_dragState.initialLocalBounds.valid ||
        !m_dragState.initialWorldBounds.valid) {
        return false;
    }

    const Vector3 currentScale = actor->getScale();
    const auto scaleRatio = [](float current, float initial, float* ratio) {
        if (!ratio || std::abs(initial) <= 1e-6f) {
            return false;
        }
        const float value = current / initial;
        if (value <= 0.0f || !std::isfinite(value)) {
            return false;
        }
        *ratio = value;
        return true;
    };

    float ratioX = 1.0f;
    float ratioY = 1.0f;
    float ratioZ = 1.0f;
    if (!scaleRatio(currentScale.x, m_dragState.initialScale.x, &ratioX) ||
        !scaleRatio(currentScale.y, m_dragState.initialScale.y, &ratioY) ||
        !scaleRatio(currentScale.z, m_dragState.initialScale.z, &ratioZ)) {
        LOG_ERROR("ModelScaleWidgetHandler::calculateScaleDragCandidateBounds - Invalid scale ratio");
        return false;
    }

    constexpr float kRatioEpsilon = 1e-4f;
    const bool uniformScale = std::abs(ratioX - ratioY) <= kRatioEpsilon &&
                              std::abs(ratioX - ratioZ) <= kRatioEpsilon;
    if (uniformScale) {
        *worldBounds = transformedWorldBoundsFromInitial(
            m_dragState.initialWorldBounds,
            m_dragState.initialPosition,
            actor->getPosition(),
            ratioX);
    } else {
        *worldBounds = transformBounds(m_dragState.initialLocalBounds,
                                       actor->getTransformMatrix());
    }

    if (!worldBounds->valid) {
        LOG_ERROR("ModelScaleWidgetHandler::calculateScaleDragCandidateBounds - Invalid candidate bounds");
        return false;
    }

    LOG_DEBUG("Scale finalization uses transform-only bounds: actor={}, uniform={}, ratios=({:.6f},{:.6f},{:.6f})",
              actor->getDBInstanceID().getValue(),
              uniformScale ? "true" : "false",
              ratioX,
              ratioY,
              ratioZ);
    return true;
}

bool ModelScaleWidgetHandler::performSnapToGround() {
    if (!m_dragState.isDragging || m_dragState.linkedActorId.getValue() == 0) {
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return false;
    }
    auto actorInstance = docManager->getDBInstance(m_dragState.linkedActorId);
    auto actor = std::dynamic_pointer_cast<ActorDB>(actorInstance);

    if (!actor) {
        LOG_ERROR("Failed to get ActorDB for snap to ground");
        return false;
    }

    ActorDB::BoundingBox worldBounds;
    if (!calculateScaleDragCandidateBounds(actor, &worldBounds)) {
        LOG_ERROR("ModelScaleWidgetHandler::performSnapToGround - Failed to calculate transform-only bounds");
        return false;
    }

    if (std::abs(worldBounds.min.z) > 0.001f) {
        const float correction = -worldBounds.min.z;
        Vector3 position = actor->getPosition();
        position.z += correction;
        actor->setPosition(position);
        worldBounds.min.z += correction;
        worldBounds.max.z += correction;
    }

    if (std::abs(worldBounds.min.z) > 0.001f) {
        LOG_ERROR("ModelScaleWidgetHandler::performSnapToGround - Candidate verification failed, minZ={}",
                  worldBounds.min.z);
        return false;
    }
    return true;
}
