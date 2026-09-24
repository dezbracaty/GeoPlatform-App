#include "FlattenHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_PERSISTENT_INTERACTION_ACTION(
    FlattenHandler, "model.flatten", "model.remove_flatten")

#include <ActionContext.hpp>
#include <ActionManager.hpp>
#include <DocumentManager.hpp>
#include <Geometry.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelPartDB.hpp>
#include <PrintBedDB.hpp>
#include <TransientPolyDataActorDB.hpp>
#include <SelectionBridge.hpp>
#include <TransactionManager.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <WindowDB.hpp>
#include "Foundation/Log.h"

#include <QFutureWatcher>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace {
constexpr int kClickTolerancePx = 4;
constexpr int kMaximumCandidateGroups = 254;

struct CandidateBuildResult {
    std::uint64_t generation{0};
    DBInstanceID modelId{INVALID_DB_ID};
    std::uint64_t meshRevision{0};
    Transform::Matrix4 transform{Transform::Matrix4::Identity()};
    std::vector<placement::Tri> hull;
    placement::PlaneGroupingResult groups;
    placement::PlaneOverlayMesh overlay;
    QString error;
};

QVector3D pointOf(const placement::Tri& triangle, int vertex) {
    return {triangle[vertex * 3], triangle[vertex * 3 + 1],
            triangle[vertex * 3 + 2]};
}

QVector3D transformPoint(const Transform::Matrix4& matrix, const Vector3& point) {
    const Eigen::Vector4f transformed =
        matrix * Eigen::Vector4f(point.x, point.y, point.z, 1.0f);
    return {transformed.x(), transformed.y(), transformed.z()};
}

QVector3D hullCenter(const std::vector<QVector3D>& points) {
    if (points.empty()) return {};
    QVector3D minimum(
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    QVector3D maximum(
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest());
    for (const QVector3D& point : points) {
        minimum.setX(std::min(minimum.x(), point.x()));
        minimum.setY(std::min(minimum.y(), point.y()));
        minimum.setZ(std::min(minimum.z(), point.z()));
        maximum.setX(std::max(maximum.x(), point.x()));
        maximum.setY(std::max(maximum.y(), point.y()));
        maximum.setZ(std::max(maximum.z(), point.z()));
    }
    return (minimum + maximum) * 0.5f;
}

ViewportProjectionSnapshot viewportProjection(DBInstanceID viewId) {
    auto* document = DocumentManager::instance();
    if (!document || !viewId.isValid()) return {};
    const auto window = std::dynamic_pointer_cast<WindowDB>(
        document->getDBInstance(viewId));
    if (!window) return {};
    window->updateActiveCamera();
    const auto coordinates = window->getCoordinateSystem();
    return coordinates ? coordinates->projectionSnapshot()
                       : ViewportProjectionSnapshot{};
}

std::vector<GeomTriangle> previewTriangles(
    const placement::PlaneOverlayMesh& overlay,
    int onlyGroup) {
    std::vector<GeomTriangle> output;
    output.reserve(overlay.triangles.size());
    for (std::size_t cell = 0; cell < overlay.triangles.size(); ++cell) {
        if (cell >= overlay.triangleToGroup.size()) continue;
        const int group = overlay.triangleToGroup[cell];
        if (group < 0 || (onlyGroup >= 0 && group != onlyGroup)) continue;
        const QVector3D a = pointOf(overlay.triangles[cell], 0);
        const QVector3D b = pointOf(overlay.triangles[cell], 1);
        const QVector3D c = pointOf(overlay.triangles[cell], 2);
        output.emplace_back(Vector3(a.x(), a.y(), a.z()),
                            Vector3(b.x(), b.y(), b.z()),
                            Vector3(c.x(), c.y(), c.z()));
    }
    return output;
}

vtkSmartPointer<vtkPolyData> polyDataFromTriangles(
    const std::vector<GeomTriangle>& triangles) {
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (const auto& triangle : triangles) {
        const vtkIdType base = points->GetNumberOfPoints();
        points->InsertNextPoint(triangle.vertex1.x, triangle.vertex1.y,
                                triangle.vertex1.z);
        points->InsertNextPoint(triangle.vertex2.x, triangle.vertex2.y,
                                triangle.vertex2.z);
        points->InsertNextPoint(triangle.vertex3.x, triangle.vertex3.y,
                                triangle.vertex3.z);
        const vtkIdType ids[3]{base, base + 1, base + 2};
        cells->InsertNextCell(3, ids);
    }
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(cells);
    return polyData;
}

void configurePreview(const std::shared_ptr<TransientPolyDataActorDB>& preview,
                      const Vector3& color,
                      float opacity) {
    if (!preview) return;
    preview->setDisplayName("Flatten candidate preview");
    preview->setPickable(false);
    preview->setDragable(false);
    preview->setVisible(true);
    preview->setOpacity(1.0f);
    if (const auto material = preview->getMaterial()) {
        material->setColor(color);
        material->setDiffuseColor(color);
        material->setOpacity(opacity);
        material->setMetallic(0.0f);
        material->setRoughness(0.65f);
        material->setLighting(false);
        material->setRepresentation(2);
    }
}

bool buildRayGeometry(const std::vector<placement::Tri>& hull,
                      std::vector<QVector3D>* vertices,
                      std::vector<std::array<int, 3>>* triangles) {
    if (!vertices || !triangles || hull.empty()) return false;
    vertices->clear();
    triangles->clear();
    vertices->reserve(hull.size() * 3);
    triangles->reserve(hull.size());
    for (const placement::Tri& triangle : hull) {
        const int base = static_cast<int>(vertices->size());
        vertices->push_back(pointOf(triangle, 0));
        vertices->push_back(pointOf(triangle, 1));
        vertices->push_back(pointOf(triangle, 2));
        triangles->push_back({base, base + 1, base + 2});
    }
    return true;
}

} // namespace

