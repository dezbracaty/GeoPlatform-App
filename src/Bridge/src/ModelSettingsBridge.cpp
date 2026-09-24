#include "ModelSettingsBridge.hpp"
#include "BridgeRegistration.hpp"
#include "Foundation/Log.h"

ModelSettingsBridge::ModelSettingsBridge(QObject* parent)
    : BridgeBase(parent) {
    // LOG_INFO("ModelSettingsBridge created with snapToGround={}", m_snapToGround);
}

ModelSettingsBridge* ModelSettingsBridge::instance() {
    static ModelSettingsBridge* s_instance = new ModelSettingsBridge();
    return s_instance;
}

ModelSettingsBridge* ModelSettingsBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    auto* instance = ModelSettingsBridge::instance();

    // Set QML ownership
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);

    return instance;
}

void ModelSettingsBridge::updateSnapToGround(bool value) {
    if (snapToGround() != value) {
        snapToGround(value);
        LOG_INFO("Snap to ground changed to: {}", value);
    }
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    ModelSettingsBridge, "ModelSettingsBridge", &ModelSettingsBridge::create)
