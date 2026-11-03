// Convex hull bridge adapted from OrcaSlicer TriangleMesh.cpp, its_convex_hull.
// Original revision: 3c8c667dbd402eb52e24fcc69aea3a1b5c5c72a1 (AGPL-3.0).
#include "MeshConvexHull.hpp"
#include <Eigen/Geometry>
#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>
#include <libqhullcpp/QhullError.h>
#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace GPlatform::Geometry {
IndexedTriangleMesh meshConvexHull(const std::vector<Eigen::Vector3f>& points) {
    IndexedTriangleMesh mesh;
    if (points.size() < 4) return mesh;
    std::ostringstream errors;
    orgQhull::Qhull qhull;
    qhull.disableOutputStream();
    qhull.setErrorStream(&errors);
    std::vector<realT> coordinates;
    coordinates.reserve(points.size() * 3);
    for (const auto& point : points)
        for (int i = 0; i < 3; ++i) coordinates.push_back(point[i]);
    try {
        qhull.runQhull("", 3, int(points.size()), coordinates.data(), "Qt");
    } catch (const orgQhull::QhullError&) {
        return {};
    }
    std::vector<int> vertexMap;
    for (const orgQhull::QhullFacet& facet : qhull.facetList()) {
        Eigen::Vector3i indices;
        int count = 0;
        for (const orgQhull::QhullVertex vertex : facet.vertices()) {
            if (count == 3) throw std::runtime_error("Non-triangular Qhull facet");
            const int id = vertex.id();
            if (id < 0) throw std::runtime_error("Invalid Qhull vertex");
            if (id >= int(vertexMap.size())) vertexMap.resize(id + 1, -1);
            int& mapped = vertexMap[id];
            if (mapped < 0) {
                mapped = int(mesh.vertices.size());
                const orgQhull::QhullPoint point(vertex.point());
                mesh.vertices.emplace_back(point[0], point[1], point[2]);
            }
            indices[count++] = mapped;
        }
        if (count != 3) throw std::runtime_error("Degenerate Qhull facet");
        const Eigen::Vector3f normal = (mesh.vertices[indices[1]] - mesh.vertices[indices[0]])
            .cross(mesh.vertices[indices[2]] - mesh.vertices[indices[1]]);
        const auto* outward = facet.getBaseT()->normal;
        if (normal.x()*outward[0] + normal.y()*outward[1] + normal.z()*outward[2] < 0.)
            std::swap(indices[1], indices[2]);
        mesh.indices.push_back(indices);
    }
    return mesh;
}

std::vector<Eigen::Vector3f> meshFaceNormals(const IndexedTriangleMesh& mesh) {
    std::vector<Eigen::Vector3f> normals;
    normals.reserve(mesh.indices.size());
    for (const auto& face : mesh.indices)
        // Match Orca's face_normal_normalized (including both normalizations).
        normals.push_back((mesh.vertices[face[1]] - mesh.vertices[face[0]])
            .cross(mesh.vertices[face[2]] - mesh.vertices[face[1]]).normalized().normalized());
    return normals;
}

std::vector<Eigen::Vector3i> meshFaceNeighbors(const IndexedTriangleMesh& mesh) {
    std::vector<Eigen::Vector3i> neighbors(mesh.indices.size(), Eigen::Vector3i::Constant(-1));
    std::map<std::pair<int, int>, std::pair<int, int>> edges;
    for (int face = 0; face < int(mesh.indices.size()); ++face) {
        for (int side = 0; side < 3; ++side) {
            const int a = mesh.indices[face][side], b = mesh.indices[face][(side+1)%3];
            const auto key = std::minmax(a, b);
            const auto [entry, inserted] = edges.emplace(key, std::make_pair(face, side));
            if (!inserted) {
                const auto [otherFace, otherSide] = entry->second;
                if (neighbors[otherFace][otherSide] >= 0)
                    throw std::runtime_error("Non-manifold convex hull");
                neighbors[face][side] = otherFace;
                neighbors[otherFace][otherSide] = face;
            }
        }
    }
    return neighbors;
}
}
