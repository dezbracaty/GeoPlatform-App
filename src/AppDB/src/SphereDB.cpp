#include "../include/SphereDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"
#include <cmath>
#include <algorithm>

SphereDB::SphereDB()
    : ActorDB() {
    // 构造函数保持简单，所有初始化移到 onCreated() 中
}

SphereDB::~SphereDB() {
}

// === 实现 ActorDB 的钩子函数 ===

void SphereDB::initializeSubActorProperties() {
    // 只需要初始化 SphereDB 特有的属性
    // 不需要调用父类，ActorDB 已经处理了通用属性
    setCenter(Vector3(0, 0, 0)); // 球心在原点
    setRadius(0.5f);             // 默认半径0.5

    // 细分属性
    setThetaResolution(16); // 经度细分16
    setPhiResolution(16);   // 纬度细分16

    // 部分球体属性
    setStartTheta(0.0f); // 起始经度角0
    setEndTheta(360.0f); // 结束经度角360（完整圆）
    setStartPhi(0.0f);   // 起始纬度角0
    setEndPhi(180.0f);   // 结束纬度角180（完整球）

    // 细分模式
    setLatLongTessellation(false); // 默认不使用经纬度细分模式
}

void SphereDB::afterSubActorPropertiesInitialized() {
    // SphereDB 专属：设置显眼的绿色作为默认颜色
    auto material = getMaterial();
    if (material) {
        material->setDiffuseColor(Vector3(0.0f, 1.0f, 0.0f)); // 纯绿色
        LOG_INFO("SphereDB::afterSubActorPropertiesInitialized - Set green color for Sphere ID: {}", 
                 getDBInstanceID().toString());
    }
}

// === ActorDB接口实现 ===

ActorDB::BoundingBox SphereDB::localBounds() const {
    BoundingBox bounds;

    const Vector3 center = getCenter();
    const float radius = getRadius();

    bounds.min = Vector3(
        center.x - radius,
        center.y - radius,
        center.z - radius);

    bounds.max = Vector3(
        center.x + radius,
        center.y + radius,
        center.z + radius);

    bounds.valid = true;

    return bounds;
}

std::shared_ptr<AutoRegisterDB> SphereDB::clone() const {
    auto lock = getSharedLock();

    auto cloned = trans::TransDB::create<SphereDB>();

    // 复制几何属性
    cloned->setCenter(getCenter());
    cloned->setRadius(getRadius());
    cloned->setThetaResolution(getThetaResolution());
    cloned->setPhiResolution(getPhiResolution());
    cloned->setStartTheta(getStartTheta());
    cloned->setEndTheta(getEndTheta());
    cloned->setStartPhi(getStartPhi());
    cloned->setEndPhi(getEndPhi());
    cloned->setLatLongTessellation(getLatLongTessellation());

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

// === AutoRegisterDB接口扩展实现 ===



std::vector<std::string_view> SphereDB::getPropertyNames() const {
    // All properties are handled by the base class property system
    return ActorDB::getPropertyNames();
}

PropertyMap SphereDB::serialize() const {
    // 直接使用基类的序列化
    // 所有属性都已经在属性字典中
    PropertyMap props = ActorDB::serialize();
    return props;
}

bool SphereDB::deserialize(const PropertyMap& properties) {
    // 直接使用基类的反序列化
    // 基类会处理所有在属性字典中的属性
    return ActorDB::deserialize(properties);
}

// === 私有方法 ===

void SphereDB::markGeometryChanged(const trans::Prop& prop) {
    // 直接通知变化，不需要设置标志
    notifyGeometryChange(prop.name());
}
