#include "../include/ActorDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"

ActorDB::ActorDB()
    : AutoRegisterDB() {
    // Material 初始化移到 onCreated() 中
}

ActorDB::~ActorDB() {
    // 清理资源
}

// === 实现 AutoRegisterDB 的钩子函数 ===

void ActorDB::initializeProperties() {
    // 1. 初始化 ActorDB 的通用属性
    initializeActorProperties();

    // 2. 调用子类钩子，让子类初始化自己的属性
    initializeSubActorProperties();
}

void ActorDB::afterPropertiesInitialized() {
    afterSubActorPropertiesInitialized();
}

void ActorDB::onFullyInitialized() {
    if (!ownsDefaultMaterial()) return;
    auto* document = DocumentManager::instance();
    if (!document) return;
    if (!document->registerOwnershipRelation(
            getTypeID(), TypeID::MATERIAL_DB, kMaterialRelation,
            false, DBRelationDeletePolicy::CascadeDelete)) {
        LOG_ERROR("ActorDB::onFullyInitialized - Failed to register material relation for type {}",
                  static_cast<uint32_t>(getTypeID()));
        return;
    }
    initializeMaterial();
}

// === 私有方法实现 ===

void ActorDB::initializeActorProperties() {
    // 只初始化 Transform 属性，Position/Scale 将从 Transform 中派生
    if (!hasProperty(trans::Prop(typeid(*this), "Transform"))) {
        Transform transform;
        transform.identity();
        transform.setPosition(Vector3(0, 0, 0));
        transform.setScale(Vector3(1, 1, 1));
        setTransform(transform);
    }

    // 初始化可见性属性
    if (!hasProperty(trans::Prop(typeid(*this), "Visible"))) {
        setVisible(true);
    }

    // 初始化交互属性
    if (!hasProperty(trans::Prop(typeid(*this), "Pickable"))) {
        setPickable(true);
    }
    if (!hasProperty(trans::Prop(typeid(*this), "Dragable"))) {
        setDragable(true);
    }

    // 初始化透明度
    if (!hasProperty(trans::Prop(typeid(*this), "Opacity"))) {
        setOpacity(1.0f);
    }

    // 初始化独立的缩放属性
    if (!hasProperty(trans::Prop(typeid(*this), "Scale"))) {
        setScale(Vector3(1.0f, 1.0f, 1.0f));
    }
}

// Material初始化辅助方法
void ActorDB::initializeMaterial() {
    if (getMaterial()) return;
    auto material = trans::TransDB::create<MaterialDB>();
    if (!material) return;
    material->setOpacity(1.0f);
    material->setRepresentation(2); // SURFACE
    auto* document = DocumentManager::instance();
    if (!document) {
        return;
    }
    if (!document->attachOwnedChild(
            getDBInstanceID(), material->getDBInstanceID(),
            kMaterialRelation)) {
        document->unregisterDBInstance(material->getDBInstanceID());
    }
}

std::shared_ptr<MaterialDB> ActorDB::getMaterial() const {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    const auto materialIds = document->getOwnedChildren(
        getDBInstanceID(), kMaterialRelation);
    return materialIds.empty()
        ? std::shared_ptr<MaterialDB>{}
        : document->getDB<MaterialDB>(materialIds.front());
}

bool ActorDB::removeMaterial() {
    auto* document = DocumentManager::instance();
    if (!document) return false;
    const auto material = getMaterial();
    if (!material) return true;
    return document->unregisterDBInstance(material->getDBInstanceID());
}

// === 变换方法实现（使用 Transform 类，类似 VTK 的方式） ===

void ActorDB::setPosition(const Vector3& position) {
    // 获取当前 Transform
    Transform transform = getTransform();
    transform.setPosition(position);

    // 更新 Transform 属性（会自动发送 Transform 通知）
    setTransform(transform);
}

void ActorDB::setTransform(const Transform& value) {
    trans::TransDB::setProperty(PROP_Transform(), value);
}

// 缩放方法实现 - 直接操作 Transform 矩阵
Vector3 ActorDB::getScale() const {
    return getTransform().getScale();
}

