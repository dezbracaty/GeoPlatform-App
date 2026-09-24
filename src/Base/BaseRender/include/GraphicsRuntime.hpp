#pragma once

#include "GraphicsApi.hpp"

#include <QtGlobal>

#include <mutex>

namespace GPlatform::Rendering {

using DeviceIdentity = quintptr;

struct GraphicsRuntimeProfile {
    GraphicsApi api{GraphicsApi::Unknown};
    DeviceIdentity deviceIdentity{0};

    [[nodiscard]] bool hasApi() const noexcept {
        return api != GraphicsApi::Unknown;
    }

    [[nodiscard]] bool hasDevice() const noexcept {
        return deviceIdentity != 0;
    }
};

/**
 * Process-wide graphics invariant. The API is selected before Qt creates a
 * window; the native device/share-group identity is bound when SceneGraph
 * exposes it. Neither a backend nor a view may replace either value.
 */
class GraphicsRuntime final {
public:
    static GraphicsRuntime& instance();

    bool configureApi(GraphicsApi api);
    bool bindDevice(GraphicsApi api, DeviceIdentity deviceIdentity);

    [[nodiscard]] GraphicsRuntimeProfile profile() const;
    [[nodiscard]] bool matches(
        GraphicsApi api, DeviceIdentity deviceIdentity = 0) const;

private:
    GraphicsRuntime() = default;

    mutable std::mutex m_mutex;
    GraphicsRuntimeProfile m_profile;
};

} // namespace GPlatform::Rendering
