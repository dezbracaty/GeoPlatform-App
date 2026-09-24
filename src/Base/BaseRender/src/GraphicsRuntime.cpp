#include "GraphicsRuntime.hpp"

namespace GPlatform::Rendering {

GraphicsRuntime& GraphicsRuntime::instance() {
    static GraphicsRuntime runtime;
    return runtime;
}

bool GraphicsRuntime::configureApi(GraphicsApi api) {
    if (api == GraphicsApi::Unknown) {
        return false;
    }

    std::lock_guard lock(m_mutex);
    if (m_profile.api != GraphicsApi::Unknown && m_profile.api != api) {
        return false;
    }
    m_profile.api = api;
    return true;
}

bool GraphicsRuntime::bindDevice(
    GraphicsApi api, DeviceIdentity deviceIdentity) {
    if (api == GraphicsApi::Unknown || deviceIdentity == 0) {
        return false;
    }

    std::lock_guard lock(m_mutex);
    if (m_profile.api != GraphicsApi::Unknown && m_profile.api != api) {
        return false;
    }
    if (m_profile.deviceIdentity != 0 &&
        m_profile.deviceIdentity != deviceIdentity) {
        return false;
    }
    m_profile.api = api;
    m_profile.deviceIdentity = deviceIdentity;
    return true;
}

GraphicsRuntimeProfile GraphicsRuntime::profile() const {
    std::lock_guard lock(m_mutex);
    return m_profile;
}

bool GraphicsRuntime::matches(
    GraphicsApi api, DeviceIdentity deviceIdentity) const {
    std::lock_guard lock(m_mutex);
    if (m_profile.api == GraphicsApi::Unknown || m_profile.api != api) {
        return false;
    }
    return deviceIdentity == 0 || m_profile.deviceIdentity == 0 ||
           m_profile.deviceIdentity == deviceIdentity;
}

} // namespace GPlatform::Rendering
