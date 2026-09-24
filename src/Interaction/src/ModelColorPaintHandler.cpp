#include "ModelColorPaintHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    ModelColorPaintHandler, "model.color.paint.enter", "model.color.paint.leave")
#include "ActionHandlerRegistry.hpp"
#include "ActionManager.hpp"
#include "ModelColorPaintBridge.hpp"
#include "SliceSettingsBridge.hpp"
#include "ModelColorPaintComputeCoordinator.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "ModelSurfaceColorCodec.hpp"
#include "OrcaIndexedMeshBuilder.hpp"
#include "TransientPolyDataActorDB.hpp"
#include "ModelInstanceDB.hpp"
#include "ModelFilamentSlotResolver.hpp"
#include "ModelGeometryDB.hpp"
#include "ModelGraphUtil.hpp"
#include "ModelObjectDB.hpp"
#include "ModelPartDB.hpp"
#include "SlicingConfigDB.hpp"
#include "CameraDB.hpp"
#include "TransactionManager.hpp"
#include "SurfaceBrushProjection.hpp"
#include "CameraNavigationController.hpp"
#include "DocumentManager.hpp"
#include "ViewportCoordinateSystem.hpp"
#include "WindowDB.hpp"
#include "Foundation/Log.h"

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>
#include <algorithm>
#include <limits>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkUnsignedCharArray.h>

namespace {
int projectedFilamentSlot(
    const std::shared_ptr<ModelInstanceDB>& instance,
    const std::shared_ptr<ModelPartDB>& part,
    std::size_t availableSlots) {
    auto* document = DocumentManager::instance();
    const auto config = document && instance
        ? document->getDB<GPlatform::SlicingConfigDB>(
              instance->getSlicingConfigDBId())
        : nullptr;
    return part
        ? GPlatform::Slicing::resolveModelFilamentSlot(
              *part, config.get(), availableSlots).effectiveSlot
        : 1;
}

ModelColorPaintTool paintToolFromName(const QString& name) {
    if (name == QStringLiteral("triangle")) return ModelColorPaintTool::Triangle;
    if (name == QStringLiteral("fill")) return ModelColorPaintTool::Fill;
    if (name == QStringLiteral("height_range")) return ModelColorPaintTool::HeightRange;
    if (name == QStringLiteral("gap_fill")) return ModelColorPaintTool::GapFill;
    return ModelColorPaintTool::Brush;
}

bool isBrushTool(const QString& name) {
    return name == QStringLiteral("circle") || name == QStringLiteral("sphere") ||
           name == QStringLiteral("square");
}

ViewportProjectionSnapshot viewportProjection(DBInstanceID viewId) {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    auto window = std::dynamic_pointer_cast<WindowDB>(
        document->getDBInstance(viewId));
    if (!window) return {};
    window->updateActiveCamera();
    const auto coordinates = window->getCoordinateSystem();
    return coordinates ? coordinates->projectionSnapshot() : ViewportProjectionSnapshot{};
}

QVector3D paintPointFromWorld(const Vector3& point, const Vector3& originWorld) {
    return {point.x - originWorld.x,
            point.y - originWorld.y,
            point.z - originWorld.z};
}

QVector3D paintPointFromWorld(const QVector3D& point, const Vector3& originWorld) {
    return {point.x() - originWorld.x,
            point.y() - originWorld.y,
            point.z() - originWorld.z};
}

Vector3 worldPointFromPaint(const QVector3D& point, const Vector3& originWorld) {
    return {point.x() + originWorld.x,
            point.y() + originWorld.y,
            point.z() + originWorld.z};
}

void transformIndexedMesh(OrcaIndexedMesh& mesh,
                          const Transform::Matrix4& transform) {
    for (QVector3D& vertex : mesh.vertices) {
        const Eigen::Vector4f transformed = transform * Eigen::Vector4f(
            vertex.x(), vertex.y(), vertex.z(), 1.0f);
        vertex = {transformed.x(), transformed.y(), transformed.z()};
    }
    mesh.faceNormals.clear();
    mesh.faceNormals.reserve(mesh.triangles.size());
    for (const auto& face : mesh.triangles) {
        const QVector3D& a = mesh.vertices[static_cast<std::size_t>(face[0])];
        const QVector3D& b = mesh.vertices[static_cast<std::size_t>(face[1])];
        const QVector3D& c = mesh.vertices[static_cast<std::size_t>(face[2])];
        mesh.faceNormals.push_back(QVector3D::normal(b - a, c - a));
    }
}

std::shared_ptr<ModelPartDB> primaryPart(
    const std::shared_ptr<ModelInstanceDB>& model) {
    if (!model) return {};
    const auto parts = model->parts();
    return parts.empty() ? std::shared_ptr<ModelPartDB>{} : parts.front();
}

std::vector<GeomTriangle> partTriangles(
    const std::shared_ptr<ModelPartDB>& part) {
    std::vector<GeomTriangle> triangles;
    const auto mesh = part ? part->geometry() : nullptr;
    const auto geometry = mesh
        ? mesh->read()
        : ModelGeometryDB::ReadHandle{};
    auto* poly = geometry
        ? const_cast<vtkPolyData*>(&geometry.polyData())
        : nullptr;
    if (!poly) return triangles;
    for (vtkIdType cellId = 0; cellId < poly->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = poly->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) continue;
        Vector3 points[3];
        for (int corner = 0; corner < 3; ++corner) {
            double point[3] = {0.0, 0.0, 0.0};
            poly->GetPoint(cell->GetPointId(corner), point);
            points[corner] = {
                static_cast<float>(point[0]),
                static_cast<float>(point[1]),
                static_cast<float>(point[2])};
        }
        triangles.emplace_back(points[0], points[1], points[2]);
    }
    return triangles;
}

SurfaceColorMatrix4 packedMatrix(const Transform::Matrix4& matrix) {
    SurfaceColorMatrix4 packed{};
    std::copy_n(matrix.data(), packed.size(), packed.data());
    return packed;
}

vtkSmartPointer<vtkPolyData> emptyPolyData() {
    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->SetPoints(vtkSmartPointer<vtkPoints>::New());
    return data;
}

vtkSmartPointer<vtkPolyData> polyDataForLabel(
    const std::vector<ManualSupportOrcaScaffold::LeafTriangle>& leaves,
    ManualSupportOrcaScaffold::FacetLabelId labelId) {
    auto data = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (const auto& leaf : leaves) {
        if (leaf.state != labelId) continue;
        const QVector3D normal = QVector3D::normal(
            leaf.vertices[1] - leaf.vertices[0], leaf.vertices[2] - leaf.vertices[0]);
        vtkIdType ids[3];
        for (int i = 0; i < 3; ++i) {
            const QVector3D point = leaf.vertices[i] + normal * 0.04f;
            ids[i] = points->InsertNextPoint(point.x(), point.y(), point.z());
        }
        cells->InsertNextCell(3, ids);
    }
    data->SetPoints(points);
    data->SetPolys(cells);
    return data;
}

vtkSmartPointer<vtkPolyData> polyDataForPainting(
    const std::vector<ManualSupportOrcaScaffold::LeafTriangle>& leaves,
    const std::vector<QColor>& palette,
    bool includeNoLabel = false) {
    auto data = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("Colors");
    colors->SetNumberOfComponents(4);

    for (const auto& leaf : leaves) {
        if (leaf.state == 0 && !includeNoLabel) continue;
        const QVector3D normal = QVector3D::normal(
            leaf.vertices[1] - leaf.vertices[0], leaf.vertices[2] - leaf.vertices[0]);
        vtkIdType ids[3];
        for (int i = 0; i < 3; ++i) {
            const QVector3D point = leaf.vertices[i] + normal * 0.04f;
            ids[i] = points->InsertNextPoint(point.x(), point.y(), point.z());
        }
        cells->InsertNextCell(3, ids);

        const QColor color = leaf.state == 0
            ? QColor("#d0d0d0")
            : (leaf.state <= palette.size()
            ? palette[leaf.state - 1]
            : QColor::fromHsv(static_cast<int>((leaf.state * 137u) % 360u), 190, 235));
        const unsigned char rgba[4] = {
            static_cast<unsigned char>(color.red()),
            static_cast<unsigned char>(color.green()),
            static_cast<unsigned char>(color.blue()),
            240u};
        colors->InsertNextTypedTuple(rgba);
    }

    data->SetPoints(points);
    data->SetPolys(cells);
    data->GetCellData()->SetScalars(colors);
    return data;
}