void ActorDB::setScale(const Vector3& scale) {
    Transform transform = getTransform();
    transform.setScale(scale);
    setTransform(transform);
}

// 缩放便捷方法实现
void ActorDB::setScaleX(float x) {
    Vector3 currentScale = getScale();
    setScale(Vector3(x, currentScale.y, currentScale.z));
}

void ActorDB::setScaleY(float y) {
    Vector3 currentScale = getScale();
    setScale(Vector3(currentScale.x, y, currentScale.z));
}

void ActorDB::setScaleZ(float z) {
    Vector3 currentScale = getScale();
    setScale(Vector3(currentScale.x, currentScale.y, z));
}

void ActorDB::setScaleUniform(float scale) {
    setScale(Vector3(scale, scale, scale));
}

Vector3 ActorDB::getRotation() const {
    // 从 Transform 提取欧拉角
    Transform transform = getTransform();
    return transform.getEuler();
}

void ActorDB::resetRotation() {
    // 保留位置和缩放，只重置旋转
    Vector3 pos = getPosition();
    Vector3 scale = getScale();

    Transform transform;
    transform.identity();
    transform.setPosition(pos);
    transform.setScale(scale);

    // 更新 Transform 属性
    setTransform(transform);

    // 通知变化
    // setTransform 已经发送了 Transform 通知
}

void ActorDB::setRotationFromMatrix(const Transform::Matrix4& matrix) {
    // 从矩阵设置旋转（保留当前的位置和缩放）
    Vector3 pos = getPosition();
    Vector3 scale = getScale();

    // 从输入矩阵提取旋转部分
    Transform transform;
    transform.identity();
    transform.setPosition(pos);

    // 应用旋转矩阵（只取3x3部分）
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            transform.getMatrix()(i, j) = matrix(i, j);
        }
    }

    transform.setScale(scale);

    // 更新 Transform 属性
    setTransform(transform);
    // setTransform 已经发送了 Transform 通知
}

void ActorDB::setRotationFromQuaternion(float w, float x, float y, float z) {
    // 从四元数设置旋转
    Transform transform = getTransform();
    transform.setFromQuaternion(w, x, y, z, getPosition(), getScale());
    setTransform(transform);
    // setTransform 已经发送了 Transform 通知
}

void ActorDB::rotate(float angle, const Vector3& axis) {
    // 世界坐标系增量旋转
    Transform transform = getTransform();
    transform.rotateWXYZ(angle, axis);
    setTransform(transform);
    // setTransform 已经发送了 Transform 通知
}

void ActorDB::rotateWXYZ(float angle, const Vector3& worldAxis) {
    // 显式的世界坐标系旋转（同 rotate）
    Transform transform = getTransform();
    transform.rotateWXYZ(angle, worldAxis);
    setTransform(transform);
    // setTransform 已经发送了 Transform 通知
}

void ActorDB::rotateLocal(float angle, const Vector3& localAxis) {
    // 局部坐标系增量旋转
    Transform transform = getTransform();
    transform.rotate(angle, localAxis);
    setTransform(transform);
    // setTransform 已经发送了 Transform 通知
}

void ActorDB::rotateX(float angle) {
    rotate(angle, Vector3(1, 0, 0));
}

void ActorDB::rotateY(float angle) {
    rotate(angle, Vector3(0, 1, 0));
}

void ActorDB::rotateZ(float angle) {
    rotate(angle, Vector3(0, 0, 1));
}

Transform::Matrix4 ActorDB::getTransformMatrix() const {
    Transform transform = getTransform();
    return transform.getMatrix();
}

void ActorDB::copyTransformFrom(const ActorDB& other) {
    // 复制整个 Transform
    setTransform(other.getTransform());

    // Transform 已经包含了所有信息，不需要单独设置 Position/Scale
}

// === 覆写TransDB虚函数以支持嵌套属性 ===

