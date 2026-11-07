#include "ModelScaleWidgetDBSync.hpp"
#include "VtkDBSyncFactory.hpp"

#include "Foundation/Log.h"

#include <vtkCamera.h>
#include <vtkMath.h>
#include <vtkPropCollection.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kOrcaFixedGrabberPixels = 32.0;
constexpr double kMinGrabberWorldSize = 0.01;
constexpr double kOrcaGrabberOpacity = 1.0;
const Color kOrcaUniformColor(0.0f, 1.0f, 1.0f);
const Color kOrcaUniformHoverColor(0.0f, 0.7f, 0.7f);
const Color kOrcaConstrainedColor(0.5f, 0.5f, 0.5f);

double meanSize(const Vector3& min, const Vector3& max) {
    const Vector3 size = max - min;
    return std::max(1.0, static_cast<double>(size.x + size.y + size.z) / 3.0);
}

void setLine(vtkPolyData* geometry, const Vector3& a, const Vector3& b) {
    if (!geometry) {
        return;
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    const vtkIdType id0 = points->InsertNextPoint(a.x, a.y, a.z);
    const vtkIdType id1 = points->InsertNextPoint(b.x, b.y, b.z);
    lines->InsertNextCell(2);
    lines->InsertCellPoint(id0);
    lines->InsertCellPoint(id1);
    geometry->SetPoints(points);
    geometry->SetLines(lines);
    geometry->Modified();
}

void configureActor(vtkActor* actor, const Color& color, double opacity) {
    if (!actor) {
        return;
    }

    auto property = actor->GetProperty();
    property->SetColor(color.r, color.g, color.b);
    property->SetOpacity(opacity);
    property->LightingOn();
    property->SetAmbient(0.25);
    property->SetDiffuse(0.75);
    property->SetSpecular(0.0);
}

} // namespace

void ModelScaleWidgetDBSync::createWidget()
{
    LOG_DEBUG("ScaleDBSync createWidget begin id={} renderer={}",
             m_id.getValue(),
             static_cast<const void*>(m_vtkRenderer.GetPointer()));

    auto widgetDB = documentDB<ModelScaleWidgetDB>(m_id);
    if (!widgetDB) {
        LOG_ERROR("ModelScaleWidgetDBSync::createWidget - Failed to get ModelScaleWidgetDB");
        return;
    }

    const Vector3 min = widgetDB->getModelBoundsMin();
    const Vector3 max = widgetDB->getModelBoundsMax();
    LOG_DEBUG("ScaleDBSync createWidget db visible={} linkedActor={} boundsMin=({:.3f},{:.3f},{:.3f}) boundsMax=({:.3f},{:.3f},{:.3f}) activeHandle={}",
             widgetDB->getVisible() ? 1 : 0,
             widgetDB->getLinkedActorID().getValue(),
             min.x, min.y, min.z,
             max.x, max.y, max.z,
             widgetDB->getActiveHandle());

    createAllParts(widgetDB);
    updateVisualProperties();
    updateComponentPositions(widgetDB);
    updateVisibility();
    updateTransformAndAddToRenderer();
    buildActorPartMapping();

    LOG_DEBUG("ScaleDBSync createWidget end id={} widgetProps={} actorsAdded={}",
             m_id.getValue(),
             m_widgetProps.size(),
             m_actorsAddedToRenderer ? 1 : 0);
}

void ModelScaleWidgetDBSync::createAllParts(std::shared_ptr<const ModelScaleWidgetDB> widgetDB)
{
    (void)widgetDB;

    for (int part = 0; part < GRABBER_COUNT; ++part) {
        createGrabberGeometry(part);
        if (m_grabberActors[part]) {
            m_widgetProps.push_back(m_grabberActors[part]);
        }
    }

    for (int edge = 0; edge < kConnectionCount; ++edge) {
        createConnectionGeometry(edge);
        if (m_connectionActors[edge]) {
            m_widgetProps.push_back(m_connectionActors[edge]);
        }
    }
}