bool paintStatesEqual(
    const ManualSupportOrcaScaffold::TriangleSplittingData& lhs,
    const ManualSupportOrcaScaffold::TriangleSplittingData& rhs) {
    if (lhs.bitstream != rhs.bitstream ||
        lhs.leafLabels != rhs.leafLabels ||
        lhs.usedStates != rhs.usedStates ||
        lhs.trianglesToSplit.size() != rhs.trianglesToSplit.size()) {
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

}

ModelColorPaintHandler::ModelColorPaintHandler(QObject* parent)
    : StandardActionHandler(parent),
      m_palette{QColor("#e53935"), QColor("#fdd835"), QColor("#43a047"),
                QColor("#1e88e5"), QColor("#8e24aa")} {
    m_gapPreviewTimer.setSingleShot(true);
    m_gapPreviewTimer.setInterval(100);
    connect(&m_gapPreviewTimer, &QTimer::timeout,
            this, &ModelColorPaintHandler::requestGapPreview);
    auto* bridge = ModelColorPaintBridge::instance();
    connect(bridge, &ModelColorPaintBridge::applySettingsRequested,
            this, [this](const QVariantMap& settings) { applySettingsCommand(settings); });
    connect(bridge, &ModelColorPaintBridge::clearPaintingRequested,
            this, [this] { clearPaintingCommand(); });
    connect(bridge, &ModelColorPaintBridge::performGapFillRequested,
            this, [this] { performGapFillCommand(); });
    connect(bridge, &ModelColorPaintBridge::remapFilamentsRequested,
            this, [this](const QVariantList& mapping) {
                remapFilamentsCommand(mapping);
            });
    connect(bridge, &ModelColorPaintBridge::defaultFilamentSlotRequested,
            this, [this, bridge](int slot) {
                if (!setDefaultFilamentSlotCommand(slot) && m_targetMesh) {
                    auto* document = DocumentManager::instance();
                    const auto part = document
                        ? document->getDB<ModelPartDB>(m_targetPartId)
                        : nullptr;
                    bridge->setDefaultFilamentSlotProjection(
                        projectedFilamentSlot(
                            m_targetMesh, part, m_palette.size()));
                }
            });
    connect(bridge, &ModelColorPaintBridge::finishRequested,
            this, [this] { requestFinish(); });
    connect(SliceSettingsBridge::instance(),
            &SliceSettingsBridge::filamentSlotsChanged,
            this, &ModelColorPaintHandler::syncPaletteFromActiveFilaments);
    syncPaletteFromActiveFilaments();
    connect(ModelColorPaintComputeCoordinator::instance(),
            &ModelColorPaintComputeCoordinator::strokeFinished,
            this,
            &ModelColorPaintHandler::handleStrokeFinished);
    connect(ModelColorPaintComputeCoordinator::instance(),
            &ModelColorPaintComputeCoordinator::gapPreviewFinished,
            this,
            &ModelColorPaintHandler::handleGapPreviewFinished);
}

ModelColorPaintHandler::~ModelColorPaintHandler() {
    stopDocumentObservation();
}

DBInstanceID ModelColorPaintHandler::parseModelId(const QVariant& value) {
    bool ok = false;
    const qulonglong id = value.toULongLong(&ok);
    return ok && id != 0 ? DBInstanceID(id) : DBInstanceID();
}

void ModelColorPaintHandler::onEnter(std::shared_ptr<ActionContext> context) {
    if (!context) {
        onExit();
        return;
    }
    if (getActionCode() == QStringLiteral("model.color.paint.leave")) {
        if (!requestFinish()) {
            context->setError(
                ActionErrorCode::SystemUnavailable,
                QStringLiteral("Model surface color painting is not active"));
        }
        return;
    }
    if (m_state == State::Finishing || !m_queuedStrokes.empty()) {
        context->setError(
            ActionErrorCode::SystemUnavailable,
            QStringLiteral("Model surface colors are still being committed"));
        return;
    }
    StandardActionHandler::onEnter(context);
    if (getActionCode() != QStringLiteral("model.color.paint.enter")) {
        context->setError(ActionErrorCode::InvalidParams, QStringLiteral("Unsupported model color paint action"));
        onExit();
        return;
    }

    m_targetModelId = parseModelId(getParam(QStringLiteral("modelId")));
    m_targetPartId = parseModelId(getParam(QStringLiteral("partId")));
    if (!m_targetModelId.isValid() || !startSession()) {
        context->setError(ActionErrorCode::Internal, QStringLiteral("Unable to start model surface color painting"));
        onExit();
        return;
    }
    syncPaletteFromActiveFilaments();
    auto* actionManager = ActionManager::getInstance();
    if (!actionManager) {
        context->setError(
            ActionErrorCode::SystemUnavailable,
            QStringLiteral("ActionManager unavailable"));
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
            context->setError(
                ActionErrorCode::SystemUnavailable,
                QStringLiteral("Unable to suspend direct model interaction"));
            onExit();
            return;
        }
    }
    applySettingsCommand(getParams());
    auto* bridge = ModelColorPaintBridge::instance();
    const auto targetPart = DocumentManager::instance()
        ? DocumentManager::instance()->getDB<ModelPartDB>(m_targetPartId)
        : nullptr;
    bridge->setDefaultFilamentSlotProjection(
        projectedFilamentSlot(
            m_targetMesh, targetPart, m_palette.size()));
    bridge->setCurrentColorIndex(static_cast<int>(m_currentLabelId));
    bridge->setBrushSize(m_brushSize);
    bridge->setGapArea(m_gapArea);
    bridge->setBrushShape(m_brushShape);
    context->setResult(QVariantMap{
        {QStringLiteral("active"), true},
        {QStringLiteral("modelId"), QVariant::fromValue<qulonglong>(m_targetModelId.getValue())}});
}

bool ModelColorPaintHandler::startSession() {
    auto* document = DocumentManager::instance();
    if (!document) return false;
    m_targetMesh = document->getDB<ModelInstanceDB>(m_targetModelId);
    if (!m_targetMesh) return false;
    const auto parts = m_targetMesh->parts();
    const auto selected = std::find_if(
        parts.begin(), parts.end(), [this](const auto& part) {
            return part && part->getDBInstanceID() == m_targetPartId;
        });
    if (selected == parts.end()) {
        const auto part = primaryPart(m_targetMesh);
        if (!part) return false;
        m_targetPartId = part->getDBInstanceID();
    }

    ensureStateDB(false);
    const auto targetPart = document->getDB<ModelPartDB>(m_targetPartId);
    if (!targetPart || !targetPart->geometry()) return false;
    const Transform::Matrix4 currentTransform = m_targetMesh->getTransformMatrix() *
        targetPart->getLocalTransform().getMatrix();
    const bool reuseTopology = m_hasLoadedTopology &&
        m_loadedModelId == m_targetModelId &&
        m_loadedPartId == m_targetPartId &&
        m_loadedMeshRevision == targetPart->geometry()->revision() &&
        m_loadedTransform.isApprox(currentTransform, 0.0f) &&
        m_engine.has_mesh();
    if (!reuseTopology && !loadTargetMesh(m_targetMesh)) return false;

    const int stateRevision = m_stateDB ? m_stateDB->getRevision() : 0;
    if (!reuseTopology || m_loadedStateRevision != stateRevision) {
        if (m_stateDB && !m_stateDB->getSurfaceColorData().empty()) {
            if (!restoreState()) return false;
        } else {
            m_committedState.clear();
            m_cachedPaintedFacetCount = 0;
        }
        m_loadedStateRevision = stateRevision;
    }
    m_engine.set_active_paint_state(m_currentLabelId);
    // The handler owns only the hover/draft projection engine. Formal
    // triangle splitting is enabled exclusively by the background worker.
    m_engine.set_triangle_splitting_enabled(false);
    m_state = State::Ready;
    m_finishRequested = false;
    startDocumentObservation();
    ensureHoverActor();
    refreshToolCursor();
    ModelColorPaintBridge::instance()->setActive(true);
    ModelColorPaintBridge::instance()->setCommitState(false, false);
    LOG_INFO("ModelColorPaintHandler: session started model={} colors={} paintedFacets={} reusedTopology={} revision={}",
             m_targetModelId.getValue(), m_palette.size(), paintedFacetCount(),
             reuseTopology, stateRevision);
    return true;
}

bool ModelColorPaintHandler::loadTargetMesh(
    const std::shared_ptr<ModelInstanceDB>& mesh) {
    auto* document = DocumentManager::instance();
    const auto part = document
        ? document->getDB<ModelPartDB>(m_targetPartId)
        : nullptr;
    if (!part || part->object() != mesh->object() ||
        !part->geometry()) return false;
    const auto source = partTriangles(part);
    if (source.empty()) return false;
    const std::string sourceFingerprint = ModelSurfaceColorCodec::topologyFingerprint(source);
    const Transform::Matrix4 transform = mesh->getTransformMatrix() *
        part->getLocalTransform().getMatrix();
    OrcaIndexedMesh indexed;
    QString error;
    // Build connectivity before applying the Instance transform. Converting
    // every source vertex to large translated float world coordinates first
    // can collapse distinct points and corrupt source-facet identity.
    if (!build_orca_indexed_mesh_for_manual_support(source, indexed, &error)) {
        LOG_ERROR("ModelColorPaintHandler: indexed mesh failed: {}", error.toStdString());
        return false;
    }
    m_paintOriginWorld = {
        transform(0, 3), transform(1, 3), transform(2, 3)};
    m_paintFromPart = transform;
    m_paintFromPart(0, 3) = 0.0f;
    m_paintFromPart(1, 3) = 0.0f;
    m_paintFromPart(2, 3) = 0.0f;
    transformIndexedMesh(indexed, m_paintFromPart);
    m_sourceTriangleCount = static_cast<int>(source.size());
    m_topologyFingerprint = sourceFingerprint;
    m_engine.load_mesh(indexed.vertices, indexed.triangles, indexed.neighbors, indexed.faceNormals);
    m_meshRayQueryAccelerated = m_meshRayQuery.build(indexed.vertices, indexed.triangles);
    m_sourceFacetToIndexedFacet = std::move(indexed.sourceFacetToIndexedFacet);
    m_workerTopology = std::make_shared<OrcaIndexedMesh>(std::move(indexed));
    if (!m_engine.has_mesh()) return false;
    m_loadedModelId = m_targetModelId;
    m_loadedPartId = m_targetPartId;
    m_loadedMeshRevision = part->geometry()->revision();
    m_loadedTransform = transform;
    if (m_hoverActor) m_hoverActor->setPosition(m_paintOriginWorld);
    if (m_strokePreviewActor) m_strokePreviewActor->setPosition(m_paintOriginWorld);
    m_hasLoadedTopology = true;
    m_loadedStateRevision = -1;
    return true;
}