FlattenHandler::FlattenHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

FlattenHandler::~FlattenHandler() {
    clearCandidateState();
}

bool FlattenHandler::supportsEnvironment(const QString& environment) const {
    return environment == QStringLiteral("normal") ||
           environment == QStringLiteral("editing");
}

DBInstanceID FlattenHandler::requestedModelId(const QVariantMap& params) {
    const int rawId = params.value(QStringLiteral("modelId"), 0).toInt();
    if (rawId > 0) return DBInstanceID(rawId);
    return SelectionBridge::instance()->selectedId();
}

void FlattenHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    if (getActionCode() == QStringLiteral("model.remove_flatten")) {
        onExit();
        return;
    }

    const DBInstanceID modelId = requestedModelId(getParams());
    auto* document = DocumentManager::instance();
    const auto instance = document
        ? std::dynamic_pointer_cast<ModelInstanceDB>(
              document->getDBInstance(modelId))
        : std::shared_ptr<ModelInstanceDB>{};
    if (!instance || !instance->isValid() || instance->isTempDB()) {
        if (context) {
            context->setError(ActionErrorCode::TargetRequired,
                              QStringLiteral("请选择一个有效模型后再使用手动贴面"));
        }
        LOG_ERROR("FlattenHandler: model {} is not a valid printable mesh",
                  modelId.getValue());
        onExit();
        return;
    }

    auto* actionManager = ActionManager::getInstance();
    m_modelInputSuspended = actionManager && actionManager->triggerAction(
        QStringLiteral("modelInteraction.suspendSelectionInput"));
    if (!m_modelInputSuspended) {
        if (context) {
            context->setError(
                ActionErrorCode::SystemUnavailable,
                QStringLiteral("无法暂停模型拖动，手动贴面未启动"));
        }
        LOG_ERROR("FlattenHandler: failed to suspend model interaction");
        onExit();
        return;
    }

    m_pendingGroup = getParams().value(QStringLiteral("groupIndex"), -1).toInt();
    startCandidateBuild(instance);
}

void FlattenHandler::onExit() {
    ++m_buildGeneration;
    m_pendingGroup = -1;
    m_leftDown = false;
    m_pressedOnFace = false;
    m_pressedGroup = -1;
    clearCandidateState();
    if (std::exchange(m_modelInputSuspended, false)) {
        if (auto* actionManager = ActionManager::getInstance();
            actionManager && actionManager->isAcceptingActions()) {
            actionManager->triggerAction(
                QStringLiteral("modelInteraction.resumeSelectionInput"));
        }
    }
    StandardActionHandler::onExit();
}

