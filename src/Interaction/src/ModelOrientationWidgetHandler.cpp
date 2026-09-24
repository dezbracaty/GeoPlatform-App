#include "ModelOrientationWidgetHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    ModelOrientationWidgetHandler, "model.rotate", "model.remove_orientation_widget")
#include "InteractionRuntime.hpp"
#include "PickService.hpp"
#include "ActionHandlerRegistry.hpp"
#include "ActionContext.hpp"
#include "ActionManager.hpp"
#include "transdb.h"
#include "ModelOrientationWidgetDB.hpp"
#include "DocumentManager.hpp"
#include "WindowDBManager.hpp"
#include "WindowDB.hpp"
#include "ViewportCoordinateSystem.hpp"
#include "Foundation/Log.h"
#include "ActorDB.hpp"
#include "SelectionBridge.hpp"
#include "TransactionManager.hpp"
#include "AIDescriptorHelper.hpp"
#include <ImmediateNotifyGuard.hpp>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kCoarseSnapSteps = 8;
constexpr int kFineSnapSteps = 72;
constexpr float kFineSnapBand = 0.1f;

float dotProduct(const Vector3& lhs, const Vector3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

float vectorLength(const Vector3& value) {
    return std::sqrt(dotProduct(value, value));
}

Vector3 normalized(const Vector3& value) {
    const float len = vectorLength(value);
    return len > 1e-6f ? value * (1.0f / len) : Vector3();
}

Vector3 worldAxisForPart(int partId) {
    switch (partId) {
        case 1: return Vector3(1.0f, 0.0f, 0.0f);
        case 2: return Vector3(0.0f, 1.0f, 0.0f);
        case 3: return Vector3(0.0f, 0.0f, 1.0f);
        default: return Vector3();
    }
}

Eigen::Matrix3f mousePlaneTransformForPart(int partId) {
    constexpr float halfPi = 0.5f * kPi;
    Eigen::Matrix3f rotation = Eigen::Matrix3f::Identity();
    switch (partId) {
        case 1:
            rotation *= Eigen::AngleAxisf(halfPi, Eigen::Vector3f::UnitZ()).toRotationMatrix();
            rotation *= Eigen::AngleAxisf(-halfPi, Eigen::Vector3f::UnitY()).toRotationMatrix();
            break;
        case 2:
            rotation *= Eigen::AngleAxisf(halfPi, Eigen::Vector3f::UnitY()).toRotationMatrix();
            rotation *= Eigen::AngleAxisf(halfPi, Eigen::Vector3f::UnitZ()).toRotationMatrix();
            break;
        default:
            break;
    }
    return rotation;
}

Transform::Matrix4 rotationAroundPivot(float angleDeg,
                                       const Vector3& axis,
                                       const Vector3& pivot) {
    const Vector3 unitAxis = normalized(axis);
    const Eigen::Vector3f eigenAxis(unitAxis.x, unitAxis.y, unitAxis.z);
    if (eigenAxis.norm() <= 1e-6f) {
        return Transform::Matrix4::Identity();
    }

    Transform::Matrix4 toPivot = Transform::Matrix4::Identity();
    toPivot(0, 3) = pivot.x;
    toPivot(1, 3) = pivot.y;
    toPivot(2, 3) = pivot.z;

    Transform::Matrix4 fromPivot = Transform::Matrix4::Identity();
    fromPivot(0, 3) = -pivot.x;
    fromPivot(1, 3) = -pivot.y;
    fromPivot(2, 3) = -pivot.z;

    Transform::Matrix4 rotation = Transform::Matrix4::Identity();
    rotation.block<3, 3>(0, 0) = Eigen::AngleAxisf(
        angleDeg * kPi / 180.0f, eigenAxis.normalized()).toRotationMatrix();
    return toPivot * rotation * fromPivot;
}

} // namespace

ModelOrientationWidgetHandler::ModelOrientationWidgetHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

ModelOrientationWidgetHandler::~ModelOrientationWidgetHandler() {
}

