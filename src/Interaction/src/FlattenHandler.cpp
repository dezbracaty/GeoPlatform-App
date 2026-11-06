#include <PrintBedPlacementUtil.hpp>
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
#include <utility>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace {
constexpr int kClickTolerancePx = 4;

struct CandidateBuildResult {
    std::uint64_t generation{0};
    DBInstanceID modelId{INVALID_DB_ID};
    std::uint64_t meshRevision{0};
    Transform::Matrix4 transform{Transform::Matrix4::Identity()};
    std::vector<GPlatform::Interaction::FlattenPlane> planes;
    std::vector<GeomTriangle> triangles;
    std::vector<int> triangleToPlane;
    QString error;
};

// Candidate validity also depends on semantic roles and source identity;
// ModelInstanceDB::geometryRevision intentionally describes mesh revisions.
std::uint64_t candidateRevision(const ModelInstanceDB& instance) {
    std::uint64_t hash = instance.geometryRevision();
    for (const auto& part : instance.parts()) {
        if (!part) continue;
        hash = (hash ^ std::uint64_t(part->getRole())) * 1099511628211ull;
        const auto geometry = part->geometry();
        hash = (hash ^ (geometry ? geometry->getDBInstanceID().getValue() : 0)) * 1099511628211ull;
    }
    return hash;
}

