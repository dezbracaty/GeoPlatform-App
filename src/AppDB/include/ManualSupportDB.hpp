#pragma once

#include "ActorDB.hpp"
#include <Geometry.hpp>
#include <memory>
#include <string>

/**
 * One manual-support aggregate owned by one source ModelInstanceDB.
 *
 * Selection state is transactional. Generated geometry is an immutable,
 * rebuildable cache and is accepted only for the current state revision.
 */
class ManualSupportDB final : public ActorDB {
public:
    using GeometrySnapshot = std::shared_ptr<const IndexedTriangleMesh>;

    ManualSupportDB() = default;
    ~ManualSupportDB() override = default;

    FIELD_VALUE(ManualSupportDB, std::string, SelectionStateBlob)
    FIELD_VALUE_SIMPLE(ManualSupportDB, int, Revision)
    FIELD_VALUE_SIMPLE(ManualSupportDB, int, SnapshotVersion)

    TypeID getTypeID() const override { return TypeID::MANUAL_SUPPORT_DB; }

    void replaceSelectionSnapshot(std::string blob, int revision);
    bool setGeneratedGeometry(GeometrySnapshot geometry, int sourceRevision);
    void invalidateGeneratedGeometry();
    GeometrySnapshot getGeneratedGeometry() const;
    int getGeneratedGeometryRevision() const;
    bool hasGeneratedGeometry() const;

    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;
protected:
    void setPropertyImpl(const trans::Prop& prop, const std::any& value) override;
    void initializeSubActorProperties() override;
    void afterSubActorPropertiesInitialized() override;

private:
    GeometrySnapshot m_geometry;
    int m_geometryRevision{-1};
};
