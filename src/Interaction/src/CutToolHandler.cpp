#include "CutToolHandler.hpp"

#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(CutToolHandler, "model.cut")

#include "InteractionRuntime.hpp"
#include "PickService.hpp"

#include <ActionContext.hpp>
#include <ActionManager.hpp>
#include <CutToolBridge.hpp>
#include <DocumentManager.hpp>
#include <MaterialDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelGraphUtil.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <PrintBedDB.hpp>
#include <SelectionBridge.hpp>
#include <TransactionManager.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <WindowDB.hpp>

#include <libslicer/Library.hpp>
#include <transdb.h>

#include <QFutureWatcher>
#include <QMouseEvent>
#include <QtConcurrent>

#include <vtkCellArray.h>
#include <vtkCell.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float dot(const Vector3& lhs, const Vector3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vector3 cross(const Vector3& lhs, const Vector3& rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

float length(const Vector3& value) {
    return std::sqrt(dot(value, value));
}

Vector3 normalized(const Vector3& value) {
    const float size = length(value);
    return size > 1.0e-7f ? value * (1.0f / size) : Vector3();
}

Vector3 matrixAxis(const Transform& transform, int column) {
    const auto& matrix = transform.getMatrix();
    return normalized(Vector3(matrix(0, column), matrix(1, column),
                              matrix(2, column)));
}

vtkSmartPointer<vtkPolyData> polyDataFromCutMesh(
    libslicer::PlaneCutMesh mesh) {
    if (mesh.empty()) return {};
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToFloat();
    points->Allocate(static_cast<vtkIdType>(mesh.vertices.size()));
    for (const libslicer::SliceVertex& vertex : mesh.vertices) {
        points->InsertNextPoint(vertex.x, vertex.y, vertex.z);
    }

    auto cells = vtkSmartPointer<vtkCellArray>::New();
    cells->AllocateEstimate(
        static_cast<vtkIdType>(mesh.triangles.size()), 3);
    for (const libslicer::SliceTriangle& face : mesh.triangles) {
        if (face.vertex_a >= mesh.vertices.size() ||
            face.vertex_b >= mesh.vertices.size() ||
            face.vertex_c >= mesh.vertices.size()) {
            return {};
        }
        const vtkIdType ids[3] = {
            static_cast<vtkIdType>(face.vertex_a),
            static_cast<vtkIdType>(face.vertex_b),
            static_cast<vtkIdType>(face.vertex_c)};
        cells->InsertNextCell(3, ids);
    }
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(cells);
    return polyData;
}

struct CutGeometryResult {
    bool success{false};
    vtkSmartPointer<vtkPolyData> upper;
    vtkSmartPointer<vtkPolyData> lower;
    std::string error;
};

Vector3 transformPoint(const Vector3& point,
                       const Transform::Matrix4& matrix) {
    const Eigen::Vector4f transformed =
        matrix * Eigen::Vector4f(point.x, point.y, point.z, 1.0f);
    return {transformed.x(), transformed.y(), transformed.z()};
}

std::vector<GeomTriangle> objectLocalTriangles(
    const std::shared_ptr<ModelInstanceDB>& source) {
    std::vector<GeomTriangle> triangles;
    if (!source) return triangles;
    for (const auto& part : source->parts()) {
        const auto mesh = part ? part->geometry() : nullptr;
        const auto geometry = mesh
            ? mesh->read()
            : ModelGeometryDB::ReadHandle{};
        auto* poly = geometry
            ? const_cast<vtkPolyData*>(&geometry.polyData())
            : nullptr;
        if (!poly) continue;
        const Transform::Matrix4 objectFromPart =
            part->getLocalTransform().getMatrix();
        for (vtkIdType cellId = 0; cellId < poly->GetNumberOfCells(); ++cellId) {
            vtkCell* cell = poly->GetCell(cellId);
            if (!cell || cell->GetNumberOfPoints() != 3) continue;
            Vector3 points[3];
            for (int corner = 0; corner < 3; ++corner) {
                double point[3] = {0.0, 0.0, 0.0};
                poly->GetPoint(cell->GetPointId(corner), point);
                points[corner] = transformPoint(
                    {static_cast<float>(point[0]),
                     static_cast<float>(point[1]),
                     static_cast<float>(point[2])},
                    objectFromPart);
            }
            triangles.emplace_back(points[0], points[1], points[2]);
        }
    }
    return triangles;
}

