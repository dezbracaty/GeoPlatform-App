#include "SnapPreviewDBSync.hpp"
#include "VtkDBSyncFactory.hpp"
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkShaderProperty.h>

void SnapPreviewDBSync::createActor() {
    m_actor = vtkSmartPointer<vtkActor>::New();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->ScalarVisibilityOff();
    mapper->SetInputData(vtkSmartPointer<vtkPolyData>::New());
    m_actor->SetMapper(mapper);
    // Overlay depth is actor-local and never changes global mapper settings.
    m_actor->GetShaderProperty()->AddFragmentShaderReplacement(
        "//VTK::Depth::Impl", true, "gl_FragDepth = 0.0;\n", false);
    updateGeometry({});
}

void SnapPreviewDBSync::updateGeometry(const std::string&) {
    const auto db = documentDB<SnapPreviewDB>(m_id);
    if (!db || !m_actor) return;
    const auto data = db->preview();
    if (data == m_preview) return;
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    if (data) {
        points->Allocate(vtkIdType(data->lines.size() * 2));
        for (const auto& line : data->lines) {
            vtkIdType ids[2] = {
                points->InsertNextPoint(line.start.x, line.start.y, line.start.z),
                points->InsertNextPoint(line.end.x, line.end.y, line.end.z)};
            lines->InsertNextCell(2, ids);
        }
    }
    auto geometry = vtkSmartPointer<vtkPolyData>::New();
    geometry->SetPoints(points);
    geometry->SetLines(lines);
    vtkPolyDataMapper::SafeDownCast(m_actor->GetMapper())->SetInputData(geometry);
    m_preview = data;
}

void SnapPreviewDBSync::synchronizeView() {
    const auto db = documentDB<SnapPreviewDB>(m_id);
    if (!db || !m_actor || !m_renderThread) return;
    updateGeometry({});
    m_actor->SetVisibility(db->isVisible() && m_preview && !m_preview->lines.empty() &&
                          m_preview->ownerViewId.isValid() && m_preview->ownerViewId == windowId());
    // Preview dims model materials through the rendering context. Interaction
    // hints retain their own style, including full opacity and line width.
    applyMaterialFromDB();
    m_actor->PickableOff();
    m_actor->DragableOff();
}

REGISTER_VTK_DBSYNC(TypeID::SNAP_PREVIEW_DB, SnapPreviewDBSync,
                   GPlatform::Rendering::RendererRoleSets::PrepareAndPreview)
