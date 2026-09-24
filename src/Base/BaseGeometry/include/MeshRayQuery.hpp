#pragma once

#include <QPointF>
#include <QVector3D>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

/** Immutable local-space triangle BVH shared by DB and geometry tools. */
class MeshRayQuery {
public:
    struct RayHit {
        bool valid{false};
        int facetId{-1};
        QVector3D position;
        float distance{0.0f};
    };

    MeshRayQuery();
    ~MeshRayQuery();

    MeshRayQuery(const MeshRayQuery&) = delete;
    MeshRayQuery& operator=(const MeshRayQuery&) = delete;

    bool isAvailable() const;
    bool build(const std::vector<QVector3D>& vertices,
               const std::vector<std::array<int, 3>>& triangles);
    void clear();
    float firstHitBelow(const QPointF& xy, float startZ, float fallbackZ) const;
    RayHit firstHit(const QVector3D& origin,
                    const QVector3D& direction,
                    float maxDistance = std::numeric_limits<float>::infinity()) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<QVector3D> m_fallbackVertices;
    std::vector<std::array<int, 3>> m_fallbackTriangles;
};
