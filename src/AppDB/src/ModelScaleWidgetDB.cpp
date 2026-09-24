#include "ModelScaleWidgetDB.hpp"
#include "DocumentManager.hpp"
#include "ActorDB.hpp"
#include "Foundation/Log.h"

ModelScaleWidgetDB::ModelScaleWidgetDB()
    : WidgetDB(),
      m_hasInitialValues(false) {
    // 构造函数保持简单，初始化在onCreated中进行
}

ModelScaleWidgetDB::ModelScaleWidgetDB(const DBInstanceID& linkedActorId,
                                     const Vector3& modelBoundsMin,
                                     const Vector3& modelBoundsMax)
    : WidgetDB() {
    // 在构造函数中初始化属性
    // 这些值在 onCreated() 被调用之前就已经设置好
    // 注意：我们不在这里调用 setter，而是在 initializeProperties 中设置
    // 保存参数供 initializeProperties 使用
    m_initLinkedActorId = linkedActorId;
    m_initModelBoundsMin = modelBoundsMin;
    m_initModelBoundsMax = modelBoundsMax;
    m_hasInitialValues = true;
}

ModelScaleWidgetDB::~ModelScaleWidgetDB() {
    // 析构时不需要特殊处理
}

void ModelScaleWidgetDB::initializeProperties() {
    // 先调用父类的属性初始化
    WidgetDB::initializeProperties();

    // 然后设置子类特有属性
    initializeSubWidgetProperties();
}

void ModelScaleWidgetDB::initializeSubWidgetProperties() {
    // === 关联属性 ===
    // 如果使用带参数的构造函数，使用传入的值
    if (m_hasInitialValues) {
        Vector3 center = (m_initModelBoundsMin + m_initModelBoundsMax) * 0.5f;
        LOG_DEBUG("ModelScaleWidgetDB::initializeSubWidgetProperties - Using constructor values: min=({},{},{}), max=({},{},{}), center=({},{},{})",
                 m_initModelBoundsMin.x, m_initModelBoundsMin.y, m_initModelBoundsMin.z,
                 m_initModelBoundsMax.x, m_initModelBoundsMax.y, m_initModelBoundsMax.z,
                 center.x, center.y, center.z);

        // 直接设置属性，不触发 updateFromLinkedActor
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "LinkedActorID"), m_initLinkedActorId);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelCenter"), center);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelBoundsMin"), m_initModelBoundsMin);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelBoundsMax"), m_initModelBoundsMax);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelLocalBoundsMin"), m_initModelBoundsMin);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelLocalBoundsMax"), m_initModelBoundsMax);
        trans::TransDB::setProperty(trans::Prop(typeid(*this), "ModelReferenceTransform"), Transform());
    } else {
        setLinkedActorID(DBInstanceID()); // 默认无关联
    }

    // === 视觉属性 ===
    // Match OrcaSlicer GLGizmoBase::AXES_COLOR.
    setXHandleColor(Color(1.0f, 60.0f / 255.0f, 91.0f / 255.0f));
    setYHandleColor(Color(100.0f / 255.0f, 200.0f / 255.0f, 24.0f / 255.0f));
    setZHandleColor(Color(47.0f / 255.0f, 136.0f / 255.0f, 233.0f / 255.0f));
    setUniformHandleColor(Color(0.0f, 1.0f, 1.0f)); // Orca uniform scale - cyan

    // === 模型信息初始化 ===
    // 如果使用带参数的构造函数，这些值已经在上面设置
    // 只有使用默认构造函数时才需要设置默认值
    if (!m_hasInitialValues) {
        setModelCenter(Vector3(0.0f, 0.0f, 0.0f));
        setModelBoundsMin(Vector3(-1.0f, -1.0f, -1.0f));
        setModelBoundsMax(Vector3(1.0f, 1.0f, 1.0f));
        setModelLocalBoundsMin(Vector3(-1.0f, -1.0f, -1.0f));
        setModelLocalBoundsMax(Vector3(1.0f, 1.0f, 1.0f));
        setModelReferenceTransform(Transform());
    }

    // === 交互状态 ===
    setActiveHandle(-1);                          // 无激活手柄
    setConstrainedHandle(-1);                     // 无 Ctrl 约束手柄
    setIsDragging(false);                        // 未拖动

    // === 显示控制 ===
    setShowXHandle(true);               // 显示所有手柄
    setShowYHandle(true);
    setShowZHandle(true);
    setShowUniformHandle(true);

    // 覆写WidgetDB的一些默认值
    setName("ModelScaleWidget");
    setVisible(true);                   // 默认可见
    setInteractive(true);               // 需要交互以响应鼠标事件
}

