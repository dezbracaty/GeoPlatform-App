#include "ManualSupportDB.hpp"

void ManualSupportDB::replaceSelectionSnapshot(std::string blob, int revision) {
    setSelectionStateBlob(blob);
    setRevision(std::max(0, revision));
}

bool ManualSupportDB::setGeneratedGeometry(
    GeometrySnapshot geometry,
    int sourceRevision) {
    if (sourceRevision != getRevision() ||
        (geometry && !geometry->hasValidTopology())) {
        return false;
    }
    {
        auto lock = getUniqueLock();
        m_geometry = std::move(geometry);
        m_geometryRevision = sourceRevision;
    }
    notifyGeometryChange("SupportGeometry");
    return true;
}

void ManualSupportDB::invalidateGeneratedGeometry() {
    bool changed = false;
    {
        auto lock = getUniqueLock();
        changed = static_cast<bool>(m_geometry) || m_geometryRevision != -1;
        m_geometry.reset();
        m_geometryRevision = -1;
    }
    if (changed) notifyGeometryChange("SupportGeometry");
}

ManualSupportDB::GeometrySnapshot ManualSupportDB::getGeneratedGeometry() const {
    auto lock = getSharedLock();
    return m_geometry;
}

int ManualSupportDB::getGeneratedGeometryRevision() const {
    auto lock = getSharedLock();
    return m_geometryRevision;
}

bool ManualSupportDB::hasGeneratedGeometry() const {
    const auto geometry = getGeneratedGeometry();
    return geometry && geometry->hasValidTopology() &&
        getGeneratedGeometryRevision() == getRevision();
}

ActorDB::BoundingBox ManualSupportDB::localBounds() const {
    const auto geometry = getGeneratedGeometry();
    if (!geometry || !geometry->hasValidTopology() ||
        getGeneratedGeometryRevision() != getRevision()) {
        return {};
    }
    const GeometryBounds bounds = geometry->bounds();
    return {bounds.min, bounds.max, bounds.valid};
}

std::shared_ptr<AutoRegisterDB> ManualSupportDB::clone() const {
    auto copy = trans::TransDB::create<ManualSupportDB>();
    copy->setSelectionStateBlob(getSelectionStateBlob());
    copy->setRevision(getRevision());
    copy->setSnapshotVersion(getSnapshotVersion());
    copy->copyTransformFrom(*this);
    copy->setVisible(isVisible());
    copy->setPickable(isPickable());
    copy->setDragable(isDragable());
    copy->setOpacity(getOpacity());
    if (const auto sourceMaterial = getMaterial()) {
        if (const auto targetMaterial = copy->getMaterial()) {
            targetMaterial->deserialize(sourceMaterial->serialize());
        }
    }
    // Generated geometry is a rebuildable cache and is not copied.
    return copy;
}

void ManualSupportDB::setPropertyImpl(
    const trans::Prop& prop,
    const std::any& value) {
    ActorDB::setPropertyImpl(prop, value);
    if (prop == PROP_Revision()) {
        invalidateGeneratedGeometry();
    }
}

void ManualSupportDB::initializeSubActorProperties() {
    setSelectionStateBlob(std::string());
    setRevision(0);
    setSnapshotVersion(1);
    setPickable(false);
    setDragable(false);
}

void ManualSupportDB::afterSubActorPropertiesInitialized() {
    setOpacity(0.45f);
    if (const auto material = getMaterial()) {
        const Vector3 color(0.45f, 0.82f, 1.0f);
        material->setDiffuseColor(color);
        material->setAmbientColor(color);
        material->setAmbient(0.35f);
        material->setDiffuse(1.0f);
        material->setSpecular(0.18f);
        material->setSpecularPower(18.0f);
        material->setInterpolation(true);
        material->setRepresentation(2);
        material->setEdgeVisibility(false);
        material->setLighting(true);
        material->setOpacity(0.45f);
    }
}