void FlattenHandler::clearCandidateState() {
    TransientUpdateGuard previewUpdate;
    m_candidatePreview.reset();
    m_hoverPreview.reset();
    m_previewScope.clear();
    m_candidateRayQuery.clear();
    m_planeGroups = {};
    m_planeOverlay = {};
    m_modelId = INVALID_DB_ID;
    m_meshRevision = 0;
    m_candidateTransform = Transform::Matrix4::Identity();
    m_hoveredGroup = -1;
    m_candidatesReady = false;
}

void FlattenHandler::startCandidateBuild(
    const std::shared_ptr<ModelInstanceDB>& instance) {
    if (!instance) return;
    clearCandidateState();
    m_modelId = instance->getDBInstanceID();
    m_meshRevision = instance->geometryRevision();
    m_candidateTransform = instance->getTransformMatrix();
    const std::uint64_t generation = ++m_buildGeneration;

    std::vector<QVector3D> worldPoints;
    for (const auto& part : instance->parts()) {
        if (!part || !part->hasValidMesh()) continue;
        const auto geometry = part->geometry();
        const auto readHandle = geometry
            ? geometry->read()
            : ModelGeometryDB::ReadHandle{};
        auto* polyData = readHandle
            ? const_cast<vtkPolyData*>(&readHandle.polyData())
            : nullptr;
        auto* points = polyData ? polyData->GetPoints() : nullptr;
        if (!points) continue;
        const Transform::Matrix4 combined = m_candidateTransform *
            part->getLocalTransform().getMatrix();
        double point[3];
        for (vtkIdType pointId = 0; pointId < points->GetNumberOfPoints();
             ++pointId) {
            points->GetPoint(pointId, point);
            worldPoints.push_back(transformPoint(
                combined, Vector3(static_cast<float>(point[0]),
                                  static_cast<float>(point[1]),
                                  static_cast<float>(point[2]))));
        }
    }
    if (worldPoints.size() < 4) {
        LOG_ERROR("FlattenHandler: model {} has insufficient geometry", m_modelId.getValue());
        return;
    }

    auto* watcher = new QFutureWatcher<CandidateBuildResult>(this);
    connect(watcher, &QFutureWatcher<CandidateBuildResult>::finished, this,
            [this, watcher]() {
        const CandidateBuildResult result = watcher->future().result();
        watcher->deleteLater();
        if (!isActive() || result.generation != m_buildGeneration ||
            result.modelId != m_modelId) {
            return;
        }
        auto* document = DocumentManager::instance();
        const auto instance = document
            ? std::dynamic_pointer_cast<ModelInstanceDB>(
                  document->getDBInstance(m_modelId))
            : std::shared_ptr<ModelInstanceDB>{};
        if (!instance || instance->geometryRevision() != result.meshRevision ||
            !instance->getTransformMatrix().isApprox(
                result.transform, 1.0e-5f)) {
            LOG_WARN("FlattenHandler: candidate result became stale for model {}",
                     m_modelId.getValue());
            return;
        }
        if (!result.error.isEmpty() || result.hull.empty() ||
            result.groups.groups.empty() || result.overlay.triangles.empty()) {
            LOG_ERROR("FlattenHandler: candidate build failed for model {}: {}",
                      m_modelId.getValue(), result.error.toStdString());
            return;
        }

        std::vector<QVector3D> rayVertices;
        std::vector<std::array<int, 3>> rayTriangles;
        if (!buildRayGeometry(result.overlay.triangles,
                              &rayVertices, &rayTriangles) ||
            !m_candidateRayQuery.build(rayVertices, rayTriangles)) {
            LOG_ERROR("FlattenHandler: failed to build face ray query for model {}",
                      m_modelId.getValue());
            return;
        }

        m_planeGroups = result.groups;
        m_planeOverlay = result.overlay;
        m_meshRevision = result.meshRevision;
        m_candidateTransform = result.transform;
        {
            TransientUpdateGuard previewUpdate;
            m_candidatePreview =
                m_previewScope.create<TransientPolyDataActorDB>();
            m_hoverPreview =
                m_previewScope.create<TransientPolyDataActorDB>();
            configurePreview(m_candidatePreview, Vector3(0.05f, 0.72f, 0.76f), 0.34f);
            configurePreview(m_hoverPreview, Vector3(1.0f, 0.42f, 0.05f), 0.78f);
            m_candidatePreview->setGeometry(polyDataFromTriangles(
                previewTriangles(m_planeOverlay, -1)));
            m_hoverPreview->setGeometry({});
        }
        m_candidatesReady = true;
        LOG_INFO("FlattenHandler: model {} has {} placeable face groups",
                 m_modelId.getValue(), m_planeGroups.groups.size());

        if (m_pendingGroup >= 0) {
            const int requested = std::exchange(m_pendingGroup, -1);
            flattenGroup(requested);
            emit requestExit();
        }
    });

    const DBInstanceID modelId = m_modelId;
    const std::uint64_t meshRevision = m_meshRevision;
    const Transform::Matrix4 transform = m_candidateTransform;
    watcher->setFuture(QtConcurrent::run(
        [generation, modelId, meshRevision, transform,
         points = std::move(worldPoints)]() mutable {
            CandidateBuildResult result;
            result.generation = generation;
            result.modelId = modelId;
            result.meshRevision = meshRevision;
            result.transform = transform;
            result.hull = placement::convexHull3D(points);
            if (result.hull.empty()) {
                result.error = QStringLiteral("模型凸包计算失败");
                return result;
            }
            result.groups = placement::groupCoplanarFaces(
                result.hull, hullCenter(points));
            result.groups = placement::rankFaceGroups(
                std::move(result.groups), result.hull, kMaximumCandidateGroups);
            result.overlay = placement::buildPlaneOverlay(
                result.hull, result.groups);
            if (result.groups.groups.empty()) {
                result.error = QStringLiteral("没有面积足够的可贴合平面");
            } else if (result.overlay.triangles.empty()) {
                result.error = QStringLiteral("没有可用的完整贴合面片");
            }
            return result;
        }));
}

