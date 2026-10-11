#include "FiberMaskHandler.hpp"
#include <TransientPolyDataActorDB.hpp>
#include "InteractionRegistration.hpp"
#include "InteractionRuntime.hpp"
#include "ModelInteractionHandler.hpp"
#include <ActionHandlerRegistry.hpp>
#include <ActionManager.hpp>
#include <FiberMaskBridge.hpp>
#include <FiberMaskGeometryUtils.hpp>
#include <ModelInstanceDB.hpp>
#include <SelectionBridge.hpp>
#include <SliceSettingsBridge.hpp>
#include <TransactionManager.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <WindowDB.hpp>
#include <CameraDB.hpp>
#include <CameraNavigationController.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <ModelPartDB.hpp>
#include <ModelGeometryDB.hpp>
#include <vtkCellData.h>
#include <vtkPointData.h>
#include <vtkRegularPolygonSource.h>
#include <vtkFeatureEdges.h>
#include <vtkTriangleFilter.h>
#include <vtkTubeFilter.h>
#include <vtkUnsignedCharArray.h>
#include <vtkAppendPolyData.h>
#include <vtkArrowSource.h>
#include <vtkSphereSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkPolyData.h>
#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

REGISTER_PERSISTENT_INTERACTION_ACTION(FiberMaskHandler, "model.fiberMask")
namespace {
using namespace GPlatform::Interaction;
using Shape = FiberMaskDB::Shape;
void colorCells(vtkPolyData* mesh, Vector3 color, unsigned char alpha) {
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("RGBA");
    colors->SetNumberOfComponents(4);
    unsigned char rgba[4]{static_cast<unsigned char>(std::clamp(color.x, 0.f, 1.f) * 255),
                          static_cast<unsigned char>(std::clamp(color.y, 0.f, 1.f) * 255),
                          static_cast<unsigned char>(std::clamp(color.z, 0.f, 1.f) * 255), alpha};
    for (vtkIdType i = 0; i < mesh->GetNumberOfCells(); ++i)
        colors->InsertNextTypedTuple(rgba);
    mesh->GetCellData()->SetScalars(colors);
}

vtkSmartPointer<vtkPolyData> previewGeometry(const FiberMaskDB::Region& r,
                                           const Vector3& color) {
    auto body = FiberMaskGeometryUtils::createMesh(r.shape, r.dimensions);
    if (!body || body->GetNumberOfCells() == 0) return body;
    auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
    edges->SetInputData(body);
    edges->BoundaryEdgesOn();
    edges->FeatureEdgesOn();
    edges->SetFeatureAngle(25);
    edges->ManifoldEdgesOff();
    edges->NonManifoldEdgesOff();
    edges->Update();
    auto edgeData = vtkSmartPointer<vtkPolyData>::New();
    edgeData->ShallowCopy(edges->GetOutput());
    edgeData->GetPointData()->SetNormals(nullptr);
    auto tubes = vtkSmartPointer<vtkTubeFilter>::New();
    tubes->SetInputData(edgeData);
    tubes->SetRadius(std::min({r.dimensions.x, r.dimensions.y, r.dimensions.z}) * .004);
    tubes->SetNumberOfSides(8);
    tubes->CappingOn();
    auto outline = vtkSmartPointer<vtkPolyData>::New();
    if (r.shape != Shape::Sphere) {
        tubes->Update();
        outline->DeepCopy(tubes->GetOutput());
    }
    colorCells(outline, color, 255);
    auto append = vtkSmartPointer<vtkAppendPolyData>::New();
    auto fill = vtkSmartPointer<vtkPolyData>::New();
    fill->DeepCopy(body);
    colorCells(fill, color, 70);
    append->AddInputData(fill);
    append->AddInputData(outline);
    if (r.shape == Shape::Sphere)
        for (int axis = 0; axis < 3; ++axis) {
            auto circle = vtkSmartPointer<vtkRegularPolygonSource>::New();
            circle->SetRadius(r.dimensions.x * .5);
            circle->SetNumberOfSides(96);
            circle->GeneratePolygonOff();
            circle->GeneratePolylineOn();
            circle->SetNormal(axis == 0, axis == 1, axis == 2);
            auto ring = vtkSmartPointer<vtkTubeFilter>::New();
            ring->SetInputConnection(circle->GetOutputPort());
            ring->SetRadius(r.dimensions.x * .004);
            ring->SetNumberOfSides(8);
            ring->Update();
            auto data = vtkSmartPointer<vtkPolyData>::New();
            data->DeepCopy(ring->GetOutput());
            colorCells(data, color, 255);
            append->AddInputData(data);
        }
    auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
    triangles->SetInputConnection(append->GetOutputPort());
    triangles->Update();
    auto result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(triangles->GetOutput());
    return result;
}
void configure(const std::shared_ptr<TransientPolyDataActorDB>& actor) {
    actor->setPickable(false);
    actor->setDragable(false);
    actor->setUseCellColors(true);
    actor->setOpacity(1);
    if (auto material = actor->getMaterial()) {
        material->setLighting(false);
        material->setOpacity(1);
    }
}
Vector3 point(const Eigen::Vector3f& p) {
    return {p.x(), p.y(), p.z()};
}
Eigen::Vector3f vec(Vector3 p) {
    return {p.x, p.y, p.z};
}
Vector3 transformed(const Transform::Matrix4& m, Vector3 p) {
    return point((m * Eigen::Vector4f(p.x, p.y, p.z, 1)).head<3>());
}
} // namespace

namespace {
std::shared_ptr<ModelInteractionHandler> modelInteraction() {
    return std::dynamic_pointer_cast<ModelInteractionHandler>(
        ActionHandlerRegistry::instance().getHandler("interaction.model"));
}
}
FiberMaskDB::Region FiberMaskHandler::region() const {
    auto value = m_draft.region;
    if (!m_preview) return value;
    if (!Eigen::FullPivLU<Transform::Matrix4>(m_draft.ownerWorld).isInvertible()) return {};
    value.pose.setMatrix((m_draft.ownerWorld.inverse() * m_preview->getTransformMatrix()).eval());
    return value;
}
void FiberMaskHandler::updateRegion(const std::function<void(FiberMaskDB::Region&)>& update) {
    auto value = region();
    update(value);
    const auto presenting = std::exchange(m_presenting, true);
    m_draft.region = value;
    const auto owner = model();
    m_draft.ownerWorld = owner ? owner->getTransformMatrix() : Transform::Matrix4::Identity();
    Transform world;
    world.setMatrix((m_draft.ownerWorld * value.pose.getMatrix()).eval());
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    m_preview->setTransform(world);
    m_presenting = presenting;
}
void FiberMaskHandler::loadDraft(const FiberMaskDB& source) {
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    m_draft.parameterOverrides = source.getParameterOverridesJson();
    m_draft.name = QString::fromStdString(source.getDisplayName());
    m_draft.enabled = source.getEnabled();
    updateRegion([&](auto& value) { value = source.region(); });
    m_preview->setVisible(source.getVisible());
}
bool FiberMaskHandler::objectDragActive() const {
    const auto handler = modelInteraction();
    return isActive() && handler && handler->isDragging();
}
void FiberMaskHandler::updatePointerScope() {
    const auto handler = modelInteraction();
    if (!handler || !isActive()) return;
    ModelInteractionHandler::PointerScope scope;
    scope.viewId = m_viewId;
    scope.clearOnBlankClick = false;
    // Equal-depth overlapping faces keep the selected mask draggable.
    if (m_preview && m_preview->isVisible() && m_stage == Stage::Review)
        scope.candidates.push_back(m_preview->getDBInstanceID());
    for (const auto& [id, preview] : m_previews)
        if (preview != m_preview && preview->isVisible()) scope.candidates.push_back(preview->getDBInstanceID());
    scope.beforeSelect = [this](DBInstanceID id) {
        return m_preview && (id == m_preview->getDBInstanceID() || savePendingEdit());
    };
    scope.started = [this](DBInstanceID) {
        TransientUpdateGuard transient;
        ImmediateNotifyGuard immediate;
        m_error.clear();
        m_drawingEnabled = false;
        m_snapPreview.clear();
        if (m_reticle) m_reticle->setVisible(false);
        publish();
    };
    scope.finished = [this](DBInstanceID id, bool moved, const Transform& initialTransform) {
        if (!isActive()) return;
        if (!m_preview || id != m_preview->getDBInstanceID()) return;
        if (moved && !commit()) {
            TransientUpdateGuard transient;
            ImmediateNotifyGuard immediate;
            const auto presenting = std::exchange(m_presenting, true);
            m_preview->setTransform(initialTransform);
            m_presenting = presenting;
            // The rejected position is gone. Re-evaluate geometry, preserving parameter errors.
            m_error = validationError();
        }
        rebuildPreview();
        publish();
    };
    handler->setPointerScope(this, std::move(scope));
}

