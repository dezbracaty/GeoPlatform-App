#include "VtkGlbDBSync.hpp"

#include "Foundation/Log.h"

#include <GlbDB.hpp>

#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkAssembly.h>
#include <vtkGLTFImporter.h>
#include <vtkMatrix4x4.h>
#include <vtkMemoryResourceStream.h>
#include <vtkNew.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>

#include <algorithm>

void VtkGlbDBSync::initialize(
    const DBInstanceID& id,
    vtkSmartPointer<vtkRenderer> renderer,
    std::shared_ptr<RenderThread> renderThread) {
    m_id = id;
    m_vtkRenderer = std::move(renderer);
    m_renderThread = std::move(renderThread);
    if (!m_renderThread) return;
    LOG_INFO(
        "VtkGlbDBSync: initialize model={} renderer={}",
        m_id.getValue(), m_vtkRenderer ? "ready" : "missing");
    auto self = shared_from_this();
    dispatchRenderCommand(
        [self](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            self->reloadFromDB();
        });
}

void VtkGlbDBSync::cleanup(const DBInstanceID&) {
    if (!m_renderThread) return;
    auto self = shared_from_this();
    dispatchRenderCommand(
        [self](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            self->releaseNativeScene();
        });
}

void VtkGlbDBSync::update(
    const DBInstanceID&,
    ChangeType changeType,
    const std::string& propertyName) {
    if (!m_renderThread || changeType == ChangeType::OBJECT_DELETED) return;
    auto self = shared_from_this();
    dispatchRenderCommand(
        [self, changeType, propertyName](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            if (changeType == ChangeType::OBJECT_CREATED ||
                propertyName == "AssetRevision") {
                self->reloadFromDB();
                return;
            }
            if (propertyName == "Transform") {
                self->applyTransform();
            } else if (propertyName == "Visible") {
                self->synchronizeVisibilityFromDB();
            } else if (propertyName == "Pickable") {
                self->applyModelState();
            }
        });
}

void VtkGlbDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const trans::Prop& prop) {
    update(id, changeType, prop.name());
}

void VtkGlbDBSync::setVisible(bool visible) {
    if (!m_renderThread) return;
    auto self = shared_from_this();
    dispatchRenderCommand(
        [self, visible](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            if (visible) self->reloadFromDB();
            else self->releaseNativeScene();
        });
}

void VtkGlbDBSync::applyRenderingContext() {
    if (!m_renderThread) return;
    auto self = shared_from_this();
    dispatchRenderCommand(
        [self](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            self->applyPresentationContext();
        });
}

vtkSmartPointer<vtkObject> VtkGlbDBSync::getPrimaryVTKObject() const {
    return m_assembly;
}

bool VtkGlbDBSync::ownsProp(vtkSmartPointer<vtkProp> prop) const {
    if (!prop) return false;
    if (prop == m_assembly.GetPointer()) return true;
    return std::any_of(
        m_actors.begin(), m_actors.end(),
        [prop](const vtkSmartPointer<vtkActor>& actor) {
            return actor.GetPointer() == prop.GetPointer();
        });
}

