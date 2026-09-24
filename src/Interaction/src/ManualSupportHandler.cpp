#include "ManualSupportHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    ManualSupportHandler, "support.manual.enter", "support.manual.leave")
#include "ActionHandlerRegistry.hpp"
#include "ActionManager.hpp"
#include "InteractionRuntime.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include "SlicingHandlerBridge.hpp"
#include "SlicingPreviewBridge.hpp"
#include "SliceSettingsBridge.hpp"
#include "DebugActorDB.hpp"
#include "ManualSupportDB.hpp"
#include "ManualSupportRelations.hpp"
#include "SurfaceBrushProjection.hpp"
#include "OrcaIndexedMeshBuilder.hpp"
#include "TransactionManager.hpp"
#include "Foundation/Log.h"
#include "TaskStateNotifier.hpp"

#include <CameraDB.hpp>
#include <CameraNavigationController.hpp>
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelPartDB.hpp>
#include <PrintBedDB.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <WindowDB.hpp>
#include <WindowDBManager.hpp>
#include <QDataStream>
#include <QMouseEvent>
#include <QPointer>
#include <QByteArray>
#include <QtConcurrent/QtConcurrent>
#include <cmath>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkLine.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkTriangle.h>
#include <algorithm>

namespace {

PickResult empty_pick_result() {
    PickResult result;
    return result;
}

ViewportProjectionSnapshot viewport_projection(DBInstanceID viewId) {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    auto window = std::dynamic_pointer_cast<WindowDB>(
        document->getDBInstance(viewId));
    if (!window) return {};
    window->updateActiveCamera();
    const auto coordinates = window->getCoordinateSystem();
    return coordinates ? coordinates->projectionSnapshot() : ViewportProjectionSnapshot{};
}

constexpr int kDebugRingSegmentCount = 72;
constexpr int kUnsetOverhangThresholdDeg = std::numeric_limits<int>::max();
constexpr double kTriangulationEpsilon = 1e-8;
constexpr float kContactOverlayLiftMm = 0.08f;
constexpr float kLowerModelHitEpsilonMm = 0.05f;
constexpr quint32 kManualSupportStateMagic = 0x4D535354u; // "MSST"
constexpr quint16 kManualSupportStateVersion = 1u;

int resolve_manual_support_overhang_threshold_deg() {
    auto* bridge = SliceSettingsBridge::instance();
    if (!bridge) {
        LOG_WARN("ManualSupportHandler: SliceSettingsBridge unavailable, support_angle unresolved");
        return kUnsetOverhangThresholdDeg;
    }

    bool ok = false;
    const int thresholdDeg =
        bridge->getSetting(QStringLiteral("support_angle")).toInt(&ok);
    if (!ok) {
        LOG_WARN("ManualSupportHandler: failed to resolve support_angle from SliceSettingsBridge");
        return kUnsetOverhangThresholdDeg;
    }

    return std::clamp(thresholdDeg, 0, 90);
}

void set_manual_support_print_beds_visible(bool visible) {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("ManualSupportHandler: DocumentManager unavailable, cannot update PrintBedDB visibility");
        return;
    }

    int updatedCount = 0;
    const auto printBeds = docManager->getDBInstancesByType(TypeID::PRINT_BED_DB);
    for (const auto& instance : printBeds) {
        auto printBed = std::dynamic_pointer_cast<PrintBedDB>(instance);
        if (!printBed) {
            continue;
        }

        printBed->setVisible(visible);
        ++updatedCount;
    }

    LOG_INFO("ManualSupportHandler: set {} PrintBedDB instance(s) visible={}",
             updatedCount,
             visible ? "true" : "false");
}

bool selection_state_equals(const ManualSupportOrcaScaffold::TriangleSplittingData& lhs,
                            const ManualSupportOrcaScaffold::TriangleSplittingData& rhs) {
    // Manual-support persistence remains on the legacy compact bitstream.
    // leafLabels is the generic, unbounded model-color channel and must not
    // change equality semantics for old support snapshots.
    if (lhs.bitstream != rhs.bitstream || lhs.usedStates != rhs.usedStates) {
        return false;
    }
    if (lhs.trianglesToSplit.size() != rhs.trianglesToSplit.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.trianglesToSplit.size(); ++i) {
        const auto& left = lhs.trianglesToSplit[i];
        const auto& right = rhs.trianglesToSplit[i];
        if (left.triangleIdx != right.triangleIdx ||
            left.bitstreamStartIdx != right.bitstreamStartIdx) {
            return false;
        }
    }
    return true;
}

vtkSmartPointer<vtkPolyData> create_empty_polydata() {
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(vtkSmartPointer<vtkPoints>::New());
    return polyData;
}

std::shared_ptr<const IndexedTriangleMesh> indexed_triangle_mesh_from_poly_data(
    vtkPolyData* polyData) {
    if (!polyData || polyData->GetNumberOfPoints() <= 0 ||
        polyData->GetNumberOfPolys() <= 0 ||
        static_cast<unsigned long long>(polyData->GetNumberOfPoints()) >
            std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    auto mesh = std::make_shared<IndexedTriangleMesh>();
    mesh->positions.reserve(static_cast<std::size_t>(polyData->GetNumberOfPoints()));
    for (vtkIdType pointId = 0; pointId < polyData->GetNumberOfPoints(); ++pointId) {
        double point[3]{};
        polyData->GetPoint(pointId, point);
        mesh->positions.emplace_back(
            static_cast<float>(point[0]),
            static_cast<float>(point[1]),
            static_cast<float>(point[2]));
    }

    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 2 ||
            cell->GetNumberOfPoints() < 3) {
            continue;
        }
        const auto first = static_cast<std::uint32_t>(cell->GetPointId(0));
        for (vtkIdType corner = 1; corner + 1 < cell->GetNumberOfPoints(); ++corner) {
            const auto second = static_cast<std::uint32_t>(cell->GetPointId(corner));
            const auto third = static_cast<std::uint32_t>(cell->GetPointId(corner + 1));
            mesh->indices.insert(mesh->indices.end(), {first, second, third});
        }
    }
    mesh->rebuildVertexNormals();
    return mesh->hasValidTopology() ? mesh : nullptr;
}

void configure_debug_actor_line_material(const std::shared_ptr<DebugActorDB>& actor,
                                         const Vector3& color,
                                         float opacity,
                                         float lineWidth) {
    if (!actor) {
        return;
    }
    actor->setOpacity(opacity);
    if (auto material = actor->getMaterial()) {
        material->setDiffuseColor(color);
        material->setAmbientColor(color);
        material->setEdgeColor(color);
        material->setLineWidth(lineWidth);
        material->setEdgeVisibility(true);
        material->setLighting(false);
        material->setOpacity(opacity);
    }
}

void configure_debug_actor_surface_material(const std::shared_ptr<DebugActorDB>& actor,
                                            const Vector3& color,
                                            float opacity) {
    if (!actor) {
        return;
    }
    actor->setOpacity(opacity);
    if (auto material = actor->getMaterial()) {
        material->setDiffuseColor(color);
        material->setAmbientColor(color);
        material->setAmbient(0.35f);
        material->setDiffuse(1.0f);
        material->setSpecular(0.18f);
        material->setSpecularPower(18.0f);
        material->setInterpolation(true);
        material->setRepresentation(2);
        material->setEdgeVisibility(false);
        material->setLighting(true);
        material->setOpacity(opacity);
    }
}

Vector3 transform_point_with_matrix(const Vector3& point, const Transform::Matrix4& matrix) {
    Eigen::Vector4f homogeneous(point.x, point.y, point.z, 1.0f);
    const Eigen::Vector4f transformed = matrix * homogeneous;
    return Vector3(transformed.x(), transformed.y(), transformed.z());
}

QVector3D safe_normalized(const QVector3D& v, const QVector3D& fallback) {
    const float lenSqr = v.lengthSquared();
    if (lenSqr <= 1e-12f) {
        return fallback;
    }
    return v / std::sqrt(lenSqr);
}

bool resolve_pick_for_target_model(const DBInstanceID& targetModelId,
                                   const DBInstanceID& viewId,
                                   const QPoint& requestedScreenPos,
                                   PickResult* pickResult) {
    if (!pickResult) {
        return false;
    }

    const auto strokePick = interactionPickService().cachedForView(
        viewId,
        PickChannel::ToolStroke,
        GPlatform::Rendering::PickDetail::Primitive,
        requestedScreenPos);
    const auto previewPick = strokePick
        ? std::optional<PickSnapshot>{}
        : interactionPickService().cachedForView(
              viewId,
              PickChannel::ToolPreview,
              GPlatform::Rendering::PickDetail::Primitive,
              requestedScreenPos);
    const auto& snapshot = strokePick ? strokePick : previewPick;
    if (!snapshot || snapshot->status != GPlatform::Rendering::PickStatus::Hit) {
        return false;
    }
    const PickResult& pick = snapshot->result;
    if (!pick.objectId.isValid() || !pick.primitiveIndex ||
        !pick.worldPosition) {
        return false;
    }
    if (targetModelId.isValid() && pick.objectId != targetModelId) {
        return false;
    }

    *pickResult = pick;
    return true;
}

double signed_area_xy(const std::vector<int>& loopIndices,
                      const std::vector<QVector3D>& vertices) {
    if (loopIndices.size() < 3) {
        return 0.0;
    }

    double twiceArea = 0.0;
    for (std::size_t i = 0; i < loopIndices.size(); ++i) {
        const QVector3D& current = vertices[static_cast<std::size_t>(loopIndices[i])];
        const QVector3D& next =
            vertices[static_cast<std::size_t>(loopIndices[(i + 1) % loopIndices.size()])];
        twiceArea +=
            static_cast<double>(current.x()) * static_cast<double>(next.y()) -
            static_cast<double>(next.x()) * static_cast<double>(current.y());
    }

    return twiceArea * 0.5;
}

double cross_2d(const QVector3D& a, const QVector3D& b, const QVector3D& c) {
    const double abx = static_cast<double>(b.x()) - static_cast<double>(a.x());
    const double aby = static_cast<double>(b.y()) - static_cast<double>(a.y());
    const double acx = static_cast<double>(c.x()) - static_cast<double>(a.x());
    const double acy = static_cast<double>(c.y()) - static_cast<double>(a.y());
    return abx * acy - aby * acx;
}

bool point_in_triangle_xy(const QVector3D& point,
                          const QVector3D& a,
                          const QVector3D& b,
                          const QVector3D& c);

double point_segment_distance_sq_2d(const QPointF& point,
                                    const QPointF& a,
                                    const QPointF& b);

double signed_area_xy(const std::vector<QPointF>& loopPoints) {
    if (loopPoints.size() < 3) {
        return 0.0;
    }

    double twiceArea = 0.0;
    for (std::size_t i = 0; i < loopPoints.size(); ++i) {
        const QPointF& current = loopPoints[i];
        const QPointF& next = loopPoints[(i + 1) % loopPoints.size()];
        twiceArea += current.x() * next.y() - next.x() * current.y();
    }

    return twiceArea * 0.5;
}

bool point_in_polygon_xy(const QPointF& point,
                         const std::vector<QPointF>& polygon) {
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const QPointF& pi = polygon[i];
        const QPointF& pj = polygon[j];
        const double dy = pj.y() - pi.y();
        if (((pi.y() > point.y()) == (pj.y() > point.y())) || std::abs(dy) <= 1e-12) {
            continue;
        }

        const bool intersects =
            point.x() < (pj.x() - pi.x()) * (point.y() - pi.y()) / dy + pi.x();
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_strictly_inside_triangle_2d(const QPointF& point,
                                       const QPointF& a,
                                       const QPointF& b,
                                       const QPointF& c,
                                       double minDistanceSqToEdge) {
    const QVector3D qp(point.x(), point.y(), 0.0f);
    const QVector3D qa(a.x(), a.y(), 0.0f);
    const QVector3D qb(b.x(), b.y(), 0.0f);
    const QVector3D qc(c.x(), c.y(), 0.0f);
    if (!point_in_triangle_xy(qp, qa, qb, qc)) {
        return false;
    }

    const double d0 = point_segment_distance_sq_2d(point, a, b);
    const double d1 = point_segment_distance_sq_2d(point, b, c);
    const double d2 = point_segment_distance_sq_2d(point, c, a);
    return d0 > minDistanceSqToEdge &&
           d1 > minDistanceSqToEdge &&
           d2 > minDistanceSqToEdge;
}

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

float resolve_model_stop_z_at_xy(const QPointF& point,
                                 float shaftTopZ,
                                 float groundZ,
                                 const std::vector<QVector3D>& vertices,
                                 const std::vector<std::array<int, 3>>& triangles) {
    double bestZ = static_cast<double>(groundZ);
    const double maxZ = static_cast<double>(shaftTopZ) - 1e-4;

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
        if (!interpolate_triangle_z_at_xy(point, a, b, c, &triangleZ)) {
            continue;
        }
        if (triangleZ > bestZ && triangleZ < maxZ) {
            bestZ = triangleZ;
        }
    }

    return static_cast<float>(bestZ);
}

std::vector<QPointF> sanitize_loop_points_2d(const std::vector<QPointF>& input) {
    std::vector<QPointF> sanitized;
    sanitized.reserve(input.size());

    for (const QPointF& point : input) {
        if (!sanitized.empty()) {
            const QPointF& previous = sanitized.back();
            const double dx = point.x() - previous.x();
            const double dy = point.y() - previous.y();
            if (dx * dx + dy * dy <= 1e-12) {
                continue;
            }
        }
        sanitized.push_back(point);
    }

    if (sanitized.size() >= 2) {
        const QPointF& first = sanitized.front();
        const QPointF& last = sanitized.back();
        const double dx = first.x() - last.x();
        const double dy = first.y() - last.y();
        if (dx * dx + dy * dy <= 1e-12) {
            sanitized.pop_back();
        }
    }

    return sanitized;
}

std::vector<QPointF> build_loop_xy_points(const std::vector<int>& loopIndices,
                                          const std::vector<QVector3D>& vertices) {
    std::vector<QPointF> loopPoints;
    loopPoints.reserve(loopIndices.size());
    for (const int vertexIdx : loopIndices) {
        if (vertexIdx < 0 || vertexIdx >= static_cast<int>(vertices.size())) {
            continue;
        }
        const QVector3D& vertex = vertices[static_cast<std::size_t>(vertexIdx)];
        loopPoints.push_back(QPointF(vertex.x(), vertex.y()));
    }
    return sanitize_loop_points_2d(loopPoints);
}

double point_segment_distance_sq_2d(const QPointF& point,
                                    const QPointF& a,
                                    const QPointF& b) {
    const double abx = b.x() - a.x();
    const double aby = b.y() - a.y();
    const double apx = point.x() - a.x();
    const double apy = point.y() - a.y();
    const double abLenSq = abx * abx + aby * aby;
    if (abLenSq <= 1e-12) {
        return apx * apx + apy * apy;
    }

    const double t = std::clamp((apx * abx + apy * aby) / abLenSq, 0.0, 1.0);
    const double dx = point.x() - (a.x() + t * abx);
    const double dy = point.y() - (a.y() + t * aby);
    return dx * dx + dy * dy;
}

std::vector<QPointF> reduce_loop_vertex_count(std::vector<QPointF> loopPoints,
                                              int targetVertices,
                                              double initialTolerance,
                                              double maxTolerance) {
    loopPoints = sanitize_loop_points_2d(loopPoints);
    if (loopPoints.size() <= static_cast<std::size_t>(targetVertices) || targetVertices < 3) {
        return loopPoints;
    }

    double tolerance = std::max(initialTolerance, 1e-6);
    std::vector<QPointF> bestLoop = loopPoints;

    while (true) {
        std::vector<QPointF> current = bestLoop;
        bool removedAny = true;
        while (removedAny && current.size() > static_cast<std::size_t>(targetVertices)) {
            removedAny = false;
            double bestDistanceSq = std::numeric_limits<double>::max();
            std::size_t bestIndex = current.size();

            for (std::size_t i = 0; i < current.size(); ++i) {
                const QPointF& prev = current[(i + current.size() - 1) % current.size()];
                const QPointF& curr = current[i];
                const QPointF& next = current[(i + 1) % current.size()];
                const double distanceSq = point_segment_distance_sq_2d(curr, prev, next);
                if (distanceSq <= tolerance * tolerance && distanceSq < bestDistanceSq) {
                    bestDistanceSq = distanceSq;
                    bestIndex = i;
                }
            }

            if (bestIndex < current.size()) {
                current.erase(current.begin() + static_cast<long>(bestIndex));
                removedAny = true;
            }
        }

        if (current.size() < bestLoop.size()) {
            bestLoop = current;
        }
        if (bestLoop.size() <= static_cast<std::size_t>(targetVertices)) {
            break;
        }
        if (tolerance >= maxTolerance - 1e-9) {
            break;
        }
        tolerance = std::min(maxTolerance, tolerance * 1.6);
    }

    if (signed_area_xy(bestLoop) < 0.0) {
        std::reverse(bestLoop.begin(), bestLoop.end());
    }
    return bestLoop;
}

