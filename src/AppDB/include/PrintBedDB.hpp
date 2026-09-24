#pragma once

#include "ActorDB.hpp"

#include <cstdint>
#include <string>

class PrintBedTemplateDB;

/** Scene instance of one real print bed. */
class PrintBedDB : public ActorDB {
public:
    explicit PrintBedDB();
    ~PrintBedDB() override;

    FIELD_RELATION_REF(PrintBedDB, TemplateDBId)

    std::shared_ptr<PrintBedTemplateDB> getTemplateDB();
    std::shared_ptr<const PrintBedTemplateDB> getTemplateDB() const;

    // Effective machine definition. The authoritative values live only in the
    // referenced PrintBedTemplateDB.
    float getWidth() const;
    float getHeight() const;
    float getThickness() const;
    float getPrintHeight() const;
    Polygon2 getPrintableArea() const;
    Vector3 getOrigin() const;
    Vector3 getCenter() const;
    std::string getBedModelPath() const;
    std::string getBedTexturePath() const;
    std::string getBedVendor() const;
    std::string getBedPrinterModel() const;
    float getMaxTemperature() const;
    bool getHeated() const;

    // Compatibility mutation entry points used by the property bridge. These
    // mutate the referenced definition; no machine field is copied here.
    void setWidth(float value);
    void setHeight(float value);
    void setThickness(float value);
    void setPrintHeight(float value);
    void setPrintableArea(const Polygon2& value);
    void setOrigin(const Vector3& value);
    void setBedModelPath(const std::string& value);
    void setBedTexturePath(const std::string& value);
    void setBedVendor(const std::string& value);
    void setBedPrinterModel(const std::string& value);
    void setMaxTemperature(float value);
    void setHeated(bool value);
    void notifyTemplateChanged(const std::string& propertyName);

    // Per-instance presentation.
    FIELD_GEOMETRY_VALUE_SIMPLE(PrintBedDB, bool, ShowGrid)
    FIELD_GEOMETRY_VALUE_SIMPLE(PrintBedDB, float, GridSpacing)
    FIELD_GEOMETRY_VALUE_SIMPLE(PrintBedDB, bool, ShowBounds)
    FIELD_GEOMETRY_VALUE(PrintBedDB, Vector3, GridColor)
    FIELD_GEOMETRY_VALUE(PrintBedDB, Vector3, BoundColor)

    // Aggregate, runtime placement result for this real bed. Each bit tells
    // the DBSync which build-volume boundary to visualize.
    enum CollisionFlag : std::uint32_t {
        CollisionNone = 0,
        CollisionLeft = 1u << 0,
        CollisionRight = 1u << 1,
        CollisionFront = 1u << 2,
        CollisionBack = 1u << 3,
        CollisionTop = 1u << 4,
        CollisionBottom = 1u << 5
    };
    static inline const trans::Prop& PROP_CollisionMask() {
        static const trans::Prop prop(typeid(PrintBedDB), "CollisionMask");
        return prop;
    }
    std::uint32_t getCollisionMask() const {
        return trans::TransDB::getProperty<std::uint32_t>(PROP_CollisionMask());
    }
    void setCollisionMask(std::uint32_t value) {
        if (hasProperty(PROP_CollisionMask()) && getCollisionMask() == value) return;
        trans::TransDB::setProperty(PROP_CollisionMask(), value);
    }

    bool getCollisionEffectsEnabled() const { return getCollisionMask() != 0; }
    bool isWallCollisionSet(int wallIndex) const {
        return wallIndex >= 0 && wallIndex < 6 &&
            (getCollisionMask() & (1u << static_cast<unsigned>(wallIndex))) != 0;
    }
    enum class MaterialPreset {
        MIRROR_METAL = 0,
        SEMI_METAL = 1,
        PLASTIC = 2
    };
    void applyMaterialPreset(MaterialPreset preset);
    void cycleToNextMaterialPreset();

    TypeID getTypeID() const override { return TypeID::PRINT_BED_DB; }
    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;
    std::vector<DBRelationRef> reportRelations() const override;
    bool replaceRelations(
        const std::vector<DBRelationReplacement>& replacements) override;

protected:
    void initializeSubActorProperties() override;
    void afterSubActorPropertiesInitialized() override;
    std::vector<std::string_view> getPropertyNames() const override;
    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

private:
    void markGeometryChanged(const trans::Prop& prop) {
        notifyGeometryChange(prop.name());
    }
};

namespace PrintBedViewDefaults {
constexpr float DEFAULT_GRID_SPACING = 10.0f;
constexpr float BED_COLOR_R = 0.260f;
constexpr float BED_COLOR_G = 0.270f;
constexpr float BED_COLOR_B = 0.270f;
constexpr float GRID_COLOR_R = 0.900f;
constexpr float GRID_COLOR_G = 0.900f;
constexpr float GRID_COLOR_B = 0.900f;
constexpr float GRID_LINE_WIDTH = 1.0f;
constexpr float AXIS_Z = 0.04f;
constexpr float COLLISION_COLOR_R = 1.0f;
constexpr float COLLISION_COLOR_G = 0.18f;
constexpr float COLLISION_COLOR_B = 0.12f;
constexpr float COLLISION_FILL_OPACITY = 0.20f;
constexpr float COLLISION_BORDER_OPACITY = 0.64f;
constexpr float COLLISION_BORDER_WIDTH_MM = 0.6f;
constexpr float COLLISION_SIDE_BAND_RATIO = 0.10f;
constexpr float COLLISION_SIDE_BAND_MIN_HEIGHT_MM = 16.0f;
constexpr float COLLISION_SIDE_BAND_MAX_HEIGHT_MM = 32.0f;
}
