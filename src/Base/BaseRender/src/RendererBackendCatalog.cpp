#include "RendererBackendCatalog.hpp"

namespace GPlatform::Rendering {

RendererBackendCatalog& RendererBackendCatalog::instance() {
    static RendererBackendCatalog catalog;
    return catalog;
}

QString RendererBackendCatalog::normalizedId(QStringView backendId) {
    return backendId.toString().trimmed().toLower();
}

bool RendererBackendCatalog::registerFactory(
    std::shared_ptr<IRendererBackendFactory> factory) {
    if (!factory) {
        return false;
    }

    const auto descriptor = factory->descriptor();
    const QString id = normalizedId(descriptor.backendId);
    if (id.isEmpty() || descriptor.supportedGraphicsApis.isEmpty()) {
        return false;
    }

    std::lock_guard lock(m_mutex);
    if (m_factories.contains(id)) {
        return false;
    }
    m_factories.insert(id, std::move(factory));
    m_registrationOrder.append(id);
    return true;
}

std::shared_ptr<const IRendererBackendFactory> RendererBackendCatalog::find(
    QStringView backendId) const {
    const QString id = normalizedId(backendId);
    std::lock_guard lock(m_mutex);
    const auto found = m_factories.constFind(id);
    return found == m_factories.cend() ? nullptr : found.value();
}

QStringList RendererBackendCatalog::backendIds() const {
    std::lock_guard lock(m_mutex);
    return m_registrationOrder;
}

} // namespace GPlatform::Rendering