ActorDB::BoundingBox printableBoundsAt(const ModelInstanceDB& instance,
                                        const Transform::Matrix4& matrix) {
    ActorDB::BoundingBox bounds;
    for (const auto& part : instance.parts()) {
        if (!part || part->getRole() != static_cast<int>(ModelPartRole::Model)) continue;
        const auto geometry = part->geometry();
        const auto read = geometry ? geometry->read() : ModelGeometryDB::ReadHandle{};
        if (!read) continue;
        auto* polyData = const_cast<vtkPolyData*>(&read.polyData());
        const Transform::Matrix4 combined = matrix * part->getLocalTransform().getMatrix();
        for (vtkIdType i = 0; i < polyData->GetNumberOfPoints(); ++i) {
            double vertex[3];
            polyData->GetPoint(i, vertex);
            const Eigen::Vector4f transformed = combined * Eigen::Vector4f(
                float(vertex[0]), float(vertex[1]), float(vertex[2]), 1.f);
            const Vector3 point(transformed.x(), transformed.y(), transformed.z());
            if (!bounds.valid) { bounds.min = bounds.max = point; bounds.valid = true; }
            else {
                bounds.min = {std::min(bounds.min.x, point.x), std::min(bounds.min.y, point.y), std::min(bounds.min.z, point.z)};
                bounds.max = {std::max(bounds.max.x, point.x), std::max(bounds.max.y, point.y), std::max(bounds.max.z, point.z)};
            }
        }
    }
    return bounds;
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
    const std::vector<GPlatform::Interaction::FlattenPlane>& planes, int onlyGroup,
    std::vector<int>* triangleToPlane = nullptr) {
    std::vector<GeomTriangle> output;
    const auto vertex = [](const std::array<double, 3>& point) {
        return Vector3(float(point[0]), float(point[1]), float(point[2]));
    };
    for (std::size_t group = 0; group < planes.size(); ++group) {
        if (onlyGroup >= 0 && int(group) != onlyGroup) continue;
        const auto& plane = planes[group];
        const auto& polygon = plane.vertices;
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            Vector3 a = vertex(polygon[0]), b = vertex(polygon[i]), c = vertex(polygon[i + 1]);
            const Eigen::Vector3f normal{float(plane.normal[0]), float(plane.normal[1]), float(plane.normal[2])};
            const Eigen::Vector3f ab(b.x - a.x, b.y - a.y, b.z - a.z);
            const Eigen::Vector3f ac(c.x - a.x, c.y - a.y, c.z - a.z);
            if (ab.cross(ac).dot(normal) < 0.f) std::swap(b, c);
            output.emplace_back(a, b, c);
            if (triangleToPlane) triangleToPlane->push_back(int(group));
        }
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
    preview->setDrawOnTop(true);
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

bool buildRayGeometry(const std::vector<GeomTriangle>& hull,
                      std::vector<QVector3D>* vertices,
                      std::vector<std::array<int, 3>>* triangles) {
    if (!vertices || !triangles || hull.empty()) return false;
    vertices->clear();
    triangles->clear();
    vertices->reserve(hull.size() * 3);
    triangles->reserve(hull.size());
    for (const GeomTriangle& triangle : hull) {
        const int base = static_cast<int>(vertices->size());
        for (const auto& point : {triangle.vertex1, triangle.vertex2, triangle.vertex3})
            vertices->push_back(QVector3D(point.x, point.y, point.z));
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
    m_planes.clear();
    m_triangleToPlane.clear();
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
    m_meshRevision = candidateRevision(*instance);
    m_candidateTransform = instance->getTransformMatrix();
    const std::uint64_t generation = ++m_buildGeneration;

    GPlatform::Interaction::FlattenRequest request;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            request.model_to_world[row * 4 + column] = m_candidateTransform(row, column);
    for (const auto& part : instance->parts()) {
        if (!part || !part->hasValidMesh() ||
            part->getRole() != static_cast<int>(ModelPartRole::Model)) continue;
        const auto geometry = part->geometry();
        const auto readHandle = geometry ? geometry->read() : ModelGeometryDB::ReadHandle{};
        auto* polyData = readHandle ? const_cast<vtkPolyData*>(&readHandle.polyData()) : nullptr;
        if (!polyData || !polyData->GetPoints()) continue;
        GPlatform::Geometry::TriangleMesh volume;
        const Transform::Matrix4 local = part->getLocalTransform().getMatrix();
        volume.vertices.reserve(polyData->GetNumberOfPoints());
        for (vtkIdType pointId = 0; pointId < polyData->GetNumberOfPoints(); ++pointId) {
            double point[3];
            polyData->GetPoint(pointId, point);
            const Eigen::Vector4f model = local * Eigen::Vector4f(
                float(point[0]), float(point[1]), float(point[2]), 1.f);
            volume.vertices.push_back({model.x(), model.y(), model.z()});
        }
        volume.triangles.reserve(polyData->GetNumberOfCells());
        for (vtkIdType cell = 0; cell < polyData->GetNumberOfCells(); ++cell) {
            vtkIdType count = 0;
            const vtkIdType* ids = nullptr;
            polyData->GetCellPoints(cell, count, ids);
            if (count == 3) volume.triangles.push_back({
                std::uint32_t(ids[0]), std::uint32_t(ids[1]), std::uint32_t(ids[2])});
        }
        request.volumes.push_back(std::move(volume));
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
        if (!instance || candidateRevision(*instance) != result.meshRevision ||
            !instance->getTransformMatrix().isApprox(
                result.transform, 1.0e-5f)) {
            LOG_WARN("FlattenHandler: candidate result became stale for model {}",
                     m_modelId.getValue());
            if (instance) startCandidateBuild(instance);
            return;
        }
        if (!result.error.isEmpty() || result.planes.empty() || result.triangles.empty()) {
            LOG_ERROR("FlattenHandler: candidate build failed for model {}: {}",
                      m_modelId.getValue(), result.error.toStdString());
            return;
        }

        std::vector<QVector3D> rayVertices;
        std::vector<std::array<int, 3>> rayTriangles;
        if (!buildRayGeometry(result.triangles,
                              &rayVertices, &rayTriangles) ||
            !m_candidateRayQuery.build(rayVertices, rayTriangles)) {
            LOG_ERROR("FlattenHandler: failed to build face ray query for model {}",
                      m_modelId.getValue());
            return;
        }

        m_planes = result.planes;
        m_triangleToPlane = result.triangleToPlane;
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
                result.triangles));
            m_hoverPreview->setGeometry({});
        }
        m_candidatesReady = true;
        LOG_INFO("FlattenHandler: model {} has {} placeable face groups",
                 m_modelId.getValue(), m_planes.size());

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
         request = std::move(request)]() {
            CandidateBuildResult result;
            result.generation = generation;
            result.modelId = modelId;
            result.meshRevision = meshRevision;
            result.transform = transform;
            auto built = GPlatform::Interaction::buildFlattenPlanes(request);
            if (!built.success) {
                result.error = QString::fromStdString(built.error);
            } else if (built.planes.empty()) {
                result.error = QStringLiteral("没有面积足够的可贴合平面");
            } else {
                result.planes = std::move(built.planes);
                result.triangles = previewTriangles(result.planes, -1, &result.triangleToPlane);
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
        hit.facetId < static_cast<int>(m_triangleToPlane.size())) {
        group = m_triangleToPlane[
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
        previewTriangles(m_planes, m_hoveredGroup)));
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
        groupIndex >= static_cast<int>(m_planes.size())) {
        LOG_ERROR("FlattenHandler: invalid group {} for model {}", groupIndex,
                  m_modelId.getValue());
        return false;
    }

    auto* document = DocumentManager::instance();
    const auto instance = document
        ? std::dynamic_pointer_cast<ModelInstanceDB>(
              document->getDBInstance(m_modelId))
        : std::shared_ptr<ModelInstanceDB>{};
    if (!instance || candidateRevision(*instance) != m_meshRevision ||
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

    const auto& planeNormal = m_planes[static_cast<std::size_t>(groupIndex)].normal;
    QVector3D normal{float(planeNormal[0]), float(planeNormal[1]), float(planeNormal[2])};
    if (normal.lengthSquared() <= 1.0e-12f) return false;
    normal.normalize();
    // Orca rotates in world space. Left-multiplying preserves an instance's
    // existing nonuniform scale/mirror and also handles sub-degree rotations.
    const Eigen::Quaternionf rotation = Eigen::Quaternionf::FromTwoVectors(
        Eigen::Vector3f(normal.x(), normal.y(), normal.z()), -Eigen::Vector3f::UnitZ());
    const float angleDegrees = Eigen::AngleAxisf(rotation).angle() * 180.f / float(std::acos(-1.));

    const auto beforeBounds = printableBoundsAt(*instance, instance->getTransformMatrix());
    if (!beforeBounds.valid) return false;

    Transform finalTransform = instance->getTransform();
    finalTransform.getMatrix().block<3, 3>(0, 0) =
        (rotation.toRotationMatrix() * m_candidateTransform.block<3, 3>(0, 0)).eval();
    const auto afterBounds = printableBoundsAt(*instance, finalTransform.getMatrix());
    if (!afterBounds.valid) {
        return false;
    }
    const Vector3 current = finalTransform.getPosition();
    const Vector3 beforeCenter = beforeBounds.getCenter();
    const Vector3 afterCenter = afterBounds.getCenter();
    const float groundZ = PrintBedPlacementUtil::plateToScene(*bed, bed->getOrigin()).z;
    finalTransform.setPosition(Vector3(
        current.x + beforeCenter.x - afterCenter.x,
        current.y + beforeCenter.y - afterCenter.y,
        current.z + groundZ - afterBounds.min.z));

    const auto finalBounds = printableBoundsAt(*instance, finalTransform.getMatrix());
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