std::any ActorDB::getPropertyImpl(const trans::Prop& prop) const {
    // 从 Prop 参数提取属性名
    std::string_view propName = prop.name();

    // 处理嵌套属性（如 Material.XXX）
    size_t dotPos = propName.find('.');
    if (dotPos != std::string::npos) {
        std::string containerName = std::string(propName.substr(0, dotPos));
        std::string subProp = std::string(propName.substr(dotPos + 1));

        if (containerName == "Material") {
            const auto material = getMaterial();
            if (material) {
                const auto materialProp =
                    material->findExistingPropByName(subProp);
                if (materialProp) {
                    return material->getPropertyImpl(*materialProp);
                }
            }
        }
    }

    // 🎯 自动查找 Material 属性（支持简单属性名，如 DiffuseColor）
    // 如果属性名没有点号，且 Material 对象有这个属性，自动转换为 Material.XXX
    if (dotPos == std::string::npos) {
        const auto material = getMaterial();
        if (material) {
            const auto materialProp =
                material->findExistingPropByName(std::string(propName));
            if (materialProp) {
                return material->getPropertyImpl(*materialProp);
            }
        }
    }

    // 调用基类处理普通属性
    return trans::TransDB::getPropertyImpl(prop);
}

void ActorDB::setPropertyImpl(const trans::Prop& prop, const std::any& value) {
    // 从 Prop 参数提取属性名
    std::string_view propName = prop.name();

    // Position/Scale 通过修改 Transform 来设置
    if (propName == std::string_view("Position")) {
        try {
            setPosition(std::any_cast<Vector3>(value));
        } catch (const std::bad_any_cast&) {
            LOG_ERROR("Failed to cast Position value to Vector3");
        }
        return;
    } else if (propName == std::string_view("Scale")) {
        try {
            setScale(std::any_cast<Vector3>(value));
        } catch (const std::bad_any_cast&) {
            LOG_ERROR("Failed to cast Scale value to Vector3");
        }
        return;
    } else if (propName == std::string_view("Rotation")) {
        LOG_WARN("Cannot set Rotation property directly. Use rotation methods (rotate, rotateX/Y/Z, etc.)");
        return;
    }

    // Transform has one write path, including transaction undo/redo. Update
    // the derived world-bounds cache before publishing either notification so
    // every observer sees the new transform and matching bounds together.
    if (propName == std::string_view("Transform")) {
        const auto* next = std::any_cast<Transform>(&value);
        if (!next) {
            LOG_ERROR("Failed to cast Transform property value");
            return;
        }

        std::unique_lock lock(m_mutex);
        BoundingBox translatedBounds;
        bool canTranslateCachedBounds = false;
        const std::any previousAny = trans::TransDB::getPropertyImpl(prop);
        if (const auto* previous = std::any_cast<Transform>(&previousAny)) {
            const auto& previousMatrix = previous->getMatrix();
            const auto& nextMatrix = next->getMatrix();
            bool sameLinearPart = true;
            for (int row = 0; row < 3 && sameLinearPart; ++row) {
                for (int column = 0; column < 3; ++column) {
                    if (previousMatrix(row, column) !=
                        nextMatrix(row, column)) {
                        sameLinearPart = false;
                        break;
                    }
                }
            }
            if (sameLinearPart && m_worldBounds.valid &&
                m_worldBoundsRevision == m_spatialRevision) {
                translatedBounds = m_worldBounds;
                const Vector3 delta(
                    nextMatrix(0, 3) - previousMatrix(0, 3),
                    nextMatrix(1, 3) - previousMatrix(1, 3),
                    nextMatrix(2, 3) - previousMatrix(2, 3));
                translatedBounds.min = translatedBounds.min + delta;
                translatedBounds.max = translatedBounds.max + delta;
                canTranslateCachedBounds = true;
            }
        }

        trans::TransDB::setPropertyImpl(prop, value);
        ++m_spatialRevision;
        if (canTranslateCachedBounds) {
            m_worldBounds = translatedBounds;
            m_worldBoundsRevision = m_spatialRevision;
        } else {
            m_worldBounds.valid = false;
            m_worldBoundsRevision = 0;
        }
        lock.unlock();
        onTransformChanged(*next);
        notifyPropertyChange("Transform");
        notifyPropertyChange("WorldBounds");
        return;
    }

    // 处理嵌套属性（如 Material.XXX）
    size_t dotPos = propName.find('.');
    if (dotPos != std::string::npos) {
        std::string containerName = std::string(propName.substr(0, dotPos));
        std::string subProp = std::string(propName.substr(dotPos + 1));

        if (containerName == "Material") {
            const auto material = getMaterial();
            if (material) {
                const auto materialProp =
                    material->findExistingPropByName(subProp);
                if (materialProp) {
                    material->setPropertyImpl(*materialProp, value);
                    return;
                }
            }
        }
    }

    // 🎯 自动查找 Material 属性（支持简单属性名，如 DiffuseColor）
    // 如果属性名没有点号，且 Material 对象有这个属性，自动转换为 Material.XXX
    if (dotPos == std::string::npos) {
        const auto material = getMaterial();
        if (material) {
            const auto materialProp =
                material->findExistingPropByName(std::string(propName));
            if (materialProp) {
                material->setPropertyImpl(*materialProp, value);
                return;
            }
        }
    }

    // 调用基类处理普通属性
    AutoRegisterDB::setPropertyImpl(prop, value);
}

