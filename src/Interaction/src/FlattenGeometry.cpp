// Place-on-face numerical algorithm copied/adapted from OrcaSlicer:
// GLGizmoFlatten.cpp::update_planes and Geometry/ConvexHull.cpp::convex_hull.
// Pinned revision: 3c8c667dbd402eb52e24fcc69aea3a1b5c5c72a1 (AGPL-3.0).
// Adaptations are limited to App mesh/types and local geometry primitives.
#include "FlattenGeometry.hpp"
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>

namespace GPlatform::Interaction {
namespace {
using Vec3f = Eigen::Vector3f;
using Vec3d = Eigen::Vector3d;
using Vec3i32 = Eigen::Vector3i;
using Matrix3d = Eigen::Matrix3d;
using Transform3d = Eigen::Affine3d;
using stl_normal = Vec3f;
using Pointf3s = std::vector<Vec3d>;
using Geometry::IndexedTriangleMesh;
using Geometry::meshFaceNormals;
using Geometry::meshFaceNeighbors;
constexpr double PI = 3.14159265358979323846;
struct PlaneData { Pointf3s vertices; Vec3d normal; float area = 0; };
using PointBase = Eigen::Matrix<std::int64_t, 2, 1>;
// Orca's normalized XY hull uses 1e-6 fixed-point units, truncated toward zero.
struct Point : PointBase {
    using PointBase::PointBase;
    static Point new_scale(double x, double y) {
        const double scaledX = x / 0.000001, scaledY = y / 0.000001;
        const double limit = std::ldexp(1., 63);
        if (!std::isfinite(scaledX) || !std::isfinite(scaledY) ||
            std::abs(scaledX) >= limit || std::abs(scaledY) >= limit)
            throw std::runtime_error("Flatten polygon exceeds fixed-point coordinate range");
        return Point(std::int64_t(scaledX), std::int64_t(scaledY));
    }
};
enum Orientation { ORIENTATION_CW = -1, ORIENTATION_COLINEAR = 0, ORIENTATION_CCW = 1 };
static inline Orientation orient(const Point& a, const Point& b, const Point& c) {
    // Orca's absolute-coordinate products can overflow int64 for off-origin
    // meshes. The normalized XY hull spans at most 1e6 fixed-point units;
    // subtract first, in a wide type, to retain the same orientation safely.
    const long double bx = static_cast<long double>(b.x()) - a.x();
    const long double by = static_cast<long double>(b.y()) - a.y();
    const long double cx = static_cast<long double>(c.x()) - a.x();
    const long double cy = static_cast<long double>(c.y()) - a.y();
    const long double determinant = bx * cy - by * cx;
    return determinant > 0 ? ORIENTATION_CCW :
        (determinant == 0 ? ORIENTATION_COLINEAR : ORIENTATION_CW);
}

Pointf3s polygonConvexHull(Pointf3s points)
{
    assert(points.size() >= 3);
    // sort input points
    std::sort(points.begin(), points.end(), [](const Vec3d &a, const Vec3d &b){ return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });

    int n = points.size(), k = 0;
    Pointf3s hull;

    if (n >= 3)
    {
        hull.resize(2 * n);

        // Build lower hull
        for (int i = 0; i < n; ++i)
        {
            Point p = Point::new_scale(points[i](0), points[i](1));
            while (k >= 2)
            {
                Point k1 = Point::new_scale(hull[k - 1](0), hull[k - 1](1));
                Point k2 = Point::new_scale(hull[k - 2](0), hull[k - 2](1));

                if (orient(p, k2, k1) != ORIENTATION_CCW)
                    --k;
                else
                    break;
            }

            hull[k++] = points[i];
        }

        // Build upper hull
        for (int i = n - 2, t = k + 1; i >= 0; --i)
        {
            Point p = Point::new_scale(points[i](0), points[i](1));
            while (k >= t)
            {
                Point k1 = Point::new_scale(hull[k - 1](0), hull[k - 1](1));
                Point k2 = Point::new_scale(hull[k - 2](0), hull[k - 2](1));

                if (orient(p, k2, k1) != ORIENTATION_CCW)
                    --k;
                else
                    break;
            }

            hull[k++] = points[i];
        }

        hull.resize(k);

        assert(hull.front() == hull.back());
        hull.pop_back();
    }

    return hull;
}


Pointf3s transform(const Pointf3s& points, const Transform3d& matrix) {
    Pointf3s output;
    output.reserve(points.size());
    for (const auto& point : points) output.push_back(matrix * point);
    return output;
}
Vec3d polygonBoundsSize(const Pointf3s& points) {
    Vec3d low = points.front(), high = low;
    for (const auto& point : points) { low = low.cwiseMin(point); high = high.cwiseMax(point); }
    return high - low;
}
Transform3d scaleTransform(const Vec3d& factors) {
    Transform3d matrix = Transform3d::Identity();
    matrix.linear() = factors.asDiagonal();
    return matrix;
}
std::vector<PlaneData> buildPlanes(const IndexedTriangleMesh& ch, const Transform3d& inst_matrix) {
    std::vector<PlaneData> m_planes;
    // Following constants are used for discarding too small polygons.
    const float minimal_area = 5.f; // in square mm (world coordinates)
    const float minimal_side = 1.f; // mm
    const float minimal_angle = 1.f; // degree, initial value was 10, but cause bugs

    // Now we'll go through all the facets and append Points of facets sharing the same normal.
    // This part is still performed in mesh coordinate system.
    const int                num_of_facets  = ch.indices.size();
    const std::vector<Vec3f> face_normals   = meshFaceNormals(ch);
    const std::vector<Vec3i32> face_neighbors = meshFaceNeighbors(ch);
    std::vector<int>         facet_queue(num_of_facets, 0);
    std::vector<bool>        facet_visited(num_of_facets, false);
    int                      facet_queue_cnt = 0;
    const stl_normal*        normal_ptr      = nullptr;
    int                      facet_idx       = 0;
    while (1) {
        // Find next unvisited triangle:
        for (; facet_idx < num_of_facets; ++ facet_idx)
            if (!facet_visited[facet_idx]) {
                facet_queue[facet_queue_cnt ++] = facet_idx;
                facet_visited[facet_idx] = true;
                normal_ptr = &face_normals[facet_idx];
                m_planes.emplace_back();
                break;
            }
        if (facet_idx == num_of_facets)
            break; // Everything was visited already

        while (facet_queue_cnt > 0) {
            int facet_idx = facet_queue[-- facet_queue_cnt];
            const stl_normal& this_normal = face_normals[facet_idx];
            if (std::abs(this_normal(0) - (*normal_ptr)(0)) < 0.001 && std::abs(this_normal(1) - (*normal_ptr)(1)) < 0.001 && std::abs(this_normal(2) - (*normal_ptr)(2)) < 0.001) {
                const Vec3i32 face = ch.indices[facet_idx];
                for (int j=0; j<3; ++j)
                    m_planes.back().vertices.emplace_back(ch.vertices[face[j]].cast<double>());

                facet_visited[facet_idx] = true;
                for (int j = 0; j < 3; ++ j)
                    if (int neighbor_idx = face_neighbors[facet_idx][j]; neighbor_idx >= 0 && ! facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt ++] = neighbor_idx;
            }
        }
        m_planes.back().normal = normal_ptr->cast<double>();

        Pointf3s& verts = m_planes.back().vertices;
        // Now we'll transform all the points into world coordinates, so that the areas, angles and distances
        // make real sense.
        verts = transform(verts, inst_matrix);

        // if this is a just a very small triangle, remove it to speed up further calculations (it would be rejected later anyway):
        if (verts.size() == 3 &&
            ((verts[0] - verts[1]).norm() < minimal_side
            || (verts[0] - verts[2]).norm() < minimal_side
            || (verts[1] - verts[2]).norm() < minimal_side))
            m_planes.pop_back();
    }

    // Let's prepare transformation of the normal vector from mesh to instance coordinates.
    const Matrix3d normal_matrix = inst_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();

    // Now we'll go through all the polygons, transform the points into xy plane to process them:
    for (unsigned int polygon_id=0; polygon_id < m_planes.size(); ++polygon_id) {
        Pointf3s& polygon = m_planes[polygon_id].vertices;
        const Vec3d& normal = m_planes[polygon_id].normal;

        // transform the normal according to the instance matrix:
        const Vec3d normal_transformed = normal_matrix * normal;

        // We are going to rotate about z and y to flatten the plane
        Eigen::Quaterniond q;
        Transform3d m = Transform3d::Identity();
        m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(normal_transformed, Vec3d::UnitZ()).toRotationMatrix();
        polygon = transform(polygon, m);

        // Now to remove the inner points. We'll misuse Geometry::convex_hull for that, but since
        // it works in fixed point representation, we will rescale the polygon to avoid overflows.
        // And yes, it is a nasty thing to do. Whoever has time is free to refactor.
        Vec3d bb_size = polygonBoundsSize(polygon);
        float sf = std::min(1./bb_size(0), 1./bb_size(1));
        Transform3d tr = scaleTransform({ sf, sf, 1.f });
        polygon = transform(polygon, tr);
        polygon = polygonConvexHull(polygon);
        polygon = transform(polygon, tr.inverse());

        // Calculate area of the polygons and discard ones that are too small
        float& area = m_planes[polygon_id].area;
        area = 0.f;
        for (unsigned int i = 0; i < polygon.size(); i++) // Shoelace formula
            area += polygon[i](0)*polygon[i + 1 < polygon.size() ? i + 1 : 0](1) - polygon[i + 1 < polygon.size() ? i + 1 : 0](0)*polygon[i](1);
        area = 0.5f * std::abs(area);

        bool discard = false;
        if (area < minimal_area)
            discard = true;
        else {
            // We also check the inner angles and discard polygons with angles smaller than the following threshold
            const double angle_threshold = ::cos(minimal_angle * (double)PI / 180.0);

            for (unsigned int i = 0; i < polygon.size(); ++i) {
                const Vec3d& prec = polygon[(i == 0) ? polygon.size() - 1 : i - 1];
                const Vec3d& curr = polygon[i];
                const Vec3d& next = polygon[(i == polygon.size() - 1) ? 0 : i + 1];

                if ((prec - curr).normalized().dot((next - curr).normalized()) > angle_threshold) {
                    discard = true;
                    break;
                }
            }
        }

        if (discard) {
            m_planes[polygon_id--] = std::move(m_planes.back());
            m_planes.pop_back();
            continue;
        }

        // We will shrink the polygon a little bit so it does not touch the object edges:
        Vec3d centroid = std::accumulate(polygon.begin(), polygon.end(), Vec3d(0.0, 0.0, 0.0));
        centroid /= (double)polygon.size();
        for (auto& vertex : polygon)
            vertex = 0.9f*vertex + 0.1f*centroid;

        // Polygon is now simple and convex, we'll round the corners to make them look nicer.
        // The algorithm takes a vertex, calculates middles of respective sides and moves the vertex
        // towards their average (controlled by 'aggressivity'). This is repeated k times.
        // In next iterations, the neighbours are not always taken at the middle (to increase the
        // rounding effect at the corners, where we need it most).
        const unsigned int k = 10; // number of iterations
        const float aggressivity = 0.2f;  // agressivity
        const unsigned int N = polygon.size();
        std::vector<std::pair<unsigned int, unsigned int>> neighbours;
        if (k != 0) {
            Pointf3s points_out(2*k*N); // vector long enough to store the future vertices
            for (unsigned int j=0; j<N; ++j) {
                points_out[j*2*k] = polygon[j];
                neighbours.push_back(std::make_pair((int)(j*2*k-k) < 0 ? (N-1)*2*k+k : j*2*k-k, j*2*k+k));
            }

            for (unsigned int i=0; i<k; ++i) {
                // Calculate middle of each edge so that neighbours points to something useful:
                for (unsigned int j=0; j<N; ++j)
                    if (i==0)
                        points_out[j*2*k+k] = 0.5f * (points_out[j*2*k] + points_out[j==N-1 ? 0 : (j+1)*2*k]);
                    else {
                        float r = 0.2+0.3/(k-1)*i; // the neighbours are not always taken in the middle
                        points_out[neighbours[j].first] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].first-1];
                        points_out[neighbours[j].second] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].second+1];
                    }
                // Now we have a triangle and valid neighbours, we can do an iteration:
                for (unsigned int j=0; j<N; ++j)
                    points_out[2*k*j] = (1-aggressivity) * points_out[2*k*j] +
                                        aggressivity*0.5f*(points_out[neighbours[j].first] + points_out[neighbours[j].second]);

