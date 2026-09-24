#include "../include/ModelInstanceDB.hpp"

#include "../include/ModelObjectDB.hpp"
#include "../include/ModelPartDB.hpp"
#include "../include/ModelGeometryDB.hpp"
#include <DocumentManager.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

#include <vtkPoints.h>
#include <vtkPolyData.h>

namespace {
void expand(ActorDB::BoundingBox& bounds, const Vector3& point) {
    if (!bounds.valid) {
        bounds.min = point;
        bounds.max = point;
        bounds.valid = true;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

Vector3 transformedPoint(const Transform::Matrix4& matrix, const Vector3& p) {
    const Eigen::Vector4f value =
        matrix * Eigen::Vector4f(p.x, p.y, p.z, 1.0f);
    return {value.x(), value.y(), value.z()};
}
}

const trans::Prop& ModelInstanceDB::PROP_ParentPrintBedDBId() {
    static const trans::Prop prop(typeid(ModelInstanceDB), "ParentPrintBedDBId");
    return prop;
}

DBInstanceID ModelInstanceDB::getParentPrintBedDBId() const {
    return trans::TransDB::getProperty<DBInstanceID>(PROP_ParentPrintBedDBId());
}

void ModelInstanceDB::setParentPrintBedDBId(DBInstanceID value) {
    trans::TransDB::setProperty(PROP_ParentPrintBedDBId(), value);
    onParentPrintBedDBIdChanged(value);
}

const trans::Prop& ModelInstanceDB::PROP_SlicingConfigDBId() {
    static const trans::Prop prop(typeid(ModelInstanceDB), "SlicingConfigDBId");
    return prop;
}

DBInstanceID ModelInstanceDB::getSlicingConfigDBId() const {
    return trans::TransDB::getProperty<DBInstanceID>(PROP_SlicingConfigDBId());
}

void ModelInstanceDB::setSlicingConfigDBId(DBInstanceID value) {
    trans::TransDB::setProperty(PROP_SlicingConfigDBId(), value);
    onSlicingConfigDBIdChanged(value);
}

const trans::Prop& ModelInstanceDB::PROP_Printable() {
    static const trans::Prop prop(typeid(ModelInstanceDB), "Printable");
    return prop;
}

bool ModelInstanceDB::getPrintable() const {
    return trans::TransDB::getProperty<bool>(PROP_Printable());
}

void ModelInstanceDB::setPrintable(bool value) {
    trans::TransDB::setProperty(PROP_Printable(), value);
    onPrintableChanged(value);
}

const trans::Prop& ModelInstanceDB::PROP_ArrangeOrder() {
    static const trans::Prop prop(typeid(ModelInstanceDB), "ArrangeOrder");
    return prop;
}

int ModelInstanceDB::getArrangeOrder() const {
    return trans::TransDB::getProperty<int>(PROP_ArrangeOrder());
}

void ModelInstanceDB::setArrangeOrder(int value) {
    trans::TransDB::setProperty(PROP_ArrangeOrder(), value);
    onArrangeOrderChanged(value);
}

void ModelInstanceDB::initializeSubActorProperties() {
    setParentPrintBedDBId(DBInstanceID());
    setSlicingConfigDBId(DBInstanceID());
    setPrintable(true);
    setArrangeOrder(0);
}

std::shared_ptr<ModelObjectDB> ModelInstanceDB::object() const {
    auto* document = DocumentManager::instance();
    return document
        ? document->getDB<ModelObjectDB>(
              document->getOwner(getDBInstanceID()))
        : nullptr;
}

std::vector<std::shared_ptr<ModelPartDB>> ModelInstanceDB::parts() const {
    auto owner = object();
    return owner ? owner->parts() : std::vector<std::shared_ptr<ModelPartDB>>{};
}

std::uint64_t ModelInstanceDB::geometryRevision() const {
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    for (const auto& part : parts()) {
        if (!part) continue;
        append(part->getDBInstanceID().getValue());
        append(part->getMeshRevision());
        const Transform::Matrix4 matrix =
            part->getLocalTransform().getMatrix();
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                append(static_cast<std::uint64_t>(
                    std::hash<float>{}(matrix(row, column))));
            }
        }
    }
    return hash;
}

ActorDB::BoundingBox ModelInstanceDB::localBounds() const {
    auto owner = object();
    return owner ? owner->localBounds() : BoundingBox{};
}

ActorDB::BoundingBox ModelInstanceDB::worldBounds() const {
    return worldBoundsAt(getTransformMatrix());
}

