#pragma once

#include <SystemTypes.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <MeshRayQuery.hpp>
#include <QPointF>
#include <QVector3D>
#include <cstddef>
#include <vector>

/** Builds the screen-space footprint of a surface brush from a viewport snapshot. */
class SurfaceBrushProjection final {
public:
    struct ScreenContext {
        ViewportProjectionSnapshot viewport;
        Vector3 worldCenter;
        QPointF centerScreen;
        float centerDepth{0.0f};
        float radiusUiPx{0.0f};

        bool isValid() const;
        bool projectOffsetUi(const QVector3D& point, QPointF* offsetUi) const;
        bool rayAtOffsetUi(const QPointF& offsetUi, WorldRay* ray) const;
        bool worldRadius(float* radiusWorld) const;
    };

    struct VisibleFacetProjection {
        std::vector<int> facetIds;
        std::size_t sampledRayCount{0};
    };

    static bool screenContextAt(const ViewportProjectionSnapshot& viewport,
                                const Vector3& worldHit,
                                int uiRadiusPx,
                                ScreenContext* context);
    static std::vector<WorldRay> sampleRays(const ScreenContext& context,
                                            bool squareFootprint,
                                            int spacingUiPx = 3);
    static VisibleFacetProjection collectVisibleFacets(
        const ScreenContext& context,
        const MeshRayQuery& meshQuery,
        bool squareFootprint,
        int spacingUiPx,
        int centerFacetId = -1,
        const Vector3& meshOriginWorld = {});
};
