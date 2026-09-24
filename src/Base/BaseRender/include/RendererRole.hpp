#pragma once

#include <cstdint>

namespace GPlatform::Rendering {

enum class RendererRole : std::uint32_t {
    None = 0,
    Compatibility = 1u << 0,
    Prepare = 1u << 1,
    Preview = 1u << 2,
    Realistic = 1u << 3,
};

using RendererRoleMask = std::uint32_t;

constexpr RendererRoleMask rendererRoleBit(RendererRole role) noexcept {
    return static_cast<RendererRoleMask>(role);
}

constexpr RendererRoleMask operator|(RendererRole lhs, RendererRole rhs) noexcept {
    return rendererRoleBit(lhs) | rendererRoleBit(rhs);
}

constexpr RendererRoleMask operator|(RendererRoleMask lhs, RendererRole rhs) noexcept {
    return lhs | rendererRoleBit(rhs);
}

constexpr bool supportsRendererRole(RendererRoleMask mask, RendererRole role) noexcept {
    return (mask & rendererRoleBit(role)) != 0;
}

namespace RendererRoleSets {

// Compatibility is deliberately included in every production registration
// during the migration. The existing single renderer therefore creates the
// exact same DBSync set until the dual-page UI is enabled.
inline constexpr RendererRoleMask Compatibility =
    rendererRoleBit(RendererRole::Compatibility);
inline constexpr RendererRoleMask Prepare =
    Compatibility | RendererRole::Prepare;
inline constexpr RendererRoleMask Preview =
    Compatibility | RendererRole::Preview;
inline constexpr RendererRoleMask Realistic =
    Compatibility | RendererRole::Realistic;
inline constexpr RendererRoleMask PrepareAndPreview =
    Prepare | RendererRole::Preview;

} // namespace RendererRoleSets

} // namespace GPlatform::Rendering