int regularized_loop_target_vertices(const QString& brushShape,
                                     int exactVertexCount) {
    const QString normalized = brushShape.trimmed().toLower();
    if (normalized == QStringLiteral("square") ||
        normalized == QStringLiteral("rect") ||
        normalized == QStringLiteral("box")) {
        return 4;
    }
    if (normalized == QStringLiteral("triangle") ||
        normalized == QStringLiteral("tri")) {
        return 3;
    }
    if (normalized == QStringLiteral("circle") ||
        normalized == QStringLiteral("sphere")) {
        return std::min(std::max(12, exactVertexCount / 12), 24);
    }
    return std::min(std::max(8, exactVertexCount / 8), 24);
}

std::vector<double> compute_loop_fractions_xy(const std::vector<int>& loopIndices,
                                              const std::vector<QVector3D>& vertices) {
    std::vector<QPointF> loopPoints = build_loop_xy_points(loopIndices, vertices);
    std::vector<double> fractions;
    if (loopPoints.size() < 2) {
        return fractions;
    }

    std::vector<double> cumulative;
    cumulative.reserve(loopPoints.size());
    cumulative.push_back(0.0);
    double totalLength = 0.0;
    for (std::size_t i = 1; i < loopPoints.size(); ++i) {
        const double dx = loopPoints[i].x() - loopPoints[i - 1].x();
        const double dy = loopPoints[i].y() - loopPoints[i - 1].y();
        totalLength += std::sqrt(dx * dx + dy * dy);
        cumulative.push_back(totalLength);
    }
    {
        const double dx = loopPoints.front().x() - loopPoints.back().x();
        const double dy = loopPoints.front().y() - loopPoints.back().y();
        totalLength += std::sqrt(dx * dx + dy * dy);
    }

    if (totalLength <= 1e-9) {
        fractions.assign(loopPoints.size(), 0.0);
        return fractions;
    }

    fractions.reserve(cumulative.size());
    for (double value : cumulative) {
        fractions.push_back(value / totalLength);
    }
    return fractions;
}

std::vector<double> compute_loop_fractions_2d(const std::vector<QPointF>& rawLoopPoints) {
    const std::vector<QPointF> loopPoints = sanitize_loop_points_2d(rawLoopPoints);
    std::vector<double> fractions;
    if (loopPoints.size() < 2) {
        return fractions;
    }

    std::vector<double> cumulative;
    cumulative.reserve(loopPoints.size());
    cumulative.push_back(0.0);
    double totalLength = 0.0;
    for (std::size_t i = 1; i < loopPoints.size(); ++i) {
        const double dx = loopPoints[i].x() - loopPoints[i - 1].x();
        const double dy = loopPoints[i].y() - loopPoints[i - 1].y();
        totalLength += std::sqrt(dx * dx + dy * dy);
        cumulative.push_back(totalLength);
    }
    {
        const double dx = loopPoints.front().x() - loopPoints.back().x();
        const double dy = loopPoints.front().y() - loopPoints.back().y();
        totalLength += std::sqrt(dx * dx + dy * dy);
    }

    if (totalLength <= 1e-9) {
        fractions.assign(loopPoints.size(), 0.0);
        return fractions;
    }

    fractions.reserve(cumulative.size());
    for (double value : cumulative) {
        fractions.push_back(value / totalLength);
    }
    return fractions;
}

template <typename T>
void rotate_closed_loop(std::vector<T>& values, std::size_t offset) {
    if (values.empty()) {
        return;
    }
    offset %= values.size();
    std::rotate(values.begin(), values.begin() + static_cast<long>(offset), values.end());
}

int best_cyclic_shift_for_loop_match(const std::vector<QPointF>& referenceLoop,
                                     const std::vector<QPointF>& candidateLoop) {
    if (referenceLoop.size() != candidateLoop.size() || referenceLoop.empty()) {
        return 0;
    }

    const std::size_t pointCount = referenceLoop.size();
    double bestScore = std::numeric_limits<double>::max();
    int bestShift = 0;

    for (std::size_t shift = 0; shift < pointCount; ++shift) {
        double score = 0.0;
        for (std::size_t i = 0; i < pointCount; ++i) {
            const QPointF& ref = referenceLoop[i];
            const QPointF& candidate = candidateLoop[(i + shift) % pointCount];
            const double dx = ref.x() - candidate.x();
            const double dy = ref.y() - candidate.y();
            score += dx * dx + dy * dy;
        }

        if (score < bestScore) {
            bestScore = score;
            bestShift = static_cast<int>(shift);
        }
    }

    return bestShift;
}

std::vector<QPointF> sample_closed_loop_by_fractions(const std::vector<QPointF>& rawLoopPoints,
                                                     const std::vector<double>& fractions) {
    const std::vector<QPointF> loopPoints = sanitize_loop_points_2d(rawLoopPoints);
    std::vector<QPointF> samples;
    if (loopPoints.size() < 2 || fractions.empty()) {
        return samples;
    }

    std::vector<double> edgeLengths;
    edgeLengths.reserve(loopPoints.size());
    double totalLength = 0.0;
    for (std::size_t i = 0; i < loopPoints.size(); ++i) {
        const QPointF& a = loopPoints[i];
        const QPointF& b = loopPoints[(i + 1) % loopPoints.size()];
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double edgeLength = std::sqrt(dx * dx + dy * dy);
        edgeLengths.push_back(edgeLength);
        totalLength += edgeLength;
    }

    if (totalLength <= 1e-9) {
        samples.assign(fractions.size(), loopPoints.front());
        return samples;
    }

    std::vector<double> cumulative;
    cumulative.reserve(loopPoints.size() + 1);
    cumulative.push_back(0.0);
    for (double edgeLength : edgeLengths) {
        cumulative.push_back(cumulative.back() + edgeLength);
    }

    samples.reserve(fractions.size());
    for (double fraction : fractions) {
        const double targetLength = std::clamp(fraction, 0.0, 1.0) * totalLength;
        std::size_t segmentIdx = 0;
        while (segmentIdx + 1 < cumulative.size() && cumulative[segmentIdx + 1] < targetLength) {
            ++segmentIdx;
        }
        if (segmentIdx >= loopPoints.size()) {
            segmentIdx = loopPoints.size() - 1;
        }

        const QPointF& a = loopPoints[segmentIdx];
        const QPointF& b = loopPoints[(segmentIdx + 1) % loopPoints.size()];
        const double segmentStart = cumulative[segmentIdx];
        const double segmentLength = edgeLengths[segmentIdx];
        const double t = segmentLength <= 1e-9 ? 0.0 : (targetLength - segmentStart) / segmentLength;
        samples.push_back(QPointF(
            a.x() + (b.x() - a.x()) * t,
            a.y() + (b.y() - a.y()) * t));
    }

    return samples;
}

std::vector<QPointF> build_regularized_shaft_loop_2d(const std::vector<int>& loopIndices,
                                                     const std::vector<QVector3D>& vertices,
                                                     const QString& brushShape,
                                                     double brushSizeMm) {
    const std::vector<QPointF> exactLoopPoints = build_loop_xy_points(loopIndices, vertices);
    if (exactLoopPoints.size() < 3) {
        return exactLoopPoints;
    }

    const int targetVertices =
        regularized_loop_target_vertices(brushShape, static_cast<int>(exactLoopPoints.size()));
    const double initialTolerance = std::max(brushSizeMm * 0.05, 0.15);
    const double maxTolerance = std::max(brushSizeMm * 0.35, 1.0);
    return reduce_loop_vertex_count(exactLoopPoints, targetVertices, initialTolerance, maxTolerance);
}

bool point_in_triangle_xy(const QVector3D& point,
                          const QVector3D& a,
                          const QVector3D& b,
                          const QVector3D& c);

std::vector<std::array<int, 3>> triangulate_loop_xy_points(const std::vector<QPointF>& rawLoopPoints) {
    std::vector<QPointF> loopPoints = sanitize_loop_points_2d(rawLoopPoints);
    std::vector<std::array<int, 3>> triangles;
    if (loopPoints.size() < 3) {
        return triangles;
    }

    if (signed_area_xy(loopPoints) < 0.0) {
        std::reverse(loopPoints.begin(), loopPoints.end());
    }

    std::vector<int> polygon(loopPoints.size(), 0);
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        polygon[i] = static_cast<int>(i);
    }

    triangles.reserve(polygon.size() - 2);

    while (polygon.size() > 3) {
        bool earFound = false;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const int prevIdx = polygon[(i + polygon.size() - 1) % polygon.size()];
            const int currIdx = polygon[i];
            const int nextIdx = polygon[(i + 1) % polygon.size()];

            const QVector3D prev(loopPoints[static_cast<std::size_t>(prevIdx)].x(),
                                 loopPoints[static_cast<std::size_t>(prevIdx)].y(),
                                 0.0f);
            const QVector3D curr(loopPoints[static_cast<std::size_t>(currIdx)].x(),
                                 loopPoints[static_cast<std::size_t>(currIdx)].y(),
                                 0.0f);
            const QVector3D next(loopPoints[static_cast<std::size_t>(nextIdx)].x(),
                                 loopPoints[static_cast<std::size_t>(nextIdx)].y(),
                                 0.0f);

            if (cross_2d(prev, curr, next) <= kTriangulationEpsilon) {
                continue;
            }

            bool containsOtherVertex = false;
            for (const int candidateIdx : polygon) {
                if (candidateIdx == prevIdx || candidateIdx == currIdx || candidateIdx == nextIdx) {
                    continue;
                }
                const QVector3D candidate(loopPoints[static_cast<std::size_t>(candidateIdx)].x(),
                                          loopPoints[static_cast<std::size_t>(candidateIdx)].y(),
                                          0.0f);
                if (point_in_triangle_xy(candidate, prev, curr, next)) {
                    containsOtherVertex = true;
                    break;
                }
            }
            if (containsOtherVertex) {
                continue;
            }

            triangles.push_back({prevIdx, currIdx, nextIdx});
            polygon.erase(polygon.begin() + static_cast<long>(i));
            earFound = true;
            break;
        }

        if (!earFound) {
            triangles.clear();
            for (std::size_t i = 1; i + 1 < loopPoints.size(); ++i) {
                triangles.push_back({0, static_cast<int>(i), static_cast<int>(i + 1)});
            }
            return triangles;
        }
    }

    if (polygon.size() == 3) {
        triangles.push_back({polygon[0], polygon[1], polygon[2]});
    }

    return triangles;
}

struct FootprintVertex2D {
    QPointF xy;
    bool boundary{false};
};

struct BottomFootprintTriangulation {
    std::vector<FootprintVertex2D> vertices;
    std::vector<std::array<int, 3>> triangles;
};

BottomFootprintTriangulation triangulate_regularized_bottom_footprint(
    const std::vector<QPointF>& regularizedLoop2D,
    double sampleSpacing) {
    BottomFootprintTriangulation footprint;
    if (regularizedLoop2D.size() < 3) {
        return footprint;
    }

    footprint.vertices.reserve(regularizedLoop2D.size() + 64);
    for (const QPointF& point : regularizedLoop2D) {
        footprint.vertices.push_back(FootprintVertex2D{point, true});
    }

    footprint.triangles = triangulate_loop_xy_points(regularizedLoop2D);
    if (footprint.triangles.empty()) {
        return footprint;
    }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (const QPointF& point : regularizedLoop2D) {
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    }

    const double minDistanceSqToEdge = sampleSpacing * sampleSpacing * 0.04;
    for (double y = minY + sampleSpacing * 0.5; y <= maxY + 1e-9; y += sampleSpacing) {
        for (double x = minX + sampleSpacing * 0.5; x <= maxX + 1e-9; x += sampleSpacing) {
            const QPointF samplePoint(x, y);
            if (!point_in_polygon_xy(samplePoint, regularizedLoop2D)) {
                continue;
            }

            int containingTriangleIdx = -1;
            for (std::size_t triIdx = 0; triIdx < footprint.triangles.size(); ++triIdx) {
                const auto& triangle = footprint.triangles[triIdx];
                const QPointF& a = footprint.vertices[static_cast<std::size_t>(triangle[0])].xy;
                const QPointF& b = footprint.vertices[static_cast<std::size_t>(triangle[1])].xy;
                const QPointF& c = footprint.vertices[static_cast<std::size_t>(triangle[2])].xy;
                if (point_strictly_inside_triangle_2d(samplePoint, a, b, c, minDistanceSqToEdge)) {
                    containingTriangleIdx = static_cast<int>(triIdx);
                    break;
                }
            }

            if (containingTriangleIdx < 0) {
                continue;
            }

            const int newVertexIdx = static_cast<int>(footprint.vertices.size());
            footprint.vertices.push_back(FootprintVertex2D{samplePoint, false});

            const auto triangle = footprint.triangles[static_cast<std::size_t>(containingTriangleIdx)];
            footprint.triangles[static_cast<std::size_t>(containingTriangleIdx)] =
                {triangle[0], triangle[1], newVertexIdx};
            footprint.triangles.push_back({triangle[1], triangle[2], newVertexIdx});
            footprint.triangles.push_back({triangle[2], triangle[0], newVertexIdx});
        }
    }

    return footprint;
}

bool point_in_triangle_xy(const QVector3D& point,
                          const QVector3D& a,
                          const QVector3D& b,
                          const QVector3D& c) {
    const double c0 = cross_2d(a, b, point);
    const double c1 = cross_2d(b, c, point);
    const double c2 = cross_2d(c, a, point);

    const bool hasNegative =
        (c0 < -kTriangulationEpsilon) ||
        (c1 < -kTriangulationEpsilon) ||
        (c2 < -kTriangulationEpsilon);
    const bool hasPositive =
        (c0 > kTriangulationEpsilon) ||
        (c1 > kTriangulationEpsilon) ||
        (c2 > kTriangulationEpsilon);
    return !(hasNegative && hasPositive);
}

std::vector<int> sanitize_loop_indices(const std::vector<int>& loopIndices,
                                       const std::vector<QVector3D>& vertices) {
    std::vector<int> sanitized;
    sanitized.reserve(loopIndices.size());

    for (const int idx : loopIndices) {
        if (idx < 0 || idx >= static_cast<int>(vertices.size())) {
            continue;
        }
        if (!sanitized.empty()) {
            const QVector3D& previous = vertices[static_cast<std::size_t>(sanitized.back())];
            const QVector3D& current = vertices[static_cast<std::size_t>(idx)];
            if ((previous - current).lengthSquared() <= 1e-12f) {
                continue;
            }
        }
        sanitized.push_back(idx);
    }

    if (sanitized.size() >= 2) {
        const QVector3D& first = vertices[static_cast<std::size_t>(sanitized.front())];
        const QVector3D& last = vertices[static_cast<std::size_t>(sanitized.back())];
        if ((first - last).lengthSquared() <= 1e-12f) {
            sanitized.pop_back();
        }
    }

    return sanitized;
}

std::vector<std::array<int, 3>> triangulate_loop_xy(const std::vector<int>& rawLoopIndices,
                                                    const std::vector<QVector3D>& vertices) {
    std::vector<int> loopIndices = sanitize_loop_indices(rawLoopIndices, vertices);
    std::vector<std::array<int, 3>> triangles;
    if (loopIndices.size() < 3) {
        return triangles;
    }

    if (signed_area_xy(loopIndices, vertices) < 0.0) {
        std::reverse(loopIndices.begin(), loopIndices.end());
    }

    std::vector<int> polygon = loopIndices;
    triangles.reserve(polygon.size() - 2);

    while (polygon.size() > 3) {
        bool earFound = false;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const int prevIdx = polygon[(i + polygon.size() - 1) % polygon.size()];
            const int currIdx = polygon[i];
            const int nextIdx = polygon[(i + 1) % polygon.size()];

            const QVector3D& prev = vertices[static_cast<std::size_t>(prevIdx)];
            const QVector3D& curr = vertices[static_cast<std::size_t>(currIdx)];
            const QVector3D& next = vertices[static_cast<std::size_t>(nextIdx)];

            if (cross_2d(prev, curr, next) <= kTriangulationEpsilon) {
                continue;
            }

            bool containsOtherVertex = false;
            for (const int candidateIdx : polygon) {
                if (candidateIdx == prevIdx || candidateIdx == currIdx || candidateIdx == nextIdx) {
                    continue;
                }
                if (point_in_triangle_xy(vertices[static_cast<std::size_t>(candidateIdx)], prev, curr, next)) {
                    containsOtherVertex = true;
                    break;
                }
            }
            if (containsOtherVertex) {
                continue;
            }

            triangles.push_back({prevIdx, currIdx, nextIdx});
            polygon.erase(polygon.begin() + static_cast<long>(i));
            earFound = true;
            break;
        }

        if (!earFound) {
            triangles.clear();
            for (std::size_t i = 1; i + 1 < loopIndices.size(); ++i) {
                triangles.push_back({loopIndices[0], loopIndices[i], loopIndices[i + 1]});
            }
            return triangles;
        }
    }

    if (polygon.size() == 3) {
        triangles.push_back({polygon[0], polygon[1], polygon[2]});
    }

    return triangles;
}

} // namespace

