#pragma once
#include "ActorDB.hpp"

// Authored region owned by one ModelInstance. Transform stores mask-to-model.
// The editing tool supplies presentation through ordinary temporary actors.
class FiberMaskDB final : public ActorDB {
public:
    enum class Shape { Box = 0, Cylinder = 1, Sphere = 2 };
    // Full extents and rigid pose in model coordinates. Cylinder/sphere use
    // dimensions.x as diameter; cylinder uses dimensions.z as height.
    struct Region {
        Shape shape{Shape::Box};
        Transform pose;
        Vector3 dimensions{0, 0, 0};
    };
    // Shared by authored data and unsaved drafts; does not modify the document.
    static bool validRegion(const Region& region);

    using ActorDB::deserialize;
    using ActorDB::serialize;
    inline static constexpr const char* kOwnerRelation = "model.instance.fiberMasks";
    // Keep the stored integer values for document and UI compatibility.
    FIELD_VALUE_SIMPLE(FiberMaskDB, int, Shape)
    FIELD_VALUE(FiberMaskDB, Vector3, Dimensions)
    FIELD_VALUE(FiberMaskDB, std::string, ParameterOverridesJson)
    // Later orders override earlier masks. Zero is the legacy document default.
    FIELD_VALUE_SIMPLE(FiberMaskDB, int, Order)
    FIELD_VALUE_SIMPLE(FiberMaskDB, bool, Enabled)
    static bool precedes(const FiberMaskDB& first, const FiberMaskDB& second);
    TypeID getTypeID() const override {
        return TypeID::FIBER_MASK_DB;
    }
    Region region() const;
    Transform::Matrix4 worldTransform() const;
    BoundingBox localBounds() const override;
    BoundingBox worldBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    void initializeSubActorProperties() override;
};
