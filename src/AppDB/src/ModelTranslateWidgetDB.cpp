#include "ModelTranslateWidgetDB.hpp"
#include "DocumentManager.hpp"
#include "ActorDB.hpp"
#include "Foundation/Log.h"
#include <algorithm>
#include <cmath>

ModelTranslateWidgetDB::ModelTranslateWidgetDB()
    : WidgetDB(),
      m_hasInitialValues(false) {
    // 构造函数保持简单，初始化在onCreated中进行
}

ModelTranslateWidgetDB::ModelTranslateWidgetDB(const DBInstanceID& linkedActorId,
                                             const Vector3& actorPosition)
    : WidgetDB() {
    // 在构造函数中初始化属性
    // 这些值在 onCreated() 被调用之前就已经设置好
    m_initLinkedActorId = linkedActorId;
    m_initWidgetCenter = actorPosition;
    m_hasInitialValues = true;
}

ModelTranslateWidgetDB::~ModelTranslateWidgetDB() {
    // 析构时不需要特殊处理
}

void ModelTranslateWidgetDB::initializeProperties() {
    // WidgetDB::initializeProperties() 会通过虚函数钩子调用
    // initializeSubWidgetProperties()，这里不能再调用一次。
    WidgetDB::initializeProperties();
}

void ModelTranslateWidgetDB::initializeSubWidgetProperties() {
    // === 关联属性 ===
    // 如果使用带参数的构造函数，使用传入的值
    if (m_hasInitialValues) {
        LOG_INFO("ModelTranslateWidgetDB::initializeSubWidgetProperties - Using constructor values: position=({},{},{})",
                 m_initWidgetCenter.x, m_initWidgetCenter.y, m_initWidgetCenter.z);

        // 直接设置属性，不触发 updateFromLinkedActor
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "LinkedActorID"), m_initLinkedActorId);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "WidgetCenter"), m_initWidgetCenter);
    } else {
        setLinkedActorID(DBInstanceID()); // 默认无关联
    }

    // === 视觉属性 ===
    // 使用标准的XYZ颜色方案
    setXAxisColor(Color(1.0f, 0.0f, 0.0f));        // X轴 - 红色
    setYAxisColor(Color(0.0f, 1.0f, 0.0f));        // Y轴 - 绿色
    setZAxisColor(Color(0.0f, 0.0f, 1.0f));        // Z轴 - 蓝色

    // === 位置信息初始化 ===
    if (!m_hasInitialValues) {
        setWidgetCenter(Vector3(0.0f, 0.0f, 0.0f));
    }
    setModelBoundsMin(Vector3(0.0f, 0.0f, 0.0f));
    setModelBoundsMax(Vector3(0.0f, 0.0f, 0.0f));
    setXArrowPosition(Vector3(0.0f, 0.0f, 0.0f));
    setYArrowPosition(Vector3(0.0f, 0.0f, 0.0f));
    setZArrowPosition(Vector3(0.0f, 0.0f, 0.0f));

    // === 显示控制 ===
    setShowXArrow(true);        // 显示所有轴
    setShowYArrow(true);
    setShowZArrow(true);

    // 覆写WidgetDB的一些默认值
    setName("ModelTranslateWidget");
    setVisible(true);                   // 默认可见
    setInteractive(true);               // 需要交互以响应鼠标事件
}

void ModelTranslateWidgetDB::onFullyInitialized() {
    // 调用父类的实现
    WidgetDB::onFullyInitialized();

    // 强制更新Actor位置信息
    if (getLinkedActorID().getValue() != 0) {
        LOG_DEBUG("ModelTranslateWidgetDB::onFullyInitialized - 强制更新位置信息 (LinkedActorID: {})", getLinkedActorID().getValue());
        updateFromLinkedActor();
    }

}

