#include "ModelTranslateWidgetDBSync.hpp"

#include "VtkDBSyncFactory.hpp"
#include "Foundation/Log.h"
#include <ModelTranslateWidgetDB.hpp>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkMath.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kAxisCount = 3;

Color axisColor(const ModelTranslateWidgetDB& widgetDB, int axis)
{
    switch (axis) {
        case 0: return widgetDB.getXAxisColor();
        case 1: return widgetDB.getYAxisColor();
        case 2: return widgetDB.getZAxisColor();
        default: return Color(1.0f, 1.0f, 1.0f);
    }
}

Vector3 axisDirection(int axis)
{
    switch (axis) {
        case 0: return Vector3(1.0f, 0.0f, 0.0f);
        case 1: return Vector3(0.0f, 1.0f, 0.0f);
        case 2: return Vector3(0.0f, 0.0f, 1.0f);
        default: return Vector3(0.0f, 0.0f, 1.0f);
    }
}

Vector3 axisPosition(const ModelTranslateWidgetDB& widgetDB, int axis)
{
    switch (axis) {
        case 0: return widgetDB.getXArrowPosition();
        case 1: return widgetDB.getYArrowPosition();
        case 2: return widgetDB.getZArrowPosition();
        default: return widgetDB.getWidgetCenter();
    }
}

float length(const Vector3& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vector3 normalized(const Vector3& value)
{
    const float len = length(value);
    if (len <= 1e-6f) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }
    return Vector3(value.x / len, value.y / len, value.z / len);
}

double worldUnitsPerPixel(vtkRenderer* renderer, const Vector3& center)
{
    if (!renderer) {
        return 1.0;
    }

    vtkCamera* camera = renderer->GetActiveCamera();
    int* size = renderer->GetSize();
    if (!camera || !size || size[1] <= 0) {
        return 1.0;
    }

    if (camera->GetParallelProjection()) {
        return (2.0 * camera->GetParallelScale()) / static_cast<double>(size[1]);
    }

    double cameraPosition[3] = {0.0, 0.0, 0.0};
    camera->GetPosition(cameraPosition);
    const double centerPoint[3] = {
        static_cast<double>(center.x),
        static_cast<double>(center.y),
        static_cast<double>(center.z)
    };
    const double distance = std::sqrt(vtkMath::Distance2BetweenPoints(cameraPosition, centerPoint));
    const double viewAngleRadians = vtkMath::RadiansFromDegrees(camera->GetViewAngle());
    const double worldHeight = 2.0 * distance * std::tan(viewAngleRadians * 0.5);
    return std::max(1e-3, worldHeight / static_cast<double>(size[1]));
}

void setDashedLine(vtkPolyData* polyData,
                   const Vector3& start,
                   const Vector3& end,
                   double worldPerPixel)
{
    if (!polyData) {
        return;
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();

    const Vector3 delta = end - start;
    const float totalLength = length(delta);
    if (totalLength <= 1e-6f) {
        polyData->SetPoints(points);
        polyData->SetLines(lines);
        polyData->Modified();
        return;
    }

    const Vector3 dir = normalized(delta);
    const float dashLength = static_cast<float>(std::max(1.0, 9.0 * worldPerPixel));
    const float gapLength = static_cast<float>(std::max(0.6, 5.0 * worldPerPixel));
    float cursor = 0.0f;

    while (cursor < totalLength) {
        const float segmentEnd = std::min(cursor + dashLength, totalLength);
        const Vector3 p0 = start + dir * cursor;
        const Vector3 p1 = start + dir * segmentEnd;

        const vtkIdType id0 = points->InsertNextPoint(p0.x, p0.y, p0.z);
        const vtkIdType id1 = points->InsertNextPoint(p1.x, p1.y, p1.z);
        lines->InsertNextCell(2);
        lines->InsertCellPoint(id0);
        lines->InsertCellPoint(id1);

        cursor = segmentEnd + gapLength;
    }

    polyData->SetPoints(points);
    polyData->SetLines(lines);
    polyData->Modified();
}

} // namespace

void ModelTranslateWidgetDBSync::createWidget()
{
    LOG_DEBUG("ModelTranslateWidgetDBSync::createWidget - Creating Orca-style move gizmo");

    auto widgetDB = documentDB<ModelTranslateWidgetDB>(m_id);
    if (!widgetDB) {
        LOG_ERROR("ModelTranslateWidgetDBSync::createWidget - Failed to get ModelTranslateWidgetDB");
        return;
    }

    m_grabberHeight = 8.0;
    m_grabberRadius = 3.0;
    m_connectionLineWidth = 4.0;

    createAllAxes(widgetDB);
    updateVisualProperties();
    updateComponentPositions(widgetDB);
    updateTransformAndAddToRenderer(widgetDB);
    buildActorPartMapping();

    LOG_DEBUG("ModelTranslateWidgetDBSync::createWidget - Orca-style move gizmo created");
}

