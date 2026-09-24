#include "../include/CubeDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"

CubeDB::CubeDB()
    : ActorDB() {
    // 不再需要设置回调 - FIELD_GEOMETRY_VALUE宏会自动调用markGeometryChanged
}

CubeDB::~CubeDB() {
}

// === 实现 ActorDB 的钩子函数 ===

void CubeDB::initializeSubActorProperties() {
    // 只需要初始化 CubeDB 特有的属性
    // 不需要调用父类，ActorDB 已经处理了通用属性
    setCenter(Vector3(0, 0, 0)); // 中心点默认在原点
    setXLength(1.0f);            // X方向长度默认为1
    setYLength(1.0f);            // Y方向长度默认为1
    setZLength(1.0f);            // Z方向长度默认为1
}

void CubeDB::afterSubActorPropertiesInitialized() {
    // CubeDB 专属：设置显眼的红色作为默认颜色
    auto material = getMaterial();
    if (material) {
        material->setDiffuseColor(Vector3(1.0f, 0.0f, 0.0f)); // 纯红色
        LOG_INFO("CubeDB::afterSubActorPropertiesInitialized - Set red color for Cube ID: {}", 
                 getDBInstanceID().toString());
    }
}

// === ActorDB接口实现 ===

ActorDB::BoundingBox CubeDB::localBounds() const {
    // 使用线程安全的属性访问方法（不需要手动加锁）
    const Vector3 center = getCenter();
    float xLen = getPropertySafe<float>("XLength");
    float yLen = getPropertySafe<float>("YLength");
    float zLen = getPropertySafe<float>("ZLength");

    BoundingBox bounds;

    // 计算立方体的边界框
    float halfX = xLen * 0.5f;
    float halfY = yLen * 0.5f;
    float halfZ = zLen * 0.5f;

    bounds.min = Vector3(center.x - halfX, center.y - halfY, center.z - halfZ);

    bounds.max = Vector3(center.x + halfX, center.y + halfY, center.z + halfZ);

    bounds.valid = true;

    return bounds;
}

std::shared_ptr<AutoRegisterDB> CubeDB::clone() const {
    auto lock = getSharedLock();

    auto cloned = trans::TransDB::create<CubeDB>();

    // 复制几何属性
    cloned->setCenter(getCenter());
    cloned->setXLength(getXLength());
    cloned->setYLength(getYLength());
    cloned->setZLength(getZLength());

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



std::vector<std::string_view> CubeDB::getPropertyNames() const {

    return ActorDB::getPropertyNames();
}

PropertyMap CubeDB::serialize() const {
    // 直接使用基类的序列化
    // 所有属性都已经在属性字典中
    PropertyMap props = ActorDB::serialize();
    return props;
}

bool CubeDB::deserialize(const PropertyMap& properties) {
    // 直接使用基类的反序列化
    // 基类会处理所有在属性字典中的属性
    return ActorDB::deserialize(properties);
}

// === 私有方法 ===

void CubeDB::markGeometryChanged(const trans::Prop& prop) {
    // 直接通知变化，不需要设置标志
    notifyGeometryChange(prop.name());
}
