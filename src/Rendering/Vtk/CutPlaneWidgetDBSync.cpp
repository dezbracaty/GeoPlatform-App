#include "CutPlaneWidgetDBSync.hpp"

#include "VtkDBSyncFactory.hpp"

#include <vtkAlgorithmOutput.h>
#include <vtkActor.h>
#include <vtkConeSource.h>
#include <vtkLineSource.h>
#include <vtkMatrix4x4.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRegularPolygonSource.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

vtkSmartPointer<vtkActor> actorFor(vtkAlgorithmOutput* output) {
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(output);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->SetUseBounds(false);
    return actor;
}

Color colorForPart(CutPlaneWidgetDB::Part part) {
    switch (part) {
        case CutPlaneWidgetDB::Part::Plane:
            return Color(0.9f, 0.9f, 0.9f);
        case CutPlaneWidgetDB::Part::NormalMoveHandle:
            return Color(1.0f, 0.82f, 0.12f);
        case CutPlaneWidgetDB::Part::RotateLocalXHandle:
            return Color(1.0f, 60.0f / 255.0f, 91.0f / 255.0f);
        case CutPlaneWidgetDB::Part::RotateLocalYHandle:
            return Color(100.0f / 255.0f, 200.0f / 255.0f, 24.0f / 255.0f);
        default:
            return Color(1.0f, 1.0f, 1.0f);
    }
}

} // namespace

void CutPlaneWidgetDBSync::createWidget() {
    createActors();
    updateGeometryAndTransform();
    updateStyle();
}

void CutPlaneWidgetDBSync::createActors() {
    m_planeSource = vtkSmartPointer<vtkPlaneSource>::New();
    m_normalLineSource = vtkSmartPointer<vtkLineSource>::New();
    m_normalHandleSource = vtkSmartPointer<vtkConeSource>::New();
    m_xRingSource = vtkSmartPointer<vtkRegularPolygonSource>::New();
    m_yRingSource = vtkSmartPointer<vtkRegularPolygonSource>::New();
    m_xHandleSource = vtkSmartPointer<vtkSphereSource>::New();
    m_yHandleSource = vtkSmartPointer<vtkSphereSource>::New();

    m_xRingSource->GeneratePolygonOff();
    m_yRingSource->GeneratePolygonOff();
    m_xRingSource->SetNumberOfSides(128);
    m_yRingSource->SetNumberOfSides(128);
    m_xRingSource->SetNormal(1.0, 0.0, 0.0);
    m_yRingSource->SetNormal(0.0, 1.0, 0.0);
    m_normalHandleSource->SetDirection(0.0, 0.0, 1.0);
    m_normalHandleSource->SetResolution(32);
    for (const auto& source : {m_xHandleSource, m_yHandleSource}) {
        source->SetThetaResolution(24);
        source->SetPhiResolution(16);
    }

    m_planeActor = actorFor(m_planeSource->GetOutputPort());
    m_normalLineActor = actorFor(m_normalLineSource->GetOutputPort());
    m_normalHandleActor = actorFor(m_normalHandleSource->GetOutputPort());
    m_xRingActor = actorFor(m_xRingSource->GetOutputPort());
    m_yRingActor = actorFor(m_yRingSource->GetOutputPort());
    m_xHandleActor = actorFor(m_xHandleSource->GetOutputPort());
    m_yHandleActor = actorFor(m_yHandleSource->GetOutputPort());

    // Rings and the connection line are visual guides. Only explicit 3-D
    // handles and the plane participate in renderer picking.
    m_normalLineActor->PickableOff();
    m_xRingActor->PickableOff();
    m_yRingActor->PickableOff();

    registerPickActor(m_planeActor, CutPlaneWidgetDB::Part::Plane);
    registerPickActor(m_normalHandleActor,
                      CutPlaneWidgetDB::Part::NormalMoveHandle);
    registerPickActor(m_xHandleActor,
                      CutPlaneWidgetDB::Part::RotateLocalXHandle);
    registerPickActor(m_yHandleActor,
                      CutPlaneWidgetDB::Part::RotateLocalYHandle);

    registerVisualActor(m_planeActor, CutPlaneWidgetDB::Part::Plane);
    registerVisualActor(m_normalLineActor,
                        CutPlaneWidgetDB::Part::NormalMoveHandle);
    registerVisualActor(m_normalHandleActor,
                        CutPlaneWidgetDB::Part::NormalMoveHandle);
    registerVisualActor(m_xRingActor,
                        CutPlaneWidgetDB::Part::RotateLocalXHandle);
    registerVisualActor(m_xHandleActor,
                        CutPlaneWidgetDB::Part::RotateLocalXHandle);
    registerVisualActor(m_yRingActor,
                        CutPlaneWidgetDB::Part::RotateLocalYHandle);
    registerVisualActor(m_yHandleActor,
                        CutPlaneWidgetDB::Part::RotateLocalYHandle);

    for (const auto& actor : {m_planeActor, m_normalLineActor,
                              m_normalHandleActor, m_xRingActor,
                              m_yRingActor, m_xHandleActor, m_yHandleActor}) {
        addActor(actor);
    }
}

void CutPlaneWidgetDBSync::addActor(vtkSmartPointer<vtkActor> actor) {
    if (!actor) return;
    m_widgetProps.push_back(actor);
    if (m_vtkRenderer) m_vtkRenderer->AddActor(actor);
}

void CutPlaneWidgetDBSync::registerPickActor(
    vtkSmartPointer<vtkActor> actor, CutPlaneWidgetDB::Part part) {
    if (actor) m_pickParts[actor] = static_cast<int>(part);
}