void ModelTranslateWidgetDBSync::createAllAxes(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB)
{
    (void)widgetDB;

    for (int axis = 0; axis < kAxisCount; ++axis) {
        createConnectionGeometry(axis);
        createGrabberGeometry(axis);

        if (m_connectionActors[axis]) {
            m_widgetProps.push_back(m_connectionActors[axis]);
        }
        if (m_grabberActors[axis]) {
            m_widgetProps.push_back(m_grabberActors[axis]);
        }
    }
}

void ModelTranslateWidgetDBSync::createConnectionGeometry(int axis)
{
    m_connectionGeometry[axis] = vtkSmartPointer<vtkPolyData>::New();
    m_connectionMappers[axis] = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_connectionMappers[axis]->SetInputData(m_connectionGeometry[axis]);

    m_connectionActors[axis] = vtkSmartPointer<vtkActor>::New();
    m_connectionActors[axis]->SetMapper(m_connectionMappers[axis]);
    m_connectionActors[axis]->PickableOff();
    m_connectionActors[axis]->SetUseBounds(false);
}

void ModelTranslateWidgetDBSync::createGrabberGeometry(int axis)
{
    const Vector3 direction = axisDirection(axis);

    m_grabberSources[axis] = vtkSmartPointer<vtkConeSource>::New();
    m_grabberSources[axis]->SetDirection(direction.x, direction.y, direction.z);
    m_grabberSources[axis]->SetHeight(m_grabberHeight);
    m_grabberSources[axis]->SetRadius(m_grabberRadius);
    m_grabberSources[axis]->SetResolution(m_resolution);

    m_grabberMappers[axis] = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_grabberMappers[axis]->SetInputConnection(m_grabberSources[axis]->GetOutputPort());

    m_grabberActors[axis] = vtkSmartPointer<vtkActor>::New();
    m_grabberActors[axis]->SetMapper(m_grabberMappers[axis]);
    m_grabberActors[axis]->SetUseBounds(false);
}

void ModelTranslateWidgetDBSync::updateVisualProperties()
{
    auto widgetDB = documentDB<ModelTranslateWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    for (int axis = 0; axis < kAxisCount; ++axis) {
        const Color color = axisColor(*widgetDB, axis);

        if (m_connectionActors[axis]) {
            auto property = m_connectionActors[axis]->GetProperty();
            property->SetColor(color.r, color.g, color.b);
            property->SetOpacity(1.0);
            property->SetLineWidth(m_connectionLineWidth);
            property->LightingOff();
        }

        if (m_grabberActors[axis]) {
            auto property = m_grabberActors[axis]->GetProperty();
            property->SetColor(color.r, color.g, color.b);
            property->SetOpacity(1.0);
            property->LightingOff();
            property->SetAmbient(1.0);
            property->SetDiffuse(0.0);
            property->SetSpecular(0.0);
        }
    }
}

void ModelTranslateWidgetDBSync::updateComponentPositions(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB)
{
    if (!widgetDB) {
        return;
    }

    updateAxisPositions(widgetDB);
}

void ModelTranslateWidgetDBSync::updateAxisPositions(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB)
{
    if (!widgetDB) {
        return;
    }

    const Vector3 center = widgetDB->getWidgetCenter();
    const double worldPerPixel = worldUnitsPerPixel(m_vtkRenderer, center);
    m_grabberHeight = std::clamp(32.0 * worldPerPixel, 10.0, 24.0);
    m_grabberRadius = std::clamp(12.0 * worldPerPixel, 3.5, 8.0);

    for (int axis = 0; axis < kAxisCount; ++axis) {
        const Vector3 grabberPos = axisPosition(*widgetDB, axis);

        if (m_connectionGeometry[axis]) {
            setDashedLine(m_connectionGeometry[axis], center, grabberPos, worldPerPixel);
        }

        if (m_grabberSources[axis]) {
            m_grabberSources[axis]->SetHeight(m_grabberHeight);
            m_grabberSources[axis]->SetRadius(m_grabberRadius);
            m_grabberSources[axis]->SetCenter(grabberPos.x, grabberPos.y, grabberPos.z);
            m_grabberSources[axis]->Modified();
        }
    }
}

void ModelTranslateWidgetDBSync::cleanup(const DBInstanceID& id)
{
    LOG_DEBUG("ModelTranslateWidgetDBSync::cleanup - Cleaning up widget resources for ID: {}", id.getValue());

    if (m_renderThread) {
        auto self = std::dynamic_pointer_cast<ModelTranslateWidgetDBSync>(shared_from_this());
        if (!self) {
            return;
        }

        dispatchRenderCommand([self](auto*, auto*) {
            auto renderer = self->m_vtkRenderer;
            if (!renderer) {
                return;
            }

            for (int axis = 0; axis < kAxisCount; ++axis) {
                if (self->m_connectionActors[axis]) {
                    renderer->RemoveActor(self->m_connectionActors[axis]);
                    self->m_connectionActors[axis] = nullptr;
                }
                if (self->m_grabberActors[axis]) {
                    renderer->RemoveActor(self->m_grabberActors[axis]);
                    self->m_grabberActors[axis] = nullptr;
                }
            }
        });
    }

    WidgetDBSync::cleanup(id);
}