void ModelColorPaintHandler::onExit() {
    const bool hasPendingCommits = !m_queuedStrokes.empty();
    auto* actionManager = ActionManager::getInstance();
    const bool shuttingDown = actionManager && !actionManager->isAcceptingActions();
    stopDocumentObservation();
    m_gapPreviewTimer.stop();
    ModelColorPaintComputeCoordinator::instance()->invalidateGapPreview(m_targetModelId);
    m_pendingGapPreviewToken = 0;
    m_pointerDown = false;
    clearHoverPreview();
    m_surfaceBrushCursor.clear();
    auto* bridge = ModelColorPaintBridge::instance();
    bridge->setGapPreviewStatus(false, 0);
    bridge->setActive(false);

    if (hasPendingCommits && !shuttingDown) {
        // An accepted mouse-up is a durable edit intent. If another tool takes
        // the active slot, keep the immutable requests and their previews alive
        // until the coordinator has committed the ordered queue.
        m_state = State::Finishing;
        m_finishRequested = false;
        bridge->setCommitState(false, true);
    } else {
        ModelColorPaintComputeCoordinator::instance()->invalidateModel(m_targetModelId);
        m_pendingStrokeToken = 0;
        clearStrokePreview();
        m_state = State::Inactive;
        m_finishRequested = false;
        m_engine.reset_session();
        m_targetMesh.reset();
        m_stateDB.reset();
        m_targetModelId = DBInstanceID();
        m_targetPartId = DBInstanceID();
        bridge->setCommitState(false, false);
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

bool ModelColorPaintHandler::requestFinish() {
    if (m_state == State::Inactive) return false;
    if (m_finishRequested) return true;

    m_pointerDown = false;
    m_strokeSamples.clear();
    m_strokePreviewLeaves.clear();
    clearHoverPreview();
    m_surfaceBrushCursor.clear();
    m_finishRequested = true;
    m_state = State::Finishing;

    auto* bridge = ModelColorPaintBridge::instance();
    const bool pending = !m_queuedStrokes.empty();
    bridge->setCommitState(pending, pending);
    LOG_INFO("ModelColorPaintHandler: finish requested model={} pending={}",
             m_targetModelId.getValue(), m_queuedStrokes.size());
    if (!pending) finishAfterPendingCommits();
    return true;
}

void ModelColorPaintHandler::finishAfterPendingCommits() {
    auto* bridge = ModelColorPaintBridge::instance();
    bridge->setCommitState(false, false);
    if (m_finishRequested && isActive()) {
        LOG_INFO("ModelColorPaintHandler: all accepted strokes committed; exiting model={}",
                 m_targetModelId.getValue());
        emit requestExit();
        return;
    }

    // The handler may already have lost the active slot to another tool. Its
    // cached instance still receives the worker result and can now release the
    // detached preview/session state without touching the committed DB.
    clearStrokePreview();
    m_state = State::Inactive;
    m_finishRequested = false;
    m_engine.reset_session();
    m_targetMesh.reset();
    m_stateDB.reset();
    m_targetModelId = DBInstanceID();
    m_targetPartId = DBInstanceID();
}

int ModelColorPaintHandler::indexedFacetId(int sourceFacetId) const {
    if (sourceFacetId < 0 ||
        sourceFacetId >= static_cast<int>(m_sourceFacetToIndexedFacet.size())) {
        return -1;
    }
    return m_sourceFacetToIndexedFacet[static_cast<std::size_t>(sourceFacetId)];
}

bool ModelColorPaintHandler::updateHitFromRay(const QPoint& screenPos) {
    const auto viewport = viewportProjection(m_interactionViewId);
    if (!viewport.isValid() || !m_targetMesh) {
        m_engine.update_external_hit({});
        clearHoverPreview();
        return false;
    }
    const WorldRay ray = viewport.rayFromScreen(screenPos);
    const auto modelHit = m_targetMesh->intersectWorldRay(
        ray.origin, ray.direction);
    if (!modelHit || !modelHit->partId.isValid() ||
        modelHit->primitiveIndex >
            static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        m_engine.update_external_hit({});
        clearHoverPreview();
        return false;
    }
    if (modelHit->partId != m_targetPartId) {
        // The selected Instance is the fixed interaction target. A stroke may
        // target any of its Parts, but one queued stroke must never span two
        // independent Part topologies.
        if (m_state != State::Ready || !m_queuedStrokes.empty()) {
            m_engine.update_external_hit({});
            clearHoverPreview();
            return false;
        }
        m_targetPartId = modelHit->partId;
        if (!startSession()) {
            m_engine.update_external_hit({});
            clearHoverPreview();
            return false;
        }
        const auto part = DocumentManager::instance()
            ? DocumentManager::instance()->getDB<ModelPartDB>(m_targetPartId)
            : nullptr;
        ModelColorPaintBridge::instance()->setDefaultFilamentSlotProjection(
            projectedFilamentSlot(
                m_targetMesh, part, m_palette.size()));
    }
    const int facetId = indexedFacetId(
        static_cast<int>(modelHit->primitiveIndex));
    if (facetId < 0) {
        m_engine.update_external_hit({});
        clearHoverPreview();
        return false;
    }
    const Vector3 worldHit = modelHit->worldPosition;
    QVector3D cameraWorld(worldHit.x, worldHit.y, worldHit.z + 1000.0f);
    if (auto current = CameraNavigationController::currentCamera()) {
        const Vector3 position = current->getPosition();
        cameraWorld = {position.x, position.y, position.z};
    }
    SurfaceBrushProjection::ScreenContext projection;
    if (!SurfaceBrushProjection::screenContextAt(
            viewport, worldHit, brushCursorRadiusPx(), &projection)) {
        m_engine.update_external_hit({});
        clearHoverPreview();
        return false;
    }
    float projectedRadiusWorld = 0.0f;
    if (!projection.worldRadius(&projectedRadiusWorld)) {
        m_engine.update_external_hit({});
        clearHoverPreview();
        return false;
    }
    const QVector3D paintHit = paintPointFromWorld(worldHit, m_paintOriginWorld);
    const QVector3D cameraPaint = paintPointFromWorld(cameraWorld, m_paintOriginWorld);
    m_engine.set_cursor_radius(projectedRadiusWorld);
    m_engine.update_external_hit(
        {true, screenPos, 0, facetId,
         paintHit, cameraPaint});
    m_hasHoverHit = true;
    m_hoverHit = paintHit;
    m_hoverCamera = cameraPaint;
    m_hoverFacetId = facetId;
    m_hoverRadiusWorld = projectedRadiusWorld;
    if (m_state == State::Drawing) {
        updateProjectedBrushSelection(projection);
    } else {
        refreshHoverPreview(&projection);
    }
    return true;
}

bool ModelColorPaintHandler::updateProjectedBrushSelection(
    const SurfaceBrushProjection::ScreenContext& projection) {
    if (!m_hasHoverHit || m_hoverFacetId < 0) return false;
    m_currentScreenProjection = {};
    m_currentProjectionSeedFacets.clear();
    if (!isBrushTool(m_brushShape) || m_brushShape == QStringLiteral("sphere")) {
        m_engine.set_screen_projection({});
        m_engine.set_projection_seed_facets({});
        return true;
    }

    if (!projection.isValid()) {
        m_engine.set_screen_projection({});
        m_engine.set_projection_seed_facets({});
        return false;
    }

    const Vector3 paintOriginWorld = m_paintOriginWorld;
    m_currentScreenProjection = {
        projection.radiusUiPx,
        [projection, paintOriginWorld](const QVector3D& point, QPointF* offsetUi) {
            const Vector3 world = worldPointFromPaint(point, paintOriginWorld);
            return projection.projectOffsetUi(
                QVector3D(world.x, world.y, world.z), offsetUi);
        }};
    m_engine.set_screen_projection(m_currentScreenProjection);
    const bool square = m_brushShape == QStringLiteral("square");
    const int spacingUiPx = m_meshRayQueryAccelerated ? 3 : 8;
    auto visible = SurfaceBrushProjection::collectVisibleFacets(
        projection, m_meshRayQuery, square, spacingUiPx, m_hoverFacetId,
        m_paintOriginWorld);
    m_currentProjectionSeedFacets = visible.facetIds;
    m_engine.set_projection_seed_facets(visible.facetIds);
    LOG_DEBUG("ModelColorPaintHandler: projected brush visibleFacets={} rays={} accelerated={}",
              visible.facetIds.size(), visible.sampledRayCount,
              m_meshRayQueryAccelerated);
    return true;
}

bool ModelColorPaintHandler::appendStrokeSample(const QPoint& screenPos, bool erase) {
    if (!m_hasHoverHit || m_hoverFacetId < 0 ||
        (isBrushTool(m_brushShape) && m_hoverRadiusWorld <= 0.0f)) return false;
    const bool reuseHoverPreview = m_strokeSamples.empty() && !m_hoverPreviewLeaves.empty();
    ModelColorStrokeSample sample;
    sample.screenPosition = screenPos;
    sample.facetId = m_hoverFacetId;
    sample.paintHit = m_hoverHit;
    sample.cameraPaintPosition = m_hoverCamera;
    sample.radiusWorld = m_hoverRadiusWorld;
    sample.cursorType =
        m_brushShape == QStringLiteral("sphere") ? ManualSupportOrcaScaffold::CursorType::Sphere
        : (m_brushShape == QStringLiteral("square") ? ManualSupportOrcaScaffold::CursorType::Square
           : (m_brushShape == QStringLiteral("triangle") ? ManualSupportOrcaScaffold::CursorType::Triangle
                                                          : ManualSupportOrcaScaffold::CursorType::Circle));
    sample.screenProjection = m_currentScreenProjection;
    sample.projectionSeedFacets = m_currentProjectionSeedFacets;
    sample.tool = paintToolFromName(m_brushShape);
    sample.smartFillAngleDeg = static_cast<float>(m_smartFillAngle);
    sample.heightRange = static_cast<float>(m_heightRange);
    sample.gapArea = static_cast<float>(m_gapArea);
    sample.edgeDetection = m_edgeDetection;
    sample.erase = erase;
    m_strokeSamples.push_back(std::move(sample));

    std::vector<ManualSupportOrcaScaffold::LeafTriangle> preview;
    if (reuseHoverPreview) {
        preview = m_hoverPreviewLeaves;
    } else if (m_brushShape == QStringLiteral("triangle")) {
        preview = m_engine.collect_triangle_preview(m_hoverFacetId, m_hoverHit);
    } else if (m_brushShape == QStringLiteral("fill")) {
        preview = m_engine.collect_fill_preview(
            m_hoverFacetId, m_hoverHit, static_cast<float>(m_smartFillAngle),
            m_edgeDetection);
    } else if (m_brushShape == QStringLiteral("height_range")) {
        preview = m_engine.collect_height_range_preview(
            m_hoverHit.z(), static_cast<float>(m_heightRange));
    } else {
        preview = m_engine.collect_projected_preview(
            m_hoverFacetId, m_hoverHit, m_hoverCamera, m_hoverRadiusWorld);
    }
    for (auto& leaf : preview) {
        // Erasing remains a visible pending operation instead of exposing a
        // temporary mutation of the committed surface-color engine.
        leaf.state = m_currentLabelId == 0 ? 1 : m_currentLabelId;
        m_strokePreviewLeaves.push_back(std::move(leaf));
    }
    refreshStrokePreview();
    return true;
}

bool ModelColorPaintHandler::onMousePressEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    QElapsedTimer timer;
    timer.start();
    if (!event || event->button() != Qt::LeftButton || m_state != State::Ready ||
        m_brushShape == QStringLiteral("gap_fill") ||
        !updateHitFromRay(event->pos())) return false;
    m_strokeSamples.clear();
    m_strokePreviewLeaves.clear();
    m_pointerDown = true;
    m_state = State::Drawing;
    m_lastPoint = event->pos();
    m_strokeAnchor = event->pos();
    if (m_hoverActor) m_hoverActor->setVisible(false);
    const bool accepted = appendStrokeSample(
        event->pos(), event->modifiers().testFlag(Qt::ShiftModifier) || m_currentLabelId == 0);
    if (timer.elapsed() >= 33) {
        LOG_WARN("ModelColorPaintHandler: slow stroke press uiMs={} previewLeaves={}",
                 timer.elapsed(), m_strokePreviewLeaves.size());
    }
    return accepted;
}

bool ModelColorPaintHandler::onMouseMoveEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!event || m_state == State::Inactive) return false;
    if (m_brushShape == QStringLiteral("gap_fill")) return false;
    if (!m_pointerDown || m_state != State::Drawing) {
        updateHitFromRay(event->pos());
        return false;
    }
    QPoint paintPosition = event->pos();
    if (m_verticalOnly) paintPosition.setX(m_strokeAnchor.x());
    else if (m_horizontalOnly) paintPosition.setY(m_strokeAnchor.y());
    if ((paintPosition - m_lastPoint).manhattanLength() < 2 ||
        !updateHitFromRay(paintPosition)) return true;
    QElapsedTimer timer;
    timer.start();
    m_lastPoint = paintPosition;
    appendStrokeSample(
        paintPosition, event->modifiers().testFlag(Qt::ShiftModifier) || m_currentLabelId == 0);
    if (timer.elapsed() >= 33) {
        LOG_WARN("ModelColorPaintHandler: slow stroke move uiMs={} samples={} previewLeaves={}",
                 timer.elapsed(), m_strokeSamples.size(), m_strokePreviewLeaves.size());
    }
    return true;
}

