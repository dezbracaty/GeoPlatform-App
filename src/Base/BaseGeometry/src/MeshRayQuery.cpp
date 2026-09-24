#include "MeshRayQuery.hpp"

// This implementation belongs to the renderer-independent BaseGeometry layer.
#include "Foundation/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(GPLATFORM_HAS_EMBREE)
#  if __has_include(<embree4/rtcore.h>)
#    include <embree4/rtcore.h>
#    define GPLATFORM_EMBREE_HEADER_AVAILABLE 1
#  endif
#endif

namespace {

bool interpolate_triangle_z_at_xy(const QPointF& point,
                                  const QVector3D& a,
                                  const QVector3D& b,
                                  const QVector3D& c,
                                  double* z) {
    if (!z) {
        return false;
    }

    const double denominator =
        (b.y() - c.y()) * (a.x() - c.x()) +
        (c.x() - b.x()) * (a.y() - c.y());
    if (std::abs(denominator) <= 1e-10) {
        return false;
    }

    const double w0 =
        ((b.y() - c.y()) * (point.x() - c.x()) +
         (c.x() - b.x()) * (point.y() - c.y())) / denominator;
    const double w1 =
        ((c.y() - a.y()) * (point.x() - c.x()) +
         (a.x() - c.x()) * (point.y() - c.y())) / denominator;
    const double w2 = 1.0 - w0 - w1;

    constexpr double kBarycentricTolerance = 1e-8;
    if (w0 < -kBarycentricTolerance ||
        w1 < -kBarycentricTolerance ||
        w2 < -kBarycentricTolerance) {
        return false;
    }

    *z = w0 * a.z() + w1 * b.z() + w2 * c.z();
    return std::isfinite(*z);
}

float brute_force_first_hit_below(const QPointF& xy,
                                  float startZ,
                                  float fallbackZ,
                                  const std::vector<QVector3D>& vertices,
                                  const std::vector<std::array<int, 3>>& triangles) {
    double bestZ = static_cast<double>(fallbackZ);
    const double maxZ = static_cast<double>(startZ) - 1e-4;

    for (const auto& triangle : triangles) {
        if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
            triangle[0] >= static_cast<int>(vertices.size()) ||
            triangle[1] >= static_cast<int>(vertices.size()) ||
            triangle[2] >= static_cast<int>(vertices.size())) {
            continue;
        }

        const QVector3D& a = vertices[static_cast<std::size_t>(triangle[0])];
        const QVector3D& b = vertices[static_cast<std::size_t>(triangle[1])];
        const QVector3D& c = vertices[static_cast<std::size_t>(triangle[2])];
        if (std::max({static_cast<double>(a.z()), static_cast<double>(b.z()), static_cast<double>(c.z())}) <= bestZ) {
            continue;
        }
        if (std::min({static_cast<double>(a.z()), static_cast<double>(b.z()), static_cast<double>(c.z())}) >= maxZ) {
            continue;
        }

        double triangleZ = 0.0;
        if (!interpolate_triangle_z_at_xy(xy, a, b, c, &triangleZ)) {
            continue;
        }
        if (triangleZ > bestZ && triangleZ < maxZ) {
            bestZ = triangleZ;
        }
    }

    return static_cast<float>(bestZ);
}

MeshRayQuery::RayHit brute_force_first_hit(
    const QVector3D& origin,
    const QVector3D& direction,
    float maxDistance,
    const std::vector<QVector3D>& vertices,
    const std::vector<std::array<int, 3>>& triangles) {
    MeshRayQuery::RayHit result;
    float bestDistance = maxDistance;
    constexpr float kRayEpsilon = 1e-5f;
    for (std::size_t facet = 0; facet < triangles.size(); ++facet) {
        const auto& triangle = triangles[facet];
        if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
            triangle[0] >= static_cast<int>(vertices.size()) ||
            triangle[1] >= static_cast<int>(vertices.size()) ||
            triangle[2] >= static_cast<int>(vertices.size())) {
            continue;
        }
        const QVector3D& v0 = vertices[static_cast<std::size_t>(triangle[0])];
        const QVector3D& v1 = vertices[static_cast<std::size_t>(triangle[1])];
        const QVector3D& v2 = vertices[static_cast<std::size_t>(triangle[2])];
        const QVector3D edge1 = v1 - v0;
        const QVector3D edge2 = v2 - v0;
        const QVector3D p = QVector3D::crossProduct(direction, edge2);
        const float determinant = QVector3D::dotProduct(edge1, p);
        if (std::abs(determinant) <= kRayEpsilon) continue;
        const float inverseDeterminant = 1.0f / determinant;
        const QVector3D fromV0 = origin - v0;
        const float u = QVector3D::dotProduct(fromV0, p) * inverseDeterminant;
        if (u < 0.0f || u > 1.0f) continue;
        const QVector3D q = QVector3D::crossProduct(fromV0, edge1);
        const float v = QVector3D::dotProduct(direction, q) * inverseDeterminant;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float distance = QVector3D::dotProduct(edge2, q) * inverseDeterminant;
        if (distance <= kRayEpsilon || distance >= bestDistance) continue;
        bestDistance = distance;
        result.valid = true;
        result.facetId = static_cast<int>(facet);
        result.distance = distance;
        result.position = origin + direction * distance;
    }
    return result;
}

} // namespace