ManualSupportHandler::ManualSupportHandler(QObject* parent)
    : StandardActionHandler(parent) {
    m_supportRegionGenerationToken = std::make_shared<std::atomic<quint64>>(0);
    m_meshRayQuery = std::make_shared<MeshRayQuery>();

    auto* bridge = SlicingHandlerBridge::instance();
    connect(bridge, &SlicingHandlerBridge::manualSupportApplySettingsRequested,
            this, [this](const QVariantMap& settings) {
                QString error;
                if (!applySettingsCommand(settings, &error)) {
                    LOG_WARN("manualSupportApplySettings rejected: {}", error.toStdString());
                }
            });
    connect(bridge, &SlicingHandlerBridge::manualSupportDebugVisibilityRequested,
            this, [this](const QVariantMap& visibility) {
                QString error;
                if (!applyDebugVisibilityCommand(visibility, &error)) {
                    LOG_WARN("manualSupportSetDebugVisibility rejected: {}", error.toStdString());
                }
            });
    connect(bridge, &SlicingHandlerBridge::manualSupportClearStrokesRequested,
            this, [this]() {
                QString error;
                if (!clearStrokesCommand(&error)) {
                    LOG_WARN("manualSupportClearStrokes rejected: {}", error.toStdString());
                }
            });
    connect(bridge, &SlicingHandlerBridge::manualSupportCommitStrokeRequested,
            this, [this]() {
                QString error;
                if (!commitStrokeCommand(&error)) {
                    LOG_WARN("manualSupportCommitStroke rejected: {}", error.toStdString());
                }
            });

    connect(&interactionPickService(), &PickService::pickCompleted,
            this, [this](const PickSnapshot& snapshot) {
                if (snapshot.channel == PickChannel::ToolStroke) {
                    handleToolPickCompleted(snapshot);
                } else if (snapshot.channel == PickChannel::ToolPreview &&
                           snapshot.viewId == m_interactionViewId &&
                           snapshot.requestId == m_previewPickRequestId) {
                    m_previewPickRequestId = 0;
                    if (snapshot.isReusable() && m_state != State::Inactive) {
                        refreshManualCursor();
                    }
                }
            });
}

bool ManualSupportHandler::encodeSupportStateSnapshot(
    const ManualSupportOrcaScaffold::TriangleSplittingData& state,
    std::string* encodedBlob) {
    if (!encodedBlob) {
        return false;
    }

    QByteArray raw;
    QDataStream out(&raw, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);

    out << kManualSupportStateMagic;
    out << kManualSupportStateVersion;

    quint32 usedStatesMask = 0;
    for (std::size_t i = 0; i < state.usedStates.size() && i < 32; ++i) {
        if (state.usedStates[i]) {
            usedStatesMask |= (1u << static_cast<quint32>(i));
        }
    }
    out << usedStatesMask;

    out << static_cast<quint32>(state.trianglesToSplit.size());
    for (const auto& mapping : state.trianglesToSplit) {
        out << static_cast<qint32>(mapping.triangleIdx);
        out << static_cast<qint32>(mapping.bitstreamStartIdx);
    }

    const quint32 bitCount = static_cast<quint32>(state.bitstream.size());
    out << bitCount;
    QByteArray packedBits(static_cast<int>((bitCount + 7u) / 8u), 0);
    for (quint32 i = 0; i < bitCount; ++i) {
        if (state.bitstream[static_cast<std::size_t>(i)]) {
            packedBits[static_cast<int>(i / 8u)] =
                static_cast<char>(packedBits[static_cast<int>(i / 8u)] | (1 << (i % 8u)));
        }
    }

    if (!packedBits.isEmpty()) {
        out.writeRawData(packedBits.constData(), packedBits.size());
    }

    if (out.status() != QDataStream::Ok) {
        return false;
    }

    *encodedBlob = raw.toBase64(QByteArray::Base64Encoding).toStdString();
    return true;
}

bool ManualSupportHandler::decodeSupportStateSnapshot(
    const std::string& encodedBlob,
    ManualSupportOrcaScaffold::TriangleSplittingData* state) {
    if (!state) {
        return false;
    }
    state->clear();

    if (encodedBlob.empty()) {
        return true;
    }

    const QByteArray encoded = QByteArray::fromStdString(encodedBlob);
    const QByteArray raw = QByteArray::fromBase64(encoded, QByteArray::Base64Encoding);
    if (raw.isEmpty()) {
        return false;
    }

    QDataStream in(raw);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    quint16 version = 0;
    quint32 usedStatesMask = 0;
    quint32 mappingCount = 0;
    quint32 bitCount = 0;
    in >> magic >> version >> usedStatesMask >> mappingCount;
    if (magic != kManualSupportStateMagic || version != kManualSupportStateVersion) {
        return false;
    }

    state->trianglesToSplit.resize(static_cast<std::size_t>(mappingCount));
    for (quint32 i = 0; i < mappingCount; ++i) {
        qint32 triangleIdx = -1;
        qint32 bitstreamStart = -1;
        in >> triangleIdx >> bitstreamStart;
        state->trianglesToSplit[static_cast<std::size_t>(i)] = {
            static_cast<int>(triangleIdx),
            static_cast<int>(bitstreamStart)
        };
    }

    in >> bitCount;
    const int packedByteCount = static_cast<int>((bitCount + 7u) / 8u);
    QByteArray packedBits(packedByteCount, 0);
    if (packedByteCount > 0) {
        const int read = in.readRawData(packedBits.data(), packedByteCount);
        if (read != packedByteCount) {
            return false;
        }
    }

    if (in.status() != QDataStream::Ok) {
        return false;
    }

    state->bitstream.assign(static_cast<std::size_t>(bitCount), false);
    for (quint32 i = 0; i < bitCount; ++i) {
        const unsigned char b = static_cast<unsigned char>(packedBits[static_cast<int>(i / 8u)]);
        const bool bit = (b & (1u << (i % 8u))) != 0;
        state->bitstream[static_cast<std::size_t>(i)] = bit;
    }

    state->usedStates.fill(false);
    for (std::size_t i = 0; i < state->usedStates.size() && i < 32; ++i) {
        state->usedStates[i] = (usedStatesMask & (1u << static_cast<quint32>(i))) != 0;
    }
    return true;
}

void ManualSupportHandler::ensureManualSupportDB() {
    if (!m_targetModelId.isValid()) {
        return;
    }

    auto* docManager = DocumentManager::instance();
    const auto targetModel = docManager
        ? docManager->getDB<ModelInstanceDB>(m_targetModelId)
        : nullptr;
    const auto attachedSupport = targetModel
        ? ManualSupportRelations::findForModel(m_targetModelId)
        : nullptr;
    if (m_manualSupportDB && attachedSupport == m_manualSupportDB) {
        return;
    }
    m_manualSupportDB = targetModel
        ? ManualSupportRelations::findForModel(m_targetModelId)
        : nullptr;
    if (m_manualSupportDB) {
        m_supportStateInitialized = true;
        m_lastAppliedSupportStateBlob.clear();
        return;
    }

    if (!docManager || !targetModel) {
        return;
    }

    m_manualSupportDB = trans::TransDB::create<ManualSupportDB>();
    if (m_manualSupportDB) {
        docManager->attachOwnedChild(
            m_targetModelId, m_manualSupportDB->getDBInstanceID(),
            ManualSupportRelations::ManualSupportRelation);
    }
    if (!m_manualSupportDB ||
        docManager->getOwner(m_manualSupportDB->getDBInstanceID()) !=
            m_targetModelId) {
        LOG_ERROR("ManualSupportHandler: failed to create/attach ManualSupportDB");
        if (m_manualSupportDB) {
            m_manualSupportDB->removeMaterial();
            docManager->unregisterDBInstance(
                m_manualSupportDB->getDBInstanceID());
            m_manualSupportDB.reset();
        }
        return;
    }

    m_supportStateInitialized = true;
    m_lastAppliedSupportStateBlob.clear();
}

void ManualSupportHandler::registerSupportStateChangeListener() {
    if (m_supportStateListenerId != 0) {
        return;
    }
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return;
    }
    m_supportStateListenerId = docManager->addChangeListener(
        [this](const DocumentManager::ChangeNotification& notification) {
            handleSupportStateChange(notification.id, notification.changeType, notification.propertyName);
        });
}

void ManualSupportHandler::unregisterSupportStateChangeListener() {
    if (m_supportStateListenerId == 0) {
        return;
    }
    if (auto* docManager = DocumentManager::instance()) {
        docManager->removeChangeListener(m_supportStateListenerId);
    }
    m_supportStateListenerId = 0;
}

bool ManualSupportHandler::applySupportStateFromDb(bool forceApply) {
    if (!m_manualSupportDB) {
        return false;
    }

    const std::string blob = m_manualSupportDB->getSelectionStateBlob();
    if (blob.empty()) {
        LOG_DEBUG("ManualSupportHandler::applySupportStateFromDb skipped: empty blob");
        return false;
    }
    if (!forceApply && blob == m_lastAppliedSupportStateBlob) {
        LOG_DEBUG("ManualSupportHandler::applySupportStateFromDb skipped: blob unchanged");
        return false;
    }

    ManualSupportOrcaScaffold::TriangleSplittingData state;
    if (!decodeSupportStateSnapshot(blob, &state)) {
        LOG_WARN("ManualSupportHandler: failed to decode support state snapshot, modelId={}",
                 m_targetModelId.getValue());
        return false;
    }

    m_isApplyingSupportState = true;
    m_orcaScaffold.restore_state_snapshot(state);
    refreshCommittedSelectionVisualization();
    m_state = State::Ready;
    m_isApplyingSupportState = false;
    m_lastAppliedSupportStateBlob = blob;
    LOG_INFO("ManualSupportHandler::applySupportStateFromDb applied revision={} forceApply={}",
             m_manualSupportDB ? m_manualSupportDB->getRevision() : -1,
             forceApply ? "true" : "false");
    return true;
}

bool ManualSupportHandler::persistSupportStateSnapshot(bool useTransaction, const char* description) {
    ensureManualSupportDB();
    if (!m_manualSupportDB) {
        return false;
    }

    const auto state = m_orcaScaffold.snapshot_state();
    std::string blob;
    if (!encodeSupportStateSnapshot(state, &blob)) {
        LOG_WARN("ManualSupportHandler: failed to encode support state snapshot");
        return false;
    }

    if (m_manualSupportDB->getSelectionStateBlob() == blob) {
        LOG_DEBUG("ManualSupportHandler::persistSupportStateSnapshot skipped: state unchanged");
        return false;
    }

    const int nextRevision = std::max(0, m_manualSupportDB->getRevision()) + 1;
    m_ignoreSupportStateNotifications = true;
    if (useTransaction) {
        TransactionGuard guard(description ? description : "Manual Support Snapshot");
        m_manualSupportDB->replaceSelectionSnapshot(blob, nextRevision);
    } else {
        TransientUpdateGuard transientUpdate;
        m_manualSupportDB->replaceSelectionSnapshot(blob, nextRevision);
    }
    if (m_state != State::Inactive) {
        queueSupportRegionComputation(true);
    }
    m_ignoreSupportStateNotifications = false;
    m_lastAppliedSupportStateBlob = blob;
    LOG_INFO("ManualSupportHandler::persistSupportStateSnapshot persisted revision={} useTransaction={} desc='{}'",
             nextRevision,
             useTransaction ? "true" : "false",
             description ? description : "Manual Support Snapshot");
    return true;
}

void ManualSupportHandler::handleSupportStateChange(const DBInstanceID& id,
                                                    ChangeType changeType,
                                                    const std::string& propertyName) {
    if (m_state == State::Inactive ||
        !m_manualSupportDB ||
        id != m_manualSupportDB->getDBInstanceID() ||
        m_ignoreSupportStateNotifications ||
        m_isApplyingSupportState) {
        return;
    }

    if (changeType != ChangeType::PROPERTY_CHANGED) {
        return;
    }
    if (!propertyName.empty() && propertyName != "Revision") {
        return;
    }

    LOG_DEBUG("ManualSupportHandler::handleSupportStateChange id={} changeType={} property='{}'",
              id.getValue(),
              static_cast<int>(changeType),
              propertyName);
    m_manualSupportDB->invalidateGeneratedGeometry();
    if (applySupportStateFromDb(false)) {
        queueSupportRegionComputation(true);
    }
}

void ManualSupportHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    if (!context) {
        LOG_WARN("ManualSupportHandler::onEnter with null context");
        onExit();
        return;
    }

    const QString actionCode = getActionCode();

    if (actionCode == "support.manual.enter") {
        handleEnterAction();
    } else if (actionCode == "support.manual.leave") {
        handleLeaveAction();
    } else {
        context->setError(ActionErrorCode::InvalidParams, QString("Unsupported manual support action: %1").arg(actionCode));
        LOG_WARN("ManualSupportHandler::onEnter unsupported action '{}'",
                 actionCode.toStdString());
        onExit();
    }
}

void ManualSupportHandler::onExit() {
    const bool hadSession = (m_state != State::Inactive);

    ++m_gestureSequence;
    cancelPendingToolPicks();
    m_scaffoldPointerDown = false;
    cancelSupportRegionComputation();
    unregisterSupportStateChangeListener();
    m_isPointerDown = false;
    clearManualCursor();
    resetCurrentStroke();
    m_orcaScaffold.restore_state_snapshot(ManualSupportOrcaScaffold::TriangleSplittingData{});
    m_orcaScaffold.reset_session();
    m_meshRayQuery.reset();
    m_targetMeshVertices.clear();
    m_targetMeshTriangles.clear();
    m_lastStampStartFacetId = -1;
    m_lastSelectedSourceFacetIds.clear();
    m_baseSelectionState.clear();
    m_hasBaseSelectionState = false;
    m_hasSelectionStateBeforeStroke = false;
    m_supportStateInitialized = false;
    m_lastAppliedSupportStateBlob.clear();
    m_contactDiagnostics = SupportContactDiagnostics{};
    clearDebugLayers();
    m_state = State::Inactive;

    if (m_debugActor) {
        m_debugActor->setVisible(false);
    }
    if (m_debugActorPick) {
        m_debugActorPick->setVisible(false);
    }
    if (m_debugActorContact) {
        m_debugActorContact->setVisible(false);
    }
    if (auto* bridge = SlicingHandlerBridge::instance()) {
        bridge->setManualSupportActive(false);
        bridge->setManualSupportContactDiagnostics(false, 0, 0, 0, 0.0);
        bridge->updateDebugActorCount(0);
        bridge->setModelInstance({});
    }

    if (hadSession) {
        set_manual_support_print_beds_visible(true);
    }

    if (hadSession && m_switchEnvironmentOnStart) {
        EnvironmentSwitchHandler::switchEnvironment(m_returnEnvironment);
    }

    if (m_modelInputSuspended) {
        m_modelInputSuspended = false;
        if (auto* actionManager = ActionManager::getInstance();
            actionManager && actionManager->isAcceptingActions()) {
            actionManager->triggerAction(
                QStringLiteral("modelInteraction.resumeSelectionInput"));
        }
    }

    StandardActionHandler::onExit();
}

void ManualSupportHandler::resetCommittedSupportMeshState() {
    if (m_manualSupportDB) {
        m_manualSupportDB->invalidateGeneratedGeometry();
    }
}

bool ManualSupportHandler::onMousePressEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_state == State::Inactive) {
        return false;
    }

    const DBInstanceID sourceView(inputViewId());
    if (sourceView == INVALID_DB_ID ||
        !interactionPickService().supportsFeature(
            sourceView, GPlatform::Rendering::PickFeature::Primitive)) {
        clearManualCursor();
        return false;
    }
    if (m_interactionViewId != INVALID_DB_ID &&
        m_interactionViewId != sourceView) {
        abortPendingStroke();
    }
    m_interactionViewId = sourceView;
    m_isPointerDown = true;
    m_state = State::Drawing;
    resetCurrentStroke();
    m_selectionStateBeforeStroke = m_orcaScaffold.snapshot_state();
    m_hasSelectionStateBeforeStroke = true;

    ++m_gestureSequence;
    cancelPendingToolPicks();
    const auto cached = interactionPickService().cachedForView(
        m_interactionViewId,
        PickChannel::ToolPreview,
        GPlatform::Rendering::PickDetail::Primitive,
        event->pos());
    if (cached) {
        if (cached->status != GPlatform::Rendering::PickStatus::Hit ||
            !beginStrokeFromPick(cached->result, event->pos(), event->modifiers())) {
            abortPendingStroke();
            return false;
        }
        return true;
    }

    m_pressPickRequestId = requestToolPick(
        event->pos(), event->modifiers(), ToolPickPhase::Press,
        PickDelivery::OneShot);
    if (m_pressPickRequestId == 0) {
        abortPendingStroke();
        return false;
    }
    return true; // The gesture remains non-blocking while Pick completes.
}

