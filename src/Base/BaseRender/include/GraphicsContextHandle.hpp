#pragma once

#include "GraphicsApi.hpp"
#include "GraphicsRuntime.hpp"

#include <QtGlobal>

namespace GPlatform::Rendering {

/**
 * API-tagged graphics context passed from the Qt host to a renderer backend.
 *
 * The public contract deliberately carries no QOpenGLContext, MTLDevice or
 * VkDevice type. The backend interprets nativeHandle only after checking api.
 */
struct GraphicsContextHandle {
    GraphicsApi api{GraphicsApi::Unknown};
    DeviceIdentity deviceIdentity{0};
    quintptr nativeHandle{0};
    NativeHandleOwnership ownership{NativeHandleOwnership::Borrowed};

    [[nodiscard]] bool isValid() const noexcept {
        return api != GraphicsApi::Unknown && deviceIdentity != 0 &&
               nativeHandle != 0;
    }

    static GraphicsContextHandle transferred(
        GraphicsApi graphicsApi,
        DeviceIdentity graphicsDeviceIdentity,
        quintptr handle) noexcept {
        return {graphicsApi, graphicsDeviceIdentity, handle,
                NativeHandleOwnership::Transferred};
    }
};

} // namespace GPlatform::Rendering
