#pragma once

#include <BaseID.hpp>
#include <SystemTypes.hpp>

#include <QMetaType>
#include <QPointF>

#include <cstdint>
#include <optional>

namespace GPlatform::Rendering {

enum class PickFeature : std::uint32_t {
    Object = 1u << 0u,
    Surface = 1u << 1u,
    Primitive = 1u << 2u,
    SubTarget = 1u << 3u
};

using PickFeatures = std::uint32_t;

constexpr PickFeatures pickFeature(PickFeature feature) noexcept {
    return static_cast<PickFeatures>(feature);
}

enum class PickDetail : std::uint8_t {
    Object,
    Surface,
    Primitive
};

enum class PickStatus : std::uint8_t {
    Hit,
    Miss,
    Stale,
    Unsupported
};

struct RenderPickRequest {
    std::uint64_t requestId{0};
    // Renderer-neutral view coordinates: top-left origin, normalized to
    // [0, 1). Qt logical pixels, DPR and native render-target pixels never
    // cross this interface.
    QPointF normalizedPosition;
    PickDetail detail{PickDetail::Object};
};

struct PickTriangle {
    Vector3 v0;
    Vector3 v1;
    Vector3 v2;
};

struct RenderPickPayload {
    DBInstanceID objectId{INVALID_DB_ID};
    std::optional<DBInstanceID> partId;
    std::optional<std::uint32_t> subTarget;
    std::optional<Vector3> worldPosition;
    std::optional<Vector3> localPosition;
    std::optional<std::uint64_t> primitiveIndex;
    std::optional<std::uint64_t> vertexIndex;
    std::optional<PickTriangle> primitiveTriangle;
};

struct RenderPickResult {
    std::uint64_t requestId{0};
    DBInstanceID windowId{INVALID_DB_ID};
    PickStatus status{PickStatus::Miss};
    RenderPickPayload payload;
};

} // namespace GPlatform::Rendering

Q_DECLARE_METATYPE(GPlatform::Rendering::RenderPickRequest)
Q_DECLARE_METATYPE(GPlatform::Rendering::RenderPickPayload)
Q_DECLARE_METATYPE(GPlatform::Rendering::RenderPickResult)