void ModelOrientationWidgetHandler::onEnter(std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("ModelOrientationWidgetHandler::onEnter - Missing action context");
        return;
    }

    const QString actionCode = context->getActionCode();

    // 如果是删除操作，执行删除逻辑
    if (actionCode == "model.remove_orientation_widget") {
        deleteModelOrientationWidget();
        // 删除完成后不需要保持 handler 活跃
        // 注意：这里不调用 StandardActionHandler::onEnter，避免设置 active 状态
        return;
    }

    // 从 context 中获取模型 ID
    const QVariantMap params = context->getParams();
    const int modelId = params.contains("modelId") ? params.value("modelId").toInt()
        : SelectionBridge::instance()->getSelectedId();

    // 检查是否是直接激活（从Widget点击）
    const bool directActivation = params.value("directActivation", false).toBool();
    if (directActivation) {
        auto* docManager = DocumentManager::instance();
        const DBInstanceID widgetId(params.value("widgetId", 0).toInt());
        const int partId = params.value("partId", 0).toInt();
        auto widget = docManager
            ? std::dynamic_pointer_cast<ModelOrientationWidgetDB>(
                  docManager->getDBInstance(widgetId))
            : nullptr;
        if (!widget || partId < RING_X || partId > RING_Z) {
            LOG_ERROR("ModelOrientationWidgetHandler::onEnter - Invalid direct activation");
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
        if (!startDrag(m_widgetId, partId, mousePos)) {
            onExit();
        }
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (modelId <= 0 || !docManager ||
        !std::dynamic_pointer_cast<ActorDB>(docManager->getDBInstance(DBInstanceID(modelId)))) {
        LOG_WARN("ModelOrientationWidgetHandler::onEnter - Invalid model ID: {}", modelId);
        context->setError(ActionErrorCode::InvalidParams, "model.rotate requires a valid modelId or selected model");
        return;
    }

    // 参数验证通过，现在才调用 StandardActionHandler::onEnter
    StandardActionHandler::onEnter(context);

    m_activeModelId = DBInstanceID(modelId);

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
    }
}

void ModelOrientationWidgetHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    bool ok = false;
    const QString actionCode = getActionCode();
    if (actionCode == "model.rotate") {
        ok = executeAIRotate(context);
    } else {
        if (context) {
            context->setError(ActionErrorCode::InvalidParams,
                              QString("AI invoke not supported for action '%1'").arg(actionCode));
        }
        LOG_WARN("ModelOrientationWidgetHandler AI invoke rejected for action '{}'",
                 actionCode.toStdString());
    }
    if (!ok && context && !context->hasError()) {
        context->setError(ActionErrorCode::Internal, "AI rotation failed");
    }

    // AI one-shot call should not keep this persistent handler active.
    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& ModelOrientationWidgetHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"model.rotate", makeDescriptor(
            "model.rotate",
            "Rotate Model",
            "Rotate selected or specified models by angle around axis x/y/z.",
            "write",
            {
                {"angleDeg", "number", false},
                {"axis", "string", false, QVariant("z"), {"x", "y", "z"}},
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false}
            },
            {"transform", "rotate", "scene"}
        )}
    };
    return table;
}