struct MeshRayQuery::Impl {
#if defined(GPLATFORM_EMBREE_HEADER_AVAILABLE)
    struct Float3 {
        float x;
        float y;
        float z;
    };

    struct UInt3 {
        unsigned int x;
        unsigned int y;
        unsigned int z;
    };

    RTCDevice device{nullptr};
    RTCScene scene{nullptr};
    std::vector<Float3> vertices;
    std::vector<UInt3> triangles;
    std::vector<int> sourceFacetIds;

    ~Impl() {
        if (scene) {
            rtcReleaseScene(scene);
            scene = nullptr;
        }
        if (device) {
            rtcReleaseDevice(device);
            device = nullptr;
        }
    }
#endif
};

MeshRayQuery::MeshRayQuery()
    : m_impl(std::make_unique<Impl>()) {
}

MeshRayQuery::~MeshRayQuery() = default;

bool MeshRayQuery::isAvailable() const {
#if defined(GPLATFORM_EMBREE_HEADER_AVAILABLE)
    return true;
#else
    return false;
#endif
}

bool MeshRayQuery::build(const std::vector<QVector3D>& vertices,
                                     const std::vector<std::array<int, 3>>& triangles) {
    clear();

#if defined(GPLATFORM_EMBREE_HEADER_AVAILABLE)
    if (!m_impl) {
        m_impl = std::make_unique<Impl>();
    }

    if (vertices.empty() || triangles.empty()) {
        m_fallbackVertices = vertices;
        m_fallbackTriangles = triangles;
        return false;
    }

    m_impl->device = rtcNewDevice(nullptr);
    if (!m_impl->device) {
        LOG_WARN("MeshRayQuery: failed to create Embree device, fallback to brute force");
        m_fallbackVertices = vertices;
        m_fallbackTriangles = triangles;
        return false;
    }

    m_impl->scene = rtcNewScene(m_impl->device);
    if (!m_impl->scene) {
        LOG_WARN("MeshRayQuery: failed to create Embree scene, fallback to brute force");
        m_fallbackVertices = vertices;
        m_fallbackTriangles = triangles;
        return false;
    }

    m_impl->vertices.reserve(vertices.size());
    for (const QVector3D& vertex : vertices) {
        m_impl->vertices.push_back({vertex.x(), vertex.y(), vertex.z()});
    }

    m_impl->triangles.reserve(triangles.size());
    for (std::size_t sourceFacet = 0; sourceFacet < triangles.size(); ++sourceFacet) {
        const auto& triangle = triangles[sourceFacet];
        if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
            triangle[0] >= static_cast<int>(vertices.size()) ||
            triangle[1] >= static_cast<int>(vertices.size()) ||
            triangle[2] >= static_cast<int>(vertices.size())) {
            continue;
        }
        m_impl->triangles.push_back({
            static_cast<unsigned int>(triangle[0]),
            static_cast<unsigned int>(triangle[1]),
            static_cast<unsigned int>(triangle[2])
        });
        m_impl->sourceFacetIds.push_back(static_cast<int>(sourceFacet));
    }

    if (m_impl->triangles.empty()) {
        return false;
    }

    RTCGeometry geometry = rtcNewGeometry(m_impl->device, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetSharedGeometryBuffer(
        geometry,
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT3,
        m_impl->vertices.data(),
        0,
        sizeof(Impl::Float3),
        m_impl->vertices.size());
    rtcSetSharedGeometryBuffer(
        geometry,
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT3,
        m_impl->triangles.data(),
        0,
        sizeof(Impl::UInt3),
        m_impl->triangles.size());

    rtcCommitGeometry(geometry);
    rtcAttachGeometry(m_impl->scene, geometry);
    rtcReleaseGeometry(geometry);
    rtcCommitScene(m_impl->scene);
    return true;
#else
    m_fallbackVertices = vertices;
    m_fallbackTriangles = triangles;
    return false;
#endif
}

