#pragma once

#include "ActorDB.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class ModelObjectDB;
class ModelPartDB;

class ModelInstanceDB final : public ActorDB {
public:
    inline static constexpr const char* kObjectRelation =
        "model.object.instances";
    inline static constexpr const char* kSlicingConfigRelation =
        "slicingConfig.modelInstances";

    struct RayHit {
        DBInstanceID partId;
        std::uint64_t primitiveIndex{0};
        Vector3 partLocalPosition;
        Vector3 objectLocalPosition;
        Vector3 worldPosition;
        float worldDistance{0.0f};
    };

    ModelInstanceDB() = default;

    static const trans::Prop& PROP_ParentPrintBedDBId();
    DBInstanceID getParentPrintBedDBId() const;
    void setParentPrintBedDBId(DBInstanceID value);
    std::function<void(DBInstanceID)> onParentPrintBedDBIdChanged =
        [](DBInstanceID) {};

    static const trans::Prop& PROP_SlicingConfigDBId();
    DBInstanceID getSlicingConfigDBId() const;
    void setSlicingConfigDBId(DBInstanceID value);
    std::function<void(DBInstanceID)> onSlicingConfigDBIdChanged =
        [](DBInstanceID) {};

    static const trans::Prop& PROP_Printable();
    bool getPrintable() const;
    void setPrintable(bool value);
    std::function<void(bool)> onPrintableChanged = [](bool) {};

    static const trans::Prop& PROP_ArrangeOrder();
    int getArrangeOrder() const;
    void setArrangeOrder(int value);
    std::function<void(int)> onArrangeOrderChanged = [](int) {};

    TypeID getTypeID() const override { return TypeID::MODEL_INSTANCE_DB; }
    bool isValid() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

    std::shared_ptr<ModelObjectDB> object() const;
    std::vector<std::shared_ptr<ModelPartDB>> parts() const;
    std::uint64_t geometryRevision() const;
    BoundingBox localBounds() const override;
    BoundingBox worldBounds() const override;
    BoundingBox worldBoundsAt(const Transform::Matrix4& matrix) const override;
    std::optional<RayHit> intersectWorldRay(
        const Vector3& worldOrigin,
        const Vector3& worldDirection,
        float maxWorldDistance = std::numeric_limits<float>::infinity()) const;

    std::vector<DBRelationRef> reportRelations() const override;
    bool replaceRelations(
        const std::vector<DBRelationReplacement>& replacements) override;

protected:
    void initializeSubActorProperties() override;
    bool ownsDefaultMaterial() const override { return false; }
};