ViewportProjectionSnapshot FiberMaskHandler::viewProjection() const {
    auto* doc = DocumentManager::instance();
    const auto window = doc ? doc->getDB<WindowDB>(m_viewId) : nullptr;
    if (!window)
        return {};
    window->updateActiveCamera();
    auto coordinates = window->getCoordinateSystem();
    return coordinates ? coordinates->projectionSnapshot() : ViewportProjectionSnapshot{};
}

FiberMaskHandler::FiberMaskHandler(QObject* parent) : StandardActionHandler(parent) {
    connect(FiberMaskBridge::instance(), &FiberMaskBridge::requested, this,
            &FiberMaskHandler::request);
    connect(SelectionBridge::instance(), &SelectionBridge::selectionChanged, this,
            [this](const QVariantList& ids) {
        if (!isActive() || m_committing || ids.size() != 1) return;
        const DBInstanceID selectedId(ids.front().toLongLong());
        for (const auto& [maskId, actor] : m_previews) {
            if (actor != m_preview && actor->getDBInstanceID() == selectedId) {
                selectMask(maskId);
                break;
            }
        }
    });
    connect(SliceSettingsBridge::instance(), &SliceSettingsBridge::settingsChanged, this, [this] {
        if (isActive()) {
            loadConfig();
            publish();
        }
    });
}
FiberMaskHandler::~FiberMaskHandler() {
    // The registry may already be destroying its handler factories. Destruction
    // releases owned resources; normal onExit performs interaction/selection actions.
    if (m_listener)
        DocumentManager::instance()->removeChangeListener(m_listener);
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    m_snapPreview.clear();
    m_scope.clear();
}
bool FiberMaskHandler::supportsEnvironment(const QString& env) const {
    return env == "normal" || env == "editing";
}
std::shared_ptr<ModelInstanceDB> FiberMaskHandler::model() const {
    auto* doc = DocumentManager::instance();
    return doc ? doc->getDB<ModelInstanceDB>(m_modelId) : nullptr;
}
void FiberMaskHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    ++m_sessionGeneration;
    m_modelId =
        DBInstanceID(getParams()
                         .value("modelId", SelectionBridge::instance()->selectedId().getValue())
                         .toULongLong());
    if (!model() || !model()->isValid() || model()->isTempDB()) {
        context->setError(ActionErrorCode::TargetRequired,
                          tr("Select a model before creating a fiber mask."));
        onExit();
        return;
    }
    if (!ActionManager::getInstance()->triggerAction("interaction.model")) {
        context->setError(ActionErrorCode::SystemUnavailable, tr("Cannot start the fiber mask tool."));
        onExit();
        return;
    }
    synchronizePreviews();
    rebuildSnapTargets();
    m_listener = DocumentManager::instance()->addChangeListener(
        [this](const DocumentManager::ChangeNotification& change) {
            if (!isActive() || m_committing || m_presenting ||
                (change.id != m_modelId && change.dbType != TypeID::FIBER_MASK_DB &&
                 (!m_preview || change.id != m_preview->getDBInstanceID())))
                return;
            if (m_preview && change.id == m_preview->getDBInstanceID()) {
                if (change.propertyName == "Transform") {
                    updateRegion([](auto&) {});
                    rebuildPreview();
                    publish();
                }
                return;
            }
            if (change.dbType == TypeID::FIBER_MASK_DB &&
                change.propertyName == "WorldBounds")
                return;
            if (change.id == m_editId && change.propertyName != "Visible")
                m_reloadSelected = true;
            if (m_refreshPending)
                return;
            m_refreshPending = true;
            const auto generation = m_sessionGeneration;
            QTimer::singleShot(0, this, [this, generation] {
                if (generation != m_sessionGeneration)
                    return;
                m_refreshPending = false;
                if (!isActive())
                    return;
                if (!model()) {
                    emit requestExit();
                    return;
                }
                auto* doc = DocumentManager::instance();
                const bool lostOwner = m_editId.isValid() &&
                    (!doc->getDB<FiberMaskDB>(m_editId) || doc->getOwner(m_editId) != m_modelId);
                if (objectDragActive() && (m_reloadSelected || lostOwner ||
                    !model()->getTransformMatrix().isApprox(m_draft.ownerWorld)))
                    ActionManager::getInstance()->triggerAction("modelInteraction.cancelDrag");
                if (lostOwner)
                    startNew(0);
                synchronizePreviews(true);
                rebuildSnapTargets();
                if (std::exchange(m_reloadSelected, false) && m_editId.isValid() && !m_dragging)
                    selectMask(m_editId, true);
                rebuildPreview();
                publish();
            });
        });
    m_sectionZ = model()->localBounds().getCenter().z;
    m_viewId = DocumentManager::instance()->getActiveWindowId();
    m_planeIndex = 0;
    orientTopView();
    startNew(0);
    const DBInstanceID maskId(getParams().value("maskId").toULongLong());
    if (maskId.isValid())
        selectMask(maskId);
}
void FiberMaskHandler::orientTopView() {
    auto camera = CameraNavigationController::currentCamera();
    const auto owner = model();
    if (!camera || !owner)
        return;
    const auto bounds = owner->worldBounds();
    if (!bounds.valid)
        return;
    TransientUpdateGuard guard;
    ImmediateNotifyGuard immediate;
    const auto center = bounds.getCenter();
    const auto linear = owner->getTransformMatrix().block<3, 3>(0, 0).eval();
    // Match the model-local XY drawing plane, including tilted/scaled models.
    const Eigen::Vector3f normal =
        (linear.inverse().transpose() * Eigen::Vector3f::UnitZ()).normalized();
    const Eigen::Vector3f up = (linear * Eigen::Vector3f::UnitY()).normalized();
    const Eigen::Vector3f right = up.cross(normal).normalized();
    const auto localSize = vec(owner->localBounds().getSize());
    const auto span = [&](const Eigen::Vector3f& axis) {
        return (linear.transpose() * axis).cwiseAbs().dot(localSize);
    };
    const float aspect = std::max(.1f, camera->getAspectRatio());
    const float extent = std::max({span(right) / aspect, span(up), 1.f});
    const float distance = std::max(camera->getDistanceToTarget(),
        extent * .7f / std::tan(camera->getFieldOfView() * float(M_PI / 360.)) + span(normal) * .5f);
    camera->setOrbitMode(true);
    camera->setOrbitCenter(center);
    camera->setOrbitRadius(distance);
    camera->setHorizontalAngle(std::hypot(normal.x(), normal.y()) < 1.e-6f
                                  ? float(-M_PI * .5) : std::atan2(normal.y(), normal.x()));
    camera->setVerticalAngle(std::asin(std::clamp(normal.z(), -1.f, 1.f)));
    camera->setTarget(center);
    camera->setPosition(center + point(normal * distance));
    camera->setUpVector(point(up));
    if (camera->isOrthographic())
        camera->setOrthographicSize(std::max(camera->getOrthographicSize(), extent * 1.4f));
}
void FiberMaskHandler::onExit() {
    const auto ownerId = m_modelId;
    setActive(false);
    ++m_sessionGeneration;
    m_refreshPending = false;
    m_reloadSelected = false;
    if (m_listener) {
        if (auto* doc = DocumentManager::instance())
            doc->removeChangeListener(m_listener);
        m_listener = 0;
    }
    // This also cancels the gesture, without routing through an action that may
    // already be unavailable in the destination environment.
    if (const auto handler = modelInteraction()) handler->clearPointerScope(this);
    auto* document = DocumentManager::instance();
    const auto owner = document ? document->getDB<ModelInstanceDB>(ownerId) : nullptr;
    auto* manager = ActionManager::getInstance();
    bool restored = false;
    if (owner && manager && manager->isAcceptingActions() &&
        (manager->currentEnvironment() == "normal" || manager->currentEnvironment() == "editing")) {
        restored = manager->triggerAction("selection.pick", {{"modelId", ownerId.getValue()}});
    }
    if (owner && !restored) {
        SelectionBridge::instance()->setSelectedObjects({ownerId});
    } else if (!owner) {
        auto selected = SelectionBridge::instance()->getSelectedIds();
        const auto& ownedIds = m_scope.ids();
        selected.erase(std::remove_if(selected.begin(), selected.end(), [&](auto id) {
            return std::find(ownedIds.begin(), ownedIds.end(), id) != ownedIds.end();
        }), selected.end());
        SelectionBridge::instance()->setSelectedObjects(selected);
    }
    m_snapResult.reset();
    {
        TransientUpdateGuard guard;
        ImmediateNotifyGuard immediate;
        m_snapPreview.clear();
        m_preview.reset();
        m_draft = Draft{};
        m_previews.clear();
        m_previewGeometries.clear();
        m_handle.reset();
        m_reticle.reset();
        m_scope.clear();
    }
    m_sectionModel = nullptr;
    m_showSection = false;
    m_targets.clear();
    m_modelId = INVALID_DB_ID;
    m_editId = INVALID_DB_ID;
    m_viewId = INVALID_DB_ID;
    m_cameraGestureView = INVALID_DB_ID;
    m_maskPressed = false;
    m_dragging = false;
    m_parameterErrors.clear();
    m_invalidParameterInputs.clear();
    StandardActionHandler::onExit();
    FiberMaskBridge::instance()->project({{"active", false}});
}
void FiberMaskHandler::synchronizePreviews(bool synchronizeAll) {
    auto* doc = DocumentManager::instance();
    if (!doc) return;
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    const auto presenting = std::exchange(m_presenting, true);
    for (auto id : doc->getOwnedChildren(m_modelId, FiberMaskDB::kOwnerRelation)) {
        const auto mask = doc->getDB<FiberMaskDB>(id);
        if (!mask) continue;
        auto& preview = m_previews[id];
        if (!preview) {
            preview = createPreview();
            loadPreview(preview, *mask);
        } else if (synchronizeAll && preview == m_preview) {
            const auto localRegion = region();
            updateRegion([&](auto& value) { value = localRegion; });
        } else if (synchronizeAll) {
            loadPreview(preview, *mask);
        }
        preview->setVisible(mask->getVisible());
    }
    for (auto it = m_previews.begin(); it != m_previews.end();) {
        if (!doc->getDB<FiberMaskDB>(it->first) || doc->getOwner(it->first) != m_modelId) {
            m_previewGeometries.erase(it->second->getDBInstanceID());
            doc->unregisterDBInstance(it->second->getDBInstanceID());
            it = m_previews.erase(it);
        } else ++it;
    }
    m_presenting = presenting;
}

