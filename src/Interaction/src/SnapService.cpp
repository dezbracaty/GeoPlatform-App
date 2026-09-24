#include "SnapService.hpp"

#include <AutoRegisterDB.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace GPlatform::Interaction {
    namespace {
        using Vec = Eigen::Vector3d;
        Vec vector(const Vector3& p) {
            return {p.x, p.y, p.z};
        }
        Vector3 point(const Vec& p) {
            return {static_cast<float>(p.x()), static_cast<float>(p.y()),
                    static_cast<float>(p.z())};
        }

        bool onConstraint(const Vec& p, const SnapConstraint& constraint, double tolerance) {
            if (constraint.kind == SnapConstraint::Kind::Free)
                return true;
            const Vec direction = vector(constraint.direction).normalized();
            const Vec offset = p - vector(constraint.origin);
            if (constraint.kind == SnapConstraint::Kind::Plane)
                return std::abs(offset.dot(direction)) <= tolerance;
            return (offset - direction * offset.dot(direction)).norm() <= tolerance;
        }

        // Solve in world space before scoring in screen space. Projecting an
        // unreachable target onto the constraint would falsely report a feature hit.
        std::optional<Vec> closestAdmissible(
            const SnapFeature& feature, const Transform::Matrix4& transform,
            const SnapQuery& query) {
            const auto world = [&transform](const Vector3& local) -> Vec {
                const Eigen::Vector4d transformed = transform.cast<double>() *
                                                    Eigen::Vector4d(local.x, local.y, local.z, 1.0);
                return transformed.head<3>();
            };
            const Vec a = world(feature.first);
            if (!a.allFinite())
                return std::nullopt;
            if (feature.kind == SnapFeatureKind::Point)
                return onConstraint(a, query.constraint, query.constraintTolerance)
                           ? std::optional<Vec>(a)
                           : std::nullopt;

            const Vec edge = world(feature.second) - a;
            if (!edge.allFinite())
                return std::nullopt;
            const double length = edge.norm();
            if (length <= 1.0e-12)
                return onConstraint(a, query.constraint, query.constraintTolerance)
                           ? std::optional<Vec>(a)
                           : std::nullopt;
            const Vec direction = edge / length;
            // Parameter in world units, including under nonuniform/mirrored transforms.
            double parameter = (vector(query.worldPosition) - a).dot(direction);
            bool fixedIntersection = false;
            if (query.constraint.kind != SnapConstraint::Kind::Free) {
                const Vec axis = vector(query.constraint.direction).normalized();
                const Vec offset = a - vector(query.constraint.origin);
                if (query.constraint.kind == SnapConstraint::Kind::Plane) {
                    const double denominator = direction.dot(axis);
                    if (std::abs(denominator) > 1.0e-12) {
                        parameter = -offset.dot(axis) / denominator;
                        fixedIntersection = true;
                    } else if (std::abs(offset.dot(axis)) > query.constraintTolerance) {
                        return std::nullopt;
                    }
                } else {
                    const Vec perpendicular = direction - axis * direction.dot(axis);
                    const Vec separation = offset - axis * offset.dot(axis);
                    if (perpendicular.squaredNorm() > 1.0e-24) {
                        parameter = -separation.dot(perpendicular) / perpendicular.squaredNorm();
                        fixedIntersection = true;
                    } else if (separation.norm() > query.constraintTolerance) {
                        return std::nullopt;
                    }
                }
            }
            if (feature.kind == SnapFeatureKind::Segment) {
                if (fixedIntersection &&
                    (parameter < -query.constraintTolerance ||
                     parameter > length + query.constraintTolerance))
                    return std::nullopt;
                parameter = std::clamp(parameter, 0.0, length);
            }
            const Vec result = a + direction * parameter;
            if (!result.allFinite() ||
                !onConstraint(result, query.constraint, query.constraintTolerance))
                return std::nullopt;
            return result;
        }

        bool inDepthRange(const Vector3& p) {
            return vector(p).allFinite() && p.z >= 0.0f && p.z <= 1.0f;
        }

        bool preferred(const SnapResult& candidate, const SnapResult& current) {
            if (std::abs(candidate.distance - current.distance) > 1.0e-9)
                return candidate.distance < current.distance;
            // Stable tie-breaks, independent of candidate enumeration order.
            return std::make_tuple(candidate.kind, candidate.instanceId.getValue(),
                                   candidate.sourceId.getValue(), candidate.featureId) <
                   std::make_tuple(current.kind, current.instanceId.getValue(),
                                   current.sourceId.getValue(), current.featureId);
        }

        using Vec4 = Eigen::Vector4d;
        Vec4 clip(const ViewportProjectionSnapshot& projection, const Vec& p) {
            const auto c = projection.clipFromWorld(point(p));
            return {c.x, c.y, c.z, c.w};
        }
        Vec world(const Transform::Matrix4& matrix, const Vector3& p) {
            return (matrix.cast<double>() * Vec4(p.x, p.y, p.z, 1)).head<3>();
        }
        QPointF screen(const Vec4& c, const Vector2& size) {
            return {(c.x() / c.w() + 1) * size.x * .5, (1 - c.y() / c.w()) * size.y * .5};
        }
        // Homogeneous clipping preserves the source parameter through perspective and
        // also gives a finite visible extent for infinite authored lines.
        bool clipped(Vec& a, Vec& b, bool infinite, const ViewportProjectionSnapshot& projection) {
            const Vec4 ca = clip(projection, a), delta = clip(projection, b) - ca;
            double lo = infinite ? -std::numeric_limits<double>::infinity() : 0;
            double hi = infinite ? std::numeric_limits<double>::infinity() : 1;
            const auto trim = [&](double value, double slope) {
                if (std::abs(slope) < 1e-14)
                    return value >= 0;
                const double t = -value / slope;
                if (slope > 0)
                    lo = std::max(lo, t);
                else
                    hi = std::min(hi, t);
                return lo <= hi;
            };
            for (int axis = 0; axis < 3; ++axis) {
                if (!trim(ca.w() + ca[axis], delta.w() + delta[axis]) ||
                    !trim(ca.w() - ca[axis], delta.w() - delta[axis]))
                    return false;
            }
            if (!trim(ca.w() - 1e-8, delta.w()) || !std::isfinite(lo) || !std::isfinite(hi))
                return false;
            const Vec d = b - a;
            b = a + d * hi;
            a = a + d * lo;
            return a.allFinite() && b.allFinite();
        }
        struct NodeBounds {
            bool near{false};
            double distance{0};
            double front{std::numeric_limits<double>::infinity()};
        };
        NodeBounds nodeNear(const SnapGeometry::Node& node, const SnapTarget& target,
                            const SnapPointerQuery& query, const Vec& forward) {
            NodeBounds bounds;
            const auto size = query.projection.logicalSize();
            double minX = 1e100, minY = 1e100, maxX = -1e100, maxY = -1e100;
            bool outside[6] = {true, true, true, true, true, true};
            bool crossesEye = false;
            for (int i = 0; i < 8; ++i) {
                const Vec corner = world(target.localToWorld,
                                       {i & 1 ? node.max.x : node.min.x, i & 2 ? node.max.y : node.min.y, i & 4 ? node.max.z : node.min.z});
                bounds.front = std::min(bounds.front, corner.dot(forward));
                const Vec4 c = clip(query.projection, corner);
                if (!c.allFinite())
                    return {};
                for (int axis = 0; axis < 3; ++axis) {
                    outside[axis * 2] &= c.w() + c[axis] < 0;
                    outside[axis * 2 + 1] &= c.w() - c[axis] < 0;
                }
                if (c.w() <= 1e-8) {
                    crossesEye = true;
                    continue;
                }
                const auto p = screen(c, size);
                minX = std::min(minX, p.x());
                maxX = std::max(maxX, p.x());
                minY = std::min(minY, p.y());
                maxY = std::max(maxY, p.y());
            }
            // X/Y tolerance is handled by the expanded screen rectangle below.
            if (outside[4] || outside[5])
                return {};
            bounds.distance = crossesEye ? 0 : std::hypot(std::max({minX - query.position.x(), query.position.x() - maxX, 0.0}), std::max({minY - query.position.y(), query.position.y() - maxY, 0.0}));
            bounds.near = bounds.distance <= query.releasePixels;
            return bounds;
        }
    } // namespace

    std::optional<SnapResult> SnapService::query(
        const SnapQuery& query, const std::vector<SnapTarget>& targets,
        const std::optional<SnapResult>& previous) const {
        if (!vector(query.worldPosition).allFinite() ||
            !std::isfinite(query.acquireDistance) || query.acquireDistance < 0 ||
            !std::isfinite(query.releaseDistance) ||
            query.releaseDistance < query.acquireDistance ||
            !std::isfinite(query.constraintTolerance) || query.constraintTolerance < 0)
            return std::nullopt;
        if (query.constraint.kind != SnapConstraint::Kind::Free &&
            (!vector(query.constraint.origin).allFinite() ||
             !vector(query.constraint.direction).allFinite() ||
             vector(query.constraint.direction).norm() <= 1.0e-12))
            return std::nullopt;

        Vector3 screenPosition;
        if (query.distance == SnapQuery::Distance::Screen) {
            if (!query.projection.isValid())
                return std::nullopt;
            screenPosition = query.projection.screenFromWorld(query.worldPosition);
            if (!inDepthRange(screenPosition))
                return std::nullopt;
        }

        std::optional<SnapResult> best;
        std::optional<SnapResult> retained;
        for (const auto& target : targets) {
            if (!target.db || !target.localToWorld.allFinite() ||
                !target.localToWorld.row(3).isApprox(Eigen::RowVector4f(0, 0, 0, 1)))
                continue;
            const auto geometry = target.db->snapGeometry();
            if (!geometry)
                continue;
            for (std::size_t i = 0; i < geometry->features.size(); ++i) {
                const auto& feature = geometry->features[i];
                if (!feature.snappable || (target.eligible && !target.eligible(i, feature)))
                    continue;
                if (!(query.featureMask & snapFeatureMask(feature.kind)))
                    continue;
                const auto position = closestAdmissible(feature, target.localToWorld, query);
                if (!position)
                    continue;
                SnapResult candidate;
                candidate.instanceId = target.instanceId.isValid()
                                           ? target.instanceId
                                           : target.db->getDBInstanceID();
                candidate.sourceId = target.db->getDBInstanceID();
                candidate.featureId = feature.id;
                candidate.kind = feature.kind;
                candidate.geometry = geometry;
                candidate.featureIndex = i;
                candidate.worldPosition = point(*position);
                if (!vector(candidate.worldPosition).allFinite())
                    continue;
                if (query.distance == SnapQuery::Distance::Screen) {
                    const auto projected = query.projection.screenFromWorld(candidate.worldPosition);
                    if (!inDepthRange(projected))
                        continue;
                    candidate.distance = std::hypot(
                        static_cast<double>(projected.x) - screenPosition.x,
                        static_cast<double>(projected.y) - screenPosition.y);
                } else {
                    candidate.distance = (*position - vector(query.worldPosition)).norm();
                }
                if (!std::isfinite(candidate.distance))
                    continue;
                const bool sameFeature = previous && previous->geometry == geometry &&
                                         previous->instanceId == candidate.instanceId &&
                                         previous->sourceId == candidate.sourceId &&
                                         previous->featureId == candidate.featureId &&
                                         previous->kind == candidate.kind;
                if (sameFeature && candidate.distance <= query.releaseDistance &&
                    (!retained || preferred(candidate, *retained)))
                    retained = candidate;
                if (candidate.distance <= query.acquireDistance &&
                    (!best || preferred(candidate, *best)))
                    best = std::move(candidate);
            }
        }
        return retained ? retained : best;
    }

    std::optional<SnapResult> SnapService::query(const SnapPointerQuery& query,
                                          const std::vector<SnapTarget>& targets, const std::optional<SnapResult>& previous) const {
        if (!query.projection.isValid() || !std::isfinite(query.position.x()) || !std::isfinite(query.position.y()) ||
            !std::isfinite(query.acquirePixels) || query.acquirePixels < 0 ||
            !std::isfinite(query.releasePixels) || query.releasePixels < query.acquirePixels ||
            !std::isfinite(query.pointAcquirePixels) || query.pointAcquirePixels < 0 ||
            (query.preferPoints && query.pointAcquirePixels > query.acquirePixels))
            return {};
        const auto size = query.projection.logicalSize();
        if (query.position.x() < 0 || query.position.y() < 0 || query.position.x() > size.x || query.position.y() > size.y)
            return {};
        const QPointF center(size.x * .5, size.y * .5);
        const Vec forward = vector(query.projection.rayFromScreen(center).direction).normalized();
        const Vec right = (vector(query.projection.worldFromScreen(center + QPointF(1, 0), .5f)) -
                         vector(query.projection.worldFromScreen(center, .5f)))
                            .normalized();
        if (!forward.allFinite() || !right.allFinite())
            return {};
        std::optional<SnapResult> best, retained;
        std::vector<SnapResult> nearbyPoints;
        bool bestCovered = false, retainedCovered = false;
        for (const auto& target : targets) {
            if (!target.db || !target.localToWorld.allFinite() ||
                !target.localToWorld.row(3).isApprox(Transform::Matrix4::Identity().row(3)))
                continue;
            const auto geometry = target.db->snapGeometry();
            if (!geometry)
                continue;
            const double worldScale = target.localToWorld.block<3, 3>(0, 0).colwise().norm().maxCoeff();
            const auto inspect = [&](std::size_t index) {
                const auto& f = geometry->features[index];
                if (!(query.forPicking ? f.pickable : f.snappable) || (target.eligible && !target.eligible(index, f)))
                    return;
                Vec a = world(target.localToWorld, f.first), b = world(target.localToWorld, f.second);
                if (!a.allFinite() || !b.allFinite())
                    return;
                if (f.kind == SnapFeatureKind::Point || (b - a).squaredNorm() < 1e-20) {
                    b = a;
                    const Vec4 c = clip(query.projection, a);
                    if (c.w() <= 1e-8 || std::abs(c.z()) > c.w())
                        return;
                } else if (!clipped(a, b, f.kind == SnapFeatureKind::Line, query.projection))
                    return;
                const Vec4 ca = clip(query.projection, a), cb = clip(query.projection, b);
                const auto sa = screen(ca, size), sb = screen(cb, size), d = sb - sa;
                const double length = d.x() * d.x() + d.y() * d.y();
                const auto offset = query.position - sa;
                const double u = length > 1e-12 ? std::clamp((offset.x() * d.x() + offset.y() * d.y()) / length, 0.0, 1.0) : 0;
                // Perspective-correct world interpolation, not linear screen interpolation.
                const double t = (u / cb.w()) / ((1 - u) / ca.w() + u / cb.w());
                const Vec p = a + (b - a) * t;
                const auto nearest = sa + d * u;
                SnapResult hit;
                hit.instanceId = target.instanceId.isValid() ? target.instanceId : target.db->getDBInstanceID();
                hit.sourceId = target.db->getDBInstanceID();
                hit.featureId = f.id;
                hit.kind = f.kind;
                hit.featureIndex = index;
                hit.geometry = geometry;
                hit.worldPosition = point(p);
                hit.displayStart = point(a);
                hit.displayEnd = point(b);
                hit.depth = ((1 - u) * ca.z() / ca.w() + u * cb.z() / cb.w() + 1) * .5;
                hit.distance = std::hypot(query.position.x() - nearest.x(), query.position.y() - nearest.y());
                hit.viewDistance = p.dot(forward);
                const auto edge = screen(clip(query.projection, p + right * (f.radiusMm * worldScale)), size);
                const double radius = std::hypot(edge.x() - nearest.x(), edge.y() - nearest.y());
                const bool covered = hit.distance <= std::max(1.5, radius);
                const bool same = previous && previous->geometry == geometry && previous->featureId == hit.featureId &&
                                  previous->sourceId == hit.sourceId && previous->instanceId == hit.instanceId;
                if (same && hit.distance <= query.releasePixels + radius) {
                    retained = hit;
                    retainedCovered = covered;
                }
                if (query.preferPoints && hit.kind == SnapFeatureKind::Point &&
                    hit.distance <= query.pointAcquirePixels + (same ? query.releasePixels-query.acquirePixels : 0))
                    nearbyPoints.push_back(hit);
                if (hit.distance > query.acquirePixels + radius)
                    return;
                bool better = !best;
                if (best) {
                    // Directly covered strokes take precedence over nearby targets.
                    // Linear view distance avoids far-plane precision loss between layers.
                    const bool overlapping = query.preferFrontmost && covered && bestCovered;
                    if (query.preferFrontmost && covered != bestCovered)
                        better = covered;
                    else if (overlapping && std::abs(hit.viewDistance - best->viewDistance) > 1e-5)
                        better = hit.viewDistance < best->viewDistance;
                    else if (std::abs(hit.distance - best->distance) > 1e-6)
                        better = hit.distance < best->distance;
                    else if (hit.kind != best->kind)
                        better = hit.kind < best->kind;
                    else
                        better = std::make_tuple(hit.instanceId.getValue(), hit.sourceId.getValue(), hit.featureId) <
                                 std::make_tuple(best->instanceId.getValue(), best->sourceId.getValue(), best->featureId);
                }
                if (better) {
                    best = hit;
                    bestCovered = covered;
                }
            };
            if (!geometry->nodes.empty()) {
                struct Visit {
                    std::uint32_t id;
                    NodeBounds bounds;
                };
                std::vector<Visit> stack{
                    {0, nodeNear(geometry->nodes[0], target, query, forward)}
                };
                while (!stack.empty()) {
                    const auto visit = stack.back();
                    stack.pop_back();
                    const double pointRadius = query.pointAcquirePixels + (previous ? query.releasePixels-query.acquirePixels : 0);
                    if (!visit.bounds.near || (query.preferFrontmost && bestCovered &&
                                               (visit.bounds.front > best->viewDistance + 1e-5 ||
                                                visit.bounds.distance > (query.preferPoints ? std::max(1.5, pointRadius) : 1.5))))
                        continue;
                    const auto& n = geometry->nodes[visit.id];
                    if (n.count) {
                        for (auto i = n.begin; i < n.begin + n.count; ++i)
                            inspect(geometry->indices[i]);
                    } else {
                        Visit left{n.left, nodeNear(geometry->nodes[n.left], target, query, forward)};
                        Visit rightVisit{n.right, nodeNear(geometry->nodes[n.right], target, query, forward)};
                        // Visit the front child first, so covered nodes can be pruned.
                        if (left.bounds.front < rightVisit.bounds.front)
                            std::swap(left, rightVisit);
                        stack.push_back(left);
                        stack.push_back(rightVisit);
                    }
                }
            } else if (geometry->indices.empty() && geometry->infiniteLines.empty()) {
                for (std::size_t i = 0; i < geometry->features.size(); ++i)
                    inspect(i);
            }
            for (auto i : geometry->infiniteLines)
                inspect(i);
            // The endpoint of a sloped path may lie behind the nearest path
            // point. Inspect its authored group explicitly after depth pruning.
            const auto instance = target.instanceId.isValid() ? target.instanceId : target.db->getDBInstanceID();
            if (query.preferPoints && best && best->geometry == geometry && best->instanceId == instance) {
                const auto feature = geometry->features[best->featureIndex];
                for (std::size_t i = feature.groupBegin; i < std::size_t(feature.groupBegin) + feature.groupCount; ++i)
                    if (geometry->features[i].kind == SnapFeatureKind::Point) inspect(i);
            }
        }
        const auto samePath = [](const SnapResult& a, const SnapResult& b) {
            const auto& fa = a.geometry->features[a.featureIndex];
            const auto& fb = b.geometry->features[b.featureIndex];
            return a.geometry == b.geometry && a.sourceId == b.sourceId && a.instanceId == b.instanceId &&
                fa.groupCount && fa.groupBegin == fb.groupBegin && fa.groupCount == fb.groupCount;
        };
        std::optional<SnapResult> bestPoint;
        for (const auto& point : nearbyPoints) {
            if (best && query.preferFrontmost && bestCovered && !samePath(point, *best) &&
                point.viewDistance > best->viewDistance + 1e-5)
                continue;
            if (!bestPoint || std::make_tuple(point.distance, point.viewDistance, point.instanceId.getValue(), point.featureId) <
                              std::make_tuple(bestPoint->distance, bestPoint->viewDistance, bestPoint->instanceId.getValue(), bestPoint->featureId))
                bestPoint = point;
        }
        if (bestPoint) return bestPoint;
        // On a logical path follow the nearest member, not an old speed subdivision.
        // Point priority has its own small capture radius and must release outside it.
        if (query.preferPoints && retained &&
            (retained->kind == SnapFeatureKind::Point || (best && samePath(*retained, *best))))
            return best;
        // An old lock may not select a covered stroke over a newly visible front stroke.
        if (retained && best && query.preferFrontmost && bestCovered && (!retainedCovered || best->viewDistance + 1e-5 < retained->viewDistance))
            return best;
        return retained ? retained : best;
    }
} // namespace GPlatform::Interaction
