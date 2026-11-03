#include "CutPreviewDBSync.hpp"

#include "VtkDBSyncFactory.hpp"

#include <CutPlaneWidgetDB.hpp>
#include <CutPreviewDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>

#include <vtkActor.h>
#include <vtkMatrix4x4.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkShaderProperty.h>
#include <vtkTransform.h>
#include <vtkUniforms.h>

#include <cmath>

namespace {

constexpr const char* kPlaneUniform = "cutPreviewPlane";
constexpr const char* kUpperColorUniform = "cutPreviewUpperColor";
constexpr const char* kLowerColorUniform = "cutPreviewLowerColor";

vtkSmartPointer<vtkTransform> vtkTransformFor(
    const Transform::Matrix4& matrix) {
    auto vtkMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            vtkMatrix->SetElement(row, column, matrix(row, column));
        }
    }
    auto result = vtkSmartPointer<vtkTransform>::New();
    result->SetMatrix(vtkMatrix);
    return result;
}

} // namespace

void CutPreviewDBSync::initialize(
    const DBInstanceID& id,
    vtkSmartPointer<vtkRenderer> renderer,
    std::shared_ptr<RenderThread> renderThread) {
    m_id = id;
    m_vtkRenderer = std::move(renderer);
    m_renderThread = std::move(renderThread);
    if (!m_renderThread) return;

    auto self = shared_from_this();
    dispatchRenderCommand([self](auto*, auto*) {
        self->refreshPresentation();
    });
    requestRender();
}

void CutPreviewDBSync::cleanup(const DBInstanceID&) {
    if (!m_renderThread) {
        clearParts();
        return;
    }
    auto self = shared_from_this();
    dispatchRenderCommand([self](auto*, auto*) {
        self->setSourcePresentationVisible(true);
        self->clearParts();
        self->m_sourceModelId = INVALID_DB_ID;
    });
    requestRender();
}

void CutPreviewDBSync::update(
    const DBInstanceID&,
    ChangeType changeType,
    const std::string&) {
    if (changeType == ChangeType::OBJECT_DELETED || !m_renderThread) return;
    auto self = shared_from_this();
    dispatchRenderCommand([self](auto*, auto*) {
        self->refreshPresentation();
    });
    requestRender();
}

void CutPreviewDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const trans::Prop& prop) {
    update(id, changeType, prop.name());
}

void CutPreviewDBSync::setVisible(bool visible) {
    if (!m_renderThread) {
        m_visible = visible;
        return;
    }
    auto self = shared_from_this();
    dispatchRenderCommand([self, visible](auto*, auto*) {
        self->m_visible = visible;
        self->refreshPresentation();
    });
    requestRender();
}

vtkSmartPointer<vtkObject> CutPreviewDBSync::getPrimaryVTKObject() const {
    return m_parts.empty()
        ? vtkSmartPointer<vtkObject>{}
        : vtkSmartPointer<vtkObject>(m_parts.front().actor);
}

bool CutPreviewDBSync::rebuildParts(const DBInstanceID& sourceModelId) {
    clearParts();
    const auto& document = documentReadView();
    const auto source = document
        ? document->get<ModelInstanceDB>(sourceModelId) : nullptr;
    if (!source || !m_vtkRenderer) return false;

    const DBInstanceID objectId = document->getOwner(
        source->getDBInstanceID());
    const auto partIds = document->getOwnedChildren(
        objectId, ModelPartDB::kObjectRelation);
    for (const DBInstanceID partId : partIds) {
        const auto part = document->get<ModelPartDB>(partId);
        const auto geometryIds = part
            ? document->getOwnedChildren(
                  partId, ModelPartDB::kGeometryRelation)
            : std::vector<DBInstanceID>{};
        const auto geometry = geometryIds.empty()
            ? std::shared_ptr<const ModelGeometryDB>{}
            : document->get<ModelGeometryDB>(geometryIds.front());
        auto readHandle = geometry
            ? geometry->read() : ModelGeometryDB::ReadHandle{};
        if (!part || !geometry || !readHandle) continue;

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(
            const_cast<vtkPolyData*>(&readHandle.polyData()));
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->SetPickable(0);
        actor->SetUserTransform(vtkTransformFor(
            source->getTransform().getMatrix() *
            part->getLocalTransform().getMatrix()));
        actor->GetProperty()->SetAmbient(0.18);
        actor->GetProperty()->SetDiffuse(0.82);
        actor->GetProperty()->SetSpecular(0.08);
        actor->GetProperty()->SetSpecularPower(16.0);

        PartPresentation presentation;
        presentation.partId = partId;
        presentation.geometryId = geometry->getDBInstanceID();
        presentation.geometry = std::move(readHandle);
        presentation.actor = actor;
        presentation.mapper = mapper;
        installShader(presentation);
        m_vtkRenderer->AddActor(actor);
        m_parts.push_back(std::move(presentation));
    }
    return !m_parts.empty();
}

void CutPreviewDBSync::clearParts() {
    if (m_vtkRenderer) {
        for (auto& part : m_parts) {
            if (part.actor) m_vtkRenderer->RemoveActor(part.actor);
        }
    }
    m_parts.clear();
}

void CutPreviewDBSync::setSourcePresentationVisible(bool visible) {
    if (!m_sourceModelId.isValid() || !m_renderThread) return;
    if (visible && !m_sourcePresentationHidden) return;
    if (!visible && m_sourcePresentationHidden) return;

    const auto sourceSync = findDBSync(m_sourceModelId);
    if (!sourceSync) return;

    bool effectiveVisible = visible;
    if (visible) {
        const auto source = documentDB<ModelInstanceDB>(m_sourceModelId);
        effectiveVisible = source && source->getVisible();
    }
    sourceSync->setVisible(effectiveVisible);
    m_sourcePresentationHidden = !visible;
}