bool FlattenHandler::updateHoveredGroup(const QPoint& screenPosition) {
    if (!m_candidatesReady) {
        setHoveredGroup(-1);
        return false;
    }
    const auto viewport = viewportProjection(DBInstanceID(inputViewId()));
    if (!viewport.isValid()) {
        setHoveredGroup(-1);
        return false;
    }
    const WorldRay ray = viewport.rayFromScreen(screenPosition);
    const auto hit = m_candidateRayQuery.firstHit(
        QVector3D(ray.origin.x, ray.origin.y, ray.origin.z),
        QVector3D(ray.direction.x, ray.direction.y, ray.direction.z));
    int group = -1;
    if (hit.valid && hit.facetId >= 0 &&
        hit.facetId < static_cast<int>(m_planeOverlay.triangleToGroup.size())) {
        group = m_planeOverlay.triangleToGroup[
            static_cast<std::size_t>(hit.facetId)];
    }
    setHoveredGroup(group);
    return group >= 0;
}

void FlattenHandler::setHoveredGroup(int groupIndex) {
    if (groupIndex == m_hoveredGroup) return;
    m_hoveredGroup = groupIndex;
    rebuildHoveredPreview();
}

void FlattenHandler::rebuildHoveredPreview() {
    if (!m_hoverPreview) return;
    TransientUpdateGuard previewUpdate;
    if (m_hoveredGroup < 0) {
        m_hoverPreview->setGeometry({});
        return;
    }
    m_hoverPreview->setGeometry(polyDataFromTriangles(
        previewTriangles(m_planeOverlay, m_hoveredGroup)));
}

bool FlattenHandler::onMousePressEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton || !isActive()) return false;
    updateHoveredGroup(event->pos());
    m_leftDown = true;
    m_pressPosition = event->pos();
    m_pressedGroup = m_hoveredGroup;
    m_pressedOnFace = m_pressedGroup >= 0;
    return m_pressedOnFace;
}

bool FlattenHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!event || !isActive()) return false;
    if (m_leftDown) return m_pressedOnFace;
    updateHoveredGroup(event->pos());
    return false;
}

