#pragma once

#include "GraphicsApi.hpp"

#include <QList>
#include <QString>

namespace GPlatform::Rendering {

struct RendererBackendDescriptor {
    QString backendId;
    QString displayName;
    QList<GraphicsApi> supportedGraphicsApis;

    [[nodiscard]] bool supports(GraphicsApi api) const noexcept {
        return supportedGraphicsApis.contains(api);
    }
};

} // namespace GPlatform::Rendering