ActorDB::BoundingBox ModelInstanceDB::worldBoundsAt(
    const Transform::Matrix4& instanceMatrix) const {
    BoundingBox result;
    for (const auto& part : parts()) {
        if (!part || !part->hasValidMesh()) continue;
        const auto geometry = part->geometry();
        if (!geometry) continue;
        const Transform::Matrix4 combined =
            instanceMatrix * part->getLocalTransform().getMatrix();
        const BoundingBox partBounds =
            geometry->boundsAfterTransform(combined);
        if (!partBounds.valid) continue;
        expand(result, partBounds.min);
        expand(result, partBounds.max);
    }
    return result;
}

std::optional<ModelInstanceDB::RayHit> ModelInstanceDB::intersectWorldRay(
    const Vector3& worldOrigin, const Vector3& worldDirection,
    float maxWorldDistance) const {
    const float worldDirectionLength = std::sqrt(
        worldDirection.x * worldDirection.x +
        worldDirection.y * worldDirection.y +
        worldDirection.z * worldDirection.z);
    if (worldDirectionLength <= 1.0e-12f) return std::nullopt;
    const Vector3 normalizedWorld = worldDirection * (1.0f / worldDirectionLength);

    std::optional<RayHit> nearest;
    float nearestDistance = maxWorldDistance;
    for (const auto& part : parts()) {
        if (!part || !part->hasValidMesh()) continue;
        const Transform::Matrix4 worldFromPart =
            getTransform().getMatrix() * part->getLocalTransform().getMatrix();
        const float determinant = worldFromPart.block<3, 3>(0, 0).determinant();
        if (!worldFromPart.allFinite() || !std::isfinite(determinant) ||
            std::abs(determinant) <= 1.0e-12f) continue;
        const Transform::Matrix4 partFromWorld = worldFromPart.inverse();
        const Vector3 localOrigin = transformedPoint(partFromWorld, worldOrigin);
        const Eigen::Vector3f localVector = partFromWorld.block<3, 3>(0, 0) *
            Eigen::Vector3f(normalizedWorld.x, normalizedWorld.y,
                            normalizedWorld.z);
        const float localScale = localVector.norm();
        if (localScale <= 1.0e-12f) continue;
        const Vector3 localDirection{localVector.x() / localScale,
                                     localVector.y() / localScale,
                                     localVector.z() / localScale};
        const auto hit = part->intersectPartLocalRay(
            localOrigin, localDirection,
            std::isfinite(nearestDistance)
                ? nearestDistance * localScale
                : std::numeric_limits<float>::infinity());
        if (!hit) continue;
        const Vector3 worldPosition = transformedPoint(
            worldFromPart, hit->partLocalPosition);
        const Vector3 delta = worldPosition - worldOrigin;
        const float worldDistance = delta.x * normalizedWorld.x +
            delta.y * normalizedWorld.y + delta.z * normalizedWorld.z;
        if (worldDistance < 0.0f || worldDistance >= nearestDistance) continue;
        nearestDistance = worldDistance;
        nearest = RayHit{
            part->getDBInstanceID(), hit->primitiveIndex,
            hit->partLocalPosition,
            transformedPoint(part->getLocalTransform().getMatrix(),
                             hit->partLocalPosition),
            worldPosition, worldDistance};
    }
    return nearest;
}

bool ModelInstanceDB::isValid() const {
    auto owner = object();
    return ActorDB::isValid() && owner && owner->isValid();
}

std::shared_ptr<AutoRegisterDB> ModelInstanceDB::clone() const {
    auto copy = trans::TransDB::create<ModelInstanceDB>();
    copy->setDisplayName(getDisplayName());
    copy->setTransform(getTransform());
    copy->setVisible(getVisible());
    copy->setPickable(getPickable());
    copy->setDragable(getDragable());
    copy->setOpacity(getOpacity());
    copy->setPrintable(getPrintable());
    copy->setArrangeOrder(getArrangeOrder());
    return copy;
}

std::vector<DBRelationRef> ModelInstanceDB::reportRelations() const {
    std::vector<DBRelationRef> result;
    if (getParentPrintBedDBId().isValid()) {
        result.push_back({"printBed.modelInstances", getParentPrintBedDBId()});
    }
    if (getSlicingConfigDBId().isValid()) {
        result.push_back({"slicingConfig.modelInstances", getSlicingConfigDBId()});
    }
    return result;
}

bool ModelInstanceDB::replaceRelations(
    const std::vector<DBRelationReplacement>& replacements) {
    for (const auto& replacement : replacements) {
        if (!replacement.newTargetId.isValid()) return false;
        if (replacement.relationName == "printBed.modelInstances") {
            setParentPrintBedDBId(replacement.newTargetId);
        } else if (replacement.relationName == "slicingConfig.modelInstances") {
            setSlicingConfigDBId(replacement.newTargetId);
        } else {
            return false;
        }
    }
    return true;
}
