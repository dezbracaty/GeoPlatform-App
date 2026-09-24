#include "LocalModelLibraryBridge.hpp"
#include "BridgeRegistration.hpp"

#include "LocalModelLibraryService.h"

LocalModelLibraryBridge::LocalModelLibraryBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    connect(LocalModelLibraryService::instance(),
            &LocalModelLibraryService::modelsChanged,
            this,
            &LocalModelLibraryBridge::modelsChanged);
}

LocalModelLibraryBridge* LocalModelLibraryBridge::instance() {
    static auto* bridge = new LocalModelLibraryBridge();
    return bridge;
}

LocalModelLibraryBridge* LocalModelLibraryBridge::create(QQmlEngine* qmlEngine,
                                                         QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

QVariantList LocalModelLibraryBridge::models(const QString& searchText,
                                             const QString& sortMode) const {
    return LocalModelLibraryService::instance()->models(searchText, sortMode);
}

int LocalModelLibraryBridge::totalCount() const {
    return LocalModelLibraryService::instance()->records().size();
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    LocalModelLibraryBridge,
    "LocalModelLibraryBridge",
    &LocalModelLibraryBridge::create)
