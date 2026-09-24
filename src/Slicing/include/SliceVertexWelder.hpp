#pragma once

#include <libslicer/Library.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace GPlatform {

// Builds the indexed vertex array required by libslicer's in-memory mesh API.
// Positions are welded by exact float identity; callers retain control of face
// order so per-triangle annotations remain stable.
class SliceVertexWelder final {
public:
    explicit SliceVertexWelder(std::size_t expectedVertexCount = 0);

    std::uint32_t indexFor(const libslicer::SliceVertex& vertex);
    std::size_t size() const noexcept;
    std::vector<libslicer::SliceVertex> takeVertices();

private:
    struct Key {
        std::uint32_t x{0};
        std::uint32_t y{0};
        std::uint32_t z{0};

        bool operator==(const Key& other) const noexcept;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };

    static Key keyFor(const libslicer::SliceVertex& vertex) noexcept;

    std::vector<libslicer::SliceVertex> m_vertices;
    std::unordered_map<Key, std::uint32_t, KeyHash> m_indices;
};

} // namespace GPlatform