bool ManualSupportHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!event) {
        return false;
    }

    const DBInstanceID sourceView(inputViewId());
    if (sourceView == INVALID_DB_ID ||
        !interactionPickService().supportsFeature(
            sourceView, GPlatform::Rendering::PickFeature::Primitive)) {
        clearManualCursor();
        return false;
    }
    if (m_interactionViewId != INVALID_DB_ID &&
        m_interactionViewId != sourceView && m_isPointerDown) {
        abortPendingStroke();
    }
    m_interactionViewId = sourceView;

    if (m_state != State::Inactive) {
        refreshManualCursor();
    }

    if (!m_isPointerDown || m_state != State::Drawing) {
        if (m_state != State::Inactive) {
            m_previewPickRequestId = interactionPickService().requestForView(
                m_interactionViewId, event->pos(),
                PickChannel::ToolPreview,
                GPlatform::Rendering::PickDetail::Primitive,
                PickDelivery::LatestOnly);
        }
        return false;
    }

    requestToolPick(event->pos(), event->modifiers(), ToolPickPhase::Move,
                    PickDelivery::LatestOnly);
    return true;
}

bool ManualSupportHandler::onMouseReleaseEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton || !m_isPointerDown) {
        return false;
    }

    const DBInstanceID sourceView(inputViewId());
    if (sourceView != m_interactionViewId) {
        abortPendingStroke();
        return false;
    }
    m_isPointerDown = false;
    if (m_state == State::Drawing) {
        const auto cached = interactionPickService().cachedForView(
            m_interactionViewId,
            PickChannel::ToolStroke,
            GPlatform::Rendering::PickDetail::Primitive,
            event->pos());
        if (cached && m_scaffoldPointerDown) {
            finishStrokeFromPick(cached->result, event->pos(), event->modifiers());
        } else {
            const auto requestId = requestToolPick(
                event->pos(), event->modifiers(), ToolPickPhase::Release,
                PickDelivery::OneShot);
            if (requestId == 0) {
                finishStrokeFromPick(empty_pick_result(), event->pos(), event->modifiers());
            }
        }
    }

    return true;
}

bool ManualSupportHandler::onWheelEvent(QWheelEvent* event) {
    if (!event || m_state == State::Inactive) {
        return false;
    }

    if (!event->modifiers().testFlag(Qt::ControlModifier)) {
        return false;
    }

    const QPoint angleDelta = event->angleDelta();
    if (angleDelta.y() == 0) {
        return false;
    }

    const double step = 0.5;
    const double direction = angleDelta.y() > 0 ? 1.0 : -1.0;
    const double nextSize = std::clamp(m_brushSizeMm + direction * step, 1.0, 30.0);
    if (qFuzzyCompare(m_brushSizeMm, nextSize)) {
        return true;
    }

    m_brushSizeMm = nextSize;
    m_orcaScaffold.set_cursor_radius(static_cast<float>(m_brushSizeMm));
    refreshManualCursor();

    if (auto* bridge = SlicingHandlerBridge::instance()) {
        bridge->setManualSupportBrushSizeMm(m_brushSizeMm);
    }

    event->accept();
    return true;
}

void ManualSupportHandler::handleEnterAction() {
    m_targetModelId = parseTargetModelId(getParam("modelId"));
    m_switchEnvironmentOnStart = getParam("switchEnvironment", true).toBool();
    m_returnEnvironment = getParam("returnEnvironment", "slicing").toString();
    cancelSupportRegionComputation();
    if (m_returnEnvironment.isEmpty()) {
        m_returnEnvironment = "slicing";
    }

    if (!m_targetModelId.isValid()) {
        const QString error = "support.manual.enter requires a valid modelId";
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, error);
        }
        LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}", error.toStdString());
        onExit();
        return;
    }

    const bool resetSession = getParam("resetSession", true).toBool();
    if (m_manualSupportDB) {
        auto* document = DocumentManager::instance();
        const auto model = document
            ? document->getDB<ModelInstanceDB>(m_targetModelId)
            : nullptr;
        const auto attachedSupport = model
            ? ManualSupportRelations::findForModel(m_targetModelId)
            : nullptr;
        if (attachedSupport != m_manualSupportDB) {
            m_manualSupportDB.reset();
        }
    }
    if (resetSession) {
        clearAllStrokes();
        m_orcaScaffold.reset_session();
        m_baseSelectionState.clear();
        m_hasBaseSelectionState = false;
        m_hasSelectionStateBeforeStroke = false;
        resetCommittedSupportMeshState();
    } else {
        resetCurrentStroke();
        m_hasSelectionStateBeforeStroke = false;
    }

    {
        auto* docManager = DocumentManager::instance();
        if (!docManager) {
            const QString error = "DocumentManager unavailable";
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, error);
            }
            LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}", error.toStdString());
            m_orcaScaffold.clear_mesh();
            m_targetFacetNormals.clear();
            onExit();
            return;
        }

        auto model = docManager->getDB<ModelInstanceDB>(m_targetModelId);
        if (!model || !model->isValid()) {
            const QString error = QString("modelId=%1 is not a valid ModelInstanceDB")
                                      .arg(m_targetModelId.getValue());
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, error);
            }
            LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}", error.toStdString());
            m_orcaScaffold.clear_mesh();
            m_targetFacetNormals.clear();
            onExit();
            return;
        }

        if (auto* bridge = SlicingHandlerBridge::instance()) {
            bridge->setModelInstance(model);
            bridge->setShowOriginalModel(bridge->showOriginalModel());
        }

        std::vector<GeomTriangle> worldTriangles;
        std::vector<std::pair<std::uint64_t, std::uint64_t>>
            sourceFacetKeys;
        m_partPrimitiveToScaffoldFacet.clear();
        for (const auto& part : model->parts()) {
            const auto geometry = part ? part->geometry() : nullptr;
            const auto readHandle = geometry
                ? geometry->read()
                : ModelGeometryDB::ReadHandle{};
            auto* polyData = readHandle
                ? const_cast<vtkPolyData*>(&readHandle.polyData())
                : nullptr;
            if (!polyData) continue;
            const Transform::Matrix4 worldFromPart =
                model->getTransformMatrix() *
                part->getLocalTransform().getMatrix();
            for (vtkIdType cellId = 0;
                 cellId < polyData->GetNumberOfCells(); ++cellId) {
                vtkCell* cell = polyData->GetCell(cellId);
                if (!cell || cell->GetNumberOfPoints() != 3) continue;
                Vector3 points[3];
                for (int corner = 0; corner < 3; ++corner) {
                    double point[3] = {0.0, 0.0, 0.0};
                    polyData->GetPoint(cell->GetPointId(corner), point);
                    points[corner] = transform_point_with_matrix(
                        {static_cast<float>(point[0]),
                         static_cast<float>(point[1]),
                         static_cast<float>(point[2])},
                        worldFromPart);
                }
                worldTriangles.emplace_back(points[0], points[1], points[2]);
                sourceFacetKeys.emplace_back(
                    part->getDBInstanceID().getValue(),
                    static_cast<std::uint64_t>(cellId));
            }
        }

        OrcaIndexedMesh indexedMesh;
        QString buildError;
        if (worldTriangles.empty() ||
            !build_orca_indexed_mesh_for_manual_support(
                worldTriangles, indexedMesh, &buildError)) {
            const QString error = QString(
                "Failed to build Orca indexed mesh for modelId=%1: %2")
                                      .arg(m_targetModelId.getValue())
                                      .arg(buildError);
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, error);
            }
            LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}", error.toStdString());
            m_orcaScaffold.clear_mesh();
            m_targetFacetNormals.clear();
            onExit();
            return;
        }

        if (indexedMesh.sourceFacetToIndexedFacet.size() !=
            sourceFacetKeys.size()) {
            const QString error = QStringLiteral(
                "Manual support source-facet mapping is incomplete");
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, error);
            }
            LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}",
                     error.toStdString());
            m_orcaScaffold.clear_mesh();
            m_targetFacetNormals.clear();
            onExit();
            return;
        }
        for (std::size_t sourceIndex = 0;
             sourceIndex < sourceFacetKeys.size(); ++sourceIndex) {
            const int scaffoldFacet =
                indexedMesh.sourceFacetToIndexedFacet[sourceIndex];
            if (scaffoldFacet >= 0) {
                m_partPrimitiveToScaffoldFacet.emplace(
                    sourceFacetKeys[sourceIndex], scaffoldFacet);
            }
        }

        m_orcaScaffold.load_mesh(indexedMesh.vertices,
                                 indexedMesh.triangles,
                                 indexedMesh.neighbors,
                                 indexedMesh.faceNormals);
        m_targetMeshVertices = indexedMesh.vertices;
        m_targetMeshTriangles = indexedMesh.triangles;
        m_meshRayQuery = std::make_shared<MeshRayQuery>();
        m_meshRayQuery->build(m_targetMeshVertices, m_targetMeshTriangles);
        m_targetFacetNormals = indexedMesh.faceNormals;
        LOG_INFO("ManualSupportHandler: loaded model graph faces={}, sharedVertices={}",
                 indexedMesh.triangles.size(), indexedMesh.vertices.size());
    }

    if (!m_orcaScaffold.has_mesh()) {
        const QString error = QString("Manual support prerequisites not met for modelId=%1")
                                  .arg(m_targetModelId.getValue());
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, error);
        }
        LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}", error.toStdString());
        m_orcaScaffold.clear_mesh();
        m_targetFacetNormals.clear();
        onExit();
        return;
    }

    if (!getParams().contains("overhangThresholdDeg")) {
        const int resolvedThresholdDeg = resolve_manual_support_overhang_threshold_deg();
        if (resolvedThresholdDeg == kUnsetOverhangThresholdDeg) {
            const QString error = QStringLiteral("support_angle is missing or invalid");
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, error);
            }
            LOG_WARN("ManualSupportHandler::handleEnterAction rejected: {}", error.toStdString());
            onExit();
            return;
        }
        m_overhangThresholdDeg = resolvedThresholdDeg;
    }

    if (auto* previewBridge = SlicingPreviewBridge::instance()) {
        previewBridge->clearPreviewPreservingModel(static_cast<int>(m_targetModelId.getValue()));
    }

    auto* actionManager = ActionManager::getInstance();
    if (!actionManager) {
        if (m_context) {
            m_context->setError(
                ActionErrorCode::SystemUnavailable,
                QStringLiteral("ActionManager unavailable"));
        }
        onExit();
        return;
    }
    const QString environment = actionManager->currentEnvironment();
    const bool modelInputCanRun =
        environment == QStringLiteral("normal") ||
        environment == QStringLiteral("editing");
    if (modelInputCanRun) {
        m_modelInputSuspended = actionManager->triggerAction(
            QStringLiteral("modelInteraction.suspendSelectionInput"));
        if (!m_modelInputSuspended) {
            const QString error =
                QStringLiteral("Unable to suspend direct model interaction");
            if (m_context) {
                m_context->setError(ActionErrorCode::SystemUnavailable, error);
            }
            LOG_ERROR("ManualSupportHandler: {}", error.toStdString());
            onExit();
            return;
        }
    }

    if (m_switchEnvironmentOnStart) {
        // Manual support editing is hosted inside slicing workspace.
        EnvironmentSwitchHandler::switchEnvironment("slicing");
    }
    applySettingsFromParams(getParams());
    m_orcaScaffold.set_cursor_radius(static_cast<float>(m_brushSizeMm));
    m_orcaScaffold.set_cursor_type(
        m_brushShape == "sphere" ? ManualSupportOrcaScaffold::CursorType::Sphere
        : (m_brushShape == "square" ? ManualSupportOrcaScaffold::CursorType::Square
           : (m_brushShape == "triangle" ? ManualSupportOrcaScaffold::CursorType::Triangle
                                         : ManualSupportOrcaScaffold::CursorType::Circle)));
    m_orcaScaffold.set_triangle_splitting_enabled(true);
    m_orcaScaffold.set_active_paint_state(
        m_paintMode == "block" ? ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Blocker)
        : (m_paintMode == "erase" ? ManualSupportOrcaScaffold::NoLabel
                                    : ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Enforcer)));
    syncScaffoldOverhangFilter();
    applyDebugVisibilityFromParams(getParams());

    ensureDebugActorCreated();
    applyDebugVisibility();
    clearDebugLayers();
    initializeAutoSupportSelection(resetSession);
    ensureManualSupportDB();
    registerSupportStateChangeListener();
    if (resetSession) {
        (void)persistSupportStateSnapshot(false, "Manual Support Baseline");
    } else if (!applySupportStateFromDb(true)) {
        (void)persistSupportStateSnapshot(false, "Manual Support Baseline");
    }
    refreshManualCursor();

    m_state = State::Ready;
    if (m_manualSupportDB && !m_manualSupportDB->hasGeneratedGeometry()) {
        queueSupportRegionComputation(true);
    }
    set_manual_support_print_beds_visible(false);
    if (m_context) {
        m_context->setResult(buildStateResult());
    }
    if (auto* bridge = SlicingHandlerBridge::instance()) {
        bridge->setManualSupportActive(true);
        bridge->setManualSupportPaintMode(m_paintMode);
        bridge->setManualSupportBrushShape(m_brushShape);
        bridge->setManualSupportBrushSizeMm(m_brushSizeMm);
        bridge->setManualSupportDensityPercent(m_densityPercent);
        bridge->setManualSupportSmartFill(m_smartFill);
        bridge->setManualSupportClipToOverhang(m_clipToOverhang);
        bridge->setManualSupportSmartFillAngleDeg(m_smartFillAngleDeg);
        bridge->setManualSupportGapArea(m_gapArea);
        bridge->setManualSupportSectionViewRatio(m_sectionViewRatio);

        bridge->setManualDebugShowActor(m_debugActorVisible);
        bridge->setManualDebugShowStrokePolyline(m_showStrokePolyline);
        bridge->setManualDebugShowStrokePoints(m_showStrokePoints);
        bridge->setManualDebugShowPickNormals(m_showPickNormals);
        bridge->setManualDebugShowProjectedRing(m_showProjectedRing);
        bridge->setManualDebugShowMergedSelectionSurfaceOutline(m_showMergedSelectionSurfaceOutline);
        bridge->setManualDebugShowContactPatch(m_showContactPatch);
        bridge->setShowDebugData(m_debugActorVisible);
    }

    LOG_INFO("ManualSupportHandler entered: targetModelId={}, resetSession={}",
             m_targetModelId.getValue(), resetSession);
}

void ManualSupportHandler::handleLeaveAction() {
    if (m_state != State::Inactive) {
        resetCommittedSupportMeshState();
    }

    if (m_context) {
        QVariantMap result = buildStateResult();
        result.insert("state", "inactive");
        m_context->setResult(result);
    }
    onExit();

    LOG_INFO("ManualSupportHandler left");
}

bool ManualSupportHandler::applySettingsCommand(const QVariantMap& settings, QString* errorMessage) {
    Q_UNUSED(errorMessage)

    const int previousOverhangThresholdDeg = m_overhangThresholdDeg;
    applySettingsFromParams(settings);
    m_orcaScaffold.set_cursor_radius(static_cast<float>(m_brushSizeMm));
    m_orcaScaffold.set_cursor_type(
        m_brushShape == "sphere" ? ManualSupportOrcaScaffold::CursorType::Sphere
        : (m_brushShape == "square" ? ManualSupportOrcaScaffold::CursorType::Square
           : (m_brushShape == "triangle" ? ManualSupportOrcaScaffold::CursorType::Triangle
                                         : ManualSupportOrcaScaffold::CursorType::Circle)));
    m_orcaScaffold.set_triangle_splitting_enabled(true);
    syncScaffoldOverhangFilter();
    m_state = State::Ready;
    refreshManualCursor();

    if (auto* bridge = SlicingHandlerBridge::instance()) {
        bridge->setManualSupportPaintMode(m_paintMode);
        bridge->setManualSupportBrushShape(m_brushShape);
        bridge->setManualSupportBrushSizeMm(m_brushSizeMm);
        bridge->setManualSupportDensityPercent(m_densityPercent);
        bridge->setManualSupportSmartFill(m_smartFill);
        bridge->setManualSupportClipToOverhang(m_clipToOverhang);
        bridge->setManualSupportSmartFillAngleDeg(m_smartFillAngleDeg);
        bridge->setManualSupportGapArea(m_gapArea);
        bridge->setManualSupportSectionViewRatio(m_sectionViewRatio);
    }

    const bool thresholdChanged =
        settings.contains("overhangThresholdDeg") &&
        previousOverhangThresholdDeg != m_overhangThresholdDeg;
    if (thresholdChanged) {
        clearAllStrokes();
        m_baseSelectionState.clear();
        m_hasBaseSelectionState = false;
        m_selectionStateBeforeStroke.clear();
        m_hasSelectionStateBeforeStroke = false;
        initializeAutoSupportSelection(true);
        (void)persistSupportStateSnapshot(true, "Manual Support Overhang Threshold");
    }

    return true;
}