void ModelScaleWidgetDBSync::createGrabberGeometry(int part)
{
    m_grabberSources[part] = vtkSmartPointer<vtkCubeSource>::New();

    m_grabberMappers[part] = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_grabberMappers[part]->SetInputConnection(m_grabberSources[part]->GetOutputPort());

    m_grabberActors[part] = vtkSmartPointer<vtkActor>::New();
    m_grabberActors[part]->SetMapper(m_grabberMappers[part]);
    m_grabberActors[part]->PickableOn();
    m_grabberActors[part]->SetUseBounds(false);

    LOG_DEBUG("ScaleDBSync createGrabber part={} source={} mapper={} actor={} pickable={} useBounds={}",
             part,
             static_cast<const void*>(m_grabberSources[part].GetPointer()),
             static_cast<const void*>(m_grabberMappers[part].GetPointer()),
             static_cast<const void*>(m_grabberActors[part].GetPointer()),
             m_grabberActors[part]->GetPickable(),
             m_grabberActors[part]->GetUseBounds());
}

void ModelScaleWidgetDBSync::createConnectionGeometry(int edge)
{
    m_connectionGeometry[edge] = vtkSmartPointer<vtkPolyData>::New();
    m_connectionMappers[edge] = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_connectionMappers[edge]->SetInputData(m_connectionGeometry[edge]);

    m_connectionActors[edge] = vtkSmartPointer<vtkActor>::New();
    m_connectionActors[edge]->SetMapper(m_connectionMappers[edge]);
    m_connectionActors[edge]->PickableOff();
    m_connectionActors[edge]->SetUseBounds(false);

    LOG_DEBUG("ScaleDBSync createConnection edge={} polyData={} mapper={} actor={} pickable={} useBounds={}",
             edge,
             static_cast<const void*>(m_connectionGeometry[edge].GetPointer()),
             static_cast<const void*>(m_connectionMappers[edge].GetPointer()),
             static_cast<const void*>(m_connectionActors[edge].GetPointer()),
             m_connectionActors[edge]->GetPickable(),
             m_connectionActors[edge]->GetUseBounds());
}

void ModelScaleWidgetDBSync::updateComponentPositions(std::shared_ptr<const ModelScaleWidgetDB> widgetDB)
{
    if (!widgetDB) {
        return;
    }

    for (int part = 0; part < GRABBER_COUNT; ++part) {
        const Vector3 position = grabberPosition(part, *widgetDB);
        const double size = fixedGrabberWorldSize(position, m_vtkRenderer, *widgetDB);

        if (m_grabberSources[part]) {
            m_grabberSources[part]->SetCenter(position.x, position.y, position.z);
            m_grabberSources[part]->SetXLength(size);
            m_grabberSources[part]->SetYLength(size);
            m_grabberSources[part]->SetZLength(size);
            m_grabberSources[part]->Update();
        }

        if (m_grabberActors[part]) {
            m_grabberActors[part]->SetUserTransform(nullptr);
        }
    }

    const int edges[kConnectionCount][2] = {{6, 7}, {7, 8}, {8, 9}, {9, 6}};
    for (int edge = 0; edge < kConnectionCount; ++edge) {
        const int a = edges[edge][0];
        const int b = edges[edge][1];
        if (m_connectionGeometry[edge]) {
            setLine(m_connectionGeometry[edge],
                    grabberPosition(a, *widgetDB),
                    grabberPosition(b, *widgetDB));
            if (m_connectionMappers[edge]) {
                m_connectionMappers[edge]->Update();
            }
        }

        if (m_connectionActors[edge]) {
            m_connectionActors[edge]->SetUserTransform(nullptr);
        }
    }
}

void ModelScaleWidgetDBSync::updateVisualProperties()
{
    auto widgetDB = documentDB<ModelScaleWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    for (int part = 0; part < GRABBER_COUNT; ++part) {
        const bool active = widgetDB->getActiveHandle() == part;
        configureActor(m_grabberActors[part],
                       colorForPart(part, *widgetDB, active),
                       kOrcaGrabberOpacity);
    }

    for (int edge = 0; edge < kConnectionCount; ++edge) {
        const Color color = (edge < 2) ? widgetDB->getYHandleColor() : widgetDB->getXHandleColor();
        configureActor(m_connectionActors[edge], color, kOrcaGrabberOpacity);
        if (m_connectionActors[edge]) {
            m_connectionActors[edge]->GetProperty()->SetLineWidth(4.0);
        }
    }
}

