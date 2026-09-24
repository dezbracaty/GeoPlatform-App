#pragma once

#include "MaterialDB.hpp"

/**
 * Backend-independent physically based material data.
 *
 * The Document stores optical intent here. Renderer backends project these
 * values to their own native material objects without exposing native types.
 */
class PBRMaterialDB final : public MaterialDB {
public:
    enum class RefractionMode {
        NONE = 0,
        SCREEN_SPACE = 1
    };

    PBRMaterialDB() = default;
    ~PBRMaterialDB() override = default;

    std::shared_ptr<AutoRegisterDB> clone() const override;

    float getTransmission() const;
    void setTransmission(float value);

    float getIndexOfRefraction() const;
    void setIndexOfRefraction(float value);

    float getThickness() const;
    void setThickness(float value);

    Vector3 getAbsorptionColor() const;
    void setAbsorptionColor(const Vector3& value);

    float getAbsorptionDistance() const;
    void setAbsorptionDistance(float value);

    float getDispersion() const;
    void setDispersion(float value);

    float getClearCoat() const;
    void setClearCoat(float value);

    float getClearCoatRoughness() const;
    void setClearCoatRoughness(float value);

    float getAnisotropy() const;
    void setAnisotropy(float value);

    int getRefractionMode() const;
    void setRefractionMode(int value);

    RefractionMode getRefractionModeEnum() const {
        return static_cast<RefractionMode>(getRefractionMode());
    }

    void setRefractionModeEnum(RefractionMode mode) {
        setRefractionMode(static_cast<int>(mode));
    }

    void applyScreenSpaceRefraction(const Vector3& baseColor);

protected:
    void initializeSubMaterialProperties() override;

private:
#define GPLATFORM_PBR_PROP(name)                                      \
    static const trans::Prop& PROP_##name() {                         \
        static const trans::Prop prop(typeid(PBRMaterialDB), #name);  \
        return prop;                                                   \
    }
    GPLATFORM_PBR_PROP(Transmission)
    GPLATFORM_PBR_PROP(IndexOfRefraction)
    GPLATFORM_PBR_PROP(Thickness)
    GPLATFORM_PBR_PROP(AbsorptionColor)
    GPLATFORM_PBR_PROP(AbsorptionDistance)
    GPLATFORM_PBR_PROP(Dispersion)
    GPLATFORM_PBR_PROP(ClearCoat)
    GPLATFORM_PBR_PROP(ClearCoatRoughness)
    GPLATFORM_PBR_PROP(Anisotropy)
    GPLATFORM_PBR_PROP(RefractionMode)
#undef GPLATFORM_PBR_PROP
};