bool ModelColorPaintHandler::onMouseReleaseEvent(QMouseEvent* event) {
    m_interactionViewId = DBInstanceID(inputViewId());
    if (!event || event->button() != Qt::LeftButton || !m_pointerDown) return false;
    QElapsedTimer totalTimer;
    totalTimer.start();
    m_pointerDown = false;
    if (m_strokeSamples.empty() || !m_workerTopology) {
        m_state = State::Ready;
        m_strokeSamples.clear();
        m_strokePreviewLeaves.clear();
        refreshStrokePreview();
        refreshHoverPreview();
        return false;
    }

    ModelColorStrokeRequest request;
    request.modelId = m_targetModelId;
    request.partId = m_targetPartId;
    request.meshRevision = m_loadedMeshRevision;
    request.baseRevision = m_loadedStateRevision;
    request.sourceTriangleCount = m_sourceTriangleCount;
    request.topologyFingerprint = m_topologyFingerprint;
    request.topology = m_workerTopology;
    request.baseState = m_committedState;
    request.labelId = m_currentLabelId;
    request.palette = m_palette;
    request.samples = m_strokeSamples;
    const std::size_t sampleCount = m_strokeSamples.size();
    auto previewLeaves = std::move(m_strokePreviewLeaves);
    m_strokeSamples.clear();
    m_state = State::Ready;
    const bool queued = enqueueStroke(std::move(request), std::move(previewLeaves));
    if (m_hasHoverHit) refreshHoverPreview();
    LOG_INFO("ModelColorPaintHandler: stroke accepted model={} samples={} queued={} pending={} uiMs={}",
             m_targetModelId.getValue(), sampleCount, queued,
             m_queuedStrokes.size(), totalTimer.elapsed());
    return queued;
}

bool ModelColorPaintHandler::onWheelEvent(QWheelEvent* event) {
    if (!event || m_state == State::Inactive) return false;
    const double direction = event->angleDelta().y() > 0 ? 1.0 : -1.0;
    auto* bridge = ModelColorPaintBridge::instance();
    if (!event->modifiers().testFlag(Qt::ControlModifier)) return false;
    if (isBrushTool(m_brushShape)) {
        bridge->setBrushSize(m_brushSize + direction * 0.5);
    } else if (m_brushShape == QStringLiteral("height_range")) {
        bridge->setHeightRange(m_heightRange + direction * 0.1);
    } else if (m_brushShape == QStringLiteral("fill")) {
        bridge->setSmartFillAngle(m_smartFillAngle + direction);
    } else if (m_brushShape == QStringLiteral("gap_fill")) {
        bridge->setGapArea(m_gapArea + direction * 0.05);
    } else {
        return false;
    }
    return true;
}

bool ModelColorPaintHandler::onKeyPressEvent(QKeyEvent* event) {
    if (event && m_state != State::Inactive &&
        event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        const int color = event->key() - Qt::Key_0;
        if (color <= static_cast<int>(m_palette.size())) {
            ModelColorPaintBridge::instance()->setCurrentColorIndex(color);
            return true;
        }
    }
    return StandardActionHandler::onKeyPressEvent(event);
}

