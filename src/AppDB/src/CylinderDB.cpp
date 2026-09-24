#include "../include/CylinderDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"

CylinderDB::CylinderDB()
    : ActorDB() {
    // 构造函数保持简单，所有初始化移到 onCreated() 中
}

CylinderDB::~CylinderDB() {
}

// === 实现 ActorDB 的钩子函数 ===

void CylinderDB::initializeSubActorProperties() {
    // 只需要初始化 CylinderDB 特有的属性
    // 不需要调用父类，ActorDB 已经处理了通用属性
    setCenter(Vector3(0, 0, 0)); // 中心在原点
    setHeight(1.0f);             // 默认高度1.0
    setRadius(0.5f);             // 默认半径0.5
    setResolution(32);           // 默认分辨率32
    setCapping(true);            // 默认有端盖
}

void CylinderDB::afterSubActorPropertiesInitialized() {
    // CylinderDB 专属：设置显眼的蓝色作为默认颜色
    auto material = getMaterial();
    if (material) {
        material->setDiffuseColor(Vector3(0.0f, 0.0f, 1.0f)); // 纯蓝色
        LOG_INFO("CylinderDB::afterSubActorPropertiesInitialized - Set blue color for Cylinder ID: {}", 
                 getDBInstanceID().toString());
    }
}

ActorDB::BoundingBox CylinderDB::localBounds() const {
    BoundingBox bounds;

    const Vector3 center = getCenter();
    const float height = getHeight();
    const float radius = getRadius();

    // 计算圆柱体的边界框
    float halfHeight = height * 0.5f;

    bounds.min = Vector3(center.x - radius, center.y - halfHeight, center.z - radius);

    bounds.max = Vector3(center.x + radius, center.y + halfHeight, center.z + radius);

    bounds.valid = true;

    return bounds;
}

std::shared_ptr<AutoRegisterDB> CylinderDB::clone() const {
    auto lock = getSharedLock();

    auto cloned = trans::TransDB::create<CylinderDB>();

    // 复制几何属性
    cloned->setCenter(getCenter());
    cloned->setHeight(getHeight());
    cloned->setRadius(getRadius());
    cloned->setResolution(getResolution());
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



std::vector<std::string_view> CylinderDB::getPropertyNames() const {
    // Property names are handled by the base class
    return ActorDB::getPropertyNames();
}

PropertyMap CylinderDB::serialize() const {
    // 直接使用基类的序列化
    // 所有属性都已经在属性字典中
    PropertyMap props = ActorDB::serialize();
    return props;
}

bool CylinderDB::deserialize(const PropertyMap& properties) {
    // 直接使用基类的反序列化
    // 基类会处理所有在属性字典中的属性
    return ActorDB::deserialize(properties);
}

void CylinderDB::markGeometryChanged(const trans::Prop& prop) {
    // 直接通知变化，不需要设置标志
    notifyGeometryChange(prop.name());
}