void ModelScaleWidgetDBSync::updateVisibility()
{
    auto widgetDB = documentDB<ModelScaleWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    for (int part = 0; part < GRABBER_COUNT; ++part) {
        const bool enabled = isEnabledPart(part, *widgetDB);
        if (m_grabberActors[part]) {
            m_grabberActors[part]->SetVisibility(widgetDB->getVisible() && enabled ? 1 : 0);
            m_grabberActors[part]->SetPickable(widgetDB->getVisible() && enabled ? 1 : 0);
        }
    }

    for (int edge = 0; edge < kConnectionCount; ++edge) {
        if (m_connectionActors[edge]) {
            m_connectionActors[edge]->SetVisibility(widgetDB->getVisible() && widgetDB->getShowUniformHandle() ? 1 : 0);
        }
    }
}

void ModelScaleWidgetDBSync::updateTransformAndAddToRenderer()
{
    if (!m_vtkRenderer || m_actorsAddedToRenderer) {
        LOG_DEBUG("ScaleDBSync addToRenderer skipped renderer={} actorsAdded={}",
                 static_cast<const void*>(m_vtkRenderer.GetPointer()),
                 m_actorsAddedToRenderer ? 1 : 0);
        return;
    }

    vtkPropCollection* propsBefore = m_vtkRenderer->GetViewProps();
    const int beforeCount = propsBefore ? propsBefore->GetNumberOfItems() : -1;
    LOG_DEBUG("ScaleDBSync addToRenderer begin renderer={} propCountBefore={}",
             static_cast<const void*>(m_vtkRenderer.GetPointer()),
             beforeCount);

    for (int part = 0; part < GRABBER_COUNT; ++part) {
        if (m_grabberActors[part]) {
            m_vtkRenderer->AddActor(m_grabberActors[part]);
            LOG_DEBUG("ScaleDBSync AddActor grabber part={} actor={} visible={} pickable={}",
                     part,
                     static_cast<const void*>(m_grabberActors[part].GetPointer()),
                     m_grabberActors[part]->GetVisibility(),
                     m_grabberActors[part]->GetPickable());
        }
    }

    for (int edge = 0; edge < kConnectionCount; ++edge) {
        if (m_connectionActors[edge]) {
            m_vtkRenderer->AddActor(m_connectionActors[edge]);
            LOG_DEBUG("ScaleDBSync AddActor connection edge={} actor={} visible={}",
                     edge,
                     static_cast<const void*>(m_connectionActors[edge].GetPointer()),
                     m_connectionActors[edge]->GetVisibility());
        }
    }

    m_actorsAddedToRenderer = true;
    vtkPropCollection* propsAfter = m_vtkRenderer->GetViewProps();
    const int afterCount = propsAfter ? propsAfter->GetNumberOfItems() : -1;
    LOG_DEBUG("ScaleDBSync addToRenderer end renderer={} propCountAfter={} actorsAdded={}",
             static_cast<const void*>(m_vtkRenderer.GetPointer()),
             afterCount,
             m_actorsAddedToRenderer ? 1 : 0);
}

bool ModelScaleWidgetDBSync::isEnabledPart(int partId, const ModelScaleWidgetDB& widgetDB)
{
    if (partId == GRABBER_Z_MIN) {
        return false;
    }
    if (partId == GRABBER_X_MIN || partId == GRABBER_X_MAX) {
        return widgetDB.getShowXHandle();
    }
    if (partId == GRABBER_Y_MIN || partId == GRABBER_Y_MAX) {
        return widgetDB.getShowYHandle();
    }
    if (partId == GRABBER_Z_MAX) {
        return widgetDB.getShowZHandle();
    }
    if (partId >= GRABBER_UNIFORM_XMIN_YMIN && partId <= GRABBER_UNIFORM_XMIN_YMAX) {
        return widgetDB.getShowUniformHandle();
    }
    return false;
}

int ModelScaleWidgetDBSync::axisForPart(int partId)
{
    if (partId == GRABBER_X_MIN || partId == GRABBER_X_MAX) {
        return 0;
    }
    if (partId == GRABBER_Y_MIN || partId == GRABBER_Y_MAX) {
        return 1;
    }
    if (partId == GRABBER_Z_MIN || partId == GRABBER_Z_MAX) {
        return 2;
    }
    return -1;
}

Vector3 ModelScaleWidgetDBSync::grabberPosition(int partId, const ModelScaleWidgetDB& widgetDB)
{
    return widgetDB.grabberWorldPosition(partId);
}

