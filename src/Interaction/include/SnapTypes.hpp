#pragma once

#include <BaseID.hpp>
#include <SnapGeometry.hpp>
#include <Transform.hpp>
#include <ViewportCoordinateSystem.hpp>

#include <optional>
#include <functional>

class AutoRegisterDB;

namespace GPlatform::Interaction {

    struct SnapTarget {
        std::shared_ptr<const AutoRegisterDB> db;
        // Disambiguates one definition used by several scene instances. If empty,
        // the DB's own ID is used. The caller supplies the full parent transform.
        DBInstanceID instanceId{};
        Transform::Matrix4 localToWorld{Transform::Matrix4::Identity()};
        std::function<bool(std::size_t, const SnapFeature&)> eligible;
    };

    struct SnapConstraint {
        enum class Kind { Free,
                          Axis,
                          Plane };
        Kind kind{Kind::Free};
        Vector3 origin{0, 0, 0};
        // Axis direction or plane normal; must be nonzero for a constraint.
        Vector3 direction{0, 0, 1};
    };

    struct SnapQuery {
        enum class Distance { Screen,
                              World };
        Vector3 worldPosition{0, 0, 0};
        SnapConstraint constraint;
        unsigned featureMask{snapFeatureMask(SnapFeatureKind::Point) |
                             snapFeatureMask(SnapFeatureKind::Segment) |
                             snapFeatureMask(SnapFeatureKind::Line)};
        Distance distance{Distance::Screen};
        ViewportProjectionSnapshot projection;
        // Logical pixels for Screen, world units for World. World callers must
        // explicitly choose tolerances appropriate to their operation.
        double acquireDistance{8.0};
        double releaseDistance{12.0};
        double constraintTolerance{1.0e-5};
    };

    struct SnapResult {
        DBInstanceID instanceId;
        DBInstanceID sourceId;
        std::uint64_t featureId{0};
        SnapFeatureKind kind{SnapFeatureKind::Point};
        Vector3 worldPosition{0, 0, 0};
        double distance{0};
        // Keeps the published data alive and identifies the version used by this
        // hit. Replaced snapshots cannot retain an old hysteresis lock.
        SnapGeometryPtr geometry;
        std::size_t featureIndex{0};
        Vector3 displayStart, displayEnd;
        double depth{0};
        double viewDistance{0};
    };

    struct SnapPointerQuery {
        ViewportProjectionSnapshot projection;
        QPointF position;
        double acquirePixels{8}, releasePixels{12};
        bool forPicking{false};
        // Visible overlapping strokes prefer the frontmost feature.
        bool preferFrontmost{true};
        // Optional operation policy: prefer authored points near a path endpoint.
        bool preferPoints{false};
        double pointAcquirePixels{6};
    };

} // namespace GPlatform::Interaction