std::shared_ptr<ModelInstanceDB> createCutPiece(
    const std::shared_ptr<ModelInstanceDB>& source,
    vtkSmartPointer<vtkPolyData> polyData,
    const std::string& nameSuffix) {
    if (!source || !polyData || polyData->GetNumberOfCells() <= 0) return {};
    const auto object = source->object();
    const auto sourceParts = source->parts();
    if (!object || sourceParts.size() != 1 || !sourceParts.front()) return {};
    Transform identity;
    identity.identity();
    std::vector<ModelGraphUtil::PartCreateInfo> parts;
    parts.push_back({source->getDisplayName() + nameSuffix,
                     polyData,
                     identity, ModelPartRole::Model, 0,
                     sourceParts.front()->getDefaultFilamentSlot(),
                     static_cast<ModelFilamentBindingMode>(
                         sourceParts.front()->getFilamentBindingMode())});
    auto graph = ModelGraphUtil::createObject(
        {source->getDisplayName() + nameSuffix, object->getSourceFile(),
         object->getFileFormat(), true},
        std::move(parts),
        ModelGraphUtil::InstanceCreateInfo{
            source->getTransform(), source->getParentPrintBedDBId(),
            source->getPrintable(), source->getArrangeOrder(),
            source->getSlicingConfigDBId()});
    if (!graph || !graph.instance || graph.parts.empty()) return {};
    graph.instance->setVisible(source->getVisible());
    graph.instance->setPickable(source->getPickable());
    graph.instance->setDragable(source->getDragable());
    graph.instance->setOpacity(source->getOpacity());
    if (sourceParts.front()->getMaterial()) {
        graph.parts.front()->getMaterial()->copyValuesFrom(
            *sourceParts.front()->getMaterial());
    }
    return graph.instance;
}

Transform rotatedAroundWorldPoint(const Transform& source,
                                  const Vector3& center,
                                  const Eigen::Matrix3f& rotation) {
    Transform::Matrix4 before = Transform::Matrix4::Identity();
    before.block<3, 3>(0, 0) = rotation;
    before.block<3, 1>(0, 3) =
        Eigen::Vector3f(center.x, center.y, center.z) -
        rotation * Eigen::Vector3f(center.x, center.y, center.z);
    Transform result;
    result.setMatrix(before * source.getMatrix());
    return result;
}

Eigen::Matrix3f rotationBetween(const Vector3& from, const Vector3& to) {
    const Eigen::Vector3f source(from.x, from.y, from.z);
    const Eigen::Vector3f target(to.x, to.y, to.z);
    if (source.norm() <= 1.0e-7f || target.norm() <= 1.0e-7f) {
        return Eigen::Matrix3f::Identity();
    }
    return Eigen::Quaternionf::FromTwoVectors(
        source.normalized(), target.normalized()).toRotationMatrix();
}

} // namespace

CutToolHandler::CutToolHandler(QObject* parent)
    : StandardActionHandler(parent) {
    auto* bridge = CutToolBridge::instance();
    connect(bridge, &CutToolBridge::positionZRequested,
            this, &CutToolHandler::setPositionZ);
    connect(bridge, &CutToolBridge::flipRequested,
            this, &CutToolHandler::flipPlane);
    connect(bridge, &CutToolBridge::resetRequested,
            this, &CutToolHandler::resetPlane);
    connect(bridge, &CutToolBridge::keepUpperRequested,
            this, &CutToolHandler::setKeepUpper);
    connect(bridge, &CutToolBridge::keepLowerRequested,
            this, &CutToolHandler::setKeepLower);
    connect(bridge, &CutToolBridge::placeUpperOnCutRequested,
            this, &CutToolHandler::setPlaceUpperOnCut);
    connect(bridge, &CutToolBridge::placeLowerOnCutRequested,
            this, &CutToolHandler::setPlaceLowerOnCut);
    connect(bridge, &CutToolBridge::flipUpperRequested,
            this, &CutToolHandler::setFlipUpper);
    connect(bridge, &CutToolBridge::flipLowerRequested,
            this, &CutToolHandler::setFlipLower);
    connect(bridge, &CutToolBridge::cutToPartsRequested,
            this, &CutToolHandler::setCutToParts);
    connect(bridge, &CutToolBridge::performRequested,
            this, &CutToolHandler::performCut);
    connect(bridge, &CutToolBridge::cancelRequested, this, [this]() {
        if (isActive()) emit requestExit();
    });
    connect(&interactionPickService(), &PickService::pickCompleted,
            this, [this](const PickSnapshot& snapshot) {
        if (!isActive() || m_drag.active ||
            snapshot.channel != PickChannel::Hover ||
            snapshot.viewId != m_interactionViewId) {
            return;
        }
        updateHover(snapshot.status == GPlatform::Rendering::PickStatus::Hit
                        ? snapshot.result
                        : PickResult{});
    });
}

