#pragma once

#include <Geometry.hpp>

#include <cstdint>
#include <memory>
#include <vector>

// Authored by the owning DB, in its local coordinate system. A feature ID
// must be unique within a snapshot and stable while that feature exists.
enum class SnapFeatureKind : std::uint8_t {
    Point = 1,
    Segment = 2,
    Line = 4
};

constexpr unsigned snapFeatureMask(SnapFeatureKind kind) noexcept {
    return static_cast<unsigned>(kind);
}

struct SnapFeature {
    std::uint64_t id{0};
    SnapFeatureKind kind{SnapFeatureKind::Point};
    Vector3 first{0, 0, 0};
    // Segment endpoint, or a second point on an infinite line. Unused for Point.
    Vector3 second{0, 0, 0};
    bool pickable{true};
    bool snappable{true};
    float radiusMm{0}; // Visible point/line radius, for overlap depth ordering.
    // DB-authored logical path. Members remain individually queryable/filterable;
    // a hit displays all eligible path members. Zero count means this feature only.
    std::uint32_t groupBegin{0}, groupCount{0};
};

struct SnapGeometry {
    std::vector<SnapFeature> features;
    struct Node {
        Vector3 min, max;
        std::uint32_t begin{0}, count{0}, left{0}, right{0};
    };
    // Derived solely from authored features, built once before publication.
    std::vector<Node> nodes;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> infiniteLines;
    void buildIndex();
};

using SnapGeometryPtr = std::shared_ptr<const SnapGeometry>;
