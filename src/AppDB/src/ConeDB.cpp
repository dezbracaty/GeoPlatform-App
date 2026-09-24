#include "../include/ConeDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"
#include <algorithm>

ConeDB::ConeDB()
    : ActorDB() {
    // 构造函数保持简单，所有初始化移到 onCreated() 中
}

ConeDB::~ConeDB() {
}

// === 实现 ActorDB 的钩子函数 ===

void ConeDB::initializeSubActorProperties() {
    // 只需要初始化 ConeDB 特有的属性
    // 不需要调用父类，ActorDB 已经处理了通用属性
    setCenter(Vector3(0, 0, 0)); // 中心在原点
    setHeight(1.0f);             // 默认高度1.0
    setRadius(0.5f);             // 默认底面半径0.5
    setResolution(64);           // 默认分辨率64，提高渲染质量
    setDirection(2);             // 默认Z轴方向（2=Z轴，Z-up坐标系）
    setCapping(true);            // 默认有底面端盖
}

void ConeDB::afterSubActorPropertiesInitialized() {
    // ConeDB 专属：设置显眼的黄色作为默认颜色
    auto material = getMaterial();
    if (material) {
        material->setDiffuseColor(Vector3(1.0f, 1.0f, 0.0f)); // 纯黄色
        LOG_INFO("ConeDB::afterSubActorPropertiesInitialized - Set yellow color for Cone ID: {}", 
                 getDBInstanceID().toString());
    }
}

ActorDB::BoundingBox ConeDB::localBounds() const {
    BoundingBox bounds;

    const Vector3 center = getCenter();
    const float height = getHeight();
    const float radius = getRadius();
    const int direction = getDirection();

    // 根据方向计算边界框
    float halfHeight = height * 0.5f;

    if (direction == 0) { // X轴
        bounds.min = Vector3(center.x - halfHeight, center.y - radius, center.z - radius);
        bounds.max = Vector3(center.x + halfHeight, center.y + radius, center.z + radius);
    } else if (direction == 1) { // Y轴
        bounds.min = Vector3(center.x - radius, center.y - halfHeight, center.z - radius);
        bounds.max = Vector3(center.x + radius, center.y + halfHeight, center.z + radius);
    } else { // Z轴
        bounds.min = Vector3(center.x - radius, center.y - radius, center.z - halfHeight);
        bounds.max = Vector3(center.x + radius, center.y + radius, center.z + halfHeight);
    }

    bounds.valid = true;

    return bounds;
}

std::shared_ptr<AutoRegisterDB> ConeDB::clone() const {
    auto lock = getSharedLock();

    auto cloned = trans::TransDB::create<ConeDB>();

    // 复制几何属性
    cloned->setCenter(getCenter());
    cloned->setHeight(getHeight());
    cloned->setRadius(getRadius());
    cloned->setResolution(getResolution());
    cloned->setDirection(getDirection());
    cloned->setCapping(getCapping());

    // 复制基类属性
    cloned->copyTransformFrom(*this);
    cloned->setVisible(isVisible());
    auto srcMaterial = getMaterial();
    auto dstMaterial = cloned->getMaterial();
    if (srcMaterial && dstMaterial) {
        dstMaterial->deserialize(srcMaterial->serialize());
    }
    cloned->setPickable(isPickable());
    cloned->setDragable(isDragable());

    return cloned;
}

// getProperty 和 setProperty 使用基类 ActorDB 的实现
// 不需要重写，因为没有特殊处理

std::vector<std::string_view> ConeDB::getPropertyNames() const {

    return ActorDB::getPropertyNames();
}

PropertyMap ConeDB::serialize() const {
    // 直接使用基类的序列化
    // 所有属性都已经在属性字典中
    PropertyMap props = ActorDB::serialize();
    return props;
}

bool ConeDB::deserialize(const PropertyMap& properties) {
    // 直接使用基类的反序列化
    // 基类会处理所有在属性字典中的属性
    return ActorDB::deserialize(properties);
}

void ConeDB::markGeometryChanged(const trans::Prop& prop) {
    // 直接通知变化，不需要设置标志
    notifyGeometryChange(prop.name());
}