bool ModelOrientationWidgetHandler::executeAIRotate(std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("ModelOrientationWidgetHandler::executeAIRotate - null context");
        return false;
    }

    const QVariantMap params = context->getParams();
    const double angleDeg = params.value("angleDeg", params.value("angle", 0.0)).toDouble();
    if (std::abs(angleDeg) < 1e-9) {
        context->setError(ActionErrorCode::InvalidParams, "model.rotate requires non-zero angleDeg");
        LOG_WARN("AI model.rotate rejected: zero angle");
        return false;
    }

    QString axis = params.value("axis", "z").toString().trimmed().toLower();
    if (axis != "x" && axis != "y" && axis != "z") {
        context->setError(ActionErrorCode::InvalidParams, "model.rotate axis must be one of x/y/z");
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        context->setError(ActionErrorCode::SystemUnavailable, "DocumentManager not available");
        LOG_ERROR("AI model.rotate failed: DocumentManager unavailable");
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
        LOG_WARN("AI model.rotate failed: empty target ids and selection");
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

    int rotatedCount = 0;
    try {
        TransactionGuard guard("AI Rotate Selected Models");
        for (const auto& targetId : targetIds) {
            auto dbObject = docManager->getDBInstance(targetId);
            auto actor = std::dynamic_pointer_cast<ActorDB>(dbObject);
            if (!actor) {
                LOG_WARN("AI model.rotate skipped non-actor id={}", targetId.getValue());
                continue;
            }

            if (axis == "x") {
                actor->rotateX(static_cast<float>(angleDeg));
            } else if (axis == "y") {
                actor->rotateY(static_cast<float>(angleDeg));
            } else {
                actor->rotateZ(static_cast<float>(angleDeg));
            }
            ++rotatedCount;
        }
        guard.commit();
    } catch (const std::exception& e) {
        context->setError(ActionErrorCode::Internal, QString("Rotation transaction failed: %1").arg(e.what()));
        LOG_ERROR("AI model.rotate exception: {}", e.what());
        return false;
    }

    if (rotatedCount <= 0) {
        context->setError(ActionErrorCode::TargetNotFound, "No valid ActorDB targets rotated");
        return false;
    }

    context->setResult(QVariantMap{
        {"rotatedCount", rotatedCount},
        {"angleDeg", angleDeg},
        {"axis", axis}
    });
    LOG_INFO("AI model.rotate completed: rotated={} axis={} angle={}",
             rotatedCount,
             axis.toStdString(),
             angleDeg);
    return true;
}

void ModelOrientationWidgetHandler::onExit() {

    // 如果正在拖拽，先结束拖拽
    if (m_dragState.isDragging) {
        endDrag();
    }

    // 删除创建的Widget
    deleteModelOrientationWidget();

    if (auto* actionManager = ActionManager::getInstance();
        actionManager && actionManager->isAcceptingActions()) {
        actionManager->triggerAction(QStringLiteral("modelInteraction.resumeSelectionInput"));
    }

    m_activeModelId = DBInstanceID();
    m_widgetId = DBInstanceID();

    StandardActionHandler::onExit();
}

void ModelOrientationWidgetHandler::deleteModelOrientationWidget() {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        m_widgetId = DBInstanceID();
        m_activeModelId = DBInstanceID();
        return;
    }

    // 查找并删除所有的 ModelOrientationWidget
    auto modelOrientationWidgets = docManager->getDBInstancesByType(TypeID::MODEL_ORIENTATION_WIDGET_DB);

    if (!modelOrientationWidgets.empty()) {
        for (auto widget : modelOrientationWidgets) {
            if (widget) {
                docManager->unregisterDBInstance(widget->getDBInstanceID());
            }
        }
    }

    m_widgetId = DBInstanceID(0);
    m_activeModelId = DBInstanceID(0);

    // 更新SelectionBridge的widget状态
    if (auto* selectionBridge = SelectionBridge::instance()) {
        selectionBridge->updateOrientationWidgetStatus();
    }
}

bool ModelOrientationWidgetHandler::createOrGetWidget() {
    auto* docManager = DocumentManager::instance();
    if (!docManager || !m_activeModelId.isValid()) {
        return false;
    }

    // 检查是否已经存在关联到此模型的 widget
    auto instanceIds = docManager->getAllDBInstanceIds(TypeID::MODEL_ORIENTATION_WIDGET_DB);

    for (const auto& id : instanceIds) {
        auto instance = docManager->getDBInstance(id);
        if (auto widget = std::dynamic_pointer_cast<ModelOrientationWidgetDB>(instance)) {
            if (widget->getLinkedActorID() == m_activeModelId) {
                m_widgetId = id;
                widget->updateFromLinkedActor();
                widget->setVisible(true);
                return true;
            }
        }
    }

    // 获取模型实例
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
        LOG_ERROR("ModelOrientationWidgetHandler::createOrGetWidget - Invalid actor bounds");
        return false;
    }

    const Vector3 modelCenter = worldBounds.getCenter();
    Vector3 size = actor->localBounds().getSize();
    const Vector3 scale = actor->getScale();
    size.x *= std::abs(scale.x);
    size.y *= std::abs(scale.y);
    size.z *= std::abs(scale.z);
    float diagonal = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);

    TransactionGuard guard("Create ModelOrientationWidget");
    auto widget = trans::TransDB::create<ModelOrientationWidgetDB>(
        m_activeModelId, modelCenter, diagonal
    );
    if (!widget) {
        LOG_ERROR("ModelOrientationWidgetHandler::createOrGetWidget - Failed to create widget");
        return false;
    }

    widget->setName("Model Orientation Widget");
    widget->setModelBoundsMin(worldBounds.min);
    widget->setModelBoundsMax(worldBounds.max);

    // 设置圆环颜色（标准 XYZ 颜色方案）
    widget->setXRingColor(Color(1.0f, 0.0f, 0.0f));  // 红色 X 轴
    widget->setYRingColor(Color(0.0f, 1.0f, 0.0f));  // 绿色 Y 轴
    widget->setZRingColor(Color(0.0f, 0.0f, 1.0f));  // 蓝色 Z 轴

    // 设置视觉属性
    widget->setOpacity(0.8f);
    widget->setRingThickness(0.03f);
    widget->setInteractive(true);  // 确保Widget可以交互

    m_widgetId = widget->getDBInstanceID();

    // 更新SelectionBridge的widget状态
    if (auto* selectionBridge = SelectionBridge::instance()) {
        selectionBridge->updateOrientationWidgetStatus();
    }
    return true;
}

