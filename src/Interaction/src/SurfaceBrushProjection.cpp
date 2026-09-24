#include "SurfaceBrushProjection.hpp"

#include <algorithm>
#include <cmath>

bool SurfaceBrushProjection::ScreenContext::isValid() const {
    return viewport.isValid() && radiusUiPx > 0.0f &&
           std::isfinite(centerScreen.x()) && std::isfinite(centerScreen.y()) &&
           std::isfinite(centerDepth);
}

bool SurfaceBrushProjection::ScreenContext::projectOffsetUi(
    const QVector3D& point,
    QPointF* offsetUi) const {
    if (!offsetUi || !isValid()) return false;
    const Vector3 screen = viewport.screenFromWorld(Vector3(point.x(), point.y(), point.z()));
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y) || !std::isfinite(screen.z)) {
        return false;
    }
    *offsetUi = QPointF(screen.x, screen.y) - centerScreen;
    return std::isfinite(offsetUi->x()) && std::isfinite(offsetUi->y());
}

bool SurfaceBrushProjection::ScreenContext::rayAtOffsetUi(
    const QPointF& offsetUi,
    WorldRay* ray) const {
    if (!ray || !isValid()) return false;
    WorldRay result = viewport.rayFromScreen(centerScreen + offsetUi);
    const Vector3& origin = result.origin;
    Vector3& direction = result.direction;
    const float lengthSquared = direction.x * direction.x +
                                direction.y * direction.y +
                                direction.z * direction.z;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) ||
        !std::isfinite(direction.z) || lengthSquared <= 1e-12f) {
        return false;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    direction = direction * inverseLength;
    *ray = result;
    return true;
}

bool SurfaceBrushProjection::ScreenContext::worldRadius(float* radiusWorld) const {
    if (!radiusWorld || !isValid()) return false;
    const QPointF offsets[] = {
        QPointF(radiusUiPx, 0.0),
        QPointF(0.0, radiusUiPx)};
    float sum = 0.0f;
    int sampleCount = 0;
    for (const QPointF& offset : offsets) {
        const Vector3 offsetWorld = viewport.worldFromScreen(
            centerScreen + offset, centerDepth);
        const Vector3 delta = offsetWorld - worldCenter;
        const float radius = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (!std::isfinite(radius) || radius <= 1e-6f) continue;
        sum += radius;
        ++sampleCount;
    }
    if (sampleCount == 0) return false;
    const float result = sum / static_cast<float>(sampleCount);
    if (!std::isfinite(result) || result <= 0.0f) return false;
    *radiusWorld = result;
    return true;
}

bool SurfaceBrushProjection::screenContextAt(
    const ViewportProjectionSnapshot& viewport,
    const Vector3& worldHit,
    int uiRadiusPx,
    ScreenContext* context) {
    if (!context || !viewport.isValid() || uiRadiusPx <= 0) return false;
    const Vector3 screenCenter = viewport.screenFromWorld(worldHit);
    if (!std::isfinite(screenCenter.x) || !std::isfinite(screenCenter.y) ||
        !std::isfinite(screenCenter.z)) {
        return false;
    }

    ScreenContext result;
    result.viewport = viewport;
    result.worldCenter = worldHit;
    result.centerScreen = QPointF(screenCenter.x, screenCenter.y);
    result.centerDepth = screenCenter.z;
    result.radiusUiPx = static_cast<float>(uiRadiusPx);
    *context = std::move(result);
    return true;
}

std::vector<WorldRay> SurfaceBrushProjection::sampleRays(
    const ScreenContext& context,
    bool squareFootprint,
    int spacingUiPx) {
    std::vector<WorldRay> rays;
    if (!context.isValid()) return rays;

    const int spacing = std::max(spacingUiPx, 1);
    const int samplesPerRadius = std::max(
        1, static_cast<int>(std::ceil(context.radiusUiPx / static_cast<float>(spacing))));
    const double radius = context.radiusUiPx;
    const double radiusSquared = radius * radius;
    rays.reserve(static_cast<std::size_t>((samplesPerRadius * 2 + 1) *
                                          (samplesPerRadius * 2 + 1)));
    for (int y = -samplesPerRadius; y <= samplesPerRadius; ++y) {
        const double offsetY = radius * static_cast<double>(y) / samplesPerRadius;
        for (int x = -samplesPerRadius; x <= samplesPerRadius; ++x) {
            const double offsetX = radius * static_cast<double>(x) / samplesPerRadius;
            if (!squareFootprint && offsetX * offsetX + offsetY * offsetY > radiusSquared) {
                continue;
            }
            WorldRay ray;
            if (context.rayAtOffsetUi(QPointF(offsetX, offsetY), &ray)) {
                rays.push_back(ray);
            }
        }
    }
    return rays;
}

SurfaceBrushProjection::VisibleFacetProjection
SurfaceBrushProjection::collectVisibleFacets(
    const ScreenContext& context,
    const MeshRayQuery& meshQuery,
    bool squareFootprint,
    int spacingUiPx,
    int centerFacetId,
    const Vector3& meshOriginWorld) {
    VisibleFacetProjection result;
    if (centerFacetId >= 0) result.facetIds.push_back(centerFacetId);

    const auto rays = sampleRays(context, squareFootprint, spacingUiPx);
    result.sampledRayCount = rays.size();
    result.facetIds.reserve(result.facetIds.size() + rays.size());
    for (const WorldRay& ray : rays) {
        const auto hit = meshQuery.firstHit(
            QVector3D(ray.origin.x - meshOriginWorld.x,
                      ray.origin.y - meshOriginWorld.y,
                      ray.origin.z - meshOriginWorld.z),
            QVector3D(ray.direction.x, ray.direction.y, ray.direction.z));
        if (hit.valid && hit.facetId >= 0) result.facetIds.push_back(hit.facetId);
    }
    std::sort(result.facetIds.begin(), result.facetIds.end());
    result.facetIds.erase(
        std::unique(result.facetIds.begin(), result.facetIds.end()),
        result.facetIds.end());
    return result;
}
