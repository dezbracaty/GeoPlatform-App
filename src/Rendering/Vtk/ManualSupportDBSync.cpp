#include "ManualSupportDBSync.hpp"

#include "ManualSupportDB.hpp"
#include "VtkDBSyncFactory.hpp"
#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>

namespace {

vtkSmartPointer<vtkPolyData> buildPolyData(
    const ManualSupportDB::GeometrySnapshot& geometry) {
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    if (!geometry || !geometry->hasValidTopology()) return polyData;

    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(
        static_cast<vtkIdType>(geometry->positions.size()));
    for (std::size_t index = 0; index < geometry->positions.size(); ++index) {
        const Vector3& position = geometry->positions[index];
        points->SetPoint(
            static_cast<vtkIdType>(index), position.x, position.y, position.z);
    }
    auto polys = vtkSmartPointer<vtkCellArray>::New();
    for (std::size_t offset = 0; offset < geometry->indices.size(); offset += 3) {
        const vtkIdType triangle[3]{
            static_cast<vtkIdType>(geometry->indices[offset]),
            static_cast<vtkIdType>(geometry->indices[offset + 1]),
            static_cast<vtkIdType>(geometry->indices[offset + 2])};
        polys->InsertNextCell(3, triangle);
    }
    polyData->SetPoints(points);
    polyData->SetPolys(polys);

    if (geometry->normals.size() == geometry->positions.size()) {
        auto normals = vtkSmartPointer<vtkFloatArray>::New();
        normals->SetName("Normals");
        normals->SetNumberOfComponents(3);
        normals->SetNumberOfTuples(
            static_cast<vtkIdType>(geometry->normals.size()));
        for (std::size_t index = 0; index < geometry->normals.size(); ++index) {
            const Vector3& normal = geometry->normals[index];
            const float tuple[3]{normal.x, normal.y, normal.z};
            normals->SetTypedTuple(static_cast<vtkIdType>(index), tuple);
        }
        polyData->GetPointData()->SetNormals(normals);
    }
    return polyData;
}

} // namespace

void ManualSupportDBSync::createActor() {
    const auto support = documentDB<ManualSupportDB>(m_id);
    if (!support) return;
    m_actor = vtkSmartPointer<vtkActor>::New();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(buildPolyData(support->getGeneratedGeometry()));
    m_actor->SetMapper(mapper);
    applyMaterialFromDB();
    applyTransformFromDB();
}

void ManualSupportDBSync::updateGeometry(const std::string&) {
    const auto support = documentDB<ManualSupportDB>(m_id);
    if (!support || !m_actor) return;
    const auto geometry = support->getGeneratedGeometry();
    if (auto* mapper = vtkPolyDataMapper::SafeDownCast(m_actor->GetMapper())) {
        mapper->SetInputData(buildPolyData(geometry));
        mapper->Modified();
    }
}

REGISTER_VTK_DBSYNC(TypeID::MANUAL_SUPPORT_DB, ManualSupportDBSync,
                    GPlatform::Rendering::RendererRoleSets::Prepare)