bool ModelOrientationWidgetHandler::onMousePressEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!interactionPickService().supportsFeature(
            m_interactionViewId,
            GPlatform::Rendering::PickFeature::SubTarget)) {
        return false;
    }
    if (!isActive()) {
        LOG_WARN("ModelOrientationWidgetHandler not active, returning false");
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
    if (pickResult.objectId == m_widgetId && pickResult.subTarget &&
        *pickResult.subTarget > 0) {

        return startDrag(m_widgetId,
                         static_cast<int>(*pickResult.subTarget),
                         event->pos());
    }

    return false;
}

bool ModelOrientationWidgetHandler::onMouseMoveEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!isActive()) return false;

    if (m_dragState.isDragging) {
        updateDrag(event->pos());
        return true;  // 消费事件
    }

    return false;
}

bool ModelOrientationWidgetHandler::onMouseReleaseEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!isActive()) return false;

    if (m_dragState.isDragging && event->button() == Qt::LeftButton) {
        endDrag();
        return true;  // 消费事件
    }

    return false;
}

bool ModelOrientationWidgetHandler::startDrag(const DBInstanceID& widgetId,
                                               int partId,
                                               const QPoint& mousePos) {
    m_dragState = DragState();
    if (partId < RING_X || partId > RING_Z) {
        LOG_WARN("ModelOrientationWidgetHandler::startDrag - Invalid part {}", partId);
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return false;
    }

    auto widget = std::dynamic_pointer_cast<ModelOrientationWidgetDB>(
        docManager->getDBInstance(widgetId));
    if (!widget) {
        LOG_ERROR("ModelOrientationWidgetHandler::startDrag - Widget {} not found",
                  widgetId.getValue());
        return false;
    }

    const DBInstanceID linkedActorId = widget->getLinkedActorID();
    auto actor = std::dynamic_pointer_cast<ActorDB>(
        docManager->getDBInstance(linkedActorId));
    if (!actor) {
        LOG_ERROR("ModelOrientationWidgetHandler::startDrag - Actor {} not found",
                  linkedActorId.getValue());
        return false;
    }

    const auto worldBounds = actor->worldBounds();
    if (!worldBounds.valid) {
        LOG_ERROR("ModelOrientationWidgetHandler::startDrag - Invalid world bounds");
        return false;
    }

    m_dragState.widgetId = widgetId;
    m_dragState.linkedActorId = linkedActorId;
    m_dragState.draggedPart = partId;
    m_dragState.initialTransform = actor->getTransformMatrix();
    m_dragState.pivot = worldBounds.getCenter();
    m_dragState.radius = std::max(widget->calculateRingRadius(), 1.0f);

    // Orca 风格的旋转架始终保持世界坐标方向，显示轴与实际旋转轴必须一致。
    m_dragState.worldAxis = normalized(worldAxisForPart(partId));
    if (vectorLength(m_dragState.worldAxis) <= 1e-6f) {
        m_dragState = DragState();
        return false;
    }

    m_dragState.fallbackAngleRad = mouseAngleInRotationPlane(mousePos);
    try {
        widget->setActiveRing(partId);
        widget->setAccumulatedRotation(Vector3());
        widget->setIsDragging(true);
    } catch (const std::exception& error) {
        LOG_ERROR("ModelOrientationWidgetHandler::startDrag - Failed to update widget: {}",
                  error.what());
        m_dragState = DragState();
        return false;
    }

    m_dragState.isDragging = true;
    TransactionManager::instance().beginTransaction("Model Rotation");
    m_dragState.hasActiveTransaction = true;
    return true;
}