void VtkGlbDBSync::reloadFromDB() {
    const auto glb = documentDB<GlbDB>(m_id);
    if (!glb || !glb->getVisible()) {
        // Visibility is the scene-membership contract. Invisible GLBs retain
        // their DB data but do not allocate VTK importer/native scene resources.
        releaseNativeScene();
        return;
    }
    const auto snapshot = glb ? glb->acquireAssetSnapshot() : nullptr;
    if (!snapshot || !*snapshot) {
        LOG_DEBUG(
            "VtkGlbDBSync: DB asset snapshot unavailable model={} db={}",
            m_id.getValue(), glb ? "ready" : "missing");
        releaseNativeScene();
        return;
    }
    if (m_importer && snapshot->revision == m_loadedRevision) {
        applyTransform();
        applyModelState();
        return;
    }
    releaseNativeScene();

    m_sourceSnapshot = snapshot;
    const auto& bytes = snapshot->asset->bytes();
    LOG_INFO(
        "VtkGlbDBSync: parsing DB GLB model={} revision={} bytes={}",
        m_id.getValue(), snapshot->revision, bytes.size());
    auto stream = vtkSmartPointer<vtkMemoryResourceStream>::New();
    stream->SetBuffer(bytes.data(), bytes.size(), false);

    auto importer = vtkSmartPointer<vtkGLTFImporter>::New();
    importer->SetStream(stream);
    importer->SetStreamIsBinary(true);
    importer->SetCamera(-1);
    importer->SetImportArmature(false);
    importer->Update();

    auto* importedActors = importer->GetImportedActors();
    if (!importedActors || importedActors->GetNumberOfItems() == 0) {
        LOG_ERROR(
            "VtkGlbDBSync: vtkGLTFImporter produced no actors model={} revision={}",
            m_id.getValue(), snapshot->revision);
        m_sourceSnapshot.reset();
        return;
    }

    auto assembly = vtkSmartPointer<vtkAssembly>::New();
    importedActors->InitTraversal();
    while (auto* actor = importedActors->GetNextActor()) {
        m_actors.emplace_back(actor);
        m_authoredOpacities.push_back(
            actor->GetProperty() ? actor->GetProperty()->GetOpacity() : 1.0);
        assembly->AddPart(actor);
    }

    double sourceBounds[6];
    assembly->GetBounds(sourceBounds);
    m_sourceCenter = {
        (sourceBounds[0] + sourceBounds[1]) * 0.5,
        (sourceBounds[2] + sourceBounds[3]) * 0.5,
        (sourceBounds[4] + sourceBounds[5]) * 0.5};

    m_importer = std::move(importer);
    m_stream = std::move(stream);
    m_assembly = std::move(assembly);
    m_loadedRevision = snapshot->revision;
    applyTransform();
    applyModelState();
    applyPresentationContext();
    if (m_vtkRenderer) m_vtkRenderer->AddViewProp(m_assembly);
    if (m_renderThread) markSceneChanged();

    LOG_INFO(
        "VtkGlbDBSync: loaded GLB from DB model={} revision={} actors={} bytes={}",
        m_id.getValue(), m_loadedRevision, m_actors.size(), bytes.size());
}

void VtkGlbDBSync::synchronizeVisibilityFromDB() {
    const auto glb = documentDB<GlbDB>(m_id);
    if (glb && glb->getVisible()) {
        reloadFromDB();
    } else {
        releaseNativeScene();
    }
}

void VtkGlbDBSync::releaseNativeScene() {
    if (m_vtkRenderer && m_assembly) {
        m_vtkRenderer->RemoveViewProp(m_assembly);
    }
    m_actors.clear();
    m_authoredOpacities.clear();
    m_assembly = nullptr;
    m_importer = nullptr;
    m_stream = nullptr;
    m_sourceCenter = {};
    m_sourceSnapshot.reset();
    m_loadedRevision = 0;
    if (m_renderThread) markSceneChanged();
}

void VtkGlbDBSync::applyModelState() {
    const auto mesh = documentDB<GlbDB>(m_id);
    if (!mesh || !m_assembly) return;
    m_assembly->SetVisibility(mesh->getVisible() ? 1 : 0);
    m_assembly->SetPickable(mesh->isPickable() ? 1 : 0);
}

void VtkGlbDBSync::applyPresentationContext() {
    const float opacityOverride =
        getRenderingContext().getMaterialOverride().opacity;
    const bool hasOpacityOverride =
        opacityOverride >= 0.0f && opacityOverride <= 1.0f;
    const std::size_t count = std::min(
        m_actors.size(), m_authoredOpacities.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto* property = m_actors[index]->GetProperty();
        if (!property) continue;
        property->SetOpacity(
            hasOpacityOverride ? opacityOverride : m_authoredOpacities[index]);
    }
}

void VtkGlbDBSync::applyTransform() {
    const auto mesh = documentDB<GlbDB>(m_id);
    if (!mesh || !m_assembly) return;

    vtkNew<vtkMatrix4x4> dbMatrix;
    const auto& sourceMatrix = mesh->getTransformMatrix();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            dbMatrix->SetElement(row, column, sourceMatrix(row, column));
        }
    }

    constexpr double kMetersToMillimeters = 1000.0;
    vtkNew<vtkMatrix4x4> sourceToModel;
    sourceToModel->Zero();
    sourceToModel->SetElement(0, 0, kMetersToMillimeters);
    sourceToModel->SetElement(
        0, 3, -m_sourceCenter[0] * kMetersToMillimeters);
    sourceToModel->SetElement(1, 2, -kMetersToMillimeters);
    sourceToModel->SetElement(
        1, 3, m_sourceCenter[2] * kMetersToMillimeters);
    sourceToModel->SetElement(2, 1, kMetersToMillimeters);
    sourceToModel->SetElement(
        2, 3, -m_sourceCenter[1] * kMetersToMillimeters);
    sourceToModel->SetElement(3, 3, 1.0);

    vtkNew<vtkMatrix4x4> composed;
    vtkMatrix4x4::Multiply4x4(
        dbMatrix, sourceToModel, composed);
    m_assembly->SetUserMatrix(composed);
}