Color ModelScaleWidgetDBSync::colorForPart(int partId, const ModelScaleWidgetDB& widgetDB, bool active)
{
    if (partId == widgetDB.getConstrainedHandle()) {
        return kOrcaConstrainedColor;
    }
    if (active) {
        return hoverColorForPart(partId, widgetDB);
    }

    const int axis = axisForPart(partId);
    if (axis < 0) {
        return kOrcaUniformColor;
    }
    if (axis == 0) {
        return widgetDB.getXHandleColor();
    }
    if (axis == 1) {
        return widgetDB.getYHandleColor();
    }
    return widgetDB.getZHandleColor();
}

Color ModelScaleWidgetDBSync::hoverColorForPart(int partId, const ModelScaleWidgetDB& widgetDB)
{
    const int axis = axisForPart(partId);
    if (axis < 0) {
        return kOrcaUniformHoverColor;
    }

    const Color base = axis == 0 ? widgetDB.getXHandleColor()
                     : axis == 1 ? widgetDB.getYHandleColor()
                                 : widgetDB.getZHandleColor();

    if (axis == 0) {
        return Color(std::min(base.r * 1.2f, 1.0f),
                     std::min(base.g * 1.4f, 1.0f),
                     std::min(base.b * 1.4f, 1.0f));
    }
    return Color(std::min(base.r * 1.2f, 1.0f),
                 std::min(base.g * 1.2f, 1.0f),
                 std::min(base.b * 1.2f, 1.0f));
}

double ModelScaleWidgetDBSync::fixedGrabberWorldSize(const Vector3& position,
                                                     vtkRenderer* renderer,
                                                     const ModelScaleWidgetDB& widgetDB)
{
    if (!renderer) {
        return std::max(kMinGrabberWorldSize,
                        meanSize(widgetDB.getModelBoundsMin(), widgetDB.getModelBoundsMax()) * 0.08);
    }

    vtkCamera* camera = renderer->GetActiveCamera();
    const int* viewportSize = renderer->GetSize();
    if (!camera || !viewportSize || viewportSize[1] <= 0) {
        return std::max(kMinGrabberWorldSize,
                        meanSize(widgetDB.getModelBoundsMin(), widgetDB.getModelBoundsMax()) * 0.08);
    }

    double worldPerPixel = 0.0;
    if (camera->GetParallelProjection()) {
        worldPerPixel = (camera->GetParallelScale() * 2.0) / static_cast<double>(viewportSize[1]);
    } else {
        double cameraPos[3];
        camera->GetPosition(cameraPos);

        const double point[3] = {
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z)
        };
        const double distance = std::sqrt(vtkMath::Distance2BetweenPoints(cameraPos, point));
        const double radians = vtkMath::RadiansFromDegrees(camera->GetViewAngle());
        worldPerPixel = (2.0 * distance * std::tan(radians * 0.5)) / static_cast<double>(viewportSize[1]);
    }

    return std::max(kMinGrabberWorldSize, worldPerPixel * kOrcaFixedGrabberPixels);
}

void ModelScaleWidgetDBSync::onWidgetPropertyChanged(const std::string& propertyName)
{
    auto widgetDB = documentDB<ModelScaleWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    if (propertyName == "ModelCenter" ||
        propertyName == "ModelBoundsMin" ||
        propertyName == "ModelBoundsMax" ||
        propertyName == "ModelLocalBoundsMin" ||
        propertyName == "ModelLocalBoundsMax" ||
        propertyName == "ModelReferenceTransform" ||
        propertyName == "IsDragging" ||
        propertyName.empty()) {
        updateComponentPositions(widgetDB);
    }

    if (propertyName == "ActiveHandle" ||
        propertyName == "ConstrainedHandle" ||
        propertyName == "XHandleColor" ||
        propertyName == "YHandleColor" ||
        propertyName == "ZHandleColor" ||
        propertyName == "UniformHandleColor" ||
        propertyName.empty()) {
        updateVisualProperties();
    }

    if (propertyName == "Visible" ||
        propertyName == "ShowXHandle" ||
        propertyName == "ShowYHandle" ||
        propertyName == "ShowZHandle" ||
        propertyName == "ShowUniformHandle" ||
        propertyName.empty()) {
        updateVisibility();
    }
}

void ModelScaleWidgetDBSync::applyWidgetContext()
{
}

