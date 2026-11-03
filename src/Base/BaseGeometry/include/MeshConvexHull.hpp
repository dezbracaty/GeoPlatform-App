#pragma once

#include <Eigen/Core>
#include <cstdint>
#include <vector>

namespace GPlatform::Geometry {
struct MeshVertex { float x, y, z; };
struct MeshTriangle { std::uint32_t vertex_a, vertex_b, vertex_c; };
struct TriangleMesh {
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
};
struct IndexedTriangleMesh {
    std::vector<Eigen::Vector3f> vertices;
    std::vector<Eigen::Vector3i> indices;
};
// Qhull's triangulated, outward-facing convex hull, in mesh coordinates.
// Empty/degenerate inputs produce an empty mesh.
IndexedTriangleMesh meshConvexHull(const std::vector<Eigen::Vector3f>& points);
std::vector<Eigen::Vector3f> meshFaceNormals(const IndexedTriangleMesh& mesh);
std::vector<Eigen::Vector3i> meshFaceNeighbors(const IndexedTriangleMesh& mesh);
}