bool CutToolHandler::supportsEnvironment(const QString& environment) const {
    return environment == QStringLiteral("normal") ||
           environment == QStringLiteral("editing");
}

void CutToolHandler::onEnter(std::shared_ptr<ActionContext> context) {
    if (!context) return;
    if (m_cutInProgress) {
        context->setError(ActionErrorCode::SystemUnavailable,
                          QStringLiteral("切割任务仍在运行"));
        return;
    }

    bool idOk = false;
    const qulonglong rawId = context->getParams()
        .value(QStringLiteral("modelId")).toULongLong(&idOk);
    auto* document = DocumentManager::instance();
    const auto model = idOk && rawId > 0 && document
        ? document->getDB<ModelInstanceDB>(DBInstanceID(rawId))
        : std::shared_ptr<ModelInstanceDB>{};
    if (!model || !model->isValid() || model->parts().size() != 1 ||
        !model->worldBounds().valid) {
        context->setError(ActionErrorCode::TargetRequired,
                          QStringLiteral("请选择一个有效网格模型"));
        return;
    }

    StandardActionHandler::onEnter(context);
    auto* actions = ActionManager::getInstance();
    m_modelInputSuspended = actions && actions->triggerAction(
        QStringLiteral("modelInteraction.suspendSelectionInput"));
    if (!m_modelInputSuspended || !createSession(model)) {
        context->setError(ActionErrorCode::SystemUnavailable,
                          QStringLiteral("无法创建切割平面控件"));
        onExit();
        return;
    }

    ++m_sessionRevision;
    m_options = {};
    projectOptions();
    CutToolBridge::instance()->projectError({});

    CutToolBridge::instance()->projectSession(
        true, m_widget->getPlaneToWorld().getPosition().z);
    projectBuildVolume();
    context->setResult(QVariantMap{
        {QStringLiteral("active"), true},
        {QStringLiteral("widgetId"),
         QVariant::fromValue<qulonglong>(
             m_widget->getDBInstanceID().getValue())}});
}

bool CutToolHandler::createSession(
    const std::shared_ptr<ModelInstanceDB>& model) {
    const ActorDB::BoundingBox bounds = model->worldBounds();
    if (!bounds.valid) return false;

    const Vector3 size = bounds.max - bounds.min;
    const float radius = std::max({size.x, size.y, size.z, 10.0f}) * 0.65f;
    Transform plane;
    plane.setPosition(bounds.getCenter());

    m_targetModelId = model->getDBInstanceID();
    m_initialPlaneToWorld = plane;
    m_widget = m_tempScope.create<CutPlaneWidgetDB>();
    if (!m_widget) return false;
    m_widget->setTargetModelID(m_targetModelId);
    m_widget->setPlaneToWorld(plane);
    m_widget->setWidgetRadius(radius);
    m_widget->setActivePart(static_cast<int>(CutPlaneWidgetDB::Part::None));
    m_widget->setHoveredPart(static_cast<int>(CutPlaneWidgetDB::Part::None));

    m_preview = m_tempScope.create<CutPreviewDB>();
    if (!m_preview) return false;
    m_preview->setSourceModelID(m_targetModelId);
    m_preview->setPlaneWidgetID(m_widget->getDBInstanceID());
    m_preview->setEnabled(true);
    return true;
}

void CutToolHandler::onExit() {
    ++m_sessionRevision;
    finishDrag();
    if (m_widget) {
        m_widget->setHoveredPart(static_cast<int>(CutPlaneWidgetDB::Part::None));
    }
    m_preview.reset();
    m_widget.reset();
    m_tempScope.clear();
    m_targetModelId = INVALID_DB_ID;
    m_interactionViewId = INVALID_DB_ID;
    if (std::exchange(m_modelInputSuspended, false)) {
        if (auto* actions = ActionManager::getInstance();
            actions && actions->isAcceptingActions()) {
            actions->triggerAction(
                QStringLiteral("modelInteraction.resumeSelectionInput"));
        }
    }
    CutToolBridge::instance()->projectSession(false);
    m_cutInProgress = false;
    StandardActionHandler::onExit();
}

