#include "LightDB.hpp"
#include <DocumentManager.hpp>

LightDB::LightDB()
    : AutoRegisterDB() {
}

LightDB::~LightDB() {
}

void LightDB::initializeProperties() {
    // 设置默认值
    setLightType(static_cast<int>(LightType::SCENE_LIGHT));
    setColor(Vector3(1.0f, 1.0f, 1.0f));  // 白色
    setIntensity(1.0f);
    setEnabled(true);
    setPosition(Vector3(0.0f, 0.0f, 10.0f));  // 默认在上方
    setFocalPoint(Vector3(0.0f, 0.0f, 0.0f)); // 默认指向原点
    setName("Light");

    // 设置属性变化回调
    onLightTypeChanged = [this](int) { notifyLightChange(PROP_LightType()); };
    onColorChanged = [this](const Vector3&) { notifyLightChange(PROP_Color()); };
    onIntensityChanged = [this](float) { notifyLightChange(PROP_Intensity()); };
    onEnabledChanged = [this](bool) { notifyLightChange(PROP_Enabled()); };
    onPositionChanged = [this](const Vector3&) { notifyLightChange(PROP_Position()); };
    onFocalPointChanged = [this](const Vector3&) { notifyLightChange(PROP_FocalPoint()); };
}

void LightDB::notifyLightChange(const trans::Prop& prop) {
    notifyChange(ChangeType::PROPERTY_CHANGED, std::string(prop.name()));
}