void ModelOrientationWidgetHandler::updateDrag(const QPoint& currentPos) {
    if (!m_dragState.isDragging) return;

    ImmediateNotifyGuard immediateGuard;

    const RotationSample sample = calculateRotationSample(currentPos);
    if (std::abs(sample.angleDeg - m_dragState.currentAngleDeg) < 0.001f) {
        return;
    }

    if (applyRotationToLinkedActor(sample.angleDeg)) {
        updateWidgetAfterRotation(sample.angleDeg);
    }
}

bool ModelOrientationWidgetHandler::performSnapToGround() {
    if (!m_dragState.isDragging) {
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

    const auto worldBounds = actor->worldBounds();
    if (!worldBounds.valid) {
        LOG_ERROR("ModelOrientationWidgetHandler::performSnapToGround - Invalid world bounds");
        return false;
    }

    if (std::abs(worldBounds.min.z) > 0.001f) {
        Vector3 position = actor->getPosition();
        position.z -= worldBounds.min.z;
        actor->setPosition(position);
    }

    const auto verifyBounds = actor->worldBounds();
    if (!verifyBounds.valid || std::abs(verifyBounds.min.z) > 0.001f) {
        LOG_ERROR("ModelOrientationWidgetHandler::performSnapToGround - Verification failed, minZ={}",
                  verifyBounds.valid ? verifyBounds.min.z : 0.0f);
        return false;
    }
    return true;
}

void ModelOrientationWidgetHandler::endDrag() {
    if (!m_dragState.isDragging) {
        return;
    }

    const DBInstanceID widgetId = m_dragState.widgetId;
    // 旋转会改变模型的世界包围盒；鼠标释放时必须主动重新贴底。
    // 贴底发生在同一个事务提交前，因此一次撤销可同时恢复旋转和位移。
    const bool success = performSnapToGround();

    auto* docManager = DocumentManager::instance();
    auto widget = docManager
        ? std::dynamic_pointer_cast<ModelOrientationWidgetDB>(
              docManager->getDBInstance(widgetId))
        : nullptr;

    if (m_dragState.hasActiveTransaction) {
        if (success && docManager) {
            TransactionManager::instance().commitTransaction();
        } else {
            TransactionManager::instance().rollbackTransaction();
        }
    }

    if (widget) {
        widget->setActiveRing(NONE);
        widget->setIsDragging(false);
        widget->updateFromLinkedActor();
    }

    m_dragState = DragState();
}

ModelOrientationWidgetHandler::RotationSample
ModelOrientationWidgetHandler::calculateRotationSample(const QPoint& currentPos) const {
    RotationSample sample;
    float mouseRadius = 0.0f;
    float theta = mouseAngleInRotationPlane(currentPos, &mouseRadius);
    const float radius = std::max(1.0f, m_dragState.radius);

    const float coarseInner = radius / 3.0f;
    const float coarseOuter = 2.0f * coarseInner;
    const float fineInner = radius;
    const float fineOuter = radius * (1.0f + kFineSnapBand);

    sample.mouseRadius = mouseRadius;
    sample.inCoarseSnapRing = coarseInner <= mouseRadius && mouseRadius <= coarseOuter;
    sample.inFineSnapRing = fineInner <= mouseRadius && mouseRadius <= fineOuter;

    if (sample.inCoarseSnapRing) {
        const float step = 2.0f * kPi / static_cast<float>(kCoarseSnapSteps);
        theta = step * std::round(theta / step);
    } else if (sample.inFineSnapRing) {
        const float step = 2.0f * kPi / static_cast<float>(kFineSnapSteps);
        theta = step * std::round(theta / step);
    }

    if (std::abs(theta - 2.0f * kPi) <= 1e-6f) {
        theta = 0.0f;
    }
    sample.angleDeg = theta * 180.0f / kPi;
    return sample;
}

float ModelOrientationWidgetHandler::mouseAngleInRotationPlane(const QPoint& screenPos,
                                                                float* radiusOut) const {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return m_dragState.fallbackAngleRad;
    }

    const auto window = std::dynamic_pointer_cast<WindowDB>(
        docManager->getDBInstance(m_interactionViewId));
    const auto coordinateSystem = window ? window->getCoordinateSystem() : nullptr;
    if (!coordinateSystem) {
        LOG_ERROR("ModelOrientationWidgetHandler - Viewport coordinate system unavailable");
        return m_dragState.fallbackAngleRad;
    }

    const WorldRay ray = coordinateSystem->rayFromScreen(QPointF(screenPos));

    // gizmo 固定在世界坐标系，只需把选中世界轴的旋转平面转到 XY 平面。
    const Eigen::Matrix3f worldToPlane =
        mousePlaneTransformForPart(m_dragState.draggedPart);
    const Eigen::Vector3f relativeOrigin(ray.origin.x - m_dragState.pivot.x,
                                         ray.origin.y - m_dragState.pivot.y,
                                         ray.origin.z - m_dragState.pivot.z);
    const Eigen::Vector3f rayDirection(ray.direction.x,
                                       ray.direction.y,
                                       ray.direction.z);
    const Eigen::Vector3f localOrigin = worldToPlane * relativeOrigin;
    const Eigen::Vector3f transformedDirection = worldToPlane * rayDirection;
    if (transformedDirection.norm() <= 1e-6f) {
        return m_dragState.fallbackAngleRad;
    }
    const Eigen::Vector3f localDirection = transformedDirection.normalized();

    Eigen::Vector3f localPoint = Eigen::Vector3f::UnitX();
    if (std::abs(localDirection.z()) > 1e-6f) {
        const float rayParameter = -localOrigin.z() / localDirection.z();
        localPoint = localOrigin + rayParameter * localDirection;
    } else if (std::abs(localDirection.y()) <= 1.0f - 1e-6f) {
        localPoint = localOrigin.x() >= 0.0f
            ? localOrigin
            : localOrigin + localDirection;
    }

    const float x = localPoint.x();
    const float y = localPoint.y();
    if (radiusOut) {
        *radiusOut = std::sqrt(x * x + y * y);
    }
    if (std::abs(x) <= 1e-6f && std::abs(y) <= 1e-6f) {
        return m_dragState.fallbackAngleRad;
    }

    float angle = std::atan2(y, x);
    return angle < 0.0f ? angle + 2.0f * kPi : angle;
}