bool ModelColorPaintHandler::applySettingsCommand(const QVariantMap& settings) {
    if (settings.contains(QStringLiteral("colorIndex"))) {
        m_currentLabelId = static_cast<std::uint32_t>(std::clamp(
            settings.value(QStringLiteral("colorIndex")).toInt(), 0,
            static_cast<int>(m_palette.size())));
        m_engine.set_active_paint_state(m_currentLabelId);
        updateHoverMaterial();
    }
    if (settings.contains(QStringLiteral("brushSize"))) {
        m_brushSize = std::clamp(settings.value(QStringLiteral("brushSize")).toDouble(), 0.5, 100.0);
        refreshToolCursor();
        refreshHoverPreview();
    }
    if (settings.contains(QStringLiteral("brushShape"))) {
        const QString previousShape = m_brushShape;
        m_brushShape = settings.value(QStringLiteral("brushShape")).toString().toLower();
        LOG_INFO("ModelColorPaintHandler: tool changed model={} previous={} current={}",
                 m_targetModelId.getValue(), previousShape.toStdString(),
                 m_brushShape.toStdString());
        m_engine.set_cursor_type(
            m_brushShape == QStringLiteral("sphere") ? ManualSupportOrcaScaffold::CursorType::Sphere
            : (m_brushShape == QStringLiteral("square") ? ManualSupportOrcaScaffold::CursorType::Square
               : (m_brushShape == QStringLiteral("triangle") ? ManualSupportOrcaScaffold::CursorType::Triangle
                                                              : ManualSupportOrcaScaffold::CursorType::Circle)));
        refreshToolCursor();
        if (m_brushShape == QStringLiteral("gap_fill")) {
            clearHoverPreview();
            scheduleGapPreview();
        } else {
            if (previousShape == QStringLiteral("gap_fill")) {
                m_gapPreviewTimer.stop();
                ModelColorPaintComputeCoordinator::instance()->invalidateGapPreview(
                    m_targetModelId);
                m_pendingGapPreviewToken = 0;
                clearHoverPreview();
                ModelColorPaintBridge::instance()->setGapPreviewStatus(false, 0);
            }
            refreshHoverPreview();
        }
    }
    if (settings.contains(QStringLiteral("edgeDetection"))) {
        m_edgeDetection = settings.value(QStringLiteral("edgeDetection")).toBool();
        refreshHoverPreview();
    }
    if (settings.contains(QStringLiteral("smartFillAngle"))) {
        m_smartFillAngle = std::clamp(
            settings.value(QStringLiteral("smartFillAngle")).toDouble(), 0.0, 90.0);
        refreshHoverPreview();
    }
    if (settings.contains(QStringLiteral("heightRange"))) {
        m_heightRange = std::clamp(
            settings.value(QStringLiteral("heightRange")).toDouble(), 0.1, 8.0);
        refreshHoverPreview();
    }
    if (settings.contains(QStringLiteral("gapArea"))) {
        m_gapArea = std::clamp(
            settings.value(QStringLiteral("gapArea")).toDouble(), 0.0, 5.0);
        if (m_brushShape == QStringLiteral("gap_fill")) scheduleGapPreview();
    }
    if (settings.contains(QStringLiteral("verticalOnly"))) {
        m_verticalOnly = settings.value(QStringLiteral("verticalOnly")).toBool();
    }
    if (settings.contains(QStringLiteral("horizontalOnly"))) {
        m_horizontalOnly = settings.value(QStringLiteral("horizontalOnly")).toBool();
    }
    if (settings.contains(QStringLiteral("palette"))) {
        m_palette.clear();
        for (const QVariant& value : settings.value(QStringLiteral("palette")).toList()) {
            const QColor color(value.toString());
            if (color.isValid()) m_palette.push_back(color);
        }
        updateHoverMaterial();
        // Palette ownership belongs to the active SlicingConfigDB. The paint
        // state stores facet labels only.
    }
    return true;
}

int ModelColorPaintHandler::brushCursorRadiusPx() const {
    return SurfaceBrushCursor::radiusPxForSize(m_brushSize);
}

void ModelColorPaintHandler::refreshToolCursor() {
    if (m_state == State::Inactive) return;
    if (isBrushTool(m_brushShape)) {
        m_surfaceBrushCursor.applyBrush(
            brushCursorRadiusPx(), SurfaceBrushCursor::shapeFromName(m_brushShape));
    } else if (m_brushShape == QStringLiteral("height_range")) {
        m_surfaceBrushCursor.applyTool(Qt::SizeVerCursor);
    } else if (m_brushShape == QStringLiteral("gap_fill")) {
        m_surfaceBrushCursor.applyTool(Qt::ArrowCursor);
    } else {
        m_surfaceBrushCursor.applyTool(Qt::PointingHandCursor);
    }
}

void ModelColorPaintHandler::ensureHoverActor() {
    if (m_hoverActor) {
        m_hoverActor->setPosition(m_paintOriginWorld);
        return;
    }
    TransientUpdateGuard transientUpdate;
    m_hoverActor = trans::TransDB::create<TransientPolyDataActorDB>();
    if (!m_hoverActor) return;
    m_hoverActor->setUseCellColors(true);
    m_hoverActor->setPickable(false);
    m_hoverActor->setDragable(false);
    m_hoverActor->setVisible(false);
    m_hoverActor->setGeometry(emptyPolyData());
    m_hoverActor->setPosition(m_paintOriginWorld);
    updateHoverMaterial();
}

void ModelColorPaintHandler::updateHoverMaterial() {
    if (!m_hoverActor) return;
    const QColor color = m_currentLabelId == 0 ? QColor("#ffffff") : colorForLabel(m_currentLabelId);
    const Vector3 rgb(color.redF(), color.greenF(), color.blueF());
    m_hoverActor->setOpacity(0.52f);
    if (auto material = m_hoverActor->getMaterial()) {
        material->setColor(rgb);
        material->setDiffuseColor(rgb);
        material->setAmbientColor(rgb);
        material->setLighting(false);
        material->setLineWidth(3.0f);
        material->setOpacity(0.52f);
        material->setEdgeVisibility(false);
    }
}

void ModelColorPaintHandler::refreshHoverPreview(
    const SurfaceBrushProjection::ScreenContext* suppliedProjection) {
    if (m_state != State::Ready || !m_hasHoverHit) return;
    SurfaceBrushProjection::ScreenContext ownedProjection;
    const SurfaceBrushProjection::ScreenContext* projection = suppliedProjection;
    if (!projection) {
        const Vector3 worldHover = worldPointFromPaint(
            m_hoverHit, m_paintOriginWorld);
        if (!SurfaceBrushProjection::screenContextAt(
                viewportProjection(m_interactionViewId),
                worldHover,
                brushCursorRadiusPx(), &ownedProjection)) {
            clearHoverPreview();
            return;
        }
        projection = &ownedProjection;
    }
    if (!projection->worldRadius(&m_hoverRadiusWorld)) {
        clearHoverPreview();
        return;
    }
    m_engine.set_cursor_radius(m_hoverRadiusWorld);
    if (!updateProjectedBrushSelection(*projection)) {
        clearHoverPreview();
        return;
    }
    ensureHoverActor();
    if (!m_hoverActor) return;
    std::vector<ManualSupportOrcaScaffold::LeafTriangle> preview;
    if (m_brushShape == QStringLiteral("triangle")) {
        preview = m_engine.collect_triangle_preview(m_hoverFacetId, m_hoverHit);
    } else if (m_brushShape == QStringLiteral("fill")) {
        preview = m_engine.collect_fill_preview(
            m_hoverFacetId, m_hoverHit, static_cast<float>(m_smartFillAngle),
            m_edgeDetection);
    } else if (m_brushShape == QStringLiteral("height_range")) {
        preview = m_engine.collect_height_range_preview(
            m_hoverHit.z(), static_cast<float>(m_heightRange));
    } else if (m_brushShape != QStringLiteral("gap_fill")) {
        preview = m_engine.collect_projected_preview(
            m_hoverFacetId, m_hoverHit, m_hoverCamera, m_hoverRadiusWorld);
    }
    if (preview.empty()) {
        clearHoverPreview();
        return;
    }
    m_hoverPreviewLeaves = preview;
    m_hoverActor->setGeometry(polyDataForLabel(preview, 1));
    updateHoverMaterial();
    m_hoverActor->setVisible(true);
}

void ModelColorPaintHandler::clearHoverPreview() {
    m_hasHoverHit = false;
    m_hoverFacetId = -1;
    m_hoverRadiusWorld = 0.0f;
    m_hoverPreviewLeaves.clear();
    m_engine.set_screen_projection({});
    m_engine.set_projection_seed_facets({});
    if (!m_hoverActor) return;
    m_hoverActor->setGeometry(emptyPolyData());
    m_hoverActor->setVisible(false);
}

void ModelColorPaintHandler::scheduleGapPreview() {
    if (m_state == State::Inactive || m_brushShape != QStringLiteral("gap_fill")) return;
    ModelColorPaintComputeCoordinator::instance()->invalidateGapPreview(m_targetModelId);
    m_pendingGapPreviewToken = 0;
    clearHoverPreview();
    ModelColorPaintBridge::instance()->setGapPreviewStatus(true, 0);
    m_gapPreviewTimer.start();
    LOG_INFO("ModelColorPaintHandler: gap preview scheduled model={} area={} revision={}",
             m_targetModelId.getValue(), m_gapArea, m_loadedStateRevision);
}