void ManualSupportHandler::syncScaffoldOverhangFilter() {
    const float angleDeg =
        m_clipToOverhang ? static_cast<float>(m_overhangThresholdDeg) : -1.0f;
    m_orcaScaffold.set_highlight_by_angle_deg(angleDeg);
}

void ManualSupportHandler::initializeAutoSupportSelection(bool resetSession) {
    if (!m_orcaScaffold.has_mesh()) {
        return;
    }
    if (!resetSession && m_hasBaseSelectionState) {
        return;
    }

    m_orcaScaffold.select_all_overhang_triangles(
        static_cast<float>(m_overhangThresholdDeg),
        ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Enforcer),
        true);
    m_baseSelectionState = m_orcaScaffold.snapshot_state();
    m_hasBaseSelectionState = true;
    refreshCommittedSelectionVisualization();
}

void ManualSupportHandler::refreshCommittedSelectionVisualization() {
    if (m_debugActorPick) {
        m_debugActorPick->setGeometry(create_empty_polydata());
    }
    refreshSelectionDebugSnapshot();
    updateDebugSelectionLayer(true);
}

void ManualSupportHandler::refreshManualCursor() {
    if (m_state == State::Inactive) {
        clearManualCursor();
        return;
    }

    const QString shape = m_brushShape.trimmed().toLower();
    if (shape == QStringLiteral("fill") ||
        shape == QStringLiteral("gap") ||
        shape == QStringLiteral("gap_fill") ||
        shape == QStringLiteral("gapfill")) {
        m_surfaceBrushCursor.applyTool(cursorShapeForCurrentTool());
        return;
    }
    m_surfaceBrushCursor.applyBrush(
        brushCursorRadiusPx(), SurfaceBrushCursor::shapeFromName(shape));
}

void ManualSupportHandler::clearManualCursor() {
    m_surfaceBrushCursor.clear();
}

Qt::CursorShape ManualSupportHandler::cursorShapeForCurrentTool() const {
    return Qt::PointingHandCursor;
}

int ManualSupportHandler::brushCursorRadiusPx() const {
    return SurfaceBrushCursor::radiusPxForSize(m_brushSizeMm);
}

bool ManualSupportHandler::tryComputeProjectedBrushRadiusWorld(const QPoint& requestedScreenPos,
                                                               float* radiusWorld) const {
    if (!radiusWorld || requestedScreenPos.x() < 0 || requestedScreenPos.y() < 0) {
        return false;
    }

    PickResult pick;
    if (!resolve_pick_for_target_model(
            m_targetModelId, m_interactionViewId,
            requestedScreenPos, &pick)) {
        return false;
    }

    SurfaceBrushProjection::ScreenContext projection;
    return SurfaceBrushProjection::screenContextAt(
               viewport_projection(m_interactionViewId), *pick.worldPosition,
               brushCursorRadiusPx(), &projection) &&
           projection.worldRadius(radiusWorld);
}

float ManualSupportHandler::resolvePrintBedTopZ() const {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return 0.0f;
    }

    const auto printBeds = docManager->getDBInstancesByType(TypeID::PRINT_BED_DB);
    for (const auto& instance : printBeds) {
        auto printBed = std::dynamic_pointer_cast<PrintBedDB>(instance);
        if (!printBed) {
            continue;
        }
        const Vector3 center = printBed->getCenter();
        return center.z + printBed->getThickness() * 0.5f;
    }

    return 0.0f;
}

vtkSmartPointer<vtkPolyData> ManualSupportHandler::buildProjectedSupportCandidatePolyData(
    const ManualSupportOrcaScaffold::SelectedSurfaceMesh* surfaceMesh,
    SupportContactDiagnostics* diagnostics) const {
    if (diagnostics) {
        *diagnostics = SupportContactDiagnostics{};
    }

    ManualSupportOrcaScaffold::SelectedSurfaceMesh ownedSurfaceMesh;
    const ManualSupportOrcaScaffold::SelectedSurfaceMesh* resolvedSurfaceMesh = surfaceMesh;
    if (!resolvedSurfaceMesh) {
        ownedSurfaceMesh = m_orcaScaffold.collect_selected_surface_mesh();
        resolvedSurfaceMesh = &ownedSurfaceMesh;
    }

    const auto& selectedSurfaceMesh = *resolvedSurfaceMesh;
    return buildProjectedSupportCandidatePolyDataForTask(
        selectedSurfaceMesh,
        m_meshRayQuery.get(),
        resolvePrintBedTopZ(),
        m_brushShape,
        m_brushSizeMm,
        m_densityPercent,
        diagnostics);
}

vtkSmartPointer<vtkPolyData> ManualSupportHandler::buildContactSurfacePolyData(
    const ManualSupportOrcaScaffold::SelectedSurfaceMesh* surfaceMesh) const {
    ManualSupportOrcaScaffold::SelectedSurfaceMesh ownedSurfaceMesh;
    const ManualSupportOrcaScaffold::SelectedSurfaceMesh* resolvedSurfaceMesh = surfaceMesh;
    if (!resolvedSurfaceMesh) {
        ownedSurfaceMesh = m_orcaScaffold.collect_selected_surface_mesh();
        resolvedSurfaceMesh = &ownedSurfaceMesh;
    }

    const auto& selectedSurfaceMesh = *resolvedSurfaceMesh;
    return buildContactSurfacePolyDataForTask(selectedSurfaceMesh);
}

vtkSmartPointer<vtkPolyData> ManualSupportHandler::buildProjectedSupportCandidatePolyDataForTask(
    const ManualSupportOrcaScaffold::SelectedSurfaceMesh& surfaceMesh,
    const MeshRayQuery* meshRayQuery,
    float groundZ,
    const QString& brushShape,
    double brushSizeMm,
    double densityPercentValue,
    SupportContactDiagnostics* diagnostics,
    const std::function<void(float, const QString&)>& progressCallback,
    const std::function<bool()>& shouldCancel) {
    if (diagnostics) {
        *diagnostics = SupportContactDiagnostics{};
    }
    if (shouldCancel && shouldCancel()) {
        return create_empty_polydata();
    }
    if (surfaceMesh.strictTriangles.empty() || surfaceMesh.vertices.empty()) {
        return create_empty_polydata();
    }

    SupportContactDiagnostics localDiagnostics;
    if (progressCallback) {
        progressCallback(0.08f, QStringLiteral("正在构建支撑区域..."));
    }
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto polys = vtkSmartPointer<vtkCellArray>::New();

    std::vector<vtkIdType> topPointIds(surfaceMesh.vertices.size(), -1);
    for (std::size_t i = 0; i < surfaceMesh.vertices.size(); ++i) {
        const QVector3D& vertex = surfaceMesh.vertices[i];
        topPointIds[i] = points->InsertNextPoint(vertex.x(), vertex.y(), vertex.z());
    }

    for (const auto& triangle : surfaceMesh.strictTriangles) {
        if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
            triangle[0] >= static_cast<int>(topPointIds.size()) ||
            triangle[1] >= static_cast<int>(topPointIds.size()) ||
            triangle[2] >= static_cast<int>(topPointIds.size())) {
            continue;
        }

        auto vtkTriangleCell = vtkSmartPointer<vtkTriangle>::New();
        vtkTriangleCell->GetPointIds()->SetId(0, topPointIds[static_cast<std::size_t>(triangle[0])]);
        vtkTriangleCell->GetPointIds()->SetId(1, topPointIds[static_cast<std::size_t>(triangle[1])]);
        vtkTriangleCell->GetPointIds()->SetId(2, topPointIds[static_cast<std::size_t>(triangle[2])]);
        polys->InsertNextCell(vtkTriangleCell);
        localDiagnostics.hasContactPatch = true;
        ++localDiagnostics.contactTriangleCount;
    }

    const auto firstHitBelow = [meshRayQuery, groundZ](const QPointF& point, float startZ) {
        return meshRayQuery ? meshRayQuery->firstHitBelow(point, startZ, groundZ) : groundZ;
    };

    const int totalLoopCount = std::max(static_cast<int>(surfaceMesh.outerBoundaryLoops.size()), 1);
    int processedLoopCount = 0;

    for (const auto& loop : surfaceMesh.outerBoundaryLoops) {
        if (shouldCancel && shouldCancel()) {
            return create_empty_polydata();
        }
        const float loopStartProgress =
            0.08f + (0.8f * static_cast<float>(processedLoopCount) / static_cast<float>(totalLoopCount));
        const float loopEndProgress =
            0.08f + (0.8f * static_cast<float>(processedLoopCount + 1) / static_cast<float>(totalLoopCount));
        if (loop.size() < 3) {
            ++processedLoopCount;
            if (progressCallback) {
                progressCallback(loopEndProgress, QStringLiteral("正在构建支撑区域..."));
            }
            continue;
        }

        std::vector<int> orderedLoop = sanitize_loop_indices(loop, surfaceMesh.vertices);
        if (orderedLoop.size() < 3) {
            ++processedLoopCount;
            if (progressCallback) {
                progressCallback(loopEndProgress, QStringLiteral("正在构建支撑区域..."));
            }
            continue;
        }

        std::vector<QPointF> exactLoop2D = build_loop_xy_points(orderedLoop, surfaceMesh.vertices);
        if (exactLoop2D.size() != orderedLoop.size() || exactLoop2D.size() < 3) {
            ++processedLoopCount;
            if (progressCallback) {
                progressCallback(loopEndProgress, QStringLiteral("正在构建支撑区域..."));
            }
            continue;
        }

        float shaftTopZ = std::numeric_limits<float>::max();
        for (const int vertexIdx : orderedLoop) {
            if (vertexIdx < 0 || vertexIdx >= static_cast<int>(surfaceMesh.vertices.size())) {
                continue;
            }
            shaftTopZ = std::min(shaftTopZ, surfaceMesh.vertices[static_cast<std::size_t>(vertexIdx)].z());
        }
        if (!std::isfinite(shaftTopZ)) {
            shaftTopZ = groundZ;
        }

        const std::vector<QPointF> regularizedLoop2D =
            build_regularized_shaft_loop_2d(orderedLoop, surfaceMesh.vertices, brushShape, brushSizeMm);
        if (regularizedLoop2D.size() < 3) {
            ++processedLoopCount;
            if (progressCallback) {
                progressCallback(loopEndProgress, QStringLiteral("正在构建支撑区域..."));
            }
            continue;
        }

        if (signed_area_xy(exactLoop2D) * signed_area_xy(regularizedLoop2D) < 0.0) {
            std::reverse(orderedLoop.begin(), orderedLoop.end());
            std::reverse(exactLoop2D.begin(), exactLoop2D.end());
        }

        const std::vector<double> exactFractions = compute_loop_fractions_2d(exactLoop2D);
        std::vector<QPointF> denseShaftTopLoop2D =
            sample_closed_loop_by_fractions(regularizedLoop2D, exactFractions);
        if (denseShaftTopLoop2D.size() == exactLoop2D.size()) {
            rotate_closed_loop(
                denseShaftTopLoop2D,
                static_cast<std::size_t>(best_cyclic_shift_for_loop_match(exactLoop2D, denseShaftTopLoop2D)));
        }

        if (denseShaftTopLoop2D.size() != orderedLoop.size()) {
            denseShaftTopLoop2D.clear();
            denseShaftTopLoop2D.reserve(orderedLoop.size());
            for (std::size_t i = 0; i < orderedLoop.size(); ++i) {
                denseShaftTopLoop2D.push_back(regularizedLoop2D[i % regularizedLoop2D.size()]);
            }
        }

        std::vector<vtkIdType> denseShaftTopIds;
        denseShaftTopIds.reserve(denseShaftTopLoop2D.size());
        for (const QPointF& point : denseShaftTopLoop2D) {
            denseShaftTopIds.push_back(points->InsertNextPoint(point.x(), point.y(), shaftTopZ));
        }

        for (std::size_t i = 0; i < orderedLoop.size(); ++i) {
            const int topI = orderedLoop[i];
            const int topJ = orderedLoop[(i + 1) % orderedLoop.size()];
            const vtkIdType shaftI = denseShaftTopIds[i];
            const vtkIdType shaftJ = denseShaftTopIds[(i + 1) % denseShaftTopIds.size()];
            if (topI < 0 || topJ < 0 ||
                topI >= static_cast<int>(topPointIds.size()) ||
                topJ >= static_cast<int>(topPointIds.size())) {
                continue;
            }

            auto transitionTriA = vtkSmartPointer<vtkTriangle>::New();
            transitionTriA->GetPointIds()->SetId(0, topPointIds[static_cast<std::size_t>(topI)]);
            transitionTriA->GetPointIds()->SetId(1, topPointIds[static_cast<std::size_t>(topJ)]);
            transitionTriA->GetPointIds()->SetId(2, shaftJ);
            polys->InsertNextCell(transitionTriA);

            auto transitionTriB = vtkSmartPointer<vtkTriangle>::New();
            transitionTriB->GetPointIds()->SetId(0, topPointIds[static_cast<std::size_t>(topI)]);
            transitionTriB->GetPointIds()->SetId(1, shaftJ);
            transitionTriB->GetPointIds()->SetId(2, shaftI);
            polys->InsertNextCell(transitionTriB);
        }

        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = std::numeric_limits<double>::lowest();
        for (const QPointF& point : regularizedLoop2D) {
            minX = std::min(minX, point.x());
            minY = std::min(minY, point.y());
            maxX = std::max(maxX, point.x());
            maxY = std::max(maxY, point.y());
        }

        const double densityPercent = std::clamp(densityPercentValue, 1.0, 100.0);
        double sampleSpacing = std::clamp(brushSizeMm * 0.6, 0.6, 2.5);
        sampleSpacing *= std::sqrt(100.0 / densityPercent);
        sampleSpacing = std::clamp(sampleSpacing, 0.6, 4.0);
        const int estimatedRowCount = std::max(
            1,
            static_cast<int>(std::floor((maxY - (minY + sampleSpacing * 0.5)) / sampleSpacing + 1.0 + 1e-9)));

        struct BottomVertex {
            QPointF xy;
            float z = 0.0f;
            bool boundary = false;
        };

        std::vector<BottomVertex> bottomVertices;
        bottomVertices.reserve(regularizedLoop2D.size() + 64);
        for (const QPointF& point : regularizedLoop2D) {
            const float bottomZ = firstHitBelow(point, shaftTopZ);
            bottomVertices.push_back(BottomVertex{point, bottomZ, true});
            ++localDiagnostics.totalBottomSampleCount;
            if (bottomZ > groundZ + kLowerModelHitEpsilonMm) {
                ++localDiagnostics.lowerModelHitSampleCount;
                localDiagnostics.maxLowerModelHitHeightMm = std::max(
                    localDiagnostics.maxLowerModelHitHeightMm,
                    static_cast<double>(bottomZ - groundZ));
            }
        }

        std::vector<std::array<int, 3>> bottomTriangles =
            triangulate_loop_xy_points(regularizedLoop2D);

        const double minDistanceSqToEdge = sampleSpacing * sampleSpacing * 0.04;
        int processedRowCount = 0;
        for (double y = minY + sampleSpacing * 0.5; y <= maxY + 1e-9; y += sampleSpacing) {
            if (shouldCancel && shouldCancel()) {
                return create_empty_polydata();
            }
            for (double x = minX + sampleSpacing * 0.5; x <= maxX + 1e-9; x += sampleSpacing) {
                const QPointF samplePoint(x, y);
                if (!point_in_polygon_xy(samplePoint, regularizedLoop2D)) {
                    continue;
                }

                int containingTriangleIdx = -1;
                for (std::size_t triIdx = 0; triIdx < bottomTriangles.size(); ++triIdx) {
                    const auto& triangle = bottomTriangles[triIdx];
                    const QPointF& a = bottomVertices[static_cast<std::size_t>(triangle[0])].xy;
                    const QPointF& b = bottomVertices[static_cast<std::size_t>(triangle[1])].xy;
                    const QPointF& c = bottomVertices[static_cast<std::size_t>(triangle[2])].xy;
                    if (point_strictly_inside_triangle_2d(samplePoint, a, b, c, minDistanceSqToEdge)) {
                        containingTriangleIdx = static_cast<int>(triIdx);
                        break;
                    }
                }

                if (containingTriangleIdx < 0) {
                    continue;
                }

                const int newVertexIdx = static_cast<int>(bottomVertices.size());
                const float bottomZ = firstHitBelow(samplePoint, shaftTopZ);
                bottomVertices.push_back(BottomVertex{samplePoint, bottomZ, false});
                ++localDiagnostics.totalBottomSampleCount;
                if (bottomZ > groundZ + kLowerModelHitEpsilonMm) {
                    ++localDiagnostics.lowerModelHitSampleCount;
                    localDiagnostics.maxLowerModelHitHeightMm = std::max(
                        localDiagnostics.maxLowerModelHitHeightMm,
                        static_cast<double>(bottomZ - groundZ));
                }

                const auto triangle = bottomTriangles[static_cast<std::size_t>(containingTriangleIdx)];
                bottomTriangles[static_cast<std::size_t>(containingTriangleIdx)] =
                    {triangle[0], triangle[1], newVertexIdx};
                bottomTriangles.push_back({triangle[1], triangle[2], newVertexIdx});
                bottomTriangles.push_back({triangle[2], triangle[0], newVertexIdx});
            }
            ++processedRowCount;
            if (progressCallback) {
                const float rowFraction =
                    static_cast<float>(processedRowCount) / static_cast<float>(estimatedRowCount);
                const float loopProgress =
                    loopStartProgress + (loopEndProgress - loopStartProgress) * std::clamp(rowFraction, 0.0f, 1.0f);
                progressCallback(loopProgress, QStringLiteral("正在采样支撑区域..."));
            }
        }

        std::vector<vtkIdType> shaftTopIds;
        std::vector<vtkIdType> shaftBottomIds;
        shaftTopIds.reserve(bottomVertices.size());
        shaftBottomIds.reserve(bottomVertices.size());
        for (const BottomVertex& vertex : bottomVertices) {
            shaftTopIds.push_back(points->InsertNextPoint(vertex.xy.x(), vertex.xy.y(), shaftTopZ));
            shaftBottomIds.push_back(points->InsertNextPoint(vertex.xy.x(), vertex.xy.y(), vertex.z));
        }

        for (const auto& triangle : bottomTriangles) {
            auto bottomTriangleCell = vtkSmartPointer<vtkTriangle>::New();
            bottomTriangleCell->GetPointIds()->SetId(0, shaftBottomIds[static_cast<std::size_t>(triangle[0])]);
            bottomTriangleCell->GetPointIds()->SetId(1, shaftBottomIds[static_cast<std::size_t>(triangle[2])]);
            bottomTriangleCell->GetPointIds()->SetId(2, shaftBottomIds[static_cast<std::size_t>(triangle[1])]);
            polys->InsertNextCell(bottomTriangleCell);
        }

        for (std::size_t i = 0; i < regularizedLoop2D.size(); ++i) {
            const std::size_t nextIdx = (i + 1) % regularizedLoop2D.size();
            const vtkIdType topI = shaftTopIds[i];
            const vtkIdType topJ = shaftTopIds[nextIdx];
            const vtkIdType bottomI = shaftBottomIds[i];
            const vtkIdType bottomJ = shaftBottomIds[nextIdx];

            auto sideTriA = vtkSmartPointer<vtkTriangle>::New();
            sideTriA->GetPointIds()->SetId(0, topI);
            sideTriA->GetPointIds()->SetId(1, topJ);
            sideTriA->GetPointIds()->SetId(2, bottomJ);
            polys->InsertNextCell(sideTriA);

            auto sideTriB = vtkSmartPointer<vtkTriangle>::New();
            sideTriB->GetPointIds()->SetId(0, topI);
            sideTriB->GetPointIds()->SetId(1, bottomJ);
            sideTriB->GetPointIds()->SetId(2, bottomI);
            polys->InsertNextCell(sideTriB);
        }

        ++processedLoopCount;
        if (progressCallback) {
            progressCallback(loopEndProgress, QStringLiteral("正在构建支撑区域..."));
        }
    }

    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(polys);
    if (progressCallback) {
        progressCallback(0.88f, QStringLiteral("支撑区域几何已完成"));
    }
    if (diagnostics) {
        *diagnostics = localDiagnostics;
    }
    return polyData;
}