ActorDB::BoundingBox ActorDB::worldBounds() const {
    for (;;) {
        std::uint64_t revision = 0;
        {
            std::shared_lock lock(m_mutex);
            if (m_worldBoundsRevision == m_spatialRevision) {
                return m_worldBounds;
            }
            revision = m_spatialRevision;
        }

        const BoundingBox calculated = worldBoundsAt(getTransformMatrix());
        {
            std::unique_lock lock(m_mutex);
            if (revision == m_spatialRevision) {
                m_worldBounds = calculated;
                m_worldBoundsRevision = revision;
                return calculated;
            }
        }
    }
}

void ActorDB::invalidateWorldBounds() {
    std::unique_lock lock(m_mutex);
    ++m_spatialRevision;
    m_worldBounds.valid = false;
    m_worldBoundsRevision = 0;
}


// === 通知方法实现 ===

void ActorDB::notifyPropertyChange(const std::string& propertyName) {
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, propertyName);
    }
}

void ActorDB::notifyGeometryChange(const std::string& propertyName) {
    invalidateWorldBounds();
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, propertyName);
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, "WorldBounds");
    }
}

void ActorDB::notifyGeometryChange(const trans::Prop& prop) {
    notifyGeometryChange(prop.name());
}

void ActorDB::notifyMaterialChange(const std::string& propertyName) {
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, propertyName);
    }
}

void ActorDB::notifyMaterialChange(const trans::Prop& prop) {
    notifyMaterialChange(prop.name());
}

void ActorDB::notifyVisibilityChange() {
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, "Visible");
    }
}

void ActorDB::notifyHoveredPartChange() {
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, "HoveredPart");
    }
}

Vector3 ActorDB::transformPoint(const Vector3& point) const {
    Transform::Matrix4 transformMatrix = getTransformMatrix();
    return transformPoint(point, transformMatrix);
}

Vector3 ActorDB::transformPoint(const Vector3& point, const Transform::Matrix4& matrix) const {
    // 将点转换为齐次坐标
    Eigen::Vector4f homogeneous(point.x, point.y, point.z, 1.0f);
    // 应用变换
    Eigen::Vector4f transformed = matrix * homogeneous;
    // 返回3D坐标
    return Vector3(transformed.x(), transformed.y(), transformed.z());
}

// === 工具方法：供子类使用 ===