void ModelTranslateWidgetDB::updateFromLinkedActor() {
    auto linkedId = getLinkedActorID();

    LOG_DEBUG("ModelTranslateWidgetDB::updateFromLinkedActor - updating linked actor {}", linkedId.getValue());

    if (linkedId.getValue() == 0) {
        LOG_WARN("ModelTranslateWidgetDB::updateFromLinkedActor - No linked actor");
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("ModelTranslateWidgetDB::updateFromLinkedActor - DocumentManager is null");
        return;
    }

    auto linkedActor = std::dynamic_pointer_cast<ActorDB>(
        docManager->getDBInstance(linkedId));

    if (!linkedActor) {
        LOG_ERROR("ModelTranslateWidgetDB::updateFromLinkedActor - Failed to get linked actor with ID: {}", linkedId.getValue());
        return;
    }

    LOG_DEBUG("ModelTranslateWidgetDB::updateFromLinkedActor - 成功获取 ActorDB 对象");

    Vector3 oldCenter = getWidgetCenter();

    LOG_DEBUG("当前位置数据: Widget中心({:.2f},{:.2f},{:.2f})",
             oldCenter.x, oldCenter.y, oldCenter.z);

    // 获取Actor的当前位置，拖拽时 Actor position 会基于它叠加位移。
    Vector3 actorPosition = linkedActor->getPosition();
    LOG_DEBUG("Actor当前位置: ({:.2f}, {:.2f}, {:.2f})", actorPosition.x, actorPosition.y, actorPosition.z);

    // 获取 Actor 的世界包围盒。Orca move gizmo 以选择包围盒中心为坐标系中心。
    const auto worldBounds = linkedActor->worldBounds();
    const Vector3 boundsCenter = worldBounds.valid ? worldBounds.getCenter() : actorPosition;
    const Vector3 boundsMin = worldBounds.valid ? worldBounds.min : actorPosition;
    const Vector3 boundsMax = worldBounds.valid ? worldBounds.max : actorPosition;
    Vector3 size = worldBounds.valid ? worldBounds.getSize() : Vector3(0.0f, 0.0f, 0.0f);
    float diagonal = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);

    LOG_DEBUG("Actor世界包围盒: min({:.2f},{:.2f},{:.2f}) max({:.2f},{:.2f},{:.2f}) 尺寸({:.2f},{:.2f},{:.2f}), 对角线{:.2f}",
              boundsMin.x, boundsMin.y, boundsMin.z,
              boundsMax.x, boundsMax.y, boundsMax.z,
              size.x, size.y, size.z, diagonal);

    // 开始设置属性
    LOG_DEBUG("开始设置 Widget 属性");
    setWidgetCenter(boundsCenter);
    LOG_DEBUG("设置Widget中心: ({:.2f}, {:.2f}, {:.2f})", boundsCenter.x, boundsCenter.y, boundsCenter.z);

    setModelBoundsMin(boundsMin);
    setModelBoundsMax(boundsMax);

    // 验证设置是否成功
    Vector3 newCenter = getWidgetCenter();

    // 检查是否真的发生了变化
    bool centerChanged = (newCenter.x != oldCenter.x || newCenter.y != oldCenter.y || newCenter.z != oldCenter.z);

    if (centerChanged) {
        LOG_DEBUG("位置数据已成功更新");
    } else {
        LOG_DEBUG("位置数据未变化：当前中心已与关联模型包围盒中心一致");
    }

    // 更新所有手柄的位置
    updateHandlePositions();

    LOG_DEBUG("ModelTranslateWidgetDB::updateFromLinkedActor - 完成位置更新");
}

void ModelTranslateWidgetDB::setLinkedActor(const DBInstanceID& actorId) {
    setLinkedActorID(actorId);

    // 总是更新位置信息，因为位置可能随时变化
    if (getDBInstanceID().getValue() != 0) {
        updateFromLinkedActor();
    }
}

void ModelTranslateWidgetDB::updateHandlePositions() {
    const Vector3 center = getWidgetCenter();
    const Vector3 boundsMin = getModelBoundsMin();
    const Vector3 boundsMax = getModelBoundsMax();
    const Vector3 size = boundsMax - boundsMin;
    const float diagonal = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    const float handleGap = std::clamp(diagonal * 0.08f, 5.0f, 20.0f);

    LOG_DEBUG("ModelTranslateWidgetDB::updateHandlePositions - Center: ({:.3f},{:.3f},{:.3f}), Gap: {:.3f}",
              center.x, center.y, center.z, handleGap);

    // Orca 位置规则：沿当前参考系从包围盒中心连接到 max 边界外侧的 grabber。
    Vector3 xArrowEnd(boundsMax.x + handleGap, center.y, center.z);
    Vector3 yArrowEnd(center.x, boundsMax.y + handleGap, center.z);
    Vector3 zArrowEnd(center.x, center.y, boundsMax.z + handleGap);

    setXArrowPosition(xArrowEnd);
    setYArrowPosition(yArrowEnd);
    setZArrowPosition(zArrowEnd);

    // Position/bounds setters above still participate in persistence and undo,
    // but rendering only needs one consolidated geometry refresh per update.
    notifyChange(ChangeType::PROPERTY_CHANGED, "Geometry");

    LOG_DEBUG("ModelTranslateWidgetDB::updateHandlePositions - Updated positions: X({:.3f},{:.3f},{:.3f}), Y({:.3f},{:.3f},{:.3f}), Z({:.3f},{:.3f},{:.3f})",
              xArrowEnd.x, xArrowEnd.y, xArrowEnd.z,
              yArrowEnd.x, yArrowEnd.y, yArrowEnd.z,
              zArrowEnd.x, zArrowEnd.y, zArrowEnd.z);
}
