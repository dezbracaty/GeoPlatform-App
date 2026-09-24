#include "SliceVertexWelder.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace GPlatform {
namespace {

std::uint32_t floatBits(float value) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

SliceVertexWelder::SliceVertexWelder(std::size_t expectedVertexCount) {
    m_vertices.reserve(expectedVertexCount);
    m_indices.reserve(expectedVertexCount);
}

std::uint32_t SliceVertexWelder::indexFor(const libslicer::SliceVertex& vertex) {
    const Key key = keyFor(vertex);
    const auto found = m_indices.find(key);
    if (found != m_indices.end()) return found->second;
    if (m_vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Slice mesh exceeds the supported vertex index range");
    }
    const auto index = static_cast<std::uint32_t>(m_vertices.size());
    m_vertices.push_back(vertex);
    m_indices.emplace(key, index);
    return index;
}

std::size_t SliceVertexWelder::size() const noexcept {
    return m_vertices.size();
}

std::vector<libslicer::SliceVertex> SliceVertexWelder::takeVertices() {
    return std::move(m_vertices);
}

bool SliceVertexWelder::Key::operator==(const Key& other) const noexcept {
    return x == other.x && y == other.y && z == other.z;
}

std::size_t SliceVertexWelder::KeyHash::operator()(const Key& key) const noexcept {
    constexpr std::size_t kOffset = sizeof(std::size_t) == 8
        ? static_cast<std::size_t>(1469598103934665603ull)
        : static_cast<std::size_t>(2166136261u);
    constexpr std::size_t kPrime = sizeof(std::size_t) == 8
        ? static_cast<std::size_t>(1099511628211ull)
        : static_cast<std::size_t>(16777619u);
    std::size_t hash = kOffset;
    for (const std::uint32_t word : {key.x, key.y, key.z}) {
        hash ^= static_cast<std::size_t>(word);
        hash *= kPrime;
    }
    return hash;
}

SliceVertexWelder::Key SliceVertexWelder::keyFor(
    const libslicer::SliceVertex& vertex) noexcept {
    return {floatBits(vertex.x), floatBits(vertex.y), floatBits(vertex.z)};
}

} // namespace GPlatform