                for (auto& n : neighbours) {
                    ++n.first;
                    --n.second;
                }
            }
            polygon = points_out; // replace the coarse polygon with the smooth one that we just created
        }


        // Raise a bit above the object surface to avoid flickering:
        for (auto& b : polygon)
            b(2) += 0.1f;

        // Transform back to 3D (and also back to mesh coordinates)
        polygon = transform(polygon, inst_matrix.inverse() * m.inverse());
    }

    // We'll sort the planes by area and only keep the 254 largest ones (because of the picking pass limitations):
    std::sort(m_planes.rbegin(), m_planes.rend(), [](const PlaneData& a, const PlaneData& b) { return a.area < b.area; });
    m_planes.resize(std::min((int)m_planes.size(), 254));
    return m_planes;
}
} // namespace

FlattenResult buildFlattenPlanes(const FlattenRequest& request)
{
    FlattenResult result;
    Transform3d model_to_world = Transform3d::Identity();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            model_to_world.matrix()(row, column) = request.model_to_world[row * 4 + column];
    if (!model_to_world.matrix().allFinite() ||
        !model_to_world.matrix().row(3).isApprox(Eigen::RowVector4d(0, 0, 0, 1), 1.e-12) ||
        std::abs(model_to_world.linear().determinant()) <= 1.e-12) {
        result.error = "Flatten transform must be a finite, invertible affine matrix";
        return result;
    }

    try {
        std::vector<Vec3f> merged;
        for (const Geometry::TriangleMesh& volume : request.volumes) {
            IndexedTriangleMesh mesh;
            mesh.vertices.reserve(volume.vertices.size());
            for (const Geometry::MeshVertex& point : volume.vertices) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
                    result.error = "Flatten input contains a non-finite vertex";
                    return result;
                }
                mesh.vertices.emplace_back(point.x, point.y, point.z);
            }
            mesh.indices.reserve(volume.triangles.size());
            for (const Geometry::MeshTriangle& triangle : volume.triangles) {
                if (triangle.vertex_a >= mesh.vertices.size() ||
                    triangle.vertex_b >= mesh.vertices.size() ||
                    triangle.vertex_c >= mesh.vertices.size()) {
                    result.error = "Flatten input contains an invalid triangle index";
                    return result;
                }
                mesh.indices.emplace_back(triangle.vertex_a, triangle.vertex_b, triangle.vertex_c);
            }
            if (mesh.indices.empty()) continue;
            // Same volume hull -> merged object hull path as update_planes().
            const auto hull = Geometry::meshConvexHull(mesh.vertices);
            merged.insert(merged.end(), hull.vertices.begin(), hull.vertices.end());
        }
        const auto ch = Geometry::meshConvexHull(merged);
        if (ch.indices.empty()) {
            result.error = "Flatten input has no three-dimensional printable convex hull";
            return result;
        }

        // Orca uses get_matrix_no_offset(): translation must not affect plane
        // construction or its precision. Apply the full pose only on output.
        Transform3d instance_matrix = model_to_world;
        instance_matrix.translation().setZero();
        const auto planes = buildPlanes(ch, instance_matrix);
        const Matrix3d normal_matrix = model_to_world.linear().inverse().transpose();
        result.planes.reserve(planes.size());
        for (const auto& source : planes) {
            FlattenPlane plane;
            const Vec3d normal = (normal_matrix * source.normal).normalized();
            plane.normal = {normal.x(), normal.y(), normal.z()};
            plane.area_mm2 = source.area;
            plane.vertices.reserve(source.vertices.size());
            for (const auto& point : source.vertices) {
                const Vec3d world = model_to_world * point;
                plane.vertices.push_back({world.x(), world.y(), world.z()});
            }
            result.planes.push_back(std::move(plane));
        }
        result.success = true;
    } catch (const std::exception& error) {
        result.planes.clear();
        result.error = error.what();
    } catch (...) {
        result.planes.clear();
        result.error = "Flatten plane construction failed";
    }
    return result;
}

} // namespace GPlatform::Interaction
