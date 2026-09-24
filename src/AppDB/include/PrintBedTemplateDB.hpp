#pragma once

#include <AutoRegisterDB.hpp>

/**
 * Reusable definition of one machine build volume.
 *
 * A template is document data, not a scene actor. PrintBedDB instances refer
 * to it and renderers project the referenced definition onto the real bed.
 */
class PrintBedTemplateDB final : public AutoRegisterDB {
public:
    PrintBedTemplateDB() = default;
    ~PrintBedTemplateDB() override = default;

    FIELD_VALUE(PrintBedTemplateDB, std::string, ModelId)
    FIELD_VALUE(PrintBedTemplateDB, std::string, VariantId)
    FIELD_VALUE(PrintBedTemplateDB, std::string, Vendor)
    FIELD_VALUE(PrintBedTemplateDB, std::string, PrinterModel)

    FIELD_VALUE_SIMPLE(PrintBedTemplateDB, float, Width)
    FIELD_VALUE_SIMPLE(PrintBedTemplateDB, float, Height)
    FIELD_VALUE_SIMPLE(PrintBedTemplateDB, float, Thickness)
    FIELD_VALUE_SIMPLE(PrintBedTemplateDB, float, PrintHeight)
    FIELD_VALUE(PrintBedTemplateDB, Polygon2, PrintableArea)
    FIELD_VALUE(PrintBedTemplateDB, Vector3, Origin)

    FIELD_VALUE(PrintBedTemplateDB, std::string, BedModelPath)
    FIELD_VALUE(PrintBedTemplateDB, std::string, BedTexturePath)
    FIELD_VALUE_SIMPLE(PrintBedTemplateDB, float, MaxTemperature)
    FIELD_VALUE_SIMPLE(PrintBedTemplateDB, bool, Heated)

    TypeID getTypeID() const override {
        return TypeID::PRINT_BED_TEMPLATE_DB;
    }

    bool needsVTKSync() const override { return false; }
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    void initializeProperties() override;
};

namespace PrintBedDefaults {
constexpr float PLATFORM_THICKNESS = 0.08f;
constexpr float DEFAULT_PRINT_HEIGHT = 220.0f;
constexpr float DEFAULT_WIDTH = 220.0f;
constexpr float DEFAULT_HEIGHT = 220.0f;
}