bool CutToolHandler::onMousePressEvent(QMouseEvent* event) {
    if (!event || !isActive() || !m_widget || m_cutInProgress ||
        event->button() != Qt::LeftButton) {
        return false;
    }

    m_interactionViewId = DBInstanceID(inputViewId());
    if (!interactionPickService().supportsFeature(
            m_interactionViewId,
            GPlatform::Rendering::PickFeature::SubTarget)) {
        return false;
    }

    const auto picked = interactionPickService().pickRendererForViewBlocking(
        m_interactionViewId, event->position().toPoint(),
        GPlatform::Rendering::PickDetail::Object);
    if (picked.status != GPlatform::Rendering::PickStatus::Hit ||
        picked.payload.objectId != m_widget->getDBInstanceID() ||
        !picked.payload.subTarget) {
        return false;
    }

    const auto part = static_cast<CutPlaneWidgetDB::Part>(
        static_cast<int>(*picked.payload.subTarget));
    return startDrag(part, event->position().toPoint());
}

bool CutToolHandler::startDrag(CutPlaneWidgetDB::Part part,
                               const QPoint& mousePosition) {
    if (!m_widget || part == CutPlaneWidgetDB::Part::None) return false;

    DragState drag;
    drag.active = true;
    drag.part = part;
    drag.startPlaneToWorld = m_widget->getPlaneToWorld();
    drag.centerWorld = drag.startPlaneToWorld.getPosition();

    if (part == CutPlaneWidgetDB::Part::RotateLocalXHandle ||
        part == CutPlaneWidgetDB::Part::RotateLocalYHandle) {
        drag.axisWorld = matrixAxis(
            drag.startPlaneToWorld,
            part == CutPlaneWidgetDB::Part::RotateLocalXHandle ? 0 : 1);
        const auto startVector = rotationVectorFromMouse(
            mousePosition, drag.centerWorld, drag.axisWorld);
        if (!startVector) return false;
        drag.startRotationVectorWorld = *startVector;
    } else if (part == CutPlaneWidgetDB::Part::Plane ||
               part == CutPlaneWidgetDB::Part::NormalMoveHandle) {
        drag.axisWorld = matrixAxis(drag.startPlaneToWorld, 2);
        const auto parameter = axisParameterFromMouse(
            mousePosition, drag.centerWorld, drag.axisWorld);
        if (!parameter) return false;
        drag.startAxisParameter = *parameter;
    } else {
        return false;
    }

    m_drag = drag;
    m_widget->setActivePart(static_cast<int>(part));
    m_widget->setIsDragging(true);
    return true;
}

bool CutToolHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!event || !isActive() || !m_widget || m_cutInProgress) return false;
    m_interactionViewId = DBInstanceID(inputViewId());

    if (m_drag.active) {
        updateDrag(event->position().toPoint(), event->modifiers());
        return true;
    }

    if (event->buttons() != Qt::NoButton ||
        !interactionPickService().supportsFeature(
            m_interactionViewId,
            GPlatform::Rendering::PickFeature::SubTarget)) {
        return false;
    }
    interactionPickService().requestForView(
        m_interactionViewId, event->position().toPoint(), PickChannel::Hover,
        GPlatform::Rendering::PickDetail::Object, PickDelivery::LatestOnly);
    return false;
}

void CutToolHandler::updateDrag(const QPoint& mousePosition,
                                Qt::KeyboardModifiers modifiers) {
    if (!m_drag.active || !m_widget) return;

    Transform updated = m_drag.startPlaneToWorld;
    if (m_drag.part == CutPlaneWidgetDB::Part::Plane ||
        m_drag.part == CutPlaneWidgetDB::Part::NormalMoveHandle) {
        const auto parameter = axisParameterFromMouse(
            mousePosition, m_drag.centerWorld, m_drag.axisWorld);
        if (!parameter) return;
        updated.setPosition(
            m_drag.centerWorld +
            m_drag.axisWorld * (*parameter - m_drag.startAxisParameter));
    } else {
        const auto currentVector = rotationVectorFromMouse(
            mousePosition, m_drag.centerWorld, m_drag.axisWorld);
        if (!currentVector) return;
        float radians = std::atan2(
            dot(m_drag.axisWorld,
                cross(m_drag.startRotationVectorWorld, *currentVector)),
            std::clamp(dot(m_drag.startRotationVectorWorld, *currentVector),
                       -1.0f, 1.0f));
        if (modifiers & Qt::ShiftModifier) {
            float degrees = radians * 180.0f / kPi;
            degrees = std::round(degrees / 5.0f) * 5.0f;
            radians = degrees * kPi / 180.0f;
        }
        Transform::Matrix4 localRotation = Transform::Matrix4::Identity();
        const Eigen::Vector3f localAxis =
            m_drag.part == CutPlaneWidgetDB::Part::RotateLocalXHandle
            ? Eigen::Vector3f::UnitX()
            : Eigen::Vector3f::UnitY();
        localRotation.block<3, 3>(0, 0) =
            Eigen::AngleAxisf(radians, localAxis).toRotationMatrix();
        updated.setMatrix(m_drag.startPlaneToWorld.getMatrix() * localRotation);
    }
    publishPlane(updated);
}

