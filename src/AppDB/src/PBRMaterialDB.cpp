#include "../include/PBRMaterialDB.hpp"

#include <algorithm>

void PBRMaterialDB::initializeSubMaterialProperties() {
    setTransmission(0.0f);
    setIndexOfRefraction(1.5f);
    setThickness(0.0f);
    setAbsorptionColor(Vector3(1.0f, 1.0f, 1.0f));
    setAbsorptionDistance(1.0f);
    setDispersion(0.0f);
    setClearCoat(0.0f);
    setClearCoatRoughness(0.0f);
    setAnisotropy(0.0f);
    setRefractionModeEnum(RefractionMode::NONE);
}

float PBRMaterialDB::getTransmission() const {
    return trans::TransDB::getProperty<float>(PROP_Transmission());
}

void PBRMaterialDB::setTransmission(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    trans::TransDB::setProperty(PROP_Transmission(), value);
}

float PBRMaterialDB::getIndexOfRefraction() const {
    return trans::TransDB::getProperty<float>(PROP_IndexOfRefraction());
}

void PBRMaterialDB::setIndexOfRefraction(float value) {
    value = std::clamp(value, 1.0f, 4.0f);
    trans::TransDB::setProperty(PROP_IndexOfRefraction(), value);
}

float PBRMaterialDB::getThickness() const {
    return trans::TransDB::getProperty<float>(PROP_Thickness());
}

void PBRMaterialDB::setThickness(float value) {
    value = std::max(0.0f, value);
    trans::TransDB::setProperty(PROP_Thickness(), value);
}

Vector3 PBRMaterialDB::getAbsorptionColor() const {
    return trans::TransDB::getProperty<Vector3>(PROP_AbsorptionColor());
}

void PBRMaterialDB::setAbsorptionColor(const Vector3& value) {
    const Vector3 clamped{
        std::clamp(value.x, 0.0f, 1.0f),
        std::clamp(value.y, 0.0f, 1.0f),
        std::clamp(value.z, 0.0f, 1.0f)};
    trans::TransDB::setProperty(PROP_AbsorptionColor(), clamped);
}

float PBRMaterialDB::getAbsorptionDistance() const {
    return trans::TransDB::getProperty<float>(PROP_AbsorptionDistance());
}

void PBRMaterialDB::setAbsorptionDistance(float value) {
    value = std::max(0.0001f, value);
    trans::TransDB::setProperty(PROP_AbsorptionDistance(), value);
}

float PBRMaterialDB::getDispersion() const {
    return trans::TransDB::getProperty<float>(PROP_Dispersion());
}

void PBRMaterialDB::setDispersion(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    trans::TransDB::setProperty(PROP_Dispersion(), value);
}

float PBRMaterialDB::getClearCoat() const {
    return trans::TransDB::getProperty<float>(PROP_ClearCoat());
}

void PBRMaterialDB::setClearCoat(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    trans::TransDB::setProperty(PROP_ClearCoat(), value);
}

float PBRMaterialDB::getClearCoatRoughness() const {
    return trans::TransDB::getProperty<float>(PROP_ClearCoatRoughness());
}

void PBRMaterialDB::setClearCoatRoughness(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    trans::TransDB::setProperty(PROP_ClearCoatRoughness(), value);
}

float PBRMaterialDB::getAnisotropy() const {
    return trans::TransDB::getProperty<float>(PROP_Anisotropy());
}

void PBRMaterialDB::setAnisotropy(float value) {
    value = std::clamp(value, -1.0f, 1.0f);
    trans::TransDB::setProperty(PROP_Anisotropy(), value);
}

int PBRMaterialDB::getRefractionMode() const {
    return trans::TransDB::getProperty<int>(PROP_RefractionMode());
}

void PBRMaterialDB::setRefractionMode(int value) {
    value = value == static_cast<int>(RefractionMode::SCREEN_SPACE)
        ? value
        : static_cast<int>(RefractionMode::NONE);
    trans::TransDB::setProperty(PROP_RefractionMode(), value);
}

std::shared_ptr<AutoRegisterDB> PBRMaterialDB::clone() const {
    auto copy = trans::TransDB::create<PBRMaterialDB>();
    copy->setDisplayName(getDisplayName());
    copy->copyValuesFrom(*this);
    copy->setTransmission(getTransmission());
    copy->setIndexOfRefraction(getIndexOfRefraction());
    copy->setThickness(getThickness());
    copy->setAbsorptionColor(getAbsorptionColor());
    copy->setAbsorptionDistance(getAbsorptionDistance());
    copy->setDispersion(getDispersion());
    copy->setClearCoat(getClearCoat());
    copy->setClearCoatRoughness(getClearCoatRoughness());
    copy->setAnisotropy(getAnisotropy());
    copy->setRefractionMode(getRefractionMode());
    return copy;
}

void PBRMaterialDB::applyScreenSpaceRefraction(const Vector3& baseColor) {
    setRenderingMode(static_cast<int>(RenderingMode::PBR));
    setColor(baseColor);
    setDiffuseColor(baseColor);
    setMetallic(0.0f);
    setRoughness(0.08f);
    setOpacity(1.0f);
    setTransmission(1.0f);
    setIndexOfRefraction(1.48f);
    setThickness(0.65f);
    setAbsorptionColor(Vector3(1.0f, 0.72f, 0.12f));
    setAbsorptionDistance(3.0f);
    setDispersion(0.08f);
    setClearCoat(0.35f);
    setClearCoatRoughness(0.04f);
    setAnisotropy(0.0f);
    setRefractionModeEnum(RefractionMode::SCREEN_SPACE);
}