void ModelScaleWidgetDBSync::cleanup(const DBInstanceID& id)
{
    LOG_DEBUG("ModelScaleWidgetDBSync::cleanup - Cleaning up widget resources for ID: {}", id.getValue());

    if (m_renderThread) {
        auto self = shared_from_this();
        dispatchRenderCommand([self](auto*, auto*) {
            auto widgetSync = std::dynamic_pointer_cast<ModelScaleWidgetDBSync>(self);
            if (!widgetSync) {
                return;
            }

            if (widgetSync->m_vtkRenderer) {
                for (auto& actor : widgetSync->m_grabberActors) {
                    if (actor) {
                        widgetSync->m_vtkRenderer->RemoveActor(actor);
                    }
                }
                for (auto& actor : widgetSync->m_connectionActors) {
                    if (actor) {
                        widgetSync->m_vtkRenderer->RemoveActor(actor);
                    }
                }
            }

            widgetSync->m_grabberSources.fill(nullptr);
            widgetSync->m_grabberMappers.fill(nullptr);
            widgetSync->m_grabberActors.fill(nullptr);
            widgetSync->m_connectionGeometry.fill(nullptr);
            widgetSync->m_connectionMappers.fill(nullptr);
            widgetSync->m_connectionActors.fill(nullptr);
            widgetSync->m_actorToPartMap.clear();
            widgetSync->m_originalPartProperties.clear();
            widgetSync->m_actorsAddedToRenderer = false;
        });
    }

    WidgetDBSync::cleanup(id);
}

int ModelScaleWidgetDBSync::identifyWidgetPart(vtkSmartPointer<vtkActor> actor) const
{
    if (!actor) {
        return PartId::NONE;
    }
    const auto it = m_actorToPartMap.find(actor.Get());
    return it == m_actorToPartMap.end() ? PartId::NONE : it->second;
}

bool ModelScaleWidgetDBSync::ownsProp(vtkSmartPointer<vtkProp> prop) const
{
    if (!prop) {
        return false;
    }

    vtkActor* actor = vtkActor::SafeDownCast(prop);
    if (!actor) {
        return false;
    }
    return m_actorToPartMap.find(actor) != m_actorToPartMap.end();
}

bool ModelScaleWidgetDBSync::highlightPart(int partId, const VisualStyle& style)
{
    (void)style;

    vtkActor* actor = getActorForPart(partId);
    if (!actor) {
        return false;
    }

    if (m_originalPartProperties.find(partId) == m_originalPartProperties.end()) {
        m_originalPartProperties[partId] = vtkSmartPointer<vtkProperty>::New();
        m_originalPartProperties[partId]->DeepCopy(actor->GetProperty());
    }

    auto property = actor->GetProperty();
    auto widgetDB = documentDB<ModelScaleWidgetDB>(m_id);
    if (widgetDB) {
        const Color color = hoverColorForPart(partId, *widgetDB);
        property->SetColor(color.r, color.g, color.b);
    }
    property->SetOpacity(kOrcaGrabberOpacity);
    property->LightingOn();
    property->SetAmbient(0.25);
    property->SetDiffuse(0.75);
    property->SetSpecular(0.0);
    return true;
}

bool ModelScaleWidgetDBSync::clearPartHighlight(int partId)
{
    const auto it = m_originalPartProperties.find(partId);
    if (it == m_originalPartProperties.end()) {
        return false;
    }

    vtkActor* actor = getActorForPart(partId);
    if (actor) {
        actor->GetProperty()->DeepCopy(it->second);
    }
    m_originalPartProperties.erase(it);
    return true;
}

void ModelScaleWidgetDBSync::buildActorPartMapping()
{
    m_actorToPartMap.clear();
    for (int part = 0; part < GRABBER_COUNT; ++part) {
        if (part == GRABBER_Z_MIN) {
            continue;
        }
        if (m_grabberActors[part]) {
            m_actorToPartMap[m_grabberActors[part].Get()] = part;
        }
    }
}

vtkActor* ModelScaleWidgetDBSync::getActorForPart(int partId) const
{
    if (partId < 0 || partId >= GRABBER_COUNT) {
        return nullptr;
    }
    return m_grabberActors[partId].Get();
}

REGISTER_VTK_DBSYNC(TypeID::MODEL_SCALE_WIDGET_DB, ModelScaleWidgetDBSync,
                        GPlatform::Rendering::RendererRoleSets::Prepare)
