#pragma once

#include <cstdint>

namespace GPlatform::Rendering {

enum class GraphicsApi : std::uint8_t {
    Unknown = 0,
    OpenGL,
    Metal,
    Vulkan,
    Direct3D11,
    Direct3D12
};

enum class NativeHandleOwnership : std::uint8_t {
    Borrowed = 0,
    Transferred
};

} // namespace GPlatform::Rendering
