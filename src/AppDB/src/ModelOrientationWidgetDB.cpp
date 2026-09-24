#include "ModelOrientationWidgetDB.hpp"
#include <DocumentManager.hpp>
#include "ActorDB.hpp"
#include "Foundation/Log.h"

ModelOrientationWidgetDB::ModelOrientationWidgetDB()
    : WidgetDB(),
      m_hasInitialValues(false) {
    // 构造函数保持简单，初始化在onCreated中进行
}

ModelOrientationWidgetDB::ModelOrientationWidgetDB(const DBInstanceID& linkedActorId,
                                                 const Vector3& modelCenter,
                                                 float boundsDiagonal)
    : WidgetDB() {
    // 在构造函数中初始化属性
    // 这些值在 onCreated() 被调用之前就已经设置好
    // 注意：我们不在这里调用 setter，而是在 initializeProperties 中设置
    // 保存参数供 initializeProperties 使用
    m_initLinkedActorId = linkedActorId;
    m_initModelCenter = modelCenter;
    m_initBoundsDiagonal = boundsDiagonal;
    m_hasInitialValues = true;
}

ModelOrientationWidgetDB::~ModelOrientationWidgetDB() {
    // 析构时不需要特殊处理
}

void ModelOrientationWidgetDB::initializeProperties() {
    // WidgetDB::initializeProperties() 会通过虚函数钩子调用
    // initializeSubWidgetProperties()，这里不能重复初始化。
    WidgetDB::initializeProperties();
}

void ModelOrientationWidgetDB::initializeSubWidgetProperties() {
    // === 关联属性 ===
    // 如果使用带参数的构造函数，使用传入的值
    if (m_hasInitialValues) {
        LOG_INFO("ModelOrientationWidgetDB::initializeSubWidgetProperties - Using constructor values: center=({},{},{}), diagonal={}",
                 m_initModelCenter.x, m_initModelCenter.y, m_initModelCenter.z, m_initBoundsDiagonal);

        // 直接设置属性，不触发 updateFromLinkedActor
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "LinkedActorID"), m_initLinkedActorId);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelCenter"), m_initModelCenter);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "BoundsDiagonal"), m_initBoundsDiagonal);

        // 设置包围盒（虽然没有使用，但保持一致性）
        float halfSize = m_initBoundsDiagonal * 0.3f;
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelBoundsMin"),
            m_initModelCenter - Vector3(halfSize, halfSize, halfSize));
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelBoundsMax"),
            m_initModelCenter + Vector3(halfSize, halfSize, halfSize));
    } else {
        setLinkedActorID(DBInstanceID()); // 默认无关联
    }

    // === 尺寸控制 ===
    setAutoScale(true);              // 默认自动缩放
    setScaleFactor(1.0f);           // 默认缩放因子
    setRingThickness(0.02f);        // 圆环粗细（相对于半径）
    setRingResolution(64);          // 圆环分辨率（64段）

    // === 视觉属性 ===
    // 使用标准的XYZ颜色方案
    setXRingColor(Color(1.0f, 0.2f, 0.2f));    // X轴 - 红色
    setYRingColor(Color(0.2f, 1.0f, 0.2f));    // Y轴 - 绿色
    setZRingColor(Color(0.2f, 0.2f, 1.0f));    // Z轴 - 蓝色

    setHighlightColor(Color(1.0f, 1.0f, 0.0f)); // 黄色高亮
    setOpacity(0.7f);                           // 半透明

    // === 模型信息初始化 ===
    // 如果使用带参数的构造函数，这些值已经在上面设置
    // 只有使用默认构造函数时才需要设置默认值
    if (!m_hasInitialValues) {
        setModelCenter(Vector3(0.0f, 0.0f, 0.0f));
        setModelBoundsMin(Vector3(-1.0f, -1.0f, -1.0f));
        setModelBoundsMax(Vector3(1.0f, 1.0f, 1.0f));
        setBoundsDiagonal(0.0f);  // 设置为0，表示还未从实际模型获取包围盒
    }

    // === 交互状态 ===
    setActiveRing(-1);                 // 无激活圆环
    setIsDragging(false);             // 未拖动
    setAccumulatedRotation(Vector3(0.0f, 0.0f, 0.0f));

    // === 显示控制 ===
    setShowXRing(true);               // 显示所有圆环
    setShowYRing(true);
    setShowZRing(true);

    // 覆写WidgetDB的一些默认值
    setName("ModelOrientationWidget");
    setVisible(true);                 // 默认可见
    setInteractive(true);             // 需要交互以响应鼠标事件
}

void ModelOrientationWidgetDB::onFullyInitialized() {
    // 调用父类的实现
    WidgetDB::onFullyInitialized();

    // 只有在包围盒信息未设置时才更新
    // 如果在创建时已经设置了正确的包围盒，就不需要再更新
    if (getLinkedActorID().getValue() != 0 && getBoundsDiagonal() <= 0.01f) {
        updateFromLinkedActor();
    }

}

void ModelOrientationWidgetDB::updateFromLinkedActor() {
    auto linkedId = getLinkedActorID();

    if (linkedId.getValue() == 0) {
        LOG_WARN("ModelOrientationWidgetDB::updateFromLinkedActor - No linked actor");
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("ModelOrientationWidgetDB::updateFromLinkedActor - DocumentManager is null");
        return;
    }

    auto linkedActor = std::dynamic_pointer_cast<ActorDB>(
        docManager->getDBInstance(linkedId));

    if (!linkedActor) {
        LOG_ERROR("ModelOrientationWidgetDB::updateFromLinkedActor - Failed to get linked actor");
        return;
    }

    // 中心使用当前世界包围盒；半径使用本地包围盒乘缩放，避免模型旋转后
    // 世界 AABB 尺寸改变而让旋转环忽大忽小。
    const auto worldBounds = linkedActor->worldBounds();
    const auto localBounds = linkedActor->localBounds();

    setModelCenter(worldBounds.valid ? worldBounds.getCenter() : linkedActor->getPosition());
    setModelBoundsMin(worldBounds.valid ? worldBounds.min : localBounds.min);
    setModelBoundsMax(worldBounds.valid ? worldBounds.max : localBounds.max);

    Vector3 size = localBounds.getSize();
    const Vector3 scale = linkedActor->getScale();
    size.x *= std::abs(scale.x);
    size.y *= std::abs(scale.y);
    size.z *= std::abs(scale.z);
    float diagonal = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    setBoundsDiagonal(diagonal);

}

void ModelOrientationWidgetDB::setLinkedActor(const DBInstanceID& actorId) {

    setLinkedActorID(actorId);

    // 只有在包围盒信息未设置时才更新
    // 如果在创建时已经设置了正确的包围盒，就不需要再更新
    if (getDBInstanceID().getValue() != 0 && getBoundsDiagonal() <= 0.01f) {
        updateFromLinkedActor();
    }
}