bool CutToolHandler::onMouseReleaseEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton || !m_drag.active) {
        return false;
    }
    finishDrag();
    return true;
}

void CutToolHandler::finishDrag() {
    if (m_widget) {
        m_widget->setIsDragging(false);
        m_widget->setActivePart(
            static_cast<int>(CutPlaneWidgetDB::Part::None));
    }
    m_drag = {};
}

void CutToolHandler::updateHover(const PickResult& result) {
    if (!m_widget) return;
    int part = static_cast<int>(CutPlaneWidgetDB::Part::None);
    if (result.objectId == m_widget->getDBInstanceID() && result.subTarget) {
        part = static_cast<int>(*result.subTarget);
    }
    m_widget->setHoveredPart(part);
}

void CutToolHandler::publishPlane(const Transform& planeToWorld) {
    if (!m_widget || !m_preview) return;
    const bool orientationChanged =
        !m_widget->getPlaneToWorld().getMatrix().block<3, 3>(0, 0).isApprox(
            planeToWorld.getMatrix().block<3, 3>(0, 0), 1.0e-6f);
    m_widget->setPlaneToWorld(planeToWorld);
    m_preview->setRevision(m_preview->getRevision() + 1);
    projectPositionZ();
    if (orientationChanged) projectBuildVolume();
}

void CutToolHandler::resetPlane() {
    if (!isActive()) return;
    publishPlane(m_initialPlaneToWorld);
}

void CutToolHandler::flipPlane() {
    if (!isActive() || !m_widget) return;
    Transform plane = m_widget->getPlaneToWorld();
    Transform::Matrix4 localRotation = Transform::Matrix4::Identity();
    localRotation.block<3, 3>(0, 0) =
        Eigen::AngleAxisf(kPi, Eigen::Vector3f::UnitX()).toRotationMatrix();
    plane.setMatrix(plane.getMatrix() * localRotation);
    publishPlane(plane);
}

void CutToolHandler::setPositionZ(double positionZ) {
    if (!isActive() || !m_widget) return;
    Transform plane = m_widget->getPlaneToWorld();
    Vector3 center = plane.getPosition();
    center.z = static_cast<float>(positionZ);
    plane.setPosition(center);
    publishPlane(plane);
}

void CutToolHandler::projectPositionZ() {
    if (!m_widget) return;
    const Transform& plane = m_widget->getPlaneToWorld();
    CutToolBridge::instance()->projectPlaneState(
        plane.getPosition().z,
        !plane.getMatrix().isApprox(
            m_initialPlaneToWorld.getMatrix(), 1.0e-6f));
}

void CutToolHandler::projectBuildVolume() {
    if (!m_widget) return;
    auto* document = DocumentManager::instance();
    const auto source = document
        ? document->getDB<ModelInstanceDB>(m_targetModelId)
        : std::shared_ptr<ModelInstanceDB>{};
    const auto triangles = objectLocalTriangles(source);
    if (!source || triangles.empty()) {
        CutToolBridge::instance()->projectBuildVolume({});
        return;
    }

    const Transform::Matrix4 meshToPlane =
        m_widget->getPlaneToWorld().getMatrix().inverse() *
        source->getTransform().getMatrix();
    Eigen::Vector3f minimum = Eigen::Vector3f::Constant(
        std::numeric_limits<float>::max());
    Eigen::Vector3f maximum = Eigen::Vector3f::Constant(
        std::numeric_limits<float>::lowest());
    for (const auto& triangle : triangles) {
        const Vector3 vertices[3] = {
            triangle.vertex1, triangle.vertex2, triangle.vertex3};
        for (const Vector3& vertex : vertices) {
            const Eigen::Vector4f projected = meshToPlane * Eigen::Vector4f(
                vertex.x, vertex.y, vertex.z, 1.0f);
            minimum = minimum.cwiseMin(projected.head<3>());
            maximum = maximum.cwiseMax(projected.head<3>());
        }
    }
    const Eigen::Vector3f size = maximum - minimum;
    CutToolBridge::instance()->projectBuildVolume(
        QStringLiteral("%1 × %2 × %3 mm")
            .arg(size.x(), 0, 'f', 2)
            .arg(size.y(), 0, 'f', 2)
            .arg(size.z(), 0, 'f', 2));
}