void ModelColorPaintHandler::requestGapPreview() {
    if (m_state == State::Inactive || m_brushShape != QStringLiteral("gap_fill") ||
        !m_workerTopology) {
        return;
    }
    if (m_gapArea <= 0.0) {
        ModelColorPaintComputeCoordinator::instance()->invalidateGapPreview(m_targetModelId);
        m_pendingGapPreviewToken = 0;
        clearHoverPreview();
        ModelColorPaintBridge::instance()->setGapPreviewStatus(false, 0);
        return;
    }
    ModelColorGapPreviewRequest request;
    request.modelId = m_targetModelId;
    request.partId = m_targetPartId;
    request.topology = m_workerTopology;
    request.baseState = m_committedState;
    request.gapArea = static_cast<float>(m_gapArea);
    m_pendingGapPreviewToken =
        ModelColorPaintComputeCoordinator::instance()->submitGapPreview(
            std::move(request));
    LOG_INFO("ModelColorPaintHandler: gap preview submitted model={} token={} area={} leaves={}",
             m_targetModelId.getValue(), m_pendingGapPreviewToken, m_gapArea,
             m_committedState.leafLabels.size());
    if (m_pendingGapPreviewToken == 0) {
        ModelColorPaintBridge::instance()->setGapPreviewStatus(false, 0);
    }
}

void ModelColorPaintHandler::handleGapPreviewFinished(
    std::shared_ptr<const ModelColorGapPreviewResult> result) {
    if (!result || result->modelId != m_targetModelId ||
        result->partId != m_targetPartId ||
        result->token != m_pendingGapPreviewToken) {
        return;
    }
    m_pendingGapPreviewToken = 0;
    if (m_state == State::Inactive || m_brushShape != QStringLiteral("gap_fill")) return;
    if (!m_queuedStrokes.empty()) {
        clearHoverPreview();
        ModelColorPaintBridge::instance()->setGapPreviewStatus(true, 0);
        return;
    }
    if (!result->success) {
        LOG_WARN("ModelColorPaintHandler: gap preview rejected model={} token={} error={}",
                 m_targetModelId.getValue(), result->token,
                 result->error.toStdString());
        clearHoverPreview();
        ModelColorPaintBridge::instance()->setGapPreviewStatus(false, 0);
        return;
    }
    ensureHoverActor();
    if (!m_hoverActor) return;
    m_hoverPreviewLeaves = result->candidates;
    auto geometry = polyDataForPainting(result->candidates, m_palette, true);
    m_hoverActor->setGeometry(geometry);
    m_hoverActor->setOpacity(0.72f);
    if (auto material = m_hoverActor->getMaterial()) {
        material->setLighting(false);
        material->setOpacity(0.72f);
        material->setEdgeVisibility(true);
        material->setLineWidth(2.0f);
    }
    m_hoverActor->setVisible(geometry->GetNumberOfPolys() > 0);
    ModelColorPaintBridge::instance()->setGapPreviewStatus(
        false, static_cast<int>(result->candidates.size()));
    LOG_INFO("ModelColorPaintHandler: gap preview model={} token={} candidates={} computeMs={}",
             m_targetModelId.getValue(), result->token,
             result->candidates.size(), result->computeMs);
}

void ModelColorPaintHandler::ensureStrokePreviewActor() {
    if (m_strokePreviewActor) {
        m_strokePreviewActor->setPosition(m_paintOriginWorld);
        return;
    }
    TransientUpdateGuard transientUpdate;
    m_strokePreviewActor = trans::TransDB::create<TransientPolyDataActorDB>();
    if (!m_strokePreviewActor) return;
    m_strokePreviewActor->setUseCellColors(true);
    m_strokePreviewActor->setPickable(false);
    m_strokePreviewActor->setDragable(false);
    m_strokePreviewActor->setVisible(false);
    m_strokePreviewActor->setGeometry(emptyPolyData());
    m_strokePreviewActor->setPosition(m_paintOriginWorld);
    m_strokePreviewActor->setOpacity(0.96f);
    if (auto material = m_strokePreviewActor->getMaterial()) {
        material->setColor({1.0f, 1.0f, 1.0f});
        material->setDiffuseColor({1.0f, 1.0f, 1.0f});
        material->setAmbientColor({1.0f, 1.0f, 1.0f});
        material->setAmbient(0.35f);
        material->setDiffuse(0.9f);
        material->setSpecular(0.12f);
        material->setOpacity(0.96f);
        material->setEdgeVisibility(false);
    }
}

void ModelColorPaintHandler::refreshStrokePreview() {
    ensureStrokePreviewActor();
    if (!m_strokePreviewActor) return;
    std::size_t leafCount = m_strokePreviewLeaves.size();
    for (const auto& queued : m_queuedStrokes) leafCount += queued.previewLeaves.size();
    std::vector<ManualSupportOrcaScaffold::LeafTriangle> visibleLeaves;
    visibleLeaves.reserve(leafCount);
    for (const auto& queued : m_queuedStrokes) {
        visibleLeaves.insert(visibleLeaves.end(),
                             queued.previewLeaves.begin(), queued.previewLeaves.end());
    }
    visibleLeaves.insert(visibleLeaves.end(),
                         m_strokePreviewLeaves.begin(), m_strokePreviewLeaves.end());
    auto geometry = polyDataForPainting(visibleLeaves, m_palette);
    m_strokePreviewActor->setGeometry(geometry);
    m_strokePreviewActor->setVisible(geometry->GetNumberOfPolys() > 0);
}

void ModelColorPaintHandler::clearStrokePreview() {
    m_strokePreviewLeaves.clear();
    m_strokeSamples.clear();
    m_queuedStrokes.clear();
    if (!m_strokePreviewActor) return;
    m_strokePreviewActor->setGeometry(emptyPolyData());
    m_strokePreviewActor->setVisible(false);
}

bool ModelColorPaintHandler::enqueueStroke(
    ModelColorStrokeRequest request,
    std::vector<ManualSupportOrcaScaffold::LeafTriangle> previewLeaves) {
    if (m_state == State::Inactive || !request.modelId.isValid() ||
        !request.topology || request.samples.empty()) {
        return false;
    }
    m_queuedStrokes.push_back(
        {std::move(request), std::move(previewLeaves), 0});
    ModelColorPaintBridge::instance()->setCommitState(m_finishRequested, true);
    refreshStrokePreview();
    if (m_pendingStrokeToken != 0) return true;
    return submitNextStroke();
}

bool ModelColorPaintHandler::submitNextStroke() {
    if (m_pendingStrokeToken != 0 || m_queuedStrokes.empty() ||
        m_state == State::Inactive) {
        return m_pendingStrokeToken != 0;
    }
    auto& queued = m_queuedStrokes.front();
    queued.request.meshRevision = m_loadedMeshRevision;
    queued.request.baseRevision = m_loadedStateRevision;
    queued.request.sourceTriangleCount = m_sourceTriangleCount;
    queued.request.topologyFingerprint = m_topologyFingerprint;
    queued.request.topology = m_workerTopology;
    queued.request.localFromPaint = packedMatrix(m_paintFromPart.inverse());
    queued.request.baseState = m_committedState;
    ModelColorStrokeRequest request = std::move(queued.request);
    const std::uint64_t token =
        ModelColorPaintComputeCoordinator::instance()->submit(std::move(request));
    if (token == 0) {
        LOG_ERROR("ModelColorPaintHandler: unable to submit queued stroke model={} pending={}",
                  m_targetModelId.getValue(), m_queuedStrokes.size());
        clearStrokePreview();
        return false;
    }
    queued.token = token;
    m_pendingStrokeToken = token;
    LOG_INFO("ModelColorPaintHandler: stroke dispatched model={} token={} pending={}",
             m_targetModelId.getValue(), token, m_queuedStrokes.size());
    return true;
}

void ModelColorPaintHandler::handleStrokeFinished(
    std::shared_ptr<const ModelColorStrokeResult> result) {
    if (!result || result->modelId != m_targetModelId ||
        result->token != m_pendingStrokeToken) {
        return;
    }
    m_pendingStrokeToken = 0;
    if (m_queuedStrokes.empty() ||
        m_queuedStrokes.front().token != result->token) {
        return;
    }
    const bool commitSucceeded = result->success;
    if (commitSucceeded) {
        if (result->partId.isValid() && result->partId != m_targetPartId) {
            m_targetPartId = result->partId;
            m_loadedPartId = result->partId;
            for (auto& queued : m_queuedStrokes) {
                queued.request.partId = result->partId;
            }
        }
        m_committedState = result->state;
        m_loadedStateRevision = result->committedRevision;
        m_cachedPaintedFacetCount = result->paintedFacetCount;
        ensureStateDB(false);
        m_queuedStrokes.pop_front();
    } else {
        LOG_WARN("ModelColorPaintHandler: async stroke rejected model={} token={} error={}",
                 m_targetModelId.getValue(), result->token,
                 result->error.toStdString());
        ModelColorPaintComputeCoordinator::instance()->invalidateModel(m_targetModelId);
        m_queuedStrokes.clear();
    }
    refreshStrokePreview();
    if (!commitSucceeded && m_finishRequested && isActive()) {
        // A failed save must not masquerade as a successful Done action. Keep
        // the tool open on the last committed DB revision so the user can retry.
        m_finishRequested = false;
        m_state = State::Ready;
        ModelColorPaintBridge::instance()->setCommitState(false, false);
        refreshToolCursor();
        LOG_ERROR(
            "ModelColorPaintHandler: finish aborted because the pending stroke failed model={} error={}",
            m_targetModelId.getValue(), result->error.toStdString());
        return;
    }
    if (!m_queuedStrokes.empty() && m_state != State::Inactive) {
        submitNextStroke();
    }
    ModelColorPaintBridge::instance()->setCommitState(
        m_finishRequested, !m_queuedStrokes.empty());
    if (m_queuedStrokes.empty() && m_state == State::Finishing) {
        finishAfterPendingCommits();
        return;
    }
    if (m_state != State::Inactive && m_state != State::Finishing) {
        if (m_state != State::Drawing) {
            if (m_brushShape == QStringLiteral("gap_fill") && m_queuedStrokes.empty()) {
                scheduleGapPreview();
            } else if (m_hasHoverHit) {
                refreshHoverPreview();
            }
        }
    }
}