vtkSmartPointer<vtkPolyData> ManualSupportHandler::buildContactSurfacePolyDataForTask(
    const ManualSupportOrcaScaffold::SelectedSurfaceMesh& surfaceMesh,
    const std::function<void(float, const QString&)>& progressCallback,
    const std::function<bool()>& shouldCancel) {
    if (shouldCancel && shouldCancel()) {
        return create_empty_polydata();
    }
    if (surfaceMesh.strictTriangles.empty() || surfaceMesh.vertices.empty()) {
        return create_empty_polydata();
    }

    if (progressCallback) {
        progressCallback(0.9f, QStringLiteral("正在生成接触面..."));
    }
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto polys = vtkSmartPointer<vtkCellArray>::New();

    std::vector<vtkIdType> pointIds(surfaceMesh.vertices.size(), -1);
    for (std::size_t i = 0; i < surfaceMesh.vertices.size(); ++i) {
        const QVector3D& vertex = surfaceMesh.vertices[i];
        pointIds[i] = points->InsertNextPoint(vertex.x(), vertex.y(), vertex.z() + kContactOverlayLiftMm);
    }

    const int triangleCount = std::max(static_cast<int>(surfaceMesh.strictTriangles.size()), 1);
    int processedTriangleCount = 0;
    const int progressStep = std::max(1, triangleCount / 20);
    for (const auto& triangle : surfaceMesh.strictTriangles) {
        if (shouldCancel && shouldCancel()) {
            return create_empty_polydata();
        }
        if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
            triangle[0] >= static_cast<int>(pointIds.size()) ||
            triangle[1] >= static_cast<int>(pointIds.size()) ||
            triangle[2] >= static_cast<int>(pointIds.size())) {
            continue;
        }

        auto vtkTriangleCell = vtkSmartPointer<vtkTriangle>::New();
        vtkTriangleCell->GetPointIds()->SetId(0, pointIds[static_cast<std::size_t>(triangle[0])]);
        vtkTriangleCell->GetPointIds()->SetId(1, pointIds[static_cast<std::size_t>(triangle[1])]);
        vtkTriangleCell->GetPointIds()->SetId(2, pointIds[static_cast<std::size_t>(triangle[2])]);
        polys->InsertNextCell(vtkTriangleCell);

        ++processedTriangleCount;
        if (progressCallback &&
            (processedTriangleCount == triangleCount ||
             processedTriangleCount % progressStep == 0)) {
            const float progress = 0.9f + 0.08f *
                (static_cast<float>(processedTriangleCount) / static_cast<float>(triangleCount));
            progressCallback(progress, QStringLiteral("正在生成接触面..."));
        }
    }

    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(polys);
    return polyData;
}

void ManualSupportHandler::syncContactDiagnosticsToBridge() const {
    if (auto* bridge = SlicingHandlerBridge::instance()) {
        bridge->setManualSupportContactDiagnostics(
            m_contactDiagnostics.hasContactPatch,
            m_contactDiagnostics.contactTriangleCount,
            m_contactDiagnostics.lowerModelHitSampleCount,
            m_contactDiagnostics.totalBottomSampleCount,
            m_contactDiagnostics.maxLowerModelHitHeightMm);
    }
}

void ManualSupportHandler::queueSupportRegionComputation(bool committed) {
    SupportRegionComputationRequest request;
    request.generation = ++m_supportRegionRequestSequence;
    request.sourceRevision = m_manualSupportDB
        ? m_manualSupportDB->getRevision() : 0;
    request.committed = committed;
    request.groundZ = resolvePrintBedTopZ();
    request.brushShape = m_brushShape;
    request.brushSizeMm = m_brushSizeMm;
    request.densityPercent = m_densityPercent;
    request.surfaceMesh = m_orcaScaffold.collect_selected_surface_mesh();
    request.meshRayQuery = m_meshRayQuery;

    m_latestSupportRegionRequestGeneration = request.generation;
    if (m_supportRegionGenerationToken) {
        m_supportRegionGenerationToken->store(request.generation, std::memory_order_relaxed);
    }

    if (m_supportRegionTaskActive) {
        m_pendingSupportRegionRequest = std::move(request);
        m_hasPendingSupportRegionRequest = true;
        LOG_DEBUG("ManualSupportHandler: queued pending support-region compute generation={} committed={}",
                  m_pendingSupportRegionRequest.generation,
                  m_pendingSupportRegionRequest.committed ? "true" : "false");
        return;
    }

    startSupportRegionComputation(request);
}

void ManualSupportHandler::startSupportRegionComputation(const SupportRegionComputationRequest& request) {
    m_supportRegionTaskActive = true;
    m_runningSupportRegionGeneration = request.generation;
    m_supportRegionTaskId = TaskStateNotifier::instance()->beginTask(
        QString("manual_support_region_%1").arg(request.generation),
        request.committed
            ? QStringLiteral("正在计算最终支撑区域...")
            : QStringLiteral("正在计算支撑区域..."));
    TaskStateNotifier::instance()->updateProgress(m_supportRegionTaskId, 0.05f);

    const QString taskId = m_supportRegionTaskId;
    QPointer<ManualSupportHandler> that(this);
    auto progressState = std::make_shared<QPair<int, QString>>(-1, QString());
    const std::shared_ptr<std::atomic<quint64>> generationToken = m_supportRegionGenerationToken;
    const auto shouldCancel = [generationToken, generation = request.generation]() {
        return generationToken &&
               generationToken->load(std::memory_order_relaxed) != generation;
    };

    const auto reportProgress = [that, taskId, generation = request.generation, generationToken, progressState](
                                    float progress, const QString& description) {
        if (generationToken &&
            generationToken->load(std::memory_order_relaxed) != generation) {
            return;
        }

        const int progressBucket = std::clamp(static_cast<int>(std::round(progress * 100.0f)), 0, 100);
        const QString normalizedDescription = description;
        if (progressState->first == progressBucket &&
            progressState->second == normalizedDescription) {
            return;
        }
        progressState->first = progressBucket;
        progressState->second = normalizedDescription;

        QMetaObject::invokeMethod(qApp, [that, taskId, generation, progress, description]() {
            if (!that ||
                that->m_runningSupportRegionGeneration != generation ||
                that->m_supportRegionTaskId != taskId) {
                return;
            }
            TaskStateNotifier::instance()->updateProgress(taskId, progress);
            if (!description.isEmpty()) {
                TaskStateNotifier::instance()->updateDescription(taskId, description);
            }
        }, Qt::QueuedConnection);
    };

    (void)QtConcurrent::run([that, request, taskId, reportProgress, shouldCancel]() mutable {
        SupportRegionComputationResult result;
        result.generation = request.generation;
        result.sourceRevision = request.sourceRevision;
        result.committed = request.committed;

        result.candidatePolyData = ManualSupportHandler::buildProjectedSupportCandidatePolyDataForTask(
            request.surfaceMesh,
            request.meshRayQuery.get(),
            request.groundZ,
            request.brushShape,
            request.brushSizeMm,
            request.densityPercent,
            &result.diagnostics,
            reportProgress,
            shouldCancel);

        result.contactPolyData =
            ManualSupportHandler::buildContactSurfacePolyDataForTask(
                request.surfaceMesh,
                reportProgress,
                shouldCancel);

        QMetaObject::invokeMethod(qApp, [that, result = std::move(result)]() mutable {
            if (that) {
                that->finishSupportRegionComputation(result);
            }
        }, Qt::QueuedConnection);
    });
}

void ManualSupportHandler::finishSupportRegionComputation(const SupportRegionComputationResult& result) {
    const bool finishesRunningTask =
        m_supportRegionTaskActive && result.generation == m_runningSupportRegionGeneration;

    if (finishesRunningTask) {
        if (!m_supportRegionTaskId.isEmpty()) {
            TaskStateNotifier::instance()->updateProgress(m_supportRegionTaskId, 1.0f);
            TaskStateNotifier::instance()->updateDescription(m_supportRegionTaskId, QStringLiteral("支撑区域计算完成"));
            TaskStateNotifier::instance()->endTask(m_supportRegionTaskId);
            m_supportRegionTaskId.clear();
        }
        m_supportRegionTaskActive = false;
        m_runningSupportRegionGeneration = 0;
    }

    const bool shouldApply =
        m_state != State::Inactive &&
        result.generation == m_latestSupportRegionRequestGeneration;

    if (shouldApply) {
        if (!result.committed && m_debugActor) {
            m_debugActor->setGeometry(result.candidatePolyData ? result.candidatePolyData : create_empty_polydata());
        }
        if (m_debugActorContact) {
            m_debugActorContact->setGeometry(result.contactPolyData ? result.contactPolyData : create_empty_polydata());
        }

        m_contactDiagnostics = result.diagnostics;
        syncContactDiagnosticsToBridge();

        if (result.committed) {
            if (auto* settingsBridge = SliceSettingsBridge::instance()) {
                settingsBridge->markNeedsReslice();
            }

            bool hasCommittedMesh = false;
            int pointCount = 0;
            int polygonCount = 0;
            if (result.candidatePolyData &&
                result.candidatePolyData->GetNumberOfPoints() > 0 &&
                result.candidatePolyData->GetNumberOfPolys() > 0) {
                const auto geometry = indexed_triangle_mesh_from_poly_data(
                    result.candidatePolyData);
                hasCommittedMesh = m_manualSupportDB && geometry &&
                    m_manualSupportDB->setGeneratedGeometry(
                        geometry, result.sourceRevision);
                pointCount = static_cast<int>(result.candidatePolyData->GetNumberOfPoints());
                polygonCount = static_cast<int>(result.candidatePolyData->GetNumberOfPolys());
            } else if (m_manualSupportDB &&
                       m_manualSupportDB->getRevision() == result.sourceRevision) {
                m_manualSupportDB->invalidateGeneratedGeometry();
            }

            emit committedSupportMeshReady(
                QString::fromStdString(m_targetModelId.toString()),
                hasCommittedMesh,
                pointCount,
                polygonCount);
        }

        if (result.committed && m_debugActor) {
            m_debugActor->setGeometry(create_empty_polydata());
        }

        applyDebugVisibility();
    } else {
        LOG_DEBUG("ManualSupportHandler: dropped stale support-region result generation={} latest={}",
                  result.generation,
                  m_latestSupportRegionRequestGeneration);
    }

    maybeStartPendingSupportRegionComputation();
}

void ManualSupportHandler::maybeStartPendingSupportRegionComputation() {
    if (m_supportRegionTaskActive || !m_hasPendingSupportRegionRequest) {
        return;
    }

    const SupportRegionComputationRequest request = m_pendingSupportRegionRequest;
    m_hasPendingSupportRegionRequest = false;
    startSupportRegionComputation(request);
}

void ManualSupportHandler::cancelSupportRegionComputation() {
    ++m_supportRegionRequestSequence;
    m_latestSupportRegionRequestGeneration = m_supportRegionRequestSequence;
    if (m_supportRegionGenerationToken) {
        m_supportRegionGenerationToken->store(m_latestSupportRegionRequestGeneration, std::memory_order_relaxed);
    }
    m_hasPendingSupportRegionRequest = false;

    if (!m_supportRegionTaskId.isEmpty()) {
        TaskStateNotifier::instance()->endTask(m_supportRegionTaskId);
        m_supportRegionTaskId.clear();
    }

    m_supportRegionTaskActive = false;
    m_runningSupportRegionGeneration = 0;
}

bool ManualSupportHandler::applyDebugVisibilityCommand(const QVariantMap& visibility,
                                                       QString* errorMessage) {
    Q_UNUSED(errorMessage)

    applyDebugVisibilityFromParams(visibility);
    ensureDebugActorCreated();
    applyDebugVisibility();
    m_state = State::Ready;

    if (auto* bridge = SlicingHandlerBridge::instance()) {
        bridge->setManualDebugShowActor(m_debugActorVisible);
        bridge->setManualDebugShowStrokePolyline(m_showStrokePolyline);
        bridge->setManualDebugShowStrokePoints(m_showStrokePoints);
        bridge->setManualDebugShowPickNormals(m_showPickNormals);
        bridge->setManualDebugShowProjectedRing(m_showProjectedRing);
        bridge->setManualDebugShowMergedSelectionSurfaceOutline(m_showMergedSelectionSurfaceOutline);
        bridge->setManualDebugShowContactPatch(m_showContactPatch);
        bridge->setShowDebugData(m_debugActorVisible);
    }

    if (shouldComputeMergedSurfaceOutline() && m_mergedSurfaceOutlineDirty) {
        updateMergedSurfaceOutlineLayer(false);
    }

    return true;
}

bool ManualSupportHandler::clearStrokesCommand(QString* errorMessage) {
    Q_UNUSED(errorMessage)

    resetCurrentStroke();
    m_lastStampStartFacetId = -1;
    m_lastSelectedSourceFacetIds.clear();
    m_hasSelectionStateBeforeStroke = false;
    resetCommittedSupportMeshState();

    if (m_hasBaseSelectionState) {
        m_orcaScaffold.restore_state_snapshot(m_baseSelectionState);
        refreshCommittedSelectionVisualization();
        (void)persistSupportStateSnapshot(true, "Manual Support Clear");
    } else {
        clearDebugLayers();
    }

    m_state = State::Ready;
    return true;
}

