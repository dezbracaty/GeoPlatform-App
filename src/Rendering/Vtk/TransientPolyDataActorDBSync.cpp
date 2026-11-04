#include "TransientPolyDataActorDBSync.hpp"

#include "TransientPolyDataActorDB.hpp"
#include "VtkDBSyncFactory.hpp"
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkShaderProperty.h>
#include <vtkPolyDataMapper.h>

namespace {

void configureScalars(vtkPolyDataMapper* mapper,
                      vtkPolyData* geometry,
                      bool useCellColors) {
    if (!mapper) return;
    if (useCellColors && geometry && geometry->GetCellData() &&
        geometry->GetCellData()->GetScalars()) {
        mapper->SetScalarModeToUseCellData();
        mapper->SetColorModeToDirectScalars();
        mapper->ScalarVisibilityOn();
    } else {
        mapper->ScalarVisibilityOff();
    }
}

void configureDepth(vtkActor* actor) {
    if (!actor) return;
    bool translucent = actor->GetProperty()->GetOpacity() < 1.;
    const auto mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    const auto geometry = mapper ? mapper->GetInput() : nullptr;
    const auto colors = mapper && mapper->GetScalarVisibility() &&
        mapper->GetScalarMode() == VTK_SCALAR_MODE_USE_CELL_DATA &&
        mapper->GetColorMode() == VTK_COLOR_MODE_DIRECT_SCALARS && geometry
        ? geometry->GetCellData()->GetScalars() : nullptr;
    if (colors && colors->GetDataType() == VTK_UNSIGNED_CHAR && colors->GetNumberOfComponents() == 4)
        for (vtkIdType i = 0; i < colors->GetNumberOfTuples() && !translucent; ++i)
            translucent = colors->GetComponent(i, 3) < 255;
    auto* shader = actor->GetShaderProperty();
    shader->ClearVertexShaderReplacement("//VTK::PositionVC::Impl", true);
    if (translucent) {
        // Four D24 depth increments resolve numerical coincidence. Keep this
        // actor-local; a larger bias visibly changes noncoincident occlusion.
        shader->AddVertexShaderReplacement("//VTK::PositionVC::Impl", true,
            "//VTK::PositionVC::Impl\n"
            "gl_Position.z -= (8.0 / 16777216.0) * gl_Position.w;\n", false);
    }
}

} // namespace

void TransientPolyDataActorDBSync::createActor() {
    auto db = documentDB<TransientPolyDataActorDB>(m_id);
    if (!db) return;
    m_actor = vtkSmartPointer<vtkActor>::New();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(db->getGeometry());
    configureScalars(mapper, db->getGeometry(), db->usesCellColors());
    m_actor->SetMapper(mapper);
    applyMaterialFromDB();
    applyTransformFromDB();
    configureDepth(m_actor);
}

void TransientPolyDataActorDBSync::updateGeometry(const std::string&) {
    auto db = documentDB<TransientPolyDataActorDB>(m_id);
    if (!db || !m_actor) return;
    auto geometry = db->getGeometry();
    const bool useCellColors = db->usesCellColors();
    if (auto* mapper = vtkPolyDataMapper::SafeDownCast(m_actor->GetMapper())) {
        mapper->SetInputData(geometry);
        configureScalars(mapper, geometry, useCellColors);
        mapper->Modified();
    }
    configureDepth(m_actor);
}

void TransientPolyDataActorDBSync::onMaterialChanged(const std::string&) {
    if (!m_renderThread) return;
    // The base class has already queued material/context application. Read
    // the resulting VTK opacity after that command, on the render thread.
    const auto weak = weak_from_this();
    dispatchRenderCommand([weak](auto*, auto*) {
        const auto sync = std::dynamic_pointer_cast<TransientPolyDataActorDBSync>(weak.lock());
        if (sync && sync->m_actor) configureDepth(sync->m_actor);
    });
}

void TransientPolyDataActorDBSync::onRenderingContextApplied(const RenderingContext&) {
    configureDepth(m_actor);
}

REGISTER_VTK_DBSYNC(TypeID::TRANSIENT_POLY_DATA_ACTOR_DB,
                        TransientPolyDataActorDBSync,
                        GPlatform::Rendering::RendererRoleSets::Prepare)