bool ModelColorPaintHandler::clearPaintingCommand() {
    if (m_state != State::Ready || !m_queuedStrokes.empty()) return false;
    ManualSupportOrcaScaffold::TriangleSplittingData clearedState;
    if (paintStatesEqual(m_committedState, clearedState)) return true;
    if (!persistState(clearedState, true, "Clear Model Surface Colors")) return false;
    m_committedState.clear();
    m_cachedPaintedFacetCount = 0;
    ModelColorPaintComputeCoordinator::instance()->invalidateModel(m_targetModelId);
    if (m_brushShape == QStringLiteral("gap_fill")) scheduleGapPreview();
    return true;
}

bool ModelColorPaintHandler::performGapFillCommand() {
    if (m_state != State::Ready || !m_workerTopology || m_gapArea <= 0.0 ||
        m_pendingGapPreviewToken != 0 || m_hoverPreviewLeaves.empty()) return false;
    m_gapPreviewTimer.stop();
    ModelColorPaintComputeCoordinator::instance()->invalidateGapPreview(m_targetModelId);
    m_pendingGapPreviewToken = 0;
    clearHoverPreview();
    ModelColorStrokeSample sample;
    sample.tool = ModelColorPaintTool::GapFill;
    sample.gapArea = static_cast<float>(m_gapArea);

    ModelColorStrokeRequest request;
    request.modelId = m_targetModelId;
    request.partId = m_targetPartId;
    request.meshRevision = m_loadedMeshRevision;
    request.baseRevision = m_loadedStateRevision;
    request.sourceTriangleCount = m_sourceTriangleCount;
    request.topologyFingerprint = m_topologyFingerprint;
    request.topology = m_workerTopology;
    request.baseState = m_committedState;
    request.labelId = m_currentLabelId;
    request.palette = m_palette;
    request.samples.push_back(std::move(sample));
    const bool queued = enqueueStroke(std::move(request));
    if (queued) {
        ModelColorPaintBridge::instance()->setGapPreviewStatus(true, 0);
    } else {
        scheduleGapPreview();
    }
    return queued;
}

bool ModelColorPaintHandler::remapFilamentsCommand(const QVariantList& mapping) {
    if (m_state != State::Ready || !m_queuedStrokes.empty() ||
        mapping.size() != static_cast<int>(m_palette.size())) return false;
    auto remapped = m_committedState;
    bool changed = false;
    for (auto& label : remapped.leafLabels) {
        if (label == ManualSupportOrcaScaffold::NoLabel ||
            label > static_cast<std::uint32_t>(mapping.size())) continue;
        bool ok = false;
        const int target = mapping[static_cast<int>(label - 1)].toInt(&ok);
        if (!ok || target < 0 || target >= mapping.size()) return false;
        const auto replacement = static_cast<std::uint32_t>(target + 1);
        changed |= replacement != label;
        label = replacement;
    }
    if (!changed) return true;
    remapped.update_used_states();
    if (!persistState(remapped, true, "Remap Model Surface Color Filaments")) return false;
    m_committedState = std::move(remapped);
    ModelColorPaintComputeCoordinator::instance()->invalidateModel(m_targetModelId);
    if (m_brushShape == QStringLiteral("gap_fill")) scheduleGapPreview();
    return true;
}

bool ModelColorPaintHandler::setDefaultFilamentSlotCommand(int slot) {
    if (m_state == State::Inactive || !m_targetMesh || slot < 1 ||
        slot > static_cast<int>(m_palette.size())) {
        return false;
    }
    auto* document = DocumentManager::instance();
    auto part = document
        ? document->getDB<ModelPartDB>(m_targetPartId)
        : nullptr;
    if (!part) return false;
    const auto requestedMode = ModelFilamentBindingMode::ExplicitSlot;
    if (part->getFilamentBindingMode() == static_cast<int>(requestedMode) &&
        part->getDefaultFilamentSlot() == slot) return true;

    TransactionGuard guard("Change Model Default Filament");
    const auto unique =
        ModelGraphUtil::ensureUniqueObjectForInstance(m_targetModelId);
    const DBInstanceID targetPartId = unique.mapPart(m_targetPartId);
    part = document
        ? document->getDB<ModelPartDB>(targetPartId)
        : nullptr;
    if (!unique || !part) {
        guard.rollback();
        return false;
    }
    m_targetPartId = targetPartId;
    m_loadedPartId = targetPartId;
    m_stateDB = part->surfaceColors();
    part->setDefaultFilamentSlot(slot);
    part->setFilamentBindingMode(static_cast<int>(requestedMode));
    ModelColorPaintBridge::instance()->setDefaultFilamentSlotProjection(slot);
    return true;
}

void ModelColorPaintHandler::syncPaletteFromActiveFilaments() {
    std::vector<QColor> palette;
    auto* document = DocumentManager::instance();
    const auto config = document && m_targetMesh
        ? document->getDB<GPlatform::SlicingConfigDB>(
              m_targetMesh->getSlicingConfigDBId())
        : nullptr;
    if (config && !config->getFilaments().empty()) {
        palette.reserve(config->getFilaments().size());
        for (const auto& filament : config->getFilaments()) {
            palette.push_back(filament.color.isValid()
                ? filament.color
                : QColor(QStringLiteral("#b0bec5")));
        }
    } else {
        const QVariantList slotItems =
            SliceSettingsBridge::instance()->filamentSlots();
        palette.reserve(static_cast<std::size_t>(slotItems.size()));
        for (const QVariant& value : slotItems) {
            QColor color = value.toMap()
                .value(QStringLiteral("color")).value<QColor>();
            if (!color.isValid()) color = QColor(QStringLiteral("#b0bec5"));
            palette.push_back(color);
        }
    }
    if (palette.empty()) return;
    m_palette = std::move(palette);

    auto* bridge = ModelColorPaintBridge::instance();
    if (m_currentLabelId > m_palette.size()) {
        m_currentLabelId = m_palette.empty() ? 0u : 1u;
        bridge->setCurrentColorIndex(static_cast<int>(m_currentLabelId));
        m_engine.set_active_paint_state(m_currentLabelId);
    }

    if (m_targetMesh && !m_palette.empty()) {
        const auto part = document
            ? document->getDB<ModelPartDB>(m_targetPartId)
            : nullptr;
        int defaultSlot = projectedFilamentSlot(
            m_targetMesh, part, m_palette.size());
        if (defaultSlot < 1 || defaultSlot > static_cast<int>(m_palette.size()))
            defaultSlot = 1;
        bridge->setDefaultFilamentSlotProjection(defaultSlot);
    }

    updateHoverMaterial();
    if (!m_strokePreviewLeaves.empty()) refreshStrokePreview();
}

QColor ModelColorPaintHandler::colorForLabel(std::uint32_t labelId) const {
    if (labelId > 0 && labelId <= m_palette.size()) return m_palette[labelId - 1];
    return QColor::fromHsv(static_cast<int>((labelId * 137u) % 360u), 190, 235);
}

int ModelColorPaintHandler::paintedFacetCount() const {
    return m_cachedPaintedFacetCount;
}

void ModelColorPaintHandler::ensureStateDB(bool createIfMissing) {
    auto* document = DocumentManager::instance();
    if (!document) return;
    const auto part = document->getDB<ModelPartDB>(m_targetPartId);
    if (!part) {
        m_stateDB.reset();
        return;
    }
    const auto currentState = part->surfaceColors();
    if (m_stateDB && m_stateDB == currentState) {
        return;
    }
    m_stateDB = currentState;

    if (m_stateDB || !createIfMissing) return;
    m_stateDB = trans::TransDB::create<ModelSurfaceColorDB>();
    if (m_stateDB) {
        if (!document->attachOwnedChild(
                part->getDBInstanceID(), m_stateDB->getDBInstanceID(),
                ModelPartDB::kSurfaceColorsRelation)) {
            document->unregisterDBInstance(m_stateDB->getDBInstanceID());
            m_stateDB.reset();
        }
    }
}

