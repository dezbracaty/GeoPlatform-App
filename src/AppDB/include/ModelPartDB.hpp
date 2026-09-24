#pragma once

#include "ActorDB.hpp"

#include <AutoRegisterDB.hpp>
#include <Transform.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class MaterialDB;
class ModelGeometryDB;
class ModelObjectDB;
class ModelSurfaceColorDB;

enum class ModelPartRole : int {
    Model = 0,
    SupportEnforcer = 1,
    SupportBlocker = 2
};

/**
 * Controls whether a part merely inherits the project's slicing filament or
 * was explicitly assigned to a filament slot by the user/imported project.
 */
enum class ModelFilamentBindingMode : int {
    InheritProjectDefault = 0,
    ExplicitSlot = 1
};

/** A semantic Part. Parent and child links live only in the relation graph. */
class ModelPartDB final : public AutoRegisterDB {
public:
    inline static constexpr const char* kObjectRelation =
        "model.object.parts";
    inline static constexpr const char* kGeometryRelation =
        "model.part.geometry";
    inline static constexpr const char* kMaterialRelation =
        "model.part.material";
    inline static constexpr const char* kSurfaceColorsRelation =
        "model.part.surfaceColors";

    struct RayHit {
        std::uint64_t primitiveIndex{0};
        Vector3 partLocalPosition;
        float partLocalDistance{0.0f};
    };

    ModelPartDB();
    ~ModelPartDB() override;

    static const trans::Prop& PROP_LocalTransform();
    Transform getLocalTransform() const;
    void setLocalTransform(const Transform& value);
    std::function<void(const Transform&)> onLocalTransformChanged =
        [](const Transform&) {};

    FIELD_VALUE_SIMPLE(ModelPartDB, int, Role)
    FIELD_VALUE_SIMPLE(ModelPartDB, int, OrderIndex)
    FIELD_VALUE_SIMPLE(ModelPartDB, int, DefaultFilamentSlot)
    FIELD_VALUE_SIMPLE(ModelPartDB, int, FilamentBindingMode)

    TypeID getTypeID() const override { return TypeID::MODEL_PART_DB; }
    bool needsVTKSync() const override { return false; }
    bool isValid() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

    std::shared_ptr<ModelObjectDB> object() const;
    std::shared_ptr<ModelGeometryDB> geometry() const;
    std::shared_ptr<MaterialDB> getMaterial() const;
    std::shared_ptr<ModelSurfaceColorDB> surfaceColors() const;
    bool hasValidMesh() const;
    std::uint64_t getMeshRevision() const;
    ActorDB::BoundingBox localBounds() const;
    std::optional<RayHit> intersectPartLocalRay(
        const Vector3& origin,
        const Vector3& direction,
        float maxDistance = std::numeric_limits<float>::infinity()) const;

protected:
    void initializeProperties() override;
    std::any getPropertyImpl(const trans::Prop& prop) const override;
    void setPropertyImpl(
        const trans::Prop& prop, const std::any& value) override;

private:
    Transform m_localTransform;
};
