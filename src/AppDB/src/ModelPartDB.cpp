#include "../include/ModelPartDB.hpp"

#include "../include/MaterialDB.hpp"
#include "../include/ModelGeometryDB.hpp"
#include "../include/ModelObjectDB.hpp"
#include "../include/ModelSurfaceColorDB.hpp"

#include <DocumentManager.hpp>

ModelPartDB::ModelPartDB() = default;
ModelPartDB::~ModelPartDB() = default;

void ModelPartDB::initializeProperties() {
    setProjectSemantics({});
    setSurfaceAnnotations({});
    setAnnotationsTopologyFingerprint({});
    setAnnotationsSourceTriangleCount(0);

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
            static_cast<int>(ModelFilamentBindingMode::InheritObject) &&
        role >= static_cast<int>(ModelPartRole::Model) &&
        role <= static_cast<int>(ModelPartRole::ParameterModifier);
}

std::shared_ptr<AutoRegisterDB> ModelPartDB::clone() const {
    auto copy = trans::TransDB::create<ModelPartDB>();
    copy->setProjectSemantics(getProjectSemantics());
    copy->setSurfaceAnnotations(getSurfaceAnnotations());
    copy->setAnnotationsTopologyFingerprint(getAnnotationsTopologyFingerprint());
    copy->setAnnotationsSourceTriangleCount(getAnnotationsSourceTriangleCount());

    copy->setDisplayName(getDisplayName());
    copy->setLocalTransform(getLocalTransform());
    copy->setRole(getRole());
    copy->setOrderIndex(getOrderIndex());
    copy->setDefaultFilamentSlot(getDefaultFilamentSlot());
    copy->setFilamentBindingMode(getFilamentBindingMode());
    return copy;
}