void FiberMaskHandler::startNew(int shape, bool copy) {
    if (shape < 0 || shape > 2)
        return;
    if (objectDragActive())
        ActionManager::getInstance()->triggerAction("modelInteraction.cancelDrag");
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    auto previous = m_preview;
    const auto copyRegion = region();
    m_presenting = true;
    if (previous && !m_editId.isValid()) {
        m_previewGeometries.erase(previous->getDBInstanceID());
        DocumentManager::instance()->unregisterDBInstance(previous->getDBInstanceID());
    } else if (previous)
        setPreviewGeometry(previous, copyRegion, {.12f, .67f, 1.f});
    m_preview = createPreview();
    if (!copy) m_draft = Draft{};
    updateRegion([&](auto& value) { value = copy ? copyRegion : FiberMaskDB::Region{}; });
    m_presenting = false;
    if (!copy) {
        updateRegion([&](auto& r) { r.shape = Shape(shape); });
        const auto* doc = DocumentManager::instance();
        int number = 1;
        const auto children = doc->getOwnedChildren(m_modelId, FiberMaskDB::kOwnerRelation);
        for (;;) {
            m_draft.name = tr("Fiber mask %1").arg(number++);
            const bool exists = std::any_of(children.begin(), children.end(), [&](auto id) {
                const auto mask = doc->getDB<FiberMaskDB>(id);
                return mask && QString::fromStdString(mask->getDisplayName()) == m_draft.name;
            });
            if (!exists)
                break;
        }
    }
    m_copyPlacement = copy;
    m_editId = INVALID_DB_ID;
    m_stage = Stage::Place;
    ActionManager::getInstance()->triggerAction("selection.pick", {{"modelId", m_modelId.getValue()}});
    m_reloadSelected = false;
    m_dragAxis = -1;
    m_dragging = false;
    m_error.clear();
    m_parameterErrors.clear();
    m_invalidParameterInputs.clear();
    m_depth = 0;
    if (!copy && shape == 0) {
        const auto bounds = model()->localBounds();
        updateRegion([&](auto& r) {
            r.dimensions.z = bounds.getSize().z;
            auto position = r.pose.getPosition();
            position.z = bounds.getCenter().z;
            r.pose.setPosition(position);
        });
        m_depth = region().dimensions.z;
        m_planeIndex = 0;
    }
    m_drawingEnabled = false;
    m_maskPressed = false;
    m_plane = Transform();
    if (m_planeIndex == 1)
        m_plane.rotateX(90);
    if (m_planeIndex == 2)
        m_plane.rotateY(90);
    // Free drawing uses an explicit model-local plane, independent of surface picking.
    m_plane.setPosition(model()->localBounds().getCenter());
    m_snapResult.reset();
    m_snapPreview.clear();
    if (m_reticle)
        m_reticle->setVisible(false);
    loadConfig();
    rebuildPreview();
    publish();
}
bool FiberMaskHandler::selectMask(DBInstanceID id, bool reload) {
    auto* doc = DocumentManager::instance();
    const auto mask = doc->getDB<FiberMaskDB>(id);
    if (!mask || doc->getOwner(id) != m_modelId)
        return false;
    if ((reload || id != m_editId) && objectDragActive())
        ActionManager::getInstance()->triggerAction("modelInteraction.cancelDrag");
    if (!reload) {
        if (id == m_editId)
            return true;
        if (!savePendingEdit())
            return false;
    }
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    synchronizePreviews();
    if (m_preview && !m_editId.isValid()) {
        TransientUpdateGuard transient;
        ImmediateNotifyGuard immediate;
        m_previewGeometries.erase(m_preview->getDBInstanceID());
        doc->unregisterDBInstance(m_preview->getDBInstanceID());
    }
    if (m_preview && m_preview != m_previews.at(id))
        setPreviewGeometry(m_preview, region(), {.12f, .67f, 1.f});
    m_preview = m_previews.at(id);
    const auto presenting = std::exchange(m_presenting, true);
    loadDraft(*mask);
    m_presenting = presenting;
    m_editId = id;
    m_reloadSelected = false;
    m_dragAxis = -1;
    m_stage = Stage::Review;
    m_depth = region().dimensions.z;
    m_copyPlacement = false;
    m_dragging = false;
    m_error.clear();
    m_parameterErrors.clear();
    m_invalidParameterInputs.clear();
    loadConfig();
    m_savedParameterOverrides = m_draft.parameterOverrides;
    ActionManager::getInstance()->triggerAction("selection.pick", {{"dbId", m_preview->getDBInstanceID().getValue()}});
    rebuildPreview();
    publish();
    return true;
}
bool FiberMaskHandler::hasPendingEdit() const {
    const auto mask = DocumentManager::instance()->getDB<FiberMaskDB>(m_editId);
    if (!mask)
        return true;
    const auto value = region();
    const auto samePose = [](const Transform::Matrix4& a, const Transform::Matrix4& b) {
        return a.allFinite() && b.allFinite() &&
               (a.block<3, 3>(0, 0) - b.block<3, 3>(0, 0)).cwiseAbs().maxCoeff() <= 1.e-6f &&
               (a.block<3, 1>(0, 3) - b.block<3, 1>(0, 3)).cwiseAbs().maxCoeff() <= 1.e-5f;
    };
    const Transform::Matrix4 saved = mask->getTransformMatrix();
    const Transform::Matrix4 current = value.pose.getMatrix();
    bool unchangedPose = samePose(saved, current);
    if (!unchangedPose && m_preview &&
        Eigen::FullPivLU<Transform::Matrix4>(m_draft.ownerWorld).isInvertible()) {
        // Loading a preview adds a float local/world/local round trip. Match
        // that representation too, without making tolerances depend on position.
        // The direct comparison above also covers the just-saved local pose.
        const Transform::Matrix4 savedWorld = (m_draft.ownerWorld * saved).eval();
        const Transform::Matrix4 comparable = (m_draft.ownerWorld.inverse() * savedWorld).eval();
        unchangedPose = samePose(comparable, current);
    }
    const auto d = mask->getDimensions(), draftDimensions = value.dimensions;
    const auto name = m_draft.name.trimmed().isEmpty() ? tr("Fiber mask") : m_draft.name;
    return mask->getShape() != int(value.shape) ||
           d.x != draftDimensions.x || d.y != draftDimensions.y || d.z != draftDimensions.z ||
           !unchangedPose ||
           QString::fromStdString(mask->getDisplayName()) != name ||
           mask->getEnabled() != m_draft.enabled || m_savedParameterOverrides != m_draft.parameterOverrides;
}
bool FiberMaskHandler::savePendingEdit() {
    FiberMaskBridge::instance()->flushInputs();
    // A saved selection must not lose edits when selecting another mask or starting a rectangle.
    if (m_editId.isValid() && (hasPendingEdit() || !m_parameterErrors.isEmpty()))
        return commit();
    return true;
}
QString FiberMaskHandler::validationError() const {
    if (!model())
        return tr("The source model is no longer available.");
    if (m_editId.isValid() &&
        (!DocumentManager::instance()->getDB<FiberMaskDB>(m_editId) ||
         DocumentManager::instance()->getOwner(m_editId) != m_modelId))
        return tr("The mask no longer belongs to this model.");
    if (m_stage != Stage::Review)
        return tr("Finish drawing the mask before confirming.");
    const auto value = region();
    if (!FiberMaskDB::validRegion(value))
        return tr("Mask geometry must have valid dimensions and a rigid local transform.");
    return {};
}
void FiberMaskHandler::publish() {
    if (!isActive() || !model() || !m_preview)
        return;
    const auto p = region().pose.getPosition(), d = region().dimensions,
               r = region().pose.getEuler();
    const auto validation = validationError();
    const auto configError = parameterError();
    FiberMaskBridge::instance()->project(
        {{"active", true},
         {"modelId", QVariant::fromValue<qulonglong>(m_modelId.getValue())},
         {"selectedMaskId", QVariant::fromValue<qulonglong>(m_editId.getValue())},
         {"stage", int(m_stage)},
         {"shape", int(region().shape)},
         {"plane", m_planeIndex},
         {"name", m_draft.name},
         {"modelName", QString::fromStdString(model()->getDisplayName())},
         {"editing", m_editId.isValid()},
         {"canConfirm", validation.isEmpty() && m_error.isEmpty() && configError.isEmpty() && !m_dragging && !objectDragActive()},
         {"error", !configError.isEmpty() ? configError :
                       (m_error.isEmpty() && m_stage == Stage::Review ? validation : m_error)},
         {"snapped", m_snapResult.has_value()},
         {"dragging", objectDragActive()},
         {"drawingEnabled", m_drawingEnabled && !m_cameraGestureView.isValid()},
         {"x", p.x},
         {"y", p.y},
         {"z", p.z},
         {"width", d.x},
         {"length", d.y},
         {"height", d.z},
         {"rx", r.x},
         {"ry", r.y},
         {"rz", r.z},
         {"depth", m_depth},
         {"enabled", m_draft.enabled},
         {"showSection", m_showSection},
         {"sectionZ", m_sectionZ},
         {"section", sectionPreview()}},
        parameterProjection());
}
void FiberMaskHandler::rebuildSnapTargets() {
    m_targets.clear();
    m_snapResult.reset();
    const auto owner = model();
    if (!owner)
        return;
    SnapTarget target;
    target.db = owner;
    target.instanceId = m_modelId;
    target.localToWorld = owner->getTransformMatrix();
    m_targets.push_back(target);
}
Vector3 FiberMaskHandler::snap(Vector3 p, const SnapConstraint& constraint,
                               Qt::KeyboardModifiers modifiers) {
    const auto owner = model();
    if (!owner)
        return p;
    const auto view = viewProjection();
    if (!view.isValid())
        return p;
    SnapQuery query;
    query.worldPosition = transformed(owner->getTransformMatrix(), p);
    query.projection = view;
    query.constraint = constraint;
    if (!modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::AltModifier)) {
        m_snapResult.reset();
        m_snapPreview.clear();
        updateReticle();
        return p;
    }
    m_snapResult = m_snapService.query(query, m_targets, m_snapResult);
    SnapPointerQuery display;
    display.projection = view;
    display.position = m_pointer;
    m_snapPreview.setShowCursor(true);
    m_snapPreview.show(m_viewId, display, m_targets, m_snapResult);
    updateReticle();
    return m_snapResult
               ? transformed(owner->getTransformMatrix().inverse(), m_snapResult->worldPosition)
               : p;
}
std::optional<Vector3> FiberMaskHandler::planePoint(const QPoint& mouse) {
    const auto view = viewProjection();
    if (!view.isValid() || !model())
        return {};
    const auto ray = view.rayFromScreen(mouse);
    const auto matrix = (model()->getTransformMatrix() * m_plane.getMatrix()).eval();
    const Eigen::Vector3f normal =
        matrix.block<3, 3>(0, 0).inverse().transpose() * Eigen::Vector3f(0, 0, 1);
    const auto hit = FiberMaskGeometryUtils::pointOnPlane(
        ray.origin, ray.direction, point(matrix.block<3, 1>(0, 3)), point(normal));
    return hit ? std::optional<Vector3>(transformed(model()->getTransformMatrix().inverse(), *hit))
               : std::nullopt;
}
std::optional<Vector3> FiberMaskHandler::placementPoint(const QPoint& mouse,
                                                        Qt::KeyboardModifiers modifiers) {
    m_drawingEnabled = modifiers.testFlag(Qt::ControlModifier);
    m_snapResult.reset();
    m_snapPreview.clear();
    const auto owner = model();
    const auto view = viewProjection();
    if (!owner || !view.isValid())
        return {};
    if (!m_drawingEnabled)
        return {};
    // Ctrl enables drawing. Model features/surfaces can locate the start;
    // empty space uses the chosen drawing plane.
    SnapPointerQuery query;
    query.projection = view;
    query.position = mouse;
    query.preferPoints = true;
    if (!modifiers.testFlag(Qt::AltModifier))
        m_snapResult = m_snapService.query(query, m_targets);
    if (m_snapResult) {
        m_snapPreview.setShowCursor(true);
        m_snapPreview.show(m_viewId, query, m_targets, m_snapResult);
        return transformed(owner->getTransformMatrix().inverse(), m_snapResult->worldPosition);
    }
    if (region().shape == Shape::Box)
        return planePoint(mouse);
    const auto ray = view.rayFromScreen(mouse);
    const auto hit = owner->intersectWorldRay(ray.origin, ray.direction);
    return hit ? std::optional<Vector3>(hit->objectLocalPosition) : planePoint(mouse);
}
void FiberMaskHandler::refreshPointer(Qt::KeyboardModifiers modifiers) {
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    m_drawingEnabled = modifiers.testFlag(Qt::ControlModifier);
    if (!m_drawingEnabled || m_cameraGestureView.isValid()) {
        m_snapResult.reset();
        m_snapPreview.clear();
        if (m_reticle)
            m_reticle->setVisible(false);
        publish();
        return;
    }
    if (m_dragging) {
        updatePointer(m_pointer, modifiers);
    } else if (m_stage == Stage::Place && model()) {
        const auto p = placementPoint(m_pointer, modifiers);
        updateReticle(p ? std::optional<Vector3>(transformed(model()->getTransformMatrix(), *p))
                        : std::nullopt);
        publish();
    }
}
std::optional<float> FiberMaskHandler::axisParameter(const QPoint& mouse, Vector3 origin,
                                                     Vector3 axis) {
    const auto view = viewProjection();
    if (!view.isValid() || !model())
        return {};
    const auto ray = view.rayFromScreen(mouse);
    const auto world = model()->getTransformMatrix();
    const auto o = vec(transformed(world, origin));
    const Eigen::Vector3f raw = world.block<3, 3>(0, 0) * vec(axis);
    const Eigen::Vector3f a = raw.normalized(), d = vec(ray.direction).normalized(),
                          offset = o - vec(ray.origin);
    const float dot = a.dot(d), denominator = 1 - dot * dot;
    if (denominator < 1.e-4f || raw.norm() < 1.e-8f)
        return {};
    return (dot * d.dot(offset) - a.dot(offset)) / denominator / raw.norm();
}
void FiberMaskHandler::updateFromFootprint(Vector3 end) {
    m_end = end;
    const auto local = transformed(m_plane.getMatrix().inverse(), end);
    Transform pose = m_plane;
    if (region().shape == Shape::Box) {
        const auto bounds = model()->localBounds();
        updateRegion([&](auto& r) { r.dimensions = {std::abs(local.x), std::abs(local.y), bounds.getSize().z}; });
        auto center = transformed(m_plane.getMatrix(), {local.x * .5f, local.y * .5f, 0});
        center.z = bounds.getCenter().z;
        pose.setPosition(center);
    } else {
        const float diameter = 2 * std::hypot(local.x, local.y);
        updateRegion([&](auto& r) { r.dimensions = {diameter, diameter,
                                     region().shape == Shape::Sphere ? diameter : .02f}; });
    }
    updateRegion([&](auto& r) { r.pose = pose; });
}
void FiberMaskHandler::updateDepth(float depth) {
    if (!std::isfinite(depth))
        return;
    m_depth = depth;
    const auto delta =
        point(m_plane.getMatrix().block<3, 3>(0, 0) * Eigen::Vector3f(0, 0, depth * .5f));
    const auto base = region().pose.getPosition();
    const auto normal = point(m_plane.getMatrix().block<3, 1>(0, 2));
    const float current = vec(base - m_anchor).dot(vec(normal));
    updateRegion([&](auto& r) { r.pose.setPosition(base - normal * current + delta); });
    updateRegion([&](auto& r) { r.dimensions.z = std::abs(depth); });
}
void FiberMaskHandler::applyDimensions(Vector3 d) {
    if (region().shape == Shape::Sphere)
        d.y = d.z = d.x;
    if (region().shape == Shape::Cylinder)
        d.y = d.x;
    updateRegion([&](auto& r) { r.dimensions = d; });
    if ((m_stage == Stage::Depth || m_stage == Stage::Footprint) &&
        region().shape == Shape::Box) {
        const auto end = transformed(m_plane.getMatrix().inverse(), m_end);
        auto center = transformed(m_plane.getMatrix(), {(end.x < 0 ? -1.f : 1.f) * d.x * .5f,
                                  (end.y < 0 ? -1.f : 1.f) * d.y * .5f, 0});
        center.z = model()->localBounds().getCenter().z;
        updateRegion([&](auto& r) { r.pose.setPosition(center); });
    }
    if (m_stage == Stage::Depth && d.z > 0)
        updateDepth(m_depth < 0 ? -d.z : d.z);
}
void FiberMaskHandler::updatePointer(const QPoint& mouse, Qt::KeyboardModifiers modifiers) {
    m_pointer = mouse;
    m_drawingEnabled = modifiers.testFlag(Qt::ControlModifier);
    m_error.clear();
    if (m_stage == Stage::Footprint) {
        auto p = planePoint(mouse);
        if (!p) {
            m_error = tr("Rotate the view or enter dimensions: the drawing plane is edge-on.");
            publish();
            return;
        }
        const auto matrix = (model()->getTransformMatrix() * m_plane.getMatrix()).eval();
        SnapConstraint constraint;
        constraint.kind = SnapConstraint::Kind::Plane;
        constraint.origin = point(matrix.block<3, 1>(0, 3));
        constraint.direction =
            point(matrix.block<3, 3>(0, 0).inverse().transpose() * Eigen::Vector3f(0, 0, 1));
        updateFromFootprint(snap(*p, constraint, modifiers));
    } else if (m_stage == Stage::Depth && m_dragAxis == 2) {
        const auto axis = point(m_plane.getMatrix().block<3, 1>(0, 2));
        const auto value = axisParameter(mouse, m_anchor, axis);
        if (!value) {
            m_error = tr("Rotate the view or enter a value: the axis faces the camera.");
            publish();
            return;
        }
        SnapConstraint constraint;
        constraint.kind = SnapConstraint::Kind::Axis;
        constraint.origin = transformed(model()->getTransformMatrix(), m_anchor);
        constraint.direction = point(model()->getTransformMatrix().block<3, 3>(0, 0) * vec(axis));
        const auto endpoint = snap(m_anchor + axis *
            (m_dragStartDepth + *value - m_dragStartParameter), constraint, modifiers);
        updateDepth(vec(endpoint - m_anchor).dot(vec(axis)));
    }
    rebuildPreview();
    publish();
}
bool FiberMaskHandler::onMousePressEvent(QMouseEvent* event) {
    if (!isActive() || !event || event->button() != Qt::LeftButton)
        return false;
    if (objectDragActive())
        return false;
    m_viewId = DBInstanceID(inputViewId());
    updatePointerScope();
    m_pointer = event->pos();
    if (m_cameraGestureView.isValid())
        return false;
    // Exclusive tools receive input before middleware. Let the existing model
    // handler process object presses through its normal event entry point.
    if (!m_copyPlacement && (m_stage == Stage::Place || m_stage == Stage::Review)) {
        const auto handler = modelInteraction();
        if (handler && std::static_pointer_cast<IActionHandlerBase>(handler)->onMousePressEvent(
                           MouseInputEvent{inputViewId(), event}))
            return true;
    }
    if (!event->modifiers().testFlag(Qt::ControlModifier)) {
        m_cameraGestureView = m_viewId;
        refreshPointer(event->modifiers());
        return false;
    }
    m_maskPressed = true;
    auto owner = model();
    if (!owner)
        return true;
    const auto view = viewProjection();
    if (!view.isValid()) {
        m_error = tr("The viewport projection is unavailable.");
        publish();
        return true;
    }
    if (m_stage == Stage::Review && m_editId.isValid()) {
        if (!savePendingEdit()) return true;
        startNew(0);
        m_maskPressed = true;
        m_drawingEnabled = true;
    }
    if (m_stage == Stage::Place) {
        const auto point = placementPoint(event->pos(), event->modifiers());
        if (!point) {
            m_error = tr("Rotate the view: the drawing plane is edge-on or behind the camera.");
            publish();
            return true;
        }
        const auto sourceZ = region().pose.getPosition().z;
        m_anchor = *point;
        if (region().shape == Shape::Box)
            m_anchor.z = m_copyPlacement ? sourceZ : owner->localBounds().getCenter().z;
        m_plane.setPosition(m_anchor);
        m_error.clear();
        if (m_copyPlacement) {
            updateRegion([&](auto& r) { r.pose.setPosition(m_anchor); });
            m_stage = Stage::Review;
            m_copyPlacement = false;
            rebuildPreview();
            publish();
            return true;
        }
        updateRegion([&](auto& r) { r.pose = m_plane; });
        m_stage = Stage::Footprint;
        m_dragging = true;
        updatePointer(event->pos(), event->modifiers());
        return true;
    }
    if (m_stage == Stage::Footprint) {
        m_dragging = true;
        updatePointer(event->pos(), event->modifiers());
        return true;
    }
    if (m_stage == Stage::Depth) {
        const auto origin = transformed(owner->getTransformMatrix(), m_anchor);
        const auto axis = point(m_plane.getMatrix().block<3, 1>(0, 2));
        const auto a = view.screenFromWorld(origin),
                   b = view.screenFromWorld(
                       transformed(owner->getTransformMatrix(),
                                   m_anchor + axis * (m_depth < 0 ? -handleLength()
                                                                  : handleLength())));
        const QPointF start(a.x, a.y), edge(b.x - a.x, b.y - a.y),
            offset = QPointF(event->pos()) - start;
        const double length = edge.x() * edge.x() + edge.y() * edge.y();
        if (length < 16) {
            m_error = tr("Rotate the view or enter a value: the axis faces the camera.");
            publish();
            return true;
        }
        const double t =
            std::clamp((offset.x() * edge.x() + offset.y() * edge.y()) / length, 0., 1.);
        const auto delta = offset - edge * t;
        const auto parameter = axisParameter(event->pos(), m_anchor, axis);
        if (std::hypot(delta.x(), delta.y()) > 12 || !parameter) {
            m_error = tr("Drag the depth arrow or enter a depth value.");
            publish();
            return true;
        }
        m_dragAxis = 2;
        m_dragging = true;
        m_dragStartParameter = *parameter;
        m_dragStartDepth = m_depth;
        return true;
    }
    return true;
}
bool FiberMaskHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!isActive() || !event)
        return false;
    if (objectDragActive() || m_cameraGestureView.isValid())
        return false;
    if (m_dragging) {
        if (DBInstanceID(inputViewId()) != m_viewId)
            return false;
        m_pointer = event->pos();
        // Keep the original press ownership; modifier release freezes the draft.
        if (event->buttons().testFlag(Qt::LeftButton))
            refreshPointer(event->modifiers());
        return true;
    }
    if (m_stage == Stage::Place && model()) {
        m_viewId = DBInstanceID(inputViewId());
        m_pointer = event->pos();
        refreshPointer(event->modifiers());
    }
    return false;
}
bool FiberMaskHandler::onMouseReleaseEvent(QMouseEvent* event) {
    if (!isActive() || !event || event->button() != Qt::LeftButton)
        return false;
    if (objectDragActive())
        return false;
    const auto view = DBInstanceID(inputViewId());
    if (m_cameraGestureView.isValid()) {
        if (view == m_cameraGestureView) {
            m_cameraGestureView = INVALID_DB_ID;
            m_drawingEnabled = event->modifiers().testFlag(Qt::ControlModifier);
            publish();
        }
        return false;
    }
    if (!m_maskPressed || view != m_viewId)
        return false;
    m_maskPressed = false;
    if (!m_dragging)
        return true;
    if (event->modifiers().testFlag(Qt::ControlModifier))
        updatePointer(event->pos(), event->modifiers());
    m_dragging = false;
    m_dragAxis = -1;
    m_snapPreview.clear();
    m_snapResult.reset();
    if (m_reticle)
        m_reticle->setVisible(false);
    if (m_stage == Stage::Footprint) {
        if (region().dimensions.x <= .001f || region().dimensions.y <= .001f) {
            m_error = tr("Draw a non-zero footprint.");
        } else if (region().shape == Shape::Box) {
            m_stage = Stage::Review;
            commit();
        } else if (region().shape == Shape::Sphere)
            m_stage = Stage::Review;
        else {
            m_stage = Stage::Depth;
            m_depth = 0;
            updateRegion([&](auto& r) { r.dimensions.z = 0; });
        }
    }
    rebuildPreview();
    publish();
    return true;
}
bool FiberMaskHandler::onKeyPressEvent(QKeyEvent* event) {
    if (isActive() && event &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        if (!event->isAutoRepeat() && !m_dragging && !objectDragActive() && m_editId.isValid())
            request("remove", {});
        return true;
    }
    if (objectDragActive())
        return false;
    if (isActive() && event && event->key() == Qt::Key_Control && m_viewId.isValid()) {
        refreshPointer(event->modifiers() | Qt::ControlModifier);
        return true;
    }
    if (isActive() && event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (m_stage == Stage::Depth && FiberMaskDB::validRegion(region())) {
            m_stage = Stage::Review;
            rebuildPreview();
            publish();
        } else
            commit();
        return true;
    }
    return StandardActionHandler::onKeyPressEvent(event);
}
bool FiberMaskHandler::onKeyReleaseEvent(QKeyEvent* event) {
    if (objectDragActive())
        return false;
    if (isActive() && event && event->key() == Qt::Key_Control && m_viewId.isValid()) {
        refreshPointer(event->modifiers() & ~Qt::ControlModifier);
        return true;
    }
    return false;
}
bool FiberMaskHandler::commit() {
    FiberMaskBridge::instance()->flushInputs();
    const auto error = validationError();
    if (!error.isEmpty() || !m_error.isEmpty() || !m_parameterErrors.isEmpty() || m_dragging || objectDragActive()) {
        if (!error.isEmpty())
            m_error = error;
        publish();
        return false;
    }
    if (m_editId.isValid() && !hasPendingEdit())
        return true;
    if (TransactionManager::instance().isInTransaction()) {
        m_error = tr("Cannot save the mask while another document operation is active.");
        publish();
        return false;
    }
    auto* doc = DocumentManager::instance();
    int nextOrder = 0;
    if (!m_editId.isValid()) {
        for (auto id : doc->getOwnedChildren(m_modelId, FiberMaskDB::kOwnerRelation))
            if (auto mask = doc->getDB<FiberMaskDB>(id)) nextOrder = std::max(nextOrder, mask->getOrder());
        if (nextOrder == std::numeric_limits<int>::max()) {
            m_error = tr("Cannot create another mask: coverage order is exhausted.");
            publish();
            return false;
        }
        ++nextOrder;
    }
    m_committing = true;
    {
        TransactionGuard transaction(m_editId.isValid() ? "Edit fiber mask" : "Create fiber mask");
        auto mask = m_editId.isValid() ? doc->getDB<FiberMaskDB>(m_editId)
                                       : trans::TransDB::create<FiberMaskDB>();
        if (!mask || (m_editId.isValid() && doc->getOwner(m_editId) != m_modelId) ||
            (!m_editId.isValid() && !doc->attachOwnedChild(m_modelId, mask->getDBInstanceID(),
                                                           FiberMaskDB::kOwnerRelation))) {
            transaction.rollback();
            m_committing = false;
            m_error = tr("Cannot attach the mask to its model.");
            publish();
            return false;
        }
        mask->setShape(int(region().shape));
        mask->setDimensions(region().dimensions);
        mask->setTransform(region().pose);
        mask->setDisplayName(m_draft.name.trimmed().isEmpty() ? tr("Fiber mask").toStdString()
                                                              : m_draft.name.toStdString());
        mask->setEnabled(m_draft.enabled);
        if (!m_editId.isValid()) mask->setOrder(nextOrder);
        mask->setParameterOverridesJson(m_draft.parameterOverrides);
        m_savedParameterOverrides = m_draft.parameterOverrides;
        m_editId = mask->getDBInstanceID();
        m_previews[m_editId] = m_preview;
    }
    m_committing = false;
    m_error.clear();
    m_stage = Stage::Review;
    ActionManager::getInstance()->triggerAction("selection.pick", {{"dbId", m_preview->getDBInstanceID().getValue()}});
    FiberMaskBridge::instance()->refreshModelGroups();
    rebuildPreview();
    publish();
    return true;
}
bool FiberMaskHandler::finishEdit() {
    FiberMaskBridge::instance()->flushInputs();
    if (m_stage == Stage::Place && !m_copyPlacement && m_parameterErrors.isEmpty() &&
        !m_dragging && !objectDragActive())
        return true;
    return commit();
}
bool FiberMaskHandler::prepareForAction(const QString& actionCode, HandlerType incomingType) {
    if (actionCode == "edit.delete") {
        auto state = FiberMaskBridge::instance()->state();
        state.insert("error", tr("Delete the selected mask from the mask editor."));
        FiberMaskBridge::instance()->project(std::move(state), parameterProjection());
        return false;
    }
    if (incomingType != HandlerType::Exclusive && actionCode != "model.slice" &&
        actionCode != "model.sliceAll" && actionCode != "slicing.reslice" &&
        actionCode != "support.manual.slice" && actionCode != "model.importGCode")
        return true;
    return finishEdit();
}
void FiberMaskHandler::request(const QString& operation, const QVariantMap& values) {
    if (!isActive())
        return;
    if (operation == "cancel") {
        emit requestExit();
        return;
    }
    if (!model()) {
        emit requestExit();
        return;
    }
    if (objectDragActive() && operation != "select")
        return;
    if (operation == "finish") {
        if (finishEdit())
            emit requestExit();
        return;
    }
    if (operation == "new") {
        if (!savePendingEdit())
            return;
        orientTopView();
        startNew(values.value("shape", 0).toInt());
        return;
    }
    if (operation == "select") {
        selectMask(DBInstanceID(values.value("id").toULongLong()));
        return;
    }
    if (operation == "plane" && m_stage == Stage::Place) {
        if (region().shape == Shape::Box)
            return;
        m_planeIndex = values.value("value").toInt();
        if (m_planeIndex < 0 || m_planeIndex > 2)
            m_planeIndex = 0;
        startNew(int(region().shape), m_copyPlacement);
        return;
    }
    if (operation == "confirm") {
        commit();
        return;
    }
    if (operation == "next" && (m_stage == Stage::Depth || m_stage == Stage::Footprint)) {
        if (region().dimensions.x <= .001f || region().dimensions.y <= .001f) {
            m_error = tr("Draw a non-zero footprint.");
            publish();
            return;
        }
        if (m_stage == Stage::Depth && !FiberMaskDB::validRegion(region())) {
            m_error = tr("Mask dimensions must be positive and finite.");
            publish();
            return;
        }
        if (m_stage == Stage::Footprint && region().shape == Shape::Cylinder)
            updateRegion([&](auto& r) { r.dimensions.z = 0; });
        m_stage = m_stage == Stage::Footprint && region().shape == Shape::Cylinder
                      ? Stage::Depth
                      : Stage::Review;
        m_error.clear();
        rebuildPreview();
        publish();
        return;
    }
    if (operation == "copy" && m_editId.isValid()) {
        if (!savePendingEdit())
            return;
        startNew(int(region().shape), true);
        return;
    }
    if (operation == "remove" && m_editId.isValid()) {
        if (TransactionManager::instance().isInTransaction()) {
            m_error = tr("Cannot delete the mask while another document operation is active.");
            publish();
            return;
        }
        m_committing = true;
        {
            TransactionGuard transaction("Delete fiber mask");
            DocumentManager::instance()->unregisterDBInstance(m_editId);
        }
        m_committing = false;
        FiberMaskBridge::instance()->refreshModelGroups();
        startNew(0);
        return;
    }
    m_error.clear();
    if (operation == "section") {
        m_showSection = values.value("show", m_showSection).toBool();
        m_sectionZ = values.value("z", m_sectionZ).toDouble();
    }
    if (operation == "name")
        m_draft.name = values.value("value").toString();
    if (operation == "enabled")
        m_draft.enabled = values.value("value").toBool();
    if (operation == "depth" && region().shape != Shape::Box)
        updateDepth(values.value("value").toFloat());
    if (operation == "dimensions") {
        auto d = region().dimensions;
        if (values.contains("width"))
            d.x = values.value("width").toFloat();
        if (values.contains("length"))
            d.y = values.value("length").toFloat();
        if (values.contains("height"))
            d.z = values.value("height").toFloat();
        applyDimensions(d);
    }
    if (operation == "position" && m_stage == Stage::Review) {
        auto p = region().pose.getPosition();
        if (values.contains("x"))
            p.x = values.value("x").toFloat();
        if (values.contains("y"))
            p.y = values.value("y").toFloat();
        if (values.contains("z"))
            p.z = values.value("z").toFloat();
        updateRegion([&](auto& r) { r.pose.setPosition(p); });
    }
    if (operation == "rotation" && m_stage == Stage::Review) {
        auto r = region().pose.getEuler();
        if (values.contains("rx"))
            r.x = values.value("rx").toFloat();
        if (values.contains("ry"))
            r.y = values.value("ry").toFloat();
        if (values.contains("rz"))
            r.z = values.value("rz").toFloat();
        const auto p = region().pose.getPosition();
        Transform pose;
        pose.rotateX(r.x); pose.rotateY(r.y); pose.rotateZ(r.z);
        pose.setPosition(p);
        updateRegion([&](auto& value) { value.pose = pose; });
    }
    if (operation == "parameter")
        updateParameter(values);
    rebuildPreview();
    publish();
}