bool FlattenHandler::onMouseReleaseEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton || !isActive()) return false;
    const bool owned = m_pressedOnFace;
    const int group = m_pressedGroup;
    m_leftDown = false;
    m_pressedOnFace = false;
    m_pressedGroup = -1;
    if (!owned) return false;
    if ((event->pos() - m_pressPosition).manhattanLength() <= kClickTolerancePx) {
        flattenGroup(group);
    }
    return true;
}

bool FlattenHandler::onKeyPressEvent(QKeyEvent* event) {
    return StandardActionHandler::onKeyPressEvent(event);
}

bool FlattenHandler::flattenGroup(int groupIndex) {
    if (!m_candidatesReady || groupIndex < 0 ||
        groupIndex >= static_cast<int>(m_planeGroups.groups.size())) {
        LOG_ERROR("FlattenHandler: invalid group {} for model {}", groupIndex,
                  m_modelId.getValue());
        return false;
    }

    auto* document = DocumentManager::instance();
    const auto instance = document
        ? std::dynamic_pointer_cast<ModelInstanceDB>(
              document->getDBInstance(m_modelId))
        : std::shared_ptr<ModelInstanceDB>{};
    if (!instance || instance->geometryRevision() != m_meshRevision ||
        !instance->getTransformMatrix().isApprox(
            m_candidateTransform, 1.0e-5f)) {
        LOG_WARN("FlattenHandler: model {} changed while choosing a face; rebuilding",
                 m_modelId.getValue());
        if (instance) startCandidateBuild(instance);
        return false;
    }

    const auto bed = instance->getParentPrintBedDBId().isValid()
        ? std::dynamic_pointer_cast<PrintBedDB>(
              document->getDBInstance(instance->getParentPrintBedDBId()))
        : std::shared_ptr<PrintBedDB>{};
    if (!bed) {
        LOG_ERROR("FlattenHandler: model {} has no owning print bed",
                  m_modelId.getValue());
        return false;
    }

    QVector3D normal = m_planeGroups.groups[static_cast<std::size_t>(groupIndex)].normal;
    if (normal.lengthSquared() <= 1.0e-12f) return false;
    normal.normalize();
    const QVector3D down(0.0f, 0.0f, -1.0f);
    const float dot = std::clamp(QVector3D::dotProduct(normal, down), -1.0f, 1.0f);
    float angleDegrees = 0.0f;
    QVector3D axis(1.0f, 0.0f, 0.0f);
    if (dot <= -0.9999f) {
        angleDegrees = 180.0f;
    } else if (dot < 0.9999f) {
        axis = QVector3D::crossProduct(normal, down).normalized();
        angleDegrees = static_cast<float>(
            std::acos(dot) * 180.0 / std::acos(-1.0));
    }

    const auto beforeBounds = instance->worldBounds();
    if (!beforeBounds.valid) return false;

    Transform finalTransform = instance->getTransform();
    if (angleDegrees != 0.0f) {
        finalTransform.rotateWXYZ(
            angleDegrees, Vector3(axis.x(), axis.y(), axis.z()));
    }
    const auto afterBounds = instance->worldBoundsAt(finalTransform.getMatrix());
    if (!afterBounds.valid) {
        return false;
    }
    const Vector3 current = finalTransform.getPosition();
    const Vector3 beforeCenter = beforeBounds.getCenter();
    const Vector3 afterCenter = afterBounds.getCenter();
    const float groundZ = bed->getOrigin().z;
    finalTransform.setPosition(Vector3(
        current.x + beforeCenter.x - afterCenter.x,
        current.y + beforeCenter.y - afterCenter.y,
        current.z + groundZ - afterBounds.min.z));

    const auto finalBounds = instance->worldBoundsAt(finalTransform.getMatrix());
    if (!finalBounds.valid || std::abs(finalBounds.min.z - groundZ) > 0.01f) {
        LOG_ERROR(
            "FlattenHandler: final placement bounds invalid for model {} minZ={:.6f} groundZ={:.6f}",
            m_modelId.getValue(), finalBounds.min.z, groundZ);
        return false;
    }

    TransactionGuard transaction("Place on face");
    instance->setTransform(finalTransform);
    transaction.commit();
    LOG_INFO("FlattenHandler: placed model {} on group {} angle={:.2f} groundZ={:.3f}",
             m_modelId.getValue(), groupIndex, angleDegrees, groundZ);
    startCandidateBuild(instance);
    return true;
}