void ModelTranslateWidgetDBSync::buildActorPartMapping()
{
    m_actorToPartMap.clear();

    for (int axis = 0; axis < kAxisCount; ++axis) {
        if (m_grabberActors[axis]) {
            m_actorToPartMap[m_grabberActors[axis].GetPointer()] = axis + 1;
        }
    }
}

int ModelTranslateWidgetDBSync::identifyWidgetPart(vtkSmartPointer<vtkActor> actor) const
{
    auto it = m_actorToPartMap.find(actor.GetPointer());
    if (it != m_actorToPartMap.end()) {
        return it->second;
    }
    return NONE;
}

bool ModelTranslateWidgetDBSync::ownsProp(vtkSmartPointer<vtkProp> prop) const
{
    auto actor = vtkActor::SafeDownCast(prop);
    if (!actor) {
        return false;
    }

    return m_actorToPartMap.find(actor) != m_actorToPartMap.end();
}

vtkActor* ModelTranslateWidgetDBSync::getActorForPart(int partId) const
{
    for (const auto& [actor, mappedPart] : m_actorToPartMap) {
        if (mappedPart == partId) {
            return actor;
        }
    }
    return nullptr;
}

bool ModelTranslateWidgetDBSync::highlightPart(int partId, const VisualStyle& style)
{
    vtkActor* actor = getActorForPart(partId);
    if (!actor) {
        return false;
    }

    if (m_originalPartProperties.find(partId) == m_originalPartProperties.end()) {
        m_originalPartProperties[partId] = vtkSmartPointer<vtkProperty>::New();
        m_originalPartProperties[partId]->DeepCopy(actor->GetProperty());
    }

    auto property = actor->GetProperty();
    if (style.color.has_value()) {
        const auto& color = style.color.value();
        property->SetColor(color.x, color.y, color.z);
    } else {
        property->SetColor(1.0, 1.0, 1.0);
    }
    if (style.opacity.has_value() && style.opacity.value() >= 0) {
        property->SetOpacity(style.opacity.value());
    }

    return true;
}

bool ModelTranslateWidgetDBSync::clearPartHighlight(int partId)
{
    vtkActor* actor = getActorForPart(partId);
    if (!actor) {
        return false;
    }

    auto it = m_originalPartProperties.find(partId);
    if (it != m_originalPartProperties.end()) {
        actor->GetProperty()->DeepCopy(it->second);
        m_originalPartProperties.erase(it);
        return true;
    }

    return false;
}

void ModelTranslateWidgetDBSync::onWidgetPropertyChanged(const std::string& propertyName)
{
    auto widgetDB = documentDB<ModelTranslateWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    if (propertyName == "Geometry") {
        updateAxisPositions(widgetDB);
    } else if (propertyName.find("Color") != std::string::npos) {
        updateVisualProperties();
    } else if (propertyName.find("Show") != std::string::npos) {
        updateVisibility();
    }
}

void ModelTranslateWidgetDBSync::updateVisibility()
{
    auto widgetDB = documentDB<ModelTranslateWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    const std::array<bool, kAxisCount> visible{
        widgetDB->getShowXArrow(),
        widgetDB->getShowYArrow(),
        widgetDB->getShowZArrow()
    };

    for (int axis = 0; axis < kAxisCount; ++axis) {
        if (m_connectionActors[axis]) {
            m_connectionActors[axis]->SetVisibility(visible[axis]);
        }
        if (m_grabberActors[axis]) {
            m_grabberActors[axis]->SetVisibility(visible[axis]);
        }
    }
}

void ModelTranslateWidgetDBSync::applyWidgetContext()
{
}

void ModelTranslateWidgetDBSync::updateTransformAndAddToRenderer(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB)
{
    (void)widgetDB;

    if (!m_vtkRenderer || m_actorsAddedToRenderer) {
        return;
    }

    for (int axis = 0; axis < kAxisCount; ++axis) {
        if (m_connectionActors[axis]) {
            m_vtkRenderer->AddActor(m_connectionActors[axis]);
        }
        if (m_grabberActors[axis]) {
            m_vtkRenderer->AddActor(m_grabberActors[axis]);
        }
    }

    m_actorsAddedToRenderer = true;
}

REGISTER_VTK_DBSYNC(TypeID::MODEL_TRANSLATE_WIDGET_DB, ModelTranslateWidgetDBSync,
                        GPlatform::Rendering::RendererRoleSets::Prepare)