void CutPreviewDBSync::installShader(PartPresentation& part) {
    if (!part.actor || !part.mapper) return;
    auto* shader = part.actor->GetShaderProperty();
    if (!shader) return;

    part.mapper->SetVBOShiftScaleMethod(
        vtkPolyDataMapper::DISABLE_SHIFT_SCALE);
    float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    shader->GetVertexCustomUniforms()->SetUniformMatrix4x4(
        "MCWCMatrix", identity);

    shader->AddVertexShaderReplacement(
        "//VTK::PositionVC::Dec", true,
        "//VTK::PositionVC::Dec\n"
        "out vec3 cutPreviewWorldPositionVSOutput;\n",
        false);
    shader->AddVertexShaderReplacement(
        "//VTK::PositionVC::Impl", true,
        "//VTK::PositionVC::Impl\n"
        "  cutPreviewWorldPositionVSOutput = (MCWCMatrix * vertexMC).xyz;\n",
        false);
    shader->AddGeometryShaderReplacement(
        "//VTK::PositionVC::Dec", true,
        "//VTK::PositionVC::Dec\n"
        "in vec3 cutPreviewWorldPositionVSOutput[];\n"
        "out vec3 cutPreviewWorldPositionGSOutput;\n",
        false);
    shader->AddGeometryShaderReplacement(
        "//VTK::PositionVC::Impl", true,
        "//VTK::PositionVC::Impl\n"
        "  cutPreviewWorldPositionGSOutput = "
        "cutPreviewWorldPositionVSOutput[i];\n",
        false);
    shader->AddFragmentShaderReplacement(
        "//VTK::PositionVC::Dec", true,
        "//VTK::PositionVC::Dec\n"
        "in vec3 cutPreviewWorldPositionVSOutput;\n",
        false);
    shader->AddFragmentShaderReplacement(
        "//VTK::Color::Impl", true,
        "//VTK::Color::Impl\n"
        "  float cutPreviewDistance = dot(vec4(cutPreviewWorldPositionVSOutput, 1.0), cutPreviewPlane);\n"
        "  vec3 cutPreviewColor = cutPreviewDistance >= 0.0\n"
        "    ? cutPreviewUpperColor : cutPreviewLowerColor;\n"
        "  ambientColor = ambientIntensity * cutPreviewColor;\n"
        "  diffuseColor = diffuseIntensity * cutPreviewColor;\n",
        false);
    shader->Modified();
    part.mapper->Modified();
}

void CutPreviewDBSync::updateShader(
    PartPresentation& part,
    const CutPlaneWidgetDB& widget) {
    if (!part.actor) return;
    const Transform& planeToWorld = widget.getPlaneToWorld();
    const Vector3 center = planeToWorld.getPosition();
    Vector3 normal = planeToWorld.transformVector(Vector3(0.0f, 0.0f, 1.0f));
    const float normalLength = std::sqrt(
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normalLength <= 1.0e-6f) {
        part.actor->SetVisibility(0);
        return;
    }
    normal = normal * (1.0f / normalLength);

    auto* uniforms =
        part.actor->GetShaderProperty()->GetFragmentCustomUniforms();
    const float plane[4] = {
        normal.x, normal.y, normal.z,
        -(normal.x * center.x + normal.y * center.y + normal.z * center.z)};
    const float upperColor[3] = {0.0f, 1.0f, 1.0f};
    const float lowerColor[3] = {1.0f, 0.0f, 1.0f};
    uniforms->SetUniform4f(kPlaneUniform, plane);
    uniforms->SetUniform3f(kUpperColorUniform, upperColor);
    uniforms->SetUniform3f(kLowerColorUniform, lowerColor);
    part.actor->GetShaderProperty()->Modified();
    part.actor->Modified();
}

void CutPreviewDBSync::refreshPresentation() {
    const auto preview = documentDB<CutPreviewDB>(m_id);
    const DBInstanceID requestedSource = preview
        ? preview->getSourceModelID() : DBInstanceID{};
    if (!m_visible || !preview || !preview->getEnabled() ||
        !requestedSource.isValid()) {
        setSourcePresentationVisible(true);
        clearParts();
        m_sourceModelId = INVALID_DB_ID;
        return;
    }

    const auto widget = documentDB<CutPlaneWidgetDB>(
        preview->getPlaneWidgetID());
    if (!widget || widget->getTargetModelID() != requestedSource) {
        setSourcePresentationVisible(true);
        clearParts();
        m_sourceModelId = INVALID_DB_ID;
        return;
    }

    if (m_sourceModelId != requestedSource || m_parts.empty()) {
        setSourcePresentationVisible(true);
        m_sourceModelId = requestedSource;
        if (!rebuildParts(requestedSource)) {
            m_sourceModelId = INVALID_DB_ID;
            return;
        }
    }

    const auto source = documentDB<ModelInstanceDB>(m_sourceModelId);
    if (!source) {
        setSourcePresentationVisible(true);
        clearParts();
        m_sourceModelId = INVALID_DB_ID;
        return;
    }

    setSourcePresentationVisible(false);
    for (auto& presentation : m_parts) {
        const auto part = documentDB<ModelPartDB>(presentation.partId);
        if (!part || !presentation.actor) continue;
        presentation.actor->SetUserTransform(vtkTransformFor(
            source->getTransform().getMatrix() *
            part->getLocalTransform().getMatrix()));
        presentation.actor->SetVisibility(1);
        updateShader(presentation, *widget);
    }
}

REGISTER_VTK_DBSYNC(TypeID::CUT_PREVIEW_DB, CutPreviewDBSync,
                    GPlatform::Rendering::RendererRoleSets::Prepare)
