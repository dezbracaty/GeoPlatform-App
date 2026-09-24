#pragma once

#include "FrameSynchronization.hpp"
#include "GraphicsApi.hpp"
#include "GraphicsRuntime.hpp"

#include <QMetaType>
#include <QSize>
#include <QtGlobal>

namespace GPlatform::Rendering {

/**
 * A backend-produced GPU resource that a Qt presenter can import.
 *
 * nativeTexture is interpreted only by the presenter selected for api. For
 * OpenGL it is a texture name; Metal/Vulkan backends can publish their native
 * texture/image handles without changing IRendererSession.
 */
struct PresentableFrame {
    GraphicsApi api{GraphicsApi::Unknown};
    DeviceIdentity deviceIdentity{0};
    FrameId frameId{0};
    quintptr nativeTexture{0};
    QSize pixelSize;

    [[nodiscard]] bool hasTexture() const noexcept {
        return api != GraphicsApi::Unknown && deviceIdentity != 0 &&
               nativeTexture != 0 && !pixelSize.isEmpty();
    }
};

} // namespace GPlatform::Rendering

Q_DECLARE_METATYPE(GPlatform::Rendering::PresentableFrame)