void ModelScaleWidgetDB::onFullyInitialized() {
    // 调用父类的实现
    WidgetDB::onFullyInitialized();

    // 总是从当前关联 Actor 刷新精确包围盒。
    if (getLinkedActorID().getValue() != 0) {
        LOG_DEBUG("ModelScaleWidgetDB::onFullyInitialized - 强制更新包围盒 (LinkedActorID: {})", getLinkedActorID().getValue());
        updateFromLinkedActor();
    }

}

void ModelScaleWidgetDB::updateFromLinkedActor() {
    auto linkedId = getLinkedActorID();

    LOG_DEBUG("ModelScaleWidgetDB::updateFromLinkedActor - 开始更新包围盒数据 (LinkedActorID: {})", linkedId.getValue());

    if (linkedId.getValue() == 0) {
        LOG_WARN("ModelScaleWidgetDB::updateFromLinkedActor - No linked actor");
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("ModelScaleWidgetDB::updateFromLinkedActor - DocumentManager is null");
        return;
    }

    auto linkedActor = std::dynamic_pointer_cast<ActorDB>(
        docManager->getDBInstance(linkedId));

    if (!linkedActor) {
        LOG_ERROR("ModelScaleWidgetDB::updateFromLinkedActor - Failed to get linked actor with ID: {}", linkedId.getValue());
        return;
    }

    LOG_DEBUG("ModelScaleWidgetDB::updateFromLinkedActor - 成功获取 ActorDB 对象");

    // 记录当前的包围盒数据，用于对比
    Vector3 oldCenter = getModelCenter();
    Vector3 oldMin = getModelBoundsMin();
    Vector3 oldMax = getModelBoundsMax();

    LOG_DEBUG("当前 ModelScaleWidgetDB 的包围盒数据:");
    LOG_DEBUG("   中心点: ({:.6f}, {:.6f}, {:.6f})", oldCenter.x, oldCenter.y, oldCenter.z);
    LOG_DEBUG("   最小点: ({:.6f}, {:.6f}, {:.6f})", oldMin.x, oldMin.y, oldMin.z);
    LOG_DEBUG("   最大点: ({:.6f}, {:.6f}, {:.6f})", oldMax.x, oldMax.y, oldMax.z);

    // 对比Actor的放置位置（仅用于调试）
    Vector3 actorPosition = linkedActor->getPosition();
    LOG_DEBUG("Actor放置位置: ({:.6f}, {:.6f}, {:.6f})", actorPosition.x, actorPosition.y, actorPosition.z);

    // 获取原始包围盒（用于调试）
    const auto localBounds = linkedActor->localBounds();
    LOG_DEBUG("ActorDB 原始包围盒:");
    LOG_DEBUG("   最小点: ({:.6f}, {:.6f}, {:.6f})", localBounds.min.x, localBounds.min.y, localBounds.min.z);
    LOG_DEBUG("   最大点: ({:.6f}, {:.6f}, {:.6f})", localBounds.max.x, localBounds.max.y, localBounds.max.z);

    // 获取变换后的世界坐标包围盒
    const auto worldBounds = linkedActor->worldBounds();
    LOG_DEBUG("ActorDB 世界坐标包围盒:");
    LOG_DEBUG("   最小点: ({:.6f}, {:.6f}, {:.6f})", worldBounds.min.x, worldBounds.min.y, worldBounds.min.z);
    LOG_DEBUG("   最大点: ({:.6f}, {:.6f}, {:.6f})", worldBounds.max.x, worldBounds.max.y, worldBounds.max.z);

    // 使用世界坐标包围盒计算几何中心
    Vector3 geometricCenter = (worldBounds.min + worldBounds.max) * 0.5f;
    LOG_DEBUG("计算得到的世界坐标几何中心: ({:.6f}, {:.6f}, {:.6f})", geometricCenter.x, geometricCenter.y, geometricCenter.z);

    // 计算世界坐标包围盒大小和对角线
    Vector3 size = worldBounds.getSize();
    float diagonal = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    LOG_DEBUG("计算得到的世界坐标包围盒信息:");
    LOG_DEBUG("   尺寸: ({:.6f}, {:.6f}, {:.6f})", size.x, size.y, size.z);
    LOG_DEBUG("   对角线长度: {:.6f}", diagonal);

    // 开始设置属性
    LOG_DEBUG("开始设置 ModelScaleWidgetDB 属性...");
    setModelCenter(geometricCenter);
    LOG_DEBUG("设置中心点完成: ({:.6f}, {:.6f}, {:.6f})", geometricCenter.x, geometricCenter.y, geometricCenter.z);

    setModelBoundsMin(worldBounds.min);
    LOG_DEBUG("设置最小边界完成: ({:.6f}, {:.6f}, {:.6f})", worldBounds.min.x, worldBounds.min.y, worldBounds.min.z);

    setModelBoundsMax(worldBounds.max);
    LOG_DEBUG("设置最大边界完成: ({:.6f}, {:.6f}, {:.6f})", worldBounds.max.x, worldBounds.max.y, worldBounds.max.z);

    setModelLocalBoundsMin(localBounds.min);
    setModelLocalBoundsMax(localBounds.max);
    setModelReferenceTransform(linkedActor->getTransform());

    // 不再需要设置对角线，已经移除该属性
    LOG_DEBUG("包围盒更新完成，对角线长度: {:.6f}", diagonal);

    // 验证设置是否成功
    Vector3 newCenter = getModelCenter();
    Vector3 newMin = getModelBoundsMin();
    Vector3 newMax = getModelBoundsMax();

    LOG_DEBUG("验证更新后的 ModelScaleWidgetDB 包围盒数据:");
    LOG_DEBUG("   新中心点: ({:.6f}, {:.6f}, {:.6f})", newCenter.x, newCenter.y, newCenter.z);
    LOG_DEBUG("   新最小点: ({:.6f}, {:.6f}, {:.6f})", newMin.x, newMin.y, newMin.z);
    LOG_DEBUG("   新最大点: ({:.6f}, {:.6f}, {:.6f})", newMax.x, newMax.y, newMax.z);

    // 检查是否真的发生了变化
    bool centerChanged = (newCenter.x != oldCenter.x || newCenter.y != oldCenter.y || newCenter.z != oldCenter.z);
    bool boundsChanged = (newMin.x != oldMin.x || newMin.y != oldMin.y || newMin.z != oldMin.z ||
                         newMax.x != oldMax.x || newMax.y != oldMax.y || newMax.z != oldMax.z);

    if (centerChanged || boundsChanged) {
        LOG_DEBUG("包围盒数据已成功更新");
    } else {
        LOG_DEBUG("包围盒数据未变化：当前包围盒已与关联模型一致");
    }

    LOG_DEBUG("ModelScaleWidgetDB::updateFromLinkedActor - 完成包围盒更新");
}

void ModelScaleWidgetDB::setLinkedActor(const DBInstanceID& actorId) {
    setLinkedActorID(actorId);

    // 只有在包围盒信息未设置时才更新
    // 如果在创建时已经设置了正确的包围盒，就不需要再更新
    Vector3 currentSize = getModelBoundsMax() - getModelBoundsMin();
    float currentDiagonal = std::sqrt(currentSize.x * currentSize.x + currentSize.y * currentSize.y + currentSize.z * currentSize.z);
    if (getDBInstanceID().getValue() != 0 && currentDiagonal <= 0.01f) {
        updateFromLinkedActor();
    }
}