bool ModelOrientationWidgetHandler::applyRotationToLinkedActor(float angleDeg) {
    auto* docManager = DocumentManager::instance();
    auto actor = docManager
        ? std::dynamic_pointer_cast<ActorDB>(
              docManager->getDBInstance(m_dragState.linkedActorId))
        : nullptr;
    if (!actor) {
        LOG_ERROR("ModelOrientationWidgetHandler::applyRotationToLinkedActor - Actor missing");
        return false;
    }

    Transform transform;
    transform.setMatrix(rotationAroundPivot(angleDeg,
                                            m_dragState.worldAxis,
                                            m_dragState.pivot) *
                        m_dragState.initialTransform);
    actor->setTransform(transform);
    m_dragState.currentAngleDeg = angleDeg;
    return true;
}

void ModelOrientationWidgetHandler::updateWidgetAfterRotation(float angleDeg) {
    auto* docManager = DocumentManager::instance();
    auto widget = docManager
        ? std::dynamic_pointer_cast<ModelOrientationWidgetDB>(
              docManager->getDBInstance(m_dragState.widgetId))
        : nullptr;
    if (!widget) {
        return;
    }

    Vector3 rotation;
    switch (m_dragState.draggedPart) {
        case RING_X: rotation.x = angleDeg; break;
        case RING_Y: rotation.y = angleDeg; break;
        case RING_Z: rotation.z = angleDeg; break;
        default: break;
    }
    widget->setAccumulatedRotation(rotation);
}