std::shared_ptr<TransientPolyDataActorDB> FiberMaskHandler::createPreview() {
    const auto actor = m_scope.create<TransientPolyDataActorDB>();
    configure(actor);
    actor->setPickable(true);
    actor->setDragable(true);
    return actor;
}
void FiberMaskHandler::setPreviewGeometry(
    const std::shared_ptr<TransientPolyDataActorDB>& actor,
    const FiberMaskDB::Region& region, Vector3 color) {
    FiberMaskDB::Region display{region.shape, {}, region.dimensions};
    if (display.dimensions.z == 0) display.dimensions.z = .02f;
    const auto id = actor->getDBInstanceID();
    const auto sameVector = [](Vector3 a, Vector3 b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    const auto previous = m_previewGeometries.find(id);
    if (actor->getGeometry() && previous != m_previewGeometries.end() &&
        previous->second.shape == display.shape &&
        sameVector(previous->second.dimensions, display.dimensions) &&
        sameVector(previous->second.color, color))
        return;
    actor->setGeometry(previewGeometry(display, color));
    m_previewGeometries[id] = {display.shape, display.dimensions, color};
}
void FiberMaskHandler::loadPreview(
    const std::shared_ptr<TransientPolyDataActorDB>& actor, const FiberMaskDB& source) {
    TransientUpdateGuard transient;
    ImmediateNotifyGuard immediate;
    Transform world;
    world.setMatrix(source.worldTransform());
    actor->setTransform(world);
    actor->setVisible(source.getVisible());
    setPreviewGeometry(actor, source.region(), {.12f, .67f, 1.f});
}

float FiberMaskHandler::handleLength() const {
    return std::max(3.f, std::min({region().dimensions.x, region().dimensions.y,
                                   std::max(.02f, region().dimensions.z)}) *
                             .7f);
}

void FiberMaskHandler::rebuildPreview() {
    if (!model())
        return;
    TransientUpdateGuard guard;
    ImmediateNotifyGuard immediate;
    synchronizePreviews();
    const auto presenting = std::exchange(m_presenting, true);
    if (!m_handle) {
        m_handle = m_scope.create<TransientPolyDataActorDB>();
        configure(m_handle);
    }
    const auto validation = m_stage == Stage::Review ? validationError() : QString();
    const Vector3 color = validation.isEmpty() ? Vector3(.95f, .65f, .1f) : Vector3(1.f, .16f, .14f);
    const auto source = DocumentManager::instance()->getDB<FiberMaskDB>(m_editId);
    m_preview->setVisible(m_stage != Stage::Place &&
                          (!m_editId.isValid() || (source && source->getVisible())));
    m_preview->setPickable(m_stage == Stage::Review);
    m_preview->setDragable(m_stage == Stage::Review);
    setPreviewGeometry(m_preview, region(), color);
    const auto displayRegion = region();
    updatePointerScope();
    m_presenting = presenting;
    // Only cylinder construction needs a depth arrow. Saved masks use body dragging.
    m_handle->setVisible(m_stage == Stage::Depth);
    if (m_stage == Stage::Depth &&
        std::isfinite(displayRegion.dimensions.x) && displayRegion.dimensions.x > 2.e-6f &&
        std::isfinite(displayRegion.dimensions.y) && displayRegion.dimensions.y > 2.e-6f &&
        m_plane.getMatrix().allFinite()) {
        auto arrow = vtkSmartPointer<vtkArrowSource>::New();
        arrow->SetTipResolution(16);
        arrow->SetShaftResolution(12);
        auto t = vtkSmartPointer<vtkTransform>::New();
        t->RotateY(m_depth < 0 ? 90 : -90);
        const float size = handleLength();
        t->Scale(size, size, size);
        auto filter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
        filter->SetTransform(t);
        filter->SetInputConnection(arrow->GetOutputPort());
        filter->Update();
        m_handle->setGeometry(filter->GetOutput());
        m_handle->setUseCellColors(false);
        if (auto material = m_handle->getMaterial())
            material->setDiffuseColor({.2f, 1.f, .4f});
        Transform handleTransform;
        handleTransform.setMatrix(model()->getTransformMatrix() * m_plane.getMatrix());
        m_handle->setTransform(handleTransform);
    } else if (m_handle->getGeometry())
        m_handle->setGeometry(nullptr);
}
QVariantMap FiberMaskHandler::sectionPreview() {
    if (!m_showSection || !model() || !FiberMaskDB::validRegion(region()))
        return {};
    const auto owner = model();
    if (!m_sectionModel || m_sectionRevision != owner->geometryRevision()) {
        auto append = vtkSmartPointer<vtkAppendPolyData>::New();
        for (const auto& part : owner->parts()) {
            if (!part || !part->hasValidMesh() ||
                static_cast<ModelPartRole>(part->getRole()) != ModelPartRole::Model)
                continue;
            const auto read = part->geometry()->read();
            if (!read)
                continue;
            auto transform = vtkSmartPointer<vtkTransform>::New();
            const auto matrix = part->getLocalTransform().getMatrix();
            double elements[16];
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    elements[row * 4 + col] = matrix(row, col);
            transform->SetMatrix(elements);
            auto filter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
            filter->SetInputData(const_cast<vtkPolyData*>(&read.polyData()));
            filter->SetTransform(transform);
            filter->Update();
            append->AddInputData(filter->GetOutput());
        }
        if (append->GetNumberOfInputConnections(0) == 0)
            return {};
        append->Update();
        m_sectionModel = vtkSmartPointer<vtkPolyData>::New();
        m_sectionModel->DeepCopy(append->GetOutput());
        m_sectionRevision = owner->geometryRevision();
    }
    const auto section = FiberMaskGeometryUtils::section(region(), m_sectionModel, m_sectionZ);
    const auto convert = [](const std::vector<std::array<Vector3, 3>>& triangles) {
        QVariantList result;
        for (const auto& triangle : triangles) {
            QVariantList coordinates;
            for (const auto& p : triangle) {
                coordinates.append(p.x);
                coordinates.append(p.y);
            }
            result.append(QVariant(coordinates));
        }
        return result;
    };
    const auto bounds = owner->localBounds();
    return {{"valid", section.valid},
            {"model", convert(section.modelTriangles)},
            {"covered", convert(section.coveredTriangles)},
            {"minX", bounds.min.x},
            {"minY", bounds.min.y},
            {"maxX", bounds.max.x},
            {"maxY", bounds.max.y}};
}
void FiberMaskHandler::updateReticle(std::optional<Vector3> rawWorld) {
    const auto view = viewProjection();
    if (!view.isValid())
        return;
    TransientUpdateGuard guard;
    ImmediateNotifyGuard immediate;
    if (!m_reticle) {
        m_reticle = m_scope.create<TransientPolyDataActorDB>();
        configure(m_reticle);
        m_reticle->setUseCellColors(false);
    }
    if (!m_snapResult && !rawWorld) {
        m_reticle->setVisible(false);
        return;
    }
    const auto location = m_snapResult ? m_snapResult->worldPosition : *rawWorld;
    const auto screen = view.screenFromWorld(location);
    const auto edge = view.worldFromScreen(QPointF(screen.x + 4, screen.y), screen.z);
    auto sphere = vtkSmartPointer<vtkSphereSource>::New();
    sphere->SetRadius(Eigen::Vector3f(edge.x - location.x, edge.y - location.y, edge.z - location.z).norm());
    sphere->SetThetaResolution(16);
    sphere->SetPhiResolution(12);
    sphere->Update();
    m_reticle->setGeometry(sphere->GetOutput());
    Transform t;
    t.setPosition(location);
    m_reticle->setTransform(t);
    m_reticle->setVisible(true);
    if (auto material = m_reticle->getMaterial())
        material->setDiffuseColor({.12f, 1.f, .65f});
}