void ModelColorPaintHandler::handleCommittedModelDataChanged(qulonglong modelValue) {
    if (m_state == State::Inactive || modelValue != m_targetModelId.getValue()) return;

    auto* currentDocument = DocumentManager::instance();
    std::shared_ptr<ModelSurfaceColorDB> documentState;
    if (currentDocument) {
        if (const auto part = currentDocument->getDB<ModelPartDB>(m_targetPartId)) {
            documentState = part->surfaceColors();
        }
    }
    const bool stateStillCurrent = documentState
        ? (m_stateDB == documentState && documentState->getRevision() == m_loadedStateRevision)
        : (!m_stateDB && m_loadedStateRevision == 0);
    const auto currentPart = currentDocument
        ? currentDocument->getDB<ModelPartDB>(m_targetPartId)
        : nullptr;
    Transform::Matrix4 currentTransform = Transform::Matrix4::Identity();
    if (m_targetMesh && currentPart) {
        currentTransform = m_targetMesh->getTransformMatrix() *
            currentPart->getLocalTransform().getMatrix();
    }
    const bool meshStillCurrent = m_targetMesh && currentPart &&
        currentPart->geometry() &&
        m_loadedMeshRevision == currentPart->geometry()->revision() &&
        m_loadedTransform.isApprox(currentTransform, 0.0f);
    if (stateStillCurrent && meshStillCurrent) return;
    ModelColorPaintComputeCoordinator::instance()->invalidateModel(m_targetModelId);

    // Undo/redo and external model edits replace the document truth. Never let
    // an in-progress working snapshot overwrite that newer committed state.
    m_pointerDown = false;
    m_state = State::Ready;
    m_pendingStrokeToken = 0;
    clearStrokePreview();
    m_stateDB.reset();
    ensureStateDB(false);

    auto* document = DocumentManager::instance();
    m_targetMesh = document
        ? document->getDB<ModelInstanceDB>(m_targetModelId)
        : nullptr;
    if (!m_targetMesh) {
        onExit();
        return;
    }

    const auto reloadedPart = document
        ? document->getDB<ModelPartDB>(m_targetPartId)
        : nullptr;
    Transform::Matrix4 transform = Transform::Matrix4::Identity();
    if (reloadedPart) {
        transform = m_targetMesh->getTransformMatrix() *
            reloadedPart->getLocalTransform().getMatrix();
    }
    const bool topologyStillLoaded = m_hasLoadedTopology &&
        m_loadedModelId == m_targetModelId &&
        m_loadedPartId == m_targetPartId && reloadedPart &&
        reloadedPart->geometry() &&
        m_loadedMeshRevision == reloadedPart->geometry()->revision() &&
        m_loadedTransform.isApprox(transform, 0.0f) && m_engine.has_mesh();
    if (!topologyStillLoaded && !loadTargetMesh(m_targetMesh)) {
        onExit();
        return;
    }

    if (m_stateDB && !m_stateDB->getSurfaceColorData().empty()) {
        if (!restoreState()) {
            m_committedState.clear();
        }
        m_loadedStateRevision = m_stateDB->getRevision();
    } else {
        m_committedState.clear();
        m_cachedPaintedFacetCount = 0;
        m_loadedStateRevision = 0;
    }
    m_engine.set_active_paint_state(m_currentLabelId);
    m_engine.set_triangle_splitting_enabled(false);
    if (m_brushShape == QStringLiteral("gap_fill")) scheduleGapPreview();
    else if (m_hasHoverHit) refreshHoverPreview();
}

void ModelColorPaintHandler::startDocumentObservation() {
    stopDocumentObservation();
    auto* document = DocumentManager::instance();
    if (!document || !m_targetModelId.isValid()) return;
    QPointer<ModelColorPaintHandler> that(this);
    const DBInstanceID modelId = m_targetModelId;
    m_documentListenerId = document->addChangeListener(
        [that, modelId](const DocumentManager::ChangeNotification& notification) {
            if (!that) return;
            auto* currentDocument = DocumentManager::instance();
            const DBInstanceID partId = that->m_targetPartId;
            const auto part = currentDocument
                ? currentDocument->getDB<ModelPartDB>(partId)
                : nullptr;
            const DBInstanceID geometryId = part && part->geometry()
                ? part->geometry()->getDBInstanceID()
                : DBInstanceID{};
            const bool modelChanged =
                (notification.id == modelId &&
                 notification.changeType == ChangeType::OBJECT_DELETED) ||
                (notification.id == modelId &&
                 notification.changeType == ChangeType::PROPERTY_CHANGED &&
                 (notification.propertyName == "Transform" ||
                  notification.propertyName == ModelInstanceDB::kObjectRelation)) ||
                notification.id == partId ||
                notification.id == geometryId;
            bool colorsChanged = false;
            if (notification.dbType == TypeID::MODEL_SURFACE_COLOR_DB &&
                notification.changeType != ChangeType::OBJECT_DELETED) {
                const DBInstanceID owner = currentDocument
                    ? currentDocument->getOwner(notification.id)
                    : DBInstanceID();
                colorsChanged = owner == partId &&
                    (notification.changeType == ChangeType::OBJECT_CREATED ||
                    (notification.changeType == ChangeType::PROPERTY_CHANGED &&
                      notification.propertyName == "Revision"));
            }
            if (!modelChanged && !colorsChanged) return;
            QMetaObject::invokeMethod(
                that,
                [that, modelId] {
                    if (that) that->handleCommittedModelDataChanged(modelId.getValue());
                },
                Qt::QueuedConnection);
        });
}

void ModelColorPaintHandler::stopDocumentObservation() {
    if (m_documentListenerId == 0) return;
    if (auto* document = DocumentManager::instance()) {
        document->removeChangeListener(m_documentListenerId);
    }
    m_documentListenerId = 0;
}

bool ModelColorPaintHandler::restoreState() {
    if (!m_stateDB || m_stateDB->getSurfaceColorData().empty()) return false;
    if (m_stateDB->getDataVersion() >
        static_cast<int>(ModelSurfaceColorCodec::DataVersion)) {
        LOG_ERROR("ModelColorPaintHandler: unsupported state version {} for model={}",
                  m_stateDB->getDataVersion(), m_targetModelId.getValue());
        return false;
    }
    if (m_stateDB->getSourceTriangleCount() > 0 &&
        m_stateDB->getSourceTriangleCount() != m_sourceTriangleCount) {
        LOG_ERROR("ModelColorPaintHandler: topology triangle count mismatch model={} stored={} current={}",
                  m_targetModelId.getValue(), m_stateDB->getSourceTriangleCount(),
                  m_sourceTriangleCount);
        return false;
    }
    if (!m_stateDB->getTopologyFingerprint().empty() &&
        m_stateDB->getTopologyFingerprint() != m_topologyFingerprint) {
        LOG_ERROR("ModelColorPaintHandler: topology fingerprint mismatch model={}",
                  m_targetModelId.getValue());
        return false;
    }
    if (!decodeState(m_stateDB->getSurfaceColorData(), &m_committedState)) {
        LOG_ERROR("ModelColorPaintHandler: invalid paint state for model={}",
                  m_targetModelId.getValue());
        return false;
    }
    m_cachedPaintedFacetCount = static_cast<int>(std::count_if(
        m_committedState.leafLabels.begin(), m_committedState.leafLabels.end(),
        [](const auto label) { return label != ManualSupportOrcaScaffold::NoLabel; }));
    QVariantList palette;
    for (const QColor& color : m_palette) palette.push_back(color.name(QColor::HexArgb));
    ModelColorPaintBridge::instance()->setPalette(palette);
    return true;
}

bool ModelColorPaintHandler::persistState(
    const ManualSupportOrcaScaffold::TriangleSplittingData& state,
    bool transaction,
    const char* description) {
    std::string blob;
    if (!encodeState(state, &blob)) return false;

    if (transaction) {
        TransactionGuard guard(description ? description : "Model Surface Colors");
        const auto unique =
            ModelGraphUtil::ensureUniqueObjectForInstance(m_targetModelId);
        const DBInstanceID targetPartId = unique.mapPart(m_targetPartId);
        if (!unique || !targetPartId.isValid()) {
            guard.rollback();
            return false;
        }
        if (auto* document = DocumentManager::instance()) {
            if (const auto object =
                    document->getDB<ModelObjectDB>(unique.objectId)) {
                object->setUseSourceAppearance(false);
            }
        }
        if (targetPartId != m_targetPartId) {
            m_targetPartId = targetPartId;
            m_loadedPartId = targetPartId;
            m_stateDB.reset();
        }
        ensureStateDB(true);
        if (!m_stateDB) {
            guard.rollback();
            return false;
        }
        const int revision = m_stateDB->getRevision() + 1;
        m_stateDB->setSurfaceColorData(blob);
        m_stateDB->setSourceTriangleCount(m_sourceTriangleCount);
        m_stateDB->setTopologyFingerprint(m_topologyFingerprint);
        m_stateDB->setDataVersion(
            static_cast<int>(ModelSurfaceColorCodec::DataVersion));
        m_stateDB->setRevision(revision);
        m_loadedStateRevision = revision;
    } else {
        return true;
    }
    return true;
}

bool ModelColorPaintHandler::encodeState(
    const ManualSupportOrcaScaffold::TriangleSplittingData& state, std::string* blob) {
    return ModelSurfaceColorCodec::encode(state, blob);
}

bool ModelColorPaintHandler::decodeState(
    const std::string& blob, ManualSupportOrcaScaffold::TriangleSplittingData* state) {
    return ModelSurfaceColorCodec::decode(blob, state);
}