bool ManualSupportHandler::commitStrokeCommand(QString* errorMessage) {
    if (!commitCurrentStroke(false)) {
        const QString error = "Current stroke is empty";
        if (errorMessage) {
            *errorMessage = error;
        }
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, error);
        }
        return false;
    }

    m_state = State::Ready;
    return true;
}

void ManualSupportHandler::applySettingsFromParams(const QVariantMap& params) {
    if (params.contains("paintMode")) {
        m_paintMode = params.value("paintMode").toString().toLower();
        m_orcaScaffold.set_active_paint_state(
            m_paintMode == "block" ? ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Blocker)
            : (m_paintMode == "erase" ? ManualSupportOrcaScaffold::NoLabel
                                        : ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Enforcer)));
    }
    if (params.contains("brushShape")) {
        m_brushShape = params.value("brushShape").toString().toLower();
    }
    if (params.contains("brushSizeMm")) {
        m_brushSizeMm = std::max(1.0, params.value("brushSizeMm").toDouble());
    }
    if (params.contains("densityPercent")) {
        m_densityPercent = std::clamp(params.value("densityPercent").toDouble(), 0.0, 100.0);
    }
    if (params.contains("smartFill")) {
        m_smartFill = params.value("smartFill").toBool();
    }
    if (params.contains("clipToOverhang")) {
        m_clipToOverhang = params.value("clipToOverhang").toBool();
    }
    if (params.contains("overhangThresholdDeg")) {
        m_overhangThresholdDeg = std::clamp(params.value("overhangThresholdDeg").toInt(), 0, 90);
    }
    if (params.contains("smartFillAngleDeg")) {
        m_smartFillAngleDeg = std::clamp(params.value("smartFillAngleDeg").toDouble(), 0.0, 90.0);
    }
    if (params.contains("gapArea")) {
        m_gapArea = std::clamp(params.value("gapArea").toDouble(), 0.0, 5.0);
    }
    if (params.contains("sectionViewRatio")) {
        m_sectionViewRatio = std::clamp(params.value("sectionViewRatio").toDouble(), 0.0, 1.0);
    }
}

void ManualSupportHandler::applyDebugVisibilityFromParams(const QVariantMap& params) {
    if (params.contains("showActor")) {
        m_debugActorVisible = params.value("showActor").toBool();
    }
    if (params.contains("showStrokePolyline")) {
        m_showStrokePolyline = params.value("showStrokePolyline").toBool();
    }
    if (params.contains("showStrokePoints")) {
        m_showStrokePoints = params.value("showStrokePoints").toBool();
    }
    if (params.contains("showPickNormals")) {
        m_showPickNormals = params.value("showPickNormals").toBool();
    }
    if (params.contains("showProjectedRing")) {
        m_showProjectedRing = params.value("showProjectedRing").toBool();
    }
    if (params.contains("showMergedSelectionSurfaceOutline")) {
        m_showMergedSelectionSurfaceOutline = params.value("showMergedSelectionSurfaceOutline").toBool();
    }
    if (params.contains("showContactPatch")) {
        m_showContactPatch = params.value("showContactPatch").toBool();
    }
}

std::uint64_t ManualSupportHandler::requestToolPick(
    const QPoint& screenPos,
    Qt::KeyboardModifiers modifiers,
    ToolPickPhase phase,
    PickDelivery delivery) {
    const auto requestId = interactionPickService().requestForView(
        m_interactionViewId, screenPos,
        PickChannel::ToolStroke,
        GPlatform::Rendering::PickDetail::Primitive,
        delivery);
    if (requestId == 0) {
        return 0;
    }

    if (phase == ToolPickPhase::Move && m_latestMovePickRequestId != 0) {
        m_pendingToolPicks.erase(m_latestMovePickRequestId);
    }
    m_pendingToolPicks[requestId] = PendingToolPick{
        phase, screenPos, modifiers, m_gestureSequence};
    if (phase == ToolPickPhase::Move) {
        m_latestMovePickRequestId = requestId;
    }
    return requestId;
}

void ManualSupportHandler::handleToolPickCompleted(
    const PickSnapshot& snapshot) {
    const auto pendingIt = m_pendingToolPicks.find(snapshot.requestId);
    if (pendingIt == m_pendingToolPicks.end()) {
        return;
    }
    const PendingToolPick pending = pendingIt->second;
    m_pendingToolPicks.erase(pendingIt);
    if (snapshot.requestId == m_pressPickRequestId) {
        m_pressPickRequestId = 0;
    }
    if (snapshot.requestId == m_latestMovePickRequestId) {
        m_latestMovePickRequestId = 0;
    }
    if (pending.gesture != m_gestureSequence || m_state != State::Drawing) {
        return;
    }

    const bool hit =
        snapshot.status == GPlatform::Rendering::PickStatus::Hit;
    switch (pending.phase) {
    case ToolPickPhase::Press:
        if (!hit || !beginStrokeFromPick(
                        snapshot.result, pending.screenPos, pending.modifiers)) {
            abortPendingStroke();
            return;
        }
        if (m_deferredReleasePick) {
            const auto deferred = std::move(*m_deferredReleasePick);
            m_deferredReleasePick.reset();
            finishStrokeFromPick(
                deferred.second.status == GPlatform::Rendering::PickStatus::Hit
                    ? deferred.second.result
                    : empty_pick_result(),
                deferred.first.screenPos,
                deferred.first.modifiers);
        }
        break;
    case ToolPickPhase::Move:
        if (m_scaffoldPointerDown && snapshot.isReusable()) {
            moveStrokeFromPick(snapshot.result,
                               pending.screenPos,
                               pending.modifiers);
        }
        break;
    case ToolPickPhase::Release:
        if (!m_scaffoldPointerDown && m_pressPickRequestId != 0) {
            m_deferredReleasePick = std::make_pair(pending, snapshot);
            return;
        }
        finishStrokeFromPick(
            hit ? snapshot.result : empty_pick_result(),
            pending.screenPos,
            pending.modifiers);
        break;
    }
}

bool ManualSupportHandler::beginStrokeFromPick(
    const PickResult& pick,
    const QPoint& screenPos,
    Qt::KeyboardModifiers modifiers) {
    if (!appendPointFromPickResult(pick, screenPos, false)) {
        return false;
    }

    ManualSupportOrcaScaffold::MouseInput input;
    input.screenPos = screenPos;
    input.shiftDown = modifiers.testFlag(Qt::ShiftModifier);
    input.altDown = modifiers.testFlag(Qt::AltModifier);
    input.controlDown = modifiers.testFlag(Qt::ControlModifier);
    float projectedRadiusWorld = 0.0f;
    if (tryComputeProjectedBrushRadiusWorld(screenPos, &projectedRadiusWorld)) {
        m_orcaScaffold.set_cursor_radius(projectedRadiusWorld);
    }
    const bool selected = m_orcaScaffold.on_mouse_left_down(input);
    m_scaffoldPointerDown = true;
    LOG_DEBUG("ManualSupportHandler::beginStrokeFromPick selected={} raycastFacet={}",
              selected ? "true" : "false", m_orcaScaffold.raycast_cache().facetId);
    if (selected) {
        refreshSelectionDebugSnapshot();
        updateDebugSelectionLayer(false);
    }
    return true;
}

void ManualSupportHandler::moveStrokeFromPick(
    const PickResult& pick,
    const QPoint& screenPos,
    Qt::KeyboardModifiers modifiers) {
    if (!appendPointFromPickResult(pick, screenPos, true)) {
        LOG_DEBUG("ManualSupportHandler::moveStrokeFromPick sample skipped at ({}, {})",
                  screenPos.x(), screenPos.y());
    }

    ManualSupportOrcaScaffold::MouseInput input;
    input.screenPos = screenPos;
    input.shiftDown = modifiers.testFlag(Qt::ShiftModifier);
    input.altDown = modifiers.testFlag(Qt::AltModifier);
    input.controlDown = modifiers.testFlag(Qt::ControlModifier);
    float projectedRadiusWorld = 0.0f;
    if (tryComputeProjectedBrushRadiusWorld(screenPos, &projectedRadiusWorld)) {
        m_orcaScaffold.set_cursor_radius(projectedRadiusWorld);
    }
    m_orcaScaffold.on_mouse_dragging(input);
}

void ManualSupportHandler::finishStrokeFromPick(
    const PickResult& pick,
    const QPoint& screenPos,
    Qt::KeyboardModifiers modifiers) {
    if (!m_scaffoldPointerDown) {
        abortPendingStroke();
        return;
    }

    (void)appendPointFromPickResult(pick, screenPos, true);
    ManualSupportOrcaScaffold::MouseInput input;
    input.screenPos = screenPos;
    input.shiftDown = modifiers.testFlag(Qt::ShiftModifier);
    input.altDown = modifiers.testFlag(Qt::AltModifier);
    input.controlDown = modifiers.testFlag(Qt::ControlModifier);
    const bool committedSelection = m_orcaScaffold.on_mouse_left_up(input);
    LOG_DEBUG("ManualSupportHandler::finishStrokeFromPick committedSelection={} raycastFacet={}",
              committedSelection ? "true" : "false", m_orcaScaffold.raycast_cache().facetId);
    if (committedSelection) {
        const auto currentSelectionState = m_orcaScaffold.snapshot_state();
        if (m_hasSelectionStateBeforeStroke &&
            !selection_state_equals(m_selectionStateBeforeStroke, currentSelectionState)) {
            (void)persistSupportStateSnapshot(true, "Manual Support Stroke");
        } else {
            LOG_DEBUG("ManualSupportHandler: selection unchanged, snapshot not persisted");
        }
        refreshSelectionDebugSnapshot();
        updateDebugSelectionLayer(true);
    }
    m_hasSelectionStateBeforeStroke = false;
    m_scaffoldPointerDown = false;

    const bool strokeCommitted = commitCurrentStroke(false);
    LOG_DEBUG("ManualSupportHandler::finishStrokeFromPick strokeCommitted={}",
              strokeCommitted ? "true" : "false");

    ++m_gestureSequence;
    cancelPendingToolPicks();
    m_state = State::Ready;
}

void ManualSupportHandler::abortPendingStroke() {
    ++m_gestureSequence;
    cancelPendingToolPicks();
    m_isPointerDown = false;
    m_scaffoldPointerDown = false;
    m_hasSelectionStateBeforeStroke = false;
    resetCurrentStroke();
    if (m_state != State::Inactive) {
        m_state = State::Ready;
    }
}

void ManualSupportHandler::cancelPendingToolPicks() {
    for (const auto& [requestId, pending] : m_pendingToolPicks) {
        Q_UNUSED(pending)
        interactionPickService().cancel(requestId);
    }
    m_pendingToolPicks.clear();
    m_deferredReleasePick.reset();
    m_pressPickRequestId = 0;
    m_latestMovePickRequestId = 0;
    if (m_previewPickRequestId != 0) {
        interactionPickService().cancel(m_previewPickRequestId);
        m_previewPickRequestId = 0;
    }
}

bool ManualSupportHandler::appendPointFromPickResult(
    const PickResult& pick,
    const QPoint& expectedScreenPos,
    bool enforceDistanceCheck) {
    if (!pick.objectId.isValid() || !pick.partId ||
        !pick.partId->isValid() || !pick.primitiveIndex ||
        !pick.worldPosition) {
        LOG_DEBUG("ManualSupportHandler::appendPointFromPickResult rejected: dbValid={} primitive={} world={} expected=({}, {})",
                  pick.objectId.isValid() ? "true" : "false",
                  pick.primitiveIndex ? "true" : "false",
                  pick.worldPosition ? "true" : "false",
                  expectedScreenPos.x(), expectedScreenPos.y());
        m_orcaScaffold.update_external_hit(ManualSupportOrcaScaffold::ExternalHit{});
        return false;
    }

    if (m_targetModelId.isValid() && pick.objectId != m_targetModelId) {
        LOG_DEBUG("ManualSupportHandler::appendPointFromPick rejected: target model mismatch target={} picked={}",
                  m_targetModelId.getValue(), pick.objectId.getValue());
        m_orcaScaffold.update_external_hit(ManualSupportOrcaScaffold::ExternalHit{});
        return false;
    }

    if (auto* docManager = DocumentManager::instance()) {
        auto dbObject = docManager->getDBInstance(pick.objectId);
        if (!dbObject || !std::dynamic_pointer_cast<ModelInstanceDB>(dbObject)) {
            LOG_DEBUG("ManualSupportHandler::appendPointFromPick rejected: picked dbId={} is not ModelInstanceDB",
                      pick.objectId.getValue());
            m_orcaScaffold.update_external_hit(ManualSupportOrcaScaffold::ExternalHit{});
            return false;
        }
    }

    if (enforceDistanceCheck && !m_currentStroke.empty()) {
        const int distance =
            (expectedScreenPos - m_lastSampleScreenPos).manhattanLength();
        if (distance < m_minSampleDistancePx) {
            LOG_DEBUG("ManualSupportHandler::appendPointFromPick filtered by min sample distance: distance={} threshold={}",
                      distance, m_minSampleDistancePx);
            return false;
        }
    }

    const auto mappedFacet = m_partPrimitiveToScaffoldFacet.find({
        pick.partId->getValue(), *pick.primitiveIndex});
    if (mappedFacet == m_partPrimitiveToScaffoldFacet.end()) {
        LOG_DEBUG(
            "ManualSupportHandler::appendPointFromPick rejected: part={} primitive={} has no scaffold facet",
            pick.partId->getValue(), *pick.primitiveIndex);
        m_orcaScaffold.update_external_hit(
            ManualSupportOrcaScaffold::ExternalHit{});
        return false;
    }

    StrokePoint point;
    point.screenPos = expectedScreenPos;
    point.dbId = pick.objectId;
    point.worldPos = *pick.worldPosition;
    point.cellId = mappedFacet->second;
    point.pointId = pick.vertexIndex
        ? static_cast<int>(*pick.vertexIndex)
        : -1;

    m_currentStroke.push_back(point);
    m_lastSampleScreenPos = expectedScreenPos;
    LOG_DEBUG("ManualSupportHandler::appendPointFromPick accepted: screen=({}, {}) dbId={} cellId={} pointId={} world=({:.3f},{:.3f},{:.3f}) strokePoints={}",
              expectedScreenPos.x(), expectedScreenPos.y(),
              pick.objectId.getValue(), point.cellId, point.pointId,
              point.worldPos.x, point.worldPos.y, point.worldPos.z,
              m_currentStroke.size());

    ManualSupportOrcaScaffold::ExternalHit hit;
    hit.valid = true;
    hit.screenPos = expectedScreenPos;
    hit.meshId = 0;
    hit.facetId = point.cellId;
    hit.worldHit = QVector3D(point.worldPos.x, point.worldPos.y, point.worldPos.z);

    QVector3D cameraPos(point.worldPos.x, point.worldPos.y, point.worldPos.z + 1000.0f);
    if (auto camera = CameraNavigationController::currentCamera()) {
        const Vector3 cam = camera->getPosition();
        cameraPos = QVector3D(cam.x, cam.y, cam.z);
    }
    hit.cameraPos = cameraPos;
    m_orcaScaffold.update_external_hit(hit);
    LOG_DEBUG("ManualSupportHandler::appendPointFromPick external hit updated: mesh={} facet={} screen=({}, {})",
              hit.meshId, hit.facetId, hit.screenPos.x(), hit.screenPos.y());
    updateDebugPickLayer(point, cameraPos);

    return true;
}

bool ManualSupportHandler::commitCurrentStroke(bool requireAtLeastTwoPoints) {
    if (m_currentStroke.empty()) {
        return false;
    }

    if (requireAtLeastTwoPoints && m_currentStroke.size() < 2) {
        LOG_DEBUG("ManualSupportHandler: drop short stroke with {} point(s)",
                  m_currentStroke.size());
        resetCurrentStroke();
        return false;
    }

    LOG_INFO("ManualSupportHandler: committed stroke points={}",
             m_currentStroke.size());
    resetCurrentStroke();
    return true;
}

void ManualSupportHandler::resetCurrentStroke() {
    m_currentStroke.clear();
    m_lastSampleScreenPos = QPoint();
}

void ManualSupportHandler::clearAllStrokes() {
    resetCurrentStroke();
    m_lastStampStartFacetId = -1;
    m_lastSelectedSourceFacetIds.clear();
    m_hasSelectionStateBeforeStroke = false;
    resetCommittedSupportMeshState();
    clearDebugLayers();
}