void CutToolHandler::projectOptions() {
    CutToolBridge::instance()->projectOptions(
        m_options.keepUpper, m_options.keepLower,
        m_options.placeUpperOnCut, m_options.placeLowerOnCut,
        m_options.flipUpper, m_options.flipLower, m_options.cutToParts);
}

void CutToolHandler::setKeepUpper(bool value) {
    if (!isActive() || m_cutInProgress) return;
    if (!value && !m_options.keepLower) {
        CutToolBridge::instance()->projectError(
            QStringLiteral("上部和下部至少保留一个"));
        projectOptions();
        return;
    }
    m_options.keepUpper = value;
    if (!value) {
        m_options.placeUpperOnCut = false;
        m_options.flipUpper = false;
    }
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::setKeepLower(bool value) {
    if (!isActive() || m_cutInProgress) return;
    if (!value && !m_options.keepUpper) {
        CutToolBridge::instance()->projectError(
            QStringLiteral("上部和下部至少保留一个"));
        projectOptions();
        return;
    }
    m_options.keepLower = value;
    if (!value) {
        m_options.placeLowerOnCut = false;
        m_options.flipLower = false;
    }
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::setPlaceUpperOnCut(bool value) {
    if (!isActive() || m_cutInProgress || !m_options.keepUpper ||
        m_options.cutToParts) return;
    m_options.placeUpperOnCut = value;
    if (value) m_options.flipUpper = false;
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::setPlaceLowerOnCut(bool value) {
    if (!isActive() || m_cutInProgress || !m_options.keepLower ||
        m_options.cutToParts) return;
    m_options.placeLowerOnCut = value;
    if (value) m_options.flipLower = false;
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::setFlipUpper(bool value) {
    if (!isActive() || m_cutInProgress || !m_options.keepUpper ||
        m_options.cutToParts) return;
    m_options.flipUpper = value;
    if (value) m_options.placeUpperOnCut = false;
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::setFlipLower(bool value) {
    if (!isActive() || m_cutInProgress || !m_options.keepLower ||
        m_options.cutToParts) return;
    m_options.flipLower = value;
    if (value) m_options.placeLowerOnCut = false;
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::setCutToParts(bool value) {
    if (!isActive() || m_cutInProgress) return;
    Q_UNUSED(value);
    m_options.cutToParts = false;
    CutToolBridge::instance()->projectError({});
    projectOptions();
}

void CutToolHandler::performCut() {
    if (!isActive() || m_cutInProgress || !m_widget ||
        (!m_options.keepUpper && !m_options.keepLower)) {
        return;
    }

    auto* document = DocumentManager::instance();
    const auto source = document
        ? document->getDB<ModelInstanceDB>(m_targetModelId)
        : std::shared_ptr<ModelInstanceDB>{};
    if (!source || !source->isValid() || source->parts().size() != 1) {
        CutToolBridge::instance()->projectError(
            QStringLiteral("切割目标已经不存在或网格无效"));
        return;
    }

    const auto sourceTriangles = objectLocalTriangles(source);
    if (sourceTriangles.empty()) {
        CutToolBridge::instance()->projectError(
            QStringLiteral("切割目标没有可处理的三角形"));
        return;
    }

    const Transform planeToWorld = m_widget->getPlaneToWorld();
    const Transform::Matrix4 planeMatrix = planeToWorld.getMatrix();
    if (std::abs(planeMatrix.determinant()) <= 1.0e-10f) {
        CutToolBridge::instance()->projectError(
            QStringLiteral("切割平面变换无效"));
        return;
    }
    const Transform::Matrix4 meshToPlane =
        planeMatrix.inverse() * source->getTransform().getMatrix();

    libslicer::PlaneCutRequest request;
    request.mesh.vertices.reserve(sourceTriangles.size() * 3);
    request.mesh.triangles.reserve(sourceTriangles.size());
    for (const GeomTriangle& triangle : sourceTriangles) {
        const std::uint32_t first = static_cast<std::uint32_t>(
            request.mesh.vertices.size());
        request.mesh.vertices.push_back({
            triangle.vertex1.x, triangle.vertex1.y, triangle.vertex1.z});
        request.mesh.vertices.push_back({
            triangle.vertex2.x, triangle.vertex2.y, triangle.vertex2.z});
        request.mesh.vertices.push_back({
            triangle.vertex3.x, triangle.vertex3.y, triangle.vertex3.z});
        request.mesh.triangles.push_back({first, first + 1, first + 2});
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            request.mesh_to_plane[static_cast<std::size_t>(row * 4 + column)] =
                static_cast<double>(meshToPlane(row, column));
        }
    }

    const DBInstanceID sourceId = source->getDBInstanceID();
    const CutOptions options = m_options;
    const std::uint64_t sessionRevision = m_sessionRevision;
    m_cutInProgress = true;
    CutToolBridge::instance()->projectError({});
    CutToolBridge::instance()->projectBusy(true);

    auto* watcher = new QFutureWatcher<CutGeometryResult>(this);
    connect(watcher, &QFutureWatcher<CutGeometryResult>::finished, this,
            [this, watcher, sourceId, options, planeToWorld,
             sessionRevision]() mutable {
        CutGeometryResult geometry = watcher->future().takeResult();
        watcher->deleteLater();
        if (!isActive() || sessionRevision != m_sessionRevision ||
            sourceId != m_targetModelId) {
            return;
        }

        m_cutInProgress = false;
        CutToolBridge::instance()->projectBusy(false);
        if (!geometry.success) {
            CutToolBridge::instance()->projectError(
                QString::fromStdString(geometry.error));
            return;
        }

        auto* document = DocumentManager::instance();
        const auto source = document
            ? document->getDB<ModelInstanceDB>(sourceId)
            : std::shared_ptr<ModelInstanceDB>{};
        if (!source) {
            CutToolBridge::instance()->projectError(
                QStringLiteral("切割完成前目标模型已被移除"));
            return;
        }

        float groundZ = 0.0f;
        if (source->getParentPrintBedDBId().isValid()) {
            const auto bed = std::dynamic_pointer_cast<PrintBedDB>(
                document->getDBInstance(source->getParentPrintBedDBId()));
            if (bed) groundZ = bed->getOrigin().z;
        }

        TransactionGuard transaction("Cut Model");
        std::vector<std::shared_ptr<ModelInstanceDB>> pieces;
        if (options.keepUpper) {
            if (auto piece = createCutPiece(
                    source, std::move(geometry.upper), " - Upper")) {
                pieces.push_back(std::move(piece));
            }
        }
        if (options.keepLower) {
            if (auto piece = createCutPiece(
                    source, std::move(geometry.lower), " - Lower")) {
                pieces.push_back(std::move(piece));
            }
        }
        const std::size_t expectedPieces =
            static_cast<std::size_t>(options.keepUpper) +
              static_cast<std::size_t>(options.keepLower);
        if (pieces.size() != expectedPieces) {
            transaction.rollback();
            CutToolBridge::instance()->projectError(
                QStringLiteral("无法创建切割后的模型对象"));
            return;
        }

        {
            const Vector3 planeCenter = planeToWorld.getPosition();
            const Vector3 planeNormal = matrixAxis(planeToWorld, 2);
            const Vector3 planeXAxis = matrixAxis(planeToWorld, 0);
            std::size_t pieceIndex = 0;
            const auto transformPiece = [&](const std::shared_ptr<ModelInstanceDB>& piece,
                                            bool upper,
                                            bool placeOnCut,
                                            bool flip) {
                if (!piece || (!placeOnCut && !flip)) return;
                Transform transform = piece->getTransform();
                Vector3 currentXAxis = planeXAxis;
                if (placeOnCut) {
                    const Vector3 target = upper
                        ? Vector3(0.0f, 0.0f, 1.0f)
                        : Vector3(0.0f, 0.0f, -1.0f);
                    const Eigen::Matrix3f placement =
                        rotationBetween(planeNormal, target);
                    transform = rotatedAroundWorldPoint(
                        transform, planeCenter, placement);
                    const Eigen::Vector3f placedAxis = placement *
                        Eigen::Vector3f(
                            planeXAxis.x, planeXAxis.y, planeXAxis.z);
                    currentXAxis = Vector3(
                        placedAxis.x(), placedAxis.y(), placedAxis.z());
                }
                if (flip) {
                    const Eigen::Vector3f axis(
                        currentXAxis.x, currentXAxis.y, currentXAxis.z);
                    const Eigen::Matrix3f flipped = Eigen::AngleAxisf(
                        kPi, axis.normalized()).toRotationMatrix();
                    transform = rotatedAroundWorldPoint(
                        transform, planeCenter, flipped);
                }
                piece->setTransform(transform);
                const auto bounds = piece->worldBounds();
                if (bounds.valid) {
                    transform = piece->getTransform();
                    transform.translate(Vector3(
                        0.0f, 0.0f, groundZ - bounds.min.z));
                    piece->setTransform(transform);
                }
            };
            if (options.keepUpper) {
                transformPiece(pieces[pieceIndex++], true,
                               options.placeUpperOnCut, options.flipUpper);
            }
            if (options.keepLower) {
                transformPiece(pieces[pieceIndex], false,
                               options.placeLowerOnCut, options.flipLower);
            }
        }

        std::string removeError;
        if (!ModelGraphUtil::removeInstance(sourceId, &removeError)) {
            transaction.rollback();
            CutToolBridge::instance()->projectError(
                QString::fromStdString(removeError.empty()
                    ? "Unable to replace the source model" : removeError));
            return;
        }
        transaction.commit();

        std::vector<DBInstanceID> resultIds;
        resultIds.reserve(pieces.size());
        for (const auto& piece : pieces) {
            resultIds.push_back(piece->getDBInstanceID());
        }
        emit requestExit();
        SelectionBridge::instance()->setSelectedObjects(resultIds);
    });
    watcher->setFuture(QtConcurrent::run(
        [request = std::move(request)]() mutable {
            CutGeometryResult geometry;
            libslicer::PlaneCutResult cut =
                libslicer::cut_mesh_with_plane(request);
            geometry.success = cut.success;
            geometry.error = std::move(cut.error);
            if (cut.success) {
                geometry.upper = polyDataFromCutMesh(std::move(cut.upper));
                geometry.lower = polyDataFromCutMesh(std::move(cut.lower));
                if (!geometry.upper || !geometry.lower ||
                    geometry.upper->GetNumberOfCells() <= 0 ||
                    geometry.lower->GetNumberOfCells() <= 0) {
                    geometry.success = false;
                    geometry.error = "Plane cut produced invalid output geometry";
                }
            }
            return geometry;
        }));
}

std::optional<float> CutToolHandler::axisParameterFromMouse(
    const QPoint& mousePosition,
    const Vector3& axisOrigin,
    const Vector3& rawAxisDirection) const {
    auto* document = DocumentManager::instance();
    const auto window = document
        ? std::dynamic_pointer_cast<WindowDB>(
              document->getDBInstance(m_interactionViewId))
        : nullptr;
    const auto coordinates = window ? window->getCoordinateSystem() : nullptr;
    if (!coordinates) return std::nullopt;

    const WorldRay ray = coordinates->rayFromScreen(QPointF(mousePosition));
    const Vector3 axis = normalized(rawAxisDirection);
    const Vector3 direction = normalized(ray.direction);
    if (length(axis) <= 1.0e-7f || length(direction) <= 1.0e-7f) {
        return std::nullopt;
    }
    const Vector3 axisToRay = axisOrigin - ray.origin;
    const float axisRayDot = dot(axis, direction);
    const float axisOffset = dot(axis, axisToRay);
    const float rayOffset = dot(direction, axisToRay);
    const float denominator = 1.0f - axisRayDot * axisRayDot;
    if (denominator > 1.0e-4f) {
        return (axisRayDot * rayOffset - axisOffset) / denominator;
    }
    const float rayParameter = dot(axisOrigin - ray.origin, direction);
    const Vector3 closestRayPoint = ray.origin + direction * rayParameter;
    return dot(closestRayPoint - axisOrigin, axis);
}

std::optional<Vector3> CutToolHandler::rotationVectorFromMouse(
    const QPoint& mousePosition,
    const Vector3& centerWorld,
    const Vector3& rawAxisWorld) const {
    auto* document = DocumentManager::instance();
    const auto window = document
        ? std::dynamic_pointer_cast<WindowDB>(
              document->getDBInstance(m_interactionViewId))
        : nullptr;
    const auto coordinates = window ? window->getCoordinateSystem() : nullptr;
    if (!coordinates) return std::nullopt;

    const WorldRay ray = coordinates->rayFromScreen(QPointF(mousePosition));
    const Vector3 axis = normalized(rawAxisWorld);
    const float denominator = dot(ray.direction, axis);
    if (std::abs(denominator) <= 1.0e-7f) return std::nullopt;
    const float distance = dot(centerWorld - ray.origin, axis) / denominator;
    if (distance < 0.0f) return std::nullopt;
    Vector3 radial = ray.origin + ray.direction * distance - centerWorld;
    radial = radial - axis * dot(radial, axis);
    if (length(radial) <= 1.0e-7f) return std::nullopt;
    return normalized(radial);
}
