#include "../include/ModelPartDB.hpp"

#include "../include/MaterialDB.hpp"
#include "../include/ModelGeometryDB.hpp"
#include "../include/ModelObjectDB.hpp"
#include "../include/ModelSurfaceColorDB.hpp"

#include <DocumentManager.hpp>
#include <TransactionManager.hpp>

ModelPartDB::ModelPartDB() {
    m_localTransform.identity();
}

ModelPartDB::~ModelPartDB() = default;

const trans::Prop& ModelPartDB::PROP_LocalTransform() {
    static const trans::Prop prop(typeid(ModelPartDB), "LocalTransform");
    return prop;
}

Transform ModelPartDB::getLocalTransform() const {
    return m_localTransform;
}

void ModelPartDB::setLocalTransform(const Transform& value) {
    if (m_localTransform != value) {
        if (TransactionManager::instance().isInTransaction()) {
            TransactionManager::instance().recordPropertyChange(
                shared_from_this(), PROP_LocalTransform(),
                std::any(m_localTransform), std::any(value));
        }
        setPropertyImpl(PROP_LocalTransform(), std::any(value));
    }
    onLocalTransformChanged(value);
}

std::any ModelPartDB::getPropertyImpl(const trans::Prop& prop) const {
    if (prop.nameView() == "LocalTransform") return std::any(m_localTransform);
    return AutoRegisterDB::getPropertyImpl(prop);
}

void ModelPartDB::setPropertyImpl(
    const trans::Prop& prop, const std::any& value) {
    if (prop.nameView() == "LocalTransform") {
        if (const auto* transform = std::any_cast<Transform>(&value)) {
            m_localTransform = *transform;
            notifyChange(ChangeType::PROPERTY_CHANGED, "LocalTransform");
        }
        return;
    }
    AutoRegisterDB::setPropertyImpl(prop, value);
}

void ModelPartDB::initializeProperties() {
    Transform identity;
    identity.identity();
    setLocalTransform(identity);
    setRole(static_cast<int>(ModelPartRole::Model));
    setOrderIndex(0);
    setDefaultFilamentSlot(1);
    setFilamentBindingMode(
        static_cast<int>(ModelFilamentBindingMode::InheritProjectDefault));
}

std::shared_ptr<ModelObjectDB> ModelPartDB::object() const {
    auto* document = DocumentManager::instance();
    return document
        ? document->getDB<ModelObjectDB>(
              document->getOwner(getDBInstanceID()))
        : nullptr;
}

std::shared_ptr<ModelGeometryDB> ModelPartDB::geometry() const {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    const auto ids = document->getOwnedChildren(
        getDBInstanceID(), kGeometryRelation);
    return ids.empty() ? std::shared_ptr<ModelGeometryDB>{}
                       : document->getDB<ModelGeometryDB>(ids.front());
}

std::shared_ptr<MaterialDB> ModelPartDB::getMaterial() const {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    const auto ids = document->getOwnedChildren(
        getDBInstanceID(), kMaterialRelation);
    return ids.empty() ? std::shared_ptr<MaterialDB>{}
                       : document->getDB<MaterialDB>(ids.front());
}

std::shared_ptr<ModelSurfaceColorDB> ModelPartDB::surfaceColors() const {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    const auto ids = document->getOwnedChildren(
        getDBInstanceID(), kSurfaceColorsRelation);
    return ids.empty() ? std::shared_ptr<ModelSurfaceColorDB>{}
                       : document->getDB<ModelSurfaceColorDB>(ids.front());
}

bool ModelPartDB::hasValidMesh() const {
    const auto data = geometry();
    return data && data->isValid();
}

std::uint64_t ModelPartDB::getMeshRevision() const {
    const auto data = geometry();
    return data ? data->revision() : 0;
}

ActorDB::BoundingBox ModelPartDB::localBounds() const {
    const auto data = geometry();
    return data ? data->localBounds() : ActorDB::BoundingBox{};
}

std::optional<ModelPartDB::RayHit> ModelPartDB::intersectPartLocalRay(
    const Vector3& origin, const Vector3& direction, float maxDistance) const {
    const auto data = geometry();
    if (!data) return std::nullopt;
    const auto hit = data->intersectLocalRay(origin, direction, maxDistance);
    if (!hit) return std::nullopt;
    return RayHit{hit->primitiveIndex, hit->localPosition, hit->localDistance};
}

bool ModelPartDB::isValid() const {
    const int role = getRole();
    const auto material = getMaterial();
    return AutoRegisterDB::isValid() && object() &&
        hasValidMesh() && material &&
        getDefaultFilamentSlot() >= 1 &&
        getFilamentBindingMode() >=
            static_cast<int>(ModelFilamentBindingMode::InheritProjectDefault) &&
        getFilamentBindingMode() <=
            static_cast<int>(ModelFilamentBindingMode::ExplicitSlot) &&
        role >= static_cast<int>(ModelPartRole::Model) &&
        role <= static_cast<int>(ModelPartRole::SupportBlocker);
}

std::shared_ptr<AutoRegisterDB> ModelPartDB::clone() const {
    auto copy = trans::TransDB::create<ModelPartDB>();
    copy->setDisplayName(getDisplayName());
    copy->setLocalTransform(getLocalTransform());
    copy->setRole(getRole());
    copy->setOrderIndex(getOrderIndex());
    copy->setDefaultFilamentSlot(getDefaultFilamentSlot());
    copy->setFilamentBindingMode(getFilamentBindingMode());
    return copy;
}
