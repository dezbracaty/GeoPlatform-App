#pragma once

#include "MeshConvexHull.hpp"
#include <array>
#include <string>
#include <vector>

namespace GPlatform::Interaction {
// Immutable App geometry snapshot. Only printable model parts belong here;
// each part's local transform is baked into its object-space vertices.
struct FlattenRequest {
    std::vector<Geometry::TriangleMesh> volumes;
    std::array<double, 16> model_to_world{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
struct FlattenPlane {
    std::array<double, 3> normal;
    std::vector<std::array<double, 3>> vertices;
    float area_mm2 = 0;
};
struct FlattenResult {
    bool success = false;
    std::vector<FlattenPlane> planes;
    std::string error;
};
// Orca place-on-face calculation. No DB, renderer, protocol or slicing state.
FlattenResult buildFlattenPlanes(const FlattenRequest& request);
}