void CutPlaneWidgetDBSync::registerVisualActor(
    vtkSmartPointer<vtkActor> actor, CutPlaneWidgetDB::Part part) {
    if (actor) m_visualParts[static_cast<int>(part)].push_back(actor);
}

void CutPlaneWidgetDBSync::updateGeometryAndTransform() {
    const auto widget = documentDB<CutPlaneWidgetDB>(m_id);
    if (!widget || !m_planeSource) return;

    const float radius = std::max(widget->getWidgetRadius(), 1.0f);
    const float planeHalfSize = radius * 0.72f;
    m_planeSource->SetOrigin(-planeHalfSize, -planeHalfSize, 0.0);
    m_planeSource->SetPoint1(planeHalfSize, -planeHalfSize, 0.0);
    m_planeSource->SetPoint2(-planeHalfSize, planeHalfSize, 0.0);

    const float lineEnd = radius * 0.82f;
    m_normalLineSource->SetPoint1(0.0, 0.0, 0.0);
    m_normalLineSource->SetPoint2(0.0, 0.0, lineEnd);
    m_normalHandleSource->SetCenter(0.0, 0.0, lineEnd);
    m_normalHandleSource->SetHeight(std::max(radius * 0.18f, 3.0f));
    m_normalHandleSource->SetRadius(std::max(radius * 0.065f, 1.5f));

    const float xRadius = radius * 0.92f;
    const float yRadius = radius * 1.04f;
    m_xRingSource->SetRadius(xRadius);
    m_yRingSource->SetRadius(yRadius);

    constexpr float handleAngle =
        35.0f * 3.14159265358979323846f / 180.0f;
    const float handleRadius = std::max(radius * 0.065f, 1.8f);
    m_xHandleSource->SetRadius(handleRadius);
    m_xHandleSource->SetCenter(0.0,
                               std::sin(handleAngle) * xRadius,
                               std::cos(handleAngle) * xRadius);
    m_yHandleSource->SetRadius(handleRadius);
    m_yHandleSource->SetCenter(std::sin(handleAngle) * yRadius,
                               0.0,
                               std::cos(handleAngle) * yRadius);

    const auto& source = widget->getPlaneToWorld().getMatrix();
    auto matrix = vtkSmartPointer<vtkMatrix4x4>::New();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            matrix->SetElement(row, column, source(row, column));
        }
    }
    for (const auto& actor : {m_planeActor, m_normalLineActor,
                              m_normalHandleActor, m_xRingActor,
                              m_yRingActor, m_xHandleActor, m_yHandleActor}) {
        if (actor) actor->SetUserMatrix(matrix);
    }

    m_planeSource->Modified();
    m_normalLineSource->Modified();
    m_normalHandleSource->Modified();
    m_xRingSource->Modified();
    m_yRingSource->Modified();
    m_xHandleSource->Modified();
    m_yHandleSource->Modified();
}

void CutPlaneWidgetDBSync::updateStyle() {
    const auto widget = documentDB<CutPlaneWidgetDB>(m_id);
    if (!widget) return;
    const int active = widget->getActivePart();
    const int hovered = widget->getHoveredPart();

    for (const auto& [partId, actors] : m_visualParts) {
        const auto part = static_cast<CutPlaneWidgetDB::Part>(partId);
        const Color base = colorForPart(part);
        const bool highlighted = active == partId || hovered == partId;
        const double opacity = part == CutPlaneWidgetDB::Part::Plane
            ? (highlighted ? 0.42 : 0.28)
            : 1.0;
        for (vtkActor* actor : actors) {
            if (!actor) continue;
            auto* property = actor->GetProperty();
            const double blend = highlighted ? 0.40 : 0.0;
            property->SetColor(base.r + (1.0 - base.r) * blend,
                               base.g + (1.0 - base.g) * blend,
                               base.b + (1.0 - base.b) * blend);
            property->SetOpacity(opacity);
            property->SetLineWidth(highlighted ? 5.0 : 3.0);
            property->LightingOff();
            property->SetAmbient(1.0);
            property->SetDiffuse(0.0);
        }
    }
}

void CutPlaneWidgetDBSync::onWidgetPropertyChanged(
    const std::string& propertyName) {
    if (propertyName == "PlaneToWorld" || propertyName == "WidgetRadius") {
        updateGeometryAndTransform();
    }
    if (propertyName == "ActivePart" || propertyName == "IsDragging") {
        updateStyle();
    }
}

void CutPlaneWidgetDBSync::applyWidgetContext() {
}

bool CutPlaneWidgetDBSync::ownsProp(vtkSmartPointer<vtkProp> prop) const {
    auto* actor = vtkActor::SafeDownCast(prop);
    return actor && m_pickParts.find(actor) != m_pickParts.end();
}

int CutPlaneWidgetDBSync::identifyWidgetPart(
    vtkSmartPointer<vtkActor> actor) const {
    const auto found = m_pickParts.find(actor);
    return found == m_pickParts.end()
        ? static_cast<int>(CutPlaneWidgetDB::Part::None)
        : found->second;
}

bool CutPlaneWidgetDBSync::highlightPart(int partId,
                                         const VisualStyle&) {
    if (m_visualParts.find(partId) == m_visualParts.end()) {
        return false;
    }
    updateStyle();
    return true;
}

bool CutPlaneWidgetDBSync::clearPartHighlight(int partId) {
    if (m_visualParts.find(partId) == m_visualParts.end()) return false;
    updateStyle();
    return true;
}

REGISTER_VTK_DBSYNC(TypeID::CUT_PLANE_WIDGET_DB, CutPlaneWidgetDBSync,
                    GPlatform::Rendering::RendererRoleSets::Prepare)
