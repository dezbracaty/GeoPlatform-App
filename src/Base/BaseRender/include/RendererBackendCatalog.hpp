#pragma once

#include "IRendererBackendFactory.hpp"

#include <QHash>
#include <QStringList>
#include <QStringView>

#include <memory>
#include <mutex>

namespace GPlatform::Rendering {

class RendererBackendCatalog final {
public:
    static RendererBackendCatalog& instance();

    bool registerFactory(std::shared_ptr<IRendererBackendFactory> factory);

    [[nodiscard]] std::shared_ptr<const IRendererBackendFactory> find(
        QStringView backendId) const;
    [[nodiscard]] QStringList backendIds() const;

private:
    RendererBackendCatalog() = default;

    static QString normalizedId(QStringView backendId);

    mutable std::mutex m_mutex;
    QHash<QString, std::shared_ptr<IRendererBackendFactory>> m_factories;
    QStringList m_registrationOrder;
};

// Implemented by the application composition target generated from all
// renderer backend modules enabled by CMake. Safe to call more than once.
void registerCompiledRendererBackends();

} // namespace GPlatform::Rendering