ActorDB::BoundingBox ActorDB::worldBoundsAt(const Transform::Matrix4& matrix) const {
    BoundingBox bounds = localBounds();

    // 计算包围盒的8个顶点
    Vector3 vertices[8] = {
        Vector3(bounds.min.x, bounds.min.y, bounds.min.z),
        Vector3(bounds.max.x, bounds.min.y, bounds.min.z),
        Vector3(bounds.min.x, bounds.max.y, bounds.min.z),
        Vector3(bounds.max.x, bounds.max.y, bounds.min.z),
        Vector3(bounds.min.x, bounds.min.y, bounds.max.z),
        Vector3(bounds.max.x, bounds.min.y, bounds.max.z),
        Vector3(bounds.min.x, bounds.max.y, bounds.max.z),
        Vector3(bounds.max.x, bounds.max.y, bounds.max.z)
    };

    // 变换所有顶点并计算新的包围盒
    float maxFloat = std::numeric_limits<float>::max();
    float minFloat = std::numeric_limits<float>::lowest();
    Vector3 minWorld = Vector3(maxFloat, maxFloat, maxFloat);
    Vector3 maxWorld = Vector3(minFloat, minFloat, minFloat);

    for (int i = 0; i < 8; ++i) {
        // 转换为齐次坐标
        Eigen::Vector4f vertex4(vertices[i].x, vertices[i].y, vertices[i].z, 1.0f);

        // 应用变换矩阵
        Eigen::Vector4f transformedVertex = matrix * vertex4;

        // 转换回3D坐标
        Vector3 worldVertex(transformedVertex.x(), transformedVertex.y(), transformedVertex.z());

        // 更新世界包围盒
        minWorld.x = std::min(minWorld.x, worldVertex.x);
        minWorld.y = std::min(minWorld.y, worldVertex.y);
        minWorld.z = std::min(minWorld.z, worldVertex.z);

        maxWorld.x = std::max(maxWorld.x, worldVertex.x);
        maxWorld.y = std::max(maxWorld.y, worldVertex.y);
        maxWorld.z = std::max(maxWorld.z, worldVertex.z);
    }

    BoundingBox worldBounds{minWorld, maxWorld};
    worldBounds.valid = bounds.valid;
    return worldBounds;
}


std::vector<std::string_view> ActorDB::getPropertyNames() const {
    // 获取TransDB中所有真实存在的属性
    // 这些属性通过FIELD_VALUE宏定义，存储在属性字典中
    std::vector<std::string_view> names = trans::TransDB::getPropertyNames();

    // Material的子属性（Material.Color, Material.Ambient等）
    // 这些是为了方便外部访问，虽然不在主属性字典中，但可以通过虚函数访问
    auto material = getMaterial();
    if (material) {
        std::vector<std::string_view> materialProps = material->getPropertyNames();
        for (const std::string_view& prop : materialProps) {
            // 需要创建新的字符串，因为 "Material." + prop 会创建临时字符串
            static std::vector<std::string> materialPrefixCache;
            materialPrefixCache.clear();
            for (const auto& mp : materialProps) {
                materialPrefixCache.push_back("Material." + std::string(mp));
            }
            for (const auto& prefixed : materialPrefixCache) {
                names.push_back(prefixed);
            }
            break;
        }
    }

    return names;
}

PropertyMap ActorDB::serialize() const {
    auto lock = getSharedLock();

    // 直接调用基类的 serialize，它会遍历所有属性字典
    PropertyMap props = AutoRegisterDB::serialize();

    // 如果有特殊需求，可以在这里添加额外的属性
    // 但大部分属性都已经通过 FIELD_VALUE 宏管理了

    return props;
}

bool ActorDB::deserialize(const PropertyMap& properties) {
    auto lock = getUniqueLock();

    // 特殊处理 Rotation 属性
    Vector3 rotation(0, 0, 0);
    bool hasRotation = false;
    PropertyMap filteredProperties = properties;

    // 从属性中提取旋转值，但不让基类处理它
    auto it = filteredProperties.find("Rotation");
    if (it != filteredProperties.end()) {
        if (it.value().canConvert<Vector3>()) {
            rotation = it.value().value<Vector3>();
            hasRotation = true;
            filteredProperties.erase(it);  // 移除，防止基类尝试设置
        } else {
            LOG_ERROR("Failed to convert Rotation property to Vector3");
        }
    }

    // 调用基类处理其他属性
    bool result = AutoRegisterDB::deserialize(filteredProperties);

    // 反序列化后，重建 Transform 状态
    if (result) {
        Transform transform;
        transform.identity();
        transform.setPosition(getPosition());

        // 如果有旋转数据，应用它
        if (hasRotation) {
            transform.rotateX(rotation.x);
            transform.rotateY(rotation.y);
            transform.rotateZ(rotation.z);
        }

        transform.setScale(getScale());

        // 设置 Transform 属性
        setTransform(transform);
    }

    return result;
}
