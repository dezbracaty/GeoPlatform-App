#include "SelectionBoxWidgetDBSync.hpp"

#include "VtkDBSyncFactory.hpp"
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <algorithm>

void SelectionBoxWidgetDBSync::createWidget() {
    const auto widgetDB = documentDB<SelectionBoxWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    m_boxMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_boxMapper->ScalarVisibilityOff();

    m_boxActor = vtkSmartPointer<vtkActor>::New();
    m_boxActor->SetMapper(m_boxMapper);
    m_boxActor->SetPickable(0);
    m_boxActor->SetUseBounds(false);
    m_boxActor->GetProperty()->LightingOff();
    m_boxActor->GetProperty()->SetLineWidth(2.0);
    m_boxActor->GetProperty()->SetColor(1.0, 1.0, 1.0);

    m_widgetProps.push_back(m_boxActor);
    if (m_vtkRenderer) {
        m_vtkRenderer->AddActor(m_boxActor);
    }

    rebuildGeometry(widgetDB);
    updateVisibility(widgetDB);
}

void SelectionBoxWidgetDBSync::rebuildGeometry(
    const std::shared_ptr<const SelectionBoxWidgetDB>& widgetDB) {
    if (!m_boxMapper || !widgetDB || !widgetDB->getHasValidBounds()) {
        return;
    }

    const Vector3 minimum = widgetDB->getModelBoundsMin();
    const Vector3 maximum = widgetDB->getModelBoundsMax();
    const Vector3 size(maximum.x - minimum.x,
                       maximum.y - minimum.y,
                       maximum.z - minimum.z);

    double segmentLength = std::min({size.x, size.y, size.z}) * 0.15;
    if (segmentLength <= 1e-6) {
        segmentLength = std::max({size.x, size.y, size.z, 1.0f}) * 0.05;
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    const double xs[2] = {minimum.x, maximum.x};
    const double ys[2] = {minimum.y, maximum.y};
    const double zs[2] = {minimum.z, maximum.z};

    for (int x = 0; x < 2; ++x) {
        const double xDirection = x == 0 ? 1.0 : -1.0;
        for (int y = 0; y < 2; ++y) {
            const double yDirection = y == 0 ? 1.0 : -1.0;
            for (int z = 0; z < 2; ++z) {
                const double zDirection = z == 0 ? 1.0 : -1.0;
                const double cx = xs[x];
                const double cy = ys[y];
                const double cz = zs[z];
                const vtkIdType corner = points->InsertNextPoint(cx, cy, cz);
                const vtkIdType alongX = points->InsertNextPoint(cx + xDirection * segmentLength, cy, cz);
                const vtkIdType alongY = points->InsertNextPoint(cx, cy + yDirection * segmentLength, cz);
                const vtkIdType alongZ = points->InsertNextPoint(cx, cy, cz + zDirection * segmentLength);

                lines->InsertNextCell(2);
                lines->InsertCellPoint(corner);
                lines->InsertCellPoint(alongX);
                lines->InsertNextCell(2);
                lines->InsertCellPoint(corner);
                lines->InsertCellPoint(alongY);
                lines->InsertNextCell(2);
                lines->InsertCellPoint(corner);
                lines->InsertCellPoint(alongZ);
            }
        }
    }

    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetLines(lines);
    m_boxMapper->SetInputData(polyData);
}

void SelectionBoxWidgetDBSync::updateVisibility(
    const std::shared_ptr<const SelectionBoxWidgetDB>& widgetDB) {
    if (m_boxActor && widgetDB) {
        const bool visible = widgetDB->getVisible() && widgetDB->getHasValidBounds();
        m_boxActor->SetVisibility(visible ? 1 : 0);
    }
}

void SelectionBoxWidgetDBSync::onWidgetPropertyChanged(const std::string& propertyName) {
    const auto widgetDB = documentDB<SelectionBoxWidgetDB>(m_id);
    if (!widgetDB) {
        return;
    }

    if (propertyName == "ModelBoundsMin" ||
        propertyName == "ModelBoundsMax" ||
        propertyName == "HasValidBounds") {
        rebuildGeometry(widgetDB);
        updateVisibility(widgetDB);
    }
}

void SelectionBoxWidgetDBSync::onVisibilityChanged(bool) {
    updateVisibility(documentDB<SelectionBoxWidgetDB>(m_id));
}

REGISTER_VTK_DBSYNC(TypeID::SELECTION_BOX_WIDGET_DB, SelectionBoxWidgetDBSync,
                        GPlatform::Rendering::RendererRoleSets::Prepare)