QVariantMap ManualSupportHandler::buildStateResult() const {
    QVariantMap result;
    result.insert("state", stateToString(m_state));
    result.insert("targetModelId", static_cast<qulonglong>(m_targetModelId.getValue()));
    result.insert("committedStrokeCount", 0);
    result.insert("currentStrokePointCount", static_cast<int>(m_currentStroke.size()));
    result.insert("committedPointCount", 0);
    result.insert("strokeSizes", QVariantList{});
    result.insert("paintSettings", QVariantMap{
        {"paintMode", m_paintMode},
        {"brushShape", m_brushShape},
        {"brushSizeMm", m_brushSizeMm},
        {"densityPercent", m_densityPercent},
        {"smartFill", m_smartFill},
        {"clipToOverhang", m_clipToOverhang},
        {"overhangThresholdDeg", m_overhangThresholdDeg},
        {"smartFillAngleDeg", m_smartFillAngleDeg},
        {"gapArea", m_gapArea},
        {"sectionViewRatio", m_sectionViewRatio},
    });
    result.insert("debugVisibility", QVariantMap{
        {"showActor", m_debugActorVisible},
        {"showStrokePolyline", m_showStrokePolyline},
        {"showStrokePoints", m_showStrokePoints},
        {"showPickNormals", m_showPickNormals},
        {"showProjectedRing", m_showProjectedRing},
        {"showMergedSelectionSurfaceOutline", m_showMergedSelectionSurfaceOutline},
        {"showContactPatch", m_showContactPatch},
        {"debugActorId", m_debugActor ? static_cast<qulonglong>(m_debugActor->getDBInstanceID().getValue()) : 0ull},
        {"debugPickActorId", m_debugActorPick ? static_cast<qulonglong>(m_debugActorPick->getDBInstanceID().getValue()) : 0ull},
        {"debugContactActorId", m_debugActorContact ? static_cast<qulonglong>(m_debugActorContact->getDBInstanceID().getValue()) : 0ull},
    });
    result.insert("manualSupportDbId",
                  m_manualSupportDB ? static_cast<qulonglong>(m_manualSupportDB->getDBInstanceID().getValue()) : 0ull);
    result.insert("debugSelection", QVariantMap{
        {"startFacetId", m_lastStampStartFacetId},
        {"selectedSourceFacetCount", static_cast<int>(m_lastSelectedSourceFacetIds.size())},
        {"selectedSourceFacetIds", toVariantList(m_lastSelectedSourceFacetIds)},
    });
    return result;
}

QString ManualSupportHandler::stateToString(State state) {
    switch (state) {
        case State::Inactive:
            return "inactive";
        case State::Ready:
            return "ready";
        case State::Drawing:
            return "drawing";
        default:
            return "unknown";
    }
}

DBInstanceID ManualSupportHandler::parseTargetModelId(const QVariant& rawModelId) {
    bool ok = false;
    qulonglong value = 0;

    if (rawModelId.isValid()) {
        value = rawModelId.toULongLong(&ok);
        if (!ok) {
            value = rawModelId.toString().trimmed().toULongLong(&ok);
        }
    }

    if (!ok || value == 0) {
        return DBInstanceID();
    }
    return DBInstanceID(static_cast<uint64_t>(value));
}

void ManualSupportHandler::ensureDebugActorCreated() {
    if (m_debugActor && m_debugActorPick &&
        m_debugActorContact && m_debugActorMergedOutline) {
        return;
    }

    TransientUpdateGuard transientUpdate;
    if (!m_debugActor) {
        m_debugActor = trans::TransDB::create<DebugActorDB>();
    }
    if (!m_debugActorPick) {
        m_debugActorPick = trans::TransDB::create<DebugActorDB>();
    }
    if (!m_debugActorContact) {
        m_debugActorContact = trans::TransDB::create<DebugActorDB>();
    }
    if (!m_debugActorMergedOutline) {
        m_debugActorMergedOutline = trans::TransDB::create<DebugActorDB>();
    }

    if (!m_debugActor || !m_debugActorPick ||
        !m_debugActorContact || !m_debugActorMergedOutline) {
        LOG_ERROR("ManualSupportHandler: failed to create transient debug actors");
        return;
    }

    const auto init_actor = [](const std::shared_ptr<DebugActorDB>& actor) {
        actor->setPickable(false);
        actor->setDragable(false);
        actor->setGeometry(create_empty_polydata());
    };
    init_actor(m_debugActor);
    init_actor(m_debugActorPick);
    init_actor(m_debugActorContact);
    init_actor(m_debugActorMergedOutline);

    configure_debug_actor_surface_material(m_debugActor, Vector3(0.45f, 0.82f, 1.0f), 0.45f);        // preview solid
    configure_debug_actor_line_material(m_debugActorPick, Vector3(0.2f, 0.9f, 1.0f), 1.0f, 2.5f); // pick
    configure_debug_actor_surface_material(m_debugActorContact, Vector3(1.0f, 0.45f, 0.15f), 0.8f); // contact patch
    configure_debug_actor_line_material(m_debugActorMergedOutline, Vector3(1.0f, 0.2f, 0.15f), 1.0f, 2.5f); // merged outline

    auto* bridge = SlicingHandlerBridge::instance();
    if (bridge) {
        bridge->setDebugActors({m_debugActorPick, m_debugActor, m_debugActorContact, m_debugActorMergedOutline});
    }

    LOG_INFO("ManualSupportHandler: transient debug actors created, pick={}, preview={}, contact={}, merged={}",
             m_debugActorPick->getDBInstanceID().getValue(),
             m_debugActor->getDBInstanceID().getValue(),
             m_debugActorContact->getDBInstanceID().getValue(),
             m_debugActorMergedOutline->getDBInstanceID().getValue());
}

void ManualSupportHandler::applyDebugVisibility() {
    const bool hasDebugChannelsEnabled =
        m_showStrokePolyline || m_showStrokePoints || m_showPickNormals ||
        m_showProjectedRing || m_showMergedSelectionSurfaceOutline ||
        m_showContactPatch;
    const bool baseVisible = m_debugActorVisible && hasDebugChannelsEnabled;

    const bool pickVisible = baseVisible && (m_showStrokePoints || m_showPickNormals || m_showProjectedRing);
    const bool previewVisible = baseVisible && m_showStrokePolyline;
    const bool contactVisible = baseVisible && m_showContactPatch && m_contactDiagnostics.hasContactPatch;

    int visibleCount = 0;
    if (m_debugActor) {
        m_debugActor->setVisible(previewVisible);
        if (previewVisible) {
            ++visibleCount;
        }
    }
    if (m_debugActorPick) {
        m_debugActorPick->setVisible(pickVisible);
        if (pickVisible) {
            ++visibleCount;
        }
    }
    if (m_debugActorContact) {
        m_debugActorContact->setVisible(contactVisible);
        if (contactVisible) {
            ++visibleCount;
        }
    }
    if (m_debugActorMergedOutline) {
        const bool mergedVisible = baseVisible && m_showMergedSelectionSurfaceOutline;
        m_debugActorMergedOutline->setVisible(mergedVisible);
        if (mergedVisible) {
            ++visibleCount;
        }
    }

    auto* bridge = SlicingHandlerBridge::instance();
    if (bridge) {
        bridge->updateDebugActorCount(visibleCount);
    }
}

void ManualSupportHandler::refreshSelectionDebugSnapshot() {
    m_lastStampStartFacetId = m_orcaScaffold.raycast_cache().facetId;

    auto leaves = m_orcaScaffold.collect_leaf_triangles();
    std::vector<int> sourceFacets;
    sourceFacets.reserve(leaves.size());
    for (const auto& tri : leaves) {
        if (tri.sourceTriangle >= 0) {
            sourceFacets.push_back(tri.sourceTriangle);
        }
    }
    std::sort(sourceFacets.begin(), sourceFacets.end());
    sourceFacets.erase(std::unique(sourceFacets.begin(), sourceFacets.end()), sourceFacets.end());
    m_lastSelectedSourceFacetIds = std::move(sourceFacets);
    LOG_DEBUG("ManualSupportHandler::refreshSelectionDebugSnapshot startFacet={} selectedSourceFacetCount={}",
              m_lastStampStartFacetId, m_lastSelectedSourceFacetIds.size());
}

QVariantList ManualSupportHandler::toVariantList(const std::vector<int>& values) {
    QVariantList out;
    out.reserve(static_cast<int>(values.size()));
    for (int v : values) {
        out.push_back(v);
    }
    return out;
}

void ManualSupportHandler::updateDebugPickLayer(const StrokePoint& point, const QVector3D& cameraPos) {
    if (!m_debugActorPick) {
        return;
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();

    const QVector3D hit(point.worldPos.x, point.worldPos.y, point.worldPos.z);

    if (m_showStrokePoints) {
        vtkIdType camId = points->InsertNextPoint(cameraPos.x(), cameraPos.y(), cameraPos.z());
        vtkIdType hitId = points->InsertNextPoint(hit.x(), hit.y(), hit.z());
        auto rayLine = vtkSmartPointer<vtkLine>::New();
        rayLine->GetPointIds()->SetId(0, camId);
        rayLine->GetPointIds()->SetId(1, hitId);
        lines->InsertNextCell(rayLine);
    }

    float ringRadiusWorld = 0.0f;
    if (m_showProjectedRing &&
        tryComputeProjectedBrushRadiusWorld(point.screenPos, &ringRadiusWorld) &&
        ringRadiusWorld > 1e-6f) {
        const QVector3D axisDir = safe_normalized(hit - cameraPos, QVector3D(0.0f, 0.0f, 1.0f));
        QVector3D tangent = QVector3D::crossProduct(axisDir, QVector3D(0.0f, 0.0f, 1.0f));
        if (tangent.lengthSquared() <= 1e-8f) {
            tangent = QVector3D::crossProduct(axisDir, QVector3D(1.0f, 0.0f, 0.0f));
        }
        tangent = safe_normalized(tangent, QVector3D(1.0f, 0.0f, 0.0f));
        const QVector3D bitangent =
            safe_normalized(QVector3D::crossProduct(axisDir, tangent), QVector3D(0.0f, 1.0f, 0.0f));

        std::vector<QVector3D> outlinePoints;
        const QString shape = m_brushShape.trimmed().toLower();
        if (shape == QStringLiteral("square") || shape == QStringLiteral("rect") || shape == QStringLiteral("box")) {
            outlinePoints = {
                hit - tangent * ringRadiusWorld - bitangent * ringRadiusWorld,
                hit + tangent * ringRadiusWorld - bitangent * ringRadiusWorld,
                hit + tangent * ringRadiusWorld + bitangent * ringRadiusWorld,
                hit - tangent * ringRadiusWorld + bitangent * ringRadiusWorld
            };
        } else if (shape == QStringLiteral("triangle") || shape == QStringLiteral("tri")) {
            outlinePoints = {
                hit - bitangent * ringRadiusWorld,
                hit + tangent * (0.8660254f * ringRadiusWorld) + bitangent * (0.5f * ringRadiusWorld),
                hit - tangent * (0.8660254f * ringRadiusWorld) + bitangent * (0.5f * ringRadiusWorld)
            };
        } else {
            outlinePoints.reserve(kDebugRingSegmentCount);
            for (int i = 0; i < kDebugRingSegmentCount; ++i) {
                const double angle = (2.0 * M_PI * static_cast<double>(i)) /
                                     static_cast<double>(kDebugRingSegmentCount);
                outlinePoints.push_back(
                    hit +
                    tangent * static_cast<float>(std::cos(angle) * static_cast<double>(ringRadiusWorld)) +
                    bitangent * static_cast<float>(std::sin(angle) * static_cast<double>(ringRadiusWorld)));
            }
        }

        vtkIdType firstRingId = -1;
        vtkIdType prevRingId = -1;
        for (const QVector3D& outlinePoint : outlinePoints) {
            vtkIdType ringId = points->InsertNextPoint(outlinePoint.x(), outlinePoint.y(), outlinePoint.z());
            if (firstRingId < 0) {
                firstRingId = ringId;
            } else {
                auto ringSegment = vtkSmartPointer<vtkLine>::New();
                ringSegment->GetPointIds()->SetId(0, prevRingId);
                ringSegment->GetPointIds()->SetId(1, ringId);
                lines->InsertNextCell(ringSegment);
            }
            prevRingId = ringId;
        }

        if (firstRingId >= 0 && prevRingId >= 0 && firstRingId != prevRingId) {
            auto closingSegment = vtkSmartPointer<vtkLine>::New();
            closingSegment->GetPointIds()->SetId(0, prevRingId);
            closingSegment->GetPointIds()->SetId(1, firstRingId);
            lines->InsertNextCell(closingSegment);
        }
    }

    if (m_showPickNormals &&
        point.cellId >= 0 &&
        point.cellId < static_cast<int>(m_targetFacetNormals.size())) {
        const QVector3D n = m_targetFacetNormals[static_cast<size_t>(point.cellId)];
        QVector3D normal = n.lengthSquared() > 1e-12f ? n.normalized() : QVector3D(0.0f, 0.0f, 1.0f);
        const float normalLen = static_cast<float>(std::max(1.0, m_brushSizeMm * 0.5));
        const QVector3D normalEnd = hit + normal * normalLen;
        vtkIdType n0 = points->InsertNextPoint(hit.x(), hit.y(), hit.z());
        vtkIdType n1 = points->InsertNextPoint(normalEnd.x(), normalEnd.y(), normalEnd.z());
        auto normalLine = vtkSmartPointer<vtkLine>::New();
        normalLine->GetPointIds()->SetId(0, n0);
        normalLine->GetPointIds()->SetId(1, n1);
        lines->InsertNextCell(normalLine);
    }

    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetLines(lines);
    m_debugActorPick->setGeometry(polyData);
    LOG_DEBUG("ManualSupportHandler::updateDebugPickLayer lines={} cellId={}",
              lines->GetNumberOfCells(), point.cellId);
}

void ManualSupportHandler::updateDebugSelectionLayer(bool committed) {
    auto leaves = m_orcaScaffold.collect_leaf_triangles();
    if (!committed && !m_debugActor) {
        LOG_WARN("ManualSupportHandler::updateDebugSelectionLayer preview actor missing");
        return;
    }

    size_t enforcerCount = 0;
    size_t blockerCount = 0;
    for (const auto& tri : leaves) {
        if (tri.state == ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Enforcer)) {
            ++enforcerCount;
        } else if (tri.state == ManualSupportOrcaScaffold::label(ManualSupportOrcaScaffold::PaintState::Blocker)) {
            ++blockerCount;
        }
    }

    queueSupportRegionComputation(committed);
    LOG_DEBUG("ManualSupportHandler::updateDebugSelectionLayer committed={} leaves={} enforcer={} blocker={}",
              committed ? "true" : "false",
              leaves.size(),
              enforcerCount,
              blockerCount);
    if (leaves.empty()) {
        LOG_WARN("ManualSupportHandler::updateDebugSelectionLayer leaves is empty - no triangles to display");
    }

    if (committed) {
        if (m_debugActor) {
            m_debugActor->setGeometry(create_empty_polydata());
        }
    }

    updateMergedSurfaceOutlineLayer(committed);
}

bool ManualSupportHandler::shouldComputeMergedSurfaceOutline() const {
    return m_debugActorVisible && m_showMergedSelectionSurfaceOutline;
}

void ManualSupportHandler::updateMergedSurfaceOutlineLayer(bool committed) {
    Q_UNUSED(committed)

    if (!m_debugActorMergedOutline) {
        return;
    }
    if (!shouldComputeMergedSurfaceOutline()) {
        m_mergedSurfaceOutlineDirty = true;
        return;
    }

    const auto loops = m_orcaScaffold.collect_selected_outer_boundary_loops();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();

    int renderedLoopCount = 0;
    int renderedSegmentCount = 0;
    for (const auto& loop : loops) {
        if (loop.size() < 3) {
            continue;
        }

        std::vector<vtkIdType> pointIds;
        pointIds.reserve(loop.size());
        for (const QVector3D& point : loop) {
            pointIds.push_back(points->InsertNextPoint(point.x(), point.y(), point.z()));
        }

        for (std::size_t i = 0; i < pointIds.size(); ++i) {
            const vtkIdType id0 = pointIds[i];
            const vtkIdType id1 = pointIds[(i + 1) % pointIds.size()];
            auto line = vtkSmartPointer<vtkLine>::New();
            line->GetPointIds()->SetId(0, id0);
            line->GetPointIds()->SetId(1, id1);
            lines->InsertNextCell(line);
            ++renderedSegmentCount;
        }
        ++renderedLoopCount;
    }

    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetLines(lines);
    m_debugActorMergedOutline->setGeometry(polyData);
    m_mergedSurfaceOutlineDirty = false;
    LOG_DEBUG("ManualSupportHandler::updateMergedSurfaceOutlineLayer loops={} segments={}",
              renderedLoopCount,
              renderedSegmentCount);
}

void ManualSupportHandler::clearDebugLayers() {
    cancelSupportRegionComputation();
    if (m_debugActorPick) {
        m_debugActorPick->setGeometry(create_empty_polydata());
    }
    if (m_debugActor) {
        m_debugActor->setGeometry(create_empty_polydata());
    }
    if (m_debugActorContact) {
        m_debugActorContact->setGeometry(create_empty_polydata());
    }
    if (m_debugActorMergedOutline) {
        m_debugActorMergedOutline->setGeometry(create_empty_polydata());
    }
    m_contactDiagnostics = SupportContactDiagnostics{};
    syncContactDiagnosticsToBridge();
    m_mergedSurfaceOutlineDirty = false;
}