void MeshRayQuery::clear() {
#if defined(GPLATFORM_EMBREE_HEADER_AVAILABLE)
    m_impl.reset();
    m_impl = std::make_unique<Impl>();
#endif
    m_fallbackVertices.clear();
    m_fallbackTriangles.clear();
}

float MeshRayQuery::firstHitBelow(const QPointF& xy,
                                              float startZ,
                                              float fallbackZ) const {
#if defined(GPLATFORM_EMBREE_HEADER_AVAILABLE)
    if (m_impl && m_impl->scene) {
        RTCRayHit rayhit{};
        rayhit.ray.org_x = static_cast<float>(xy.x());
        rayhit.ray.org_y = static_cast<float>(xy.y());
        rayhit.ray.org_z = startZ;
        rayhit.ray.dir_x = 0.0f;
        rayhit.ray.dir_y = 0.0f;
        rayhit.ray.dir_z = -1.0f;
        rayhit.ray.tnear = 0.0f;
        rayhit.ray.tfar = std::max(startZ - fallbackZ, 0.0f);
        rayhit.ray.mask = 0xFFFFFFFFu;
        rayhit.ray.flags = 0u;
        rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

        RTCIntersectArguments arguments;
        rtcInitIntersectArguments(&arguments);
        rtcIntersect1(m_impl->scene, &rayhit, &arguments);

        if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
            const float hitZ = startZ - rayhit.ray.tfar;
            if (std::isfinite(hitZ) && hitZ > fallbackZ) {
                return hitZ;
            }
        }
    }
#endif

    return brute_force_first_hit_below(xy, startZ, fallbackZ, m_fallbackVertices, m_fallbackTriangles);
}

MeshRayQuery::RayHit MeshRayQuery::firstHit(
    const QVector3D& origin,
    const QVector3D& direction,
    float maxDistance) const {
    QVector3D normalizedDirection = direction;
    if (std::isnan(maxDistance) || maxDistance <= 0.0f) return {};
    if (!std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
        !std::isfinite(origin.z()) || !std::isfinite(normalizedDirection.x()) ||
        !std::isfinite(normalizedDirection.y()) || !std::isfinite(normalizedDirection.z()) ||
        normalizedDirection.lengthSquared() <= 1e-12f || maxDistance <= 0.0f) {
        return {};
    }
    normalizedDirection.normalize();

#if defined(GPLATFORM_EMBREE_HEADER_AVAILABLE)
    if (m_impl && m_impl->scene) {
        RTCRayHit rayhit{};
        rayhit.ray.org_x = origin.x();
        rayhit.ray.org_y = origin.y();
        rayhit.ray.org_z = origin.z();
        rayhit.ray.dir_x = normalizedDirection.x();
        rayhit.ray.dir_y = normalizedDirection.y();
        rayhit.ray.dir_z = normalizedDirection.z();
        rayhit.ray.tnear = 1e-5f;
        rayhit.ray.tfar = maxDistance;
        rayhit.ray.mask = 0xFFFFFFFFu;
        rayhit.ray.flags = 0u;
        rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        rayhit.hit.primID = RTC_INVALID_GEOMETRY_ID;
        rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

        RTCIntersectArguments arguments;
        rtcInitIntersectArguments(&arguments);
        rtcIntersect1(m_impl->scene, &rayhit, &arguments);
        if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID &&
            rayhit.hit.primID < m_impl->sourceFacetIds.size() &&
            std::isfinite(rayhit.ray.tfar)) {
            RayHit result;
            result.valid = true;
            result.facetId = m_impl->sourceFacetIds[rayhit.hit.primID];
            result.distance = rayhit.ray.tfar;
            result.position = origin + normalizedDirection * rayhit.ray.tfar;
            return result;
        }
        // An accelerated miss is final. Falling through to the O(N) fallback
        // here would make every empty brush sample scan the complete mesh.
        return {};
    }
#endif

    return brute_force_first_hit(origin, normalizedDirection, maxDistance,
                                 m_fallbackVertices, m_fallbackTriangles);
}
