#include "ModelInstanceDBSync.hpp"

#include "VtkBackendRegistration.hpp"
#include "VtkDBSyncFactory.hpp"
#include "Foundation/Log.h"
#include <MaterialDB.hpp>
#include <ModelRenderAppearanceResolver.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <ModelSurfaceColorDB.hpp>
#include <SlicingConfigDB.hpp>

#include <vtkActorCollection.h>
#include <vtkAssembly.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkGLTFImporter.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkMemoryResourceStream.h>
#include <vtkNew.h>
#include <vtkActor.h>
#include <vtkMatrix4x4.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>
#include <vtkUnsignedIntArray.h>

#include <algorithm>
#include <utility>

namespace {
constexpr const char* kSurfaceLabelArray = "SurfaceLabel";
constexpr const char* kSourceFacetArray = "SourceFacetId";

std::vector<GeomTriangle> partTriangles(const ModelGeometryDB& data) {
    std::vector<GeomTriangle> triangles;
    const auto geometry = data.read();
    auto* poly = geometry
        ? const_cast<vtkPolyData*>(&geometry.polyData())
        : nullptr;
    if (!poly) return triangles;
    triangles.reserve(static_cast<std::size_t>(poly->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < poly->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = poly->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) continue;
        Vector3 vertices[3];
        for (int corner = 0; corner < 3; ++corner) {
            double raw[3];
            poly->GetPoint(cell->GetPointId(corner), raw);
            vertices[corner] = {
                static_cast<float>(raw[0]), static_cast<float>(raw[1]),
                static_cast<float>(raw[2])};
        }
        triangles.emplace_back(vertices[0], vertices[1], vertices[2]);
    }
    return triangles;
}

} // namespace

std::vector<std::shared_ptr<const ModelPartDB>>
ModelInstanceDBSync::partsForInstance(
    const ModelInstanceDB& instance) const {
    std::vector<std::shared_ptr<const ModelPartDB>> result;
    const auto& document = documentReadView();
    if (!document) return result;
    const DBInstanceID objectId = document->getOwner(
        instance.getDBInstanceID());
    if (!objectId.isValid()) return result;
    for (const auto partId : document->getOwnedChildren(
             objectId, ModelPartDB::kObjectRelation)) {
        if (const auto part = document->get<ModelPartDB>(partId)) {
            result.push_back(part);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs,
                                               const auto& rhs) {
        if (lhs->getOrderIndex() != rhs->getOrderIndex()) {
            return lhs->getOrderIndex() < rhs->getOrderIndex();
        }
        return lhs->getDBInstanceID().getValue() <
            rhs->getDBInstanceID().getValue();
    });
    return result;
}

std::shared_ptr<const ModelObjectDB>
ModelInstanceDBSync::objectForInstance(
    const ModelInstanceDB& instance) const {
    const auto& document = documentReadView();
    return document
        ? document->get<ModelObjectDB>(
              document->getOwner(instance.getDBInstanceID()))
        : nullptr;
}

std::shared_ptr<const ModelGeometryDB>
ModelInstanceDBSync::geometryForPart(DBInstanceID partId) const {
    const auto& document = documentReadView();
    if (!document) return {};
    const auto part = document->get<ModelPartDB>(partId);
    if (!part) return {};
    const auto ids = document->getOwnedChildren(
        partId, ModelPartDB::kGeometryRelation);
    return ids.empty() ? std::shared_ptr<const ModelGeometryDB>{}
                       : document->get<ModelGeometryDB>(ids.front());
}

std::shared_ptr<const MaterialDB>
ModelInstanceDBSync::materialForPart(DBInstanceID partId) const {
    const auto& document = documentReadView();
    if (!document) return {};
    const auto part = document->get<ModelPartDB>(partId);
    if (!part) return {};
    const auto ids = document->getOwnedChildren(
        partId, ModelPartDB::kMaterialRelation);
    return ids.empty() ? std::shared_ptr<const MaterialDB>{}
                       : document->get<MaterialDB>(ids.front());
}

namespace {

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

void applyMaterial(
    vtkActor* actor, const MaterialDB* material,
    const Vector3& displayColor) {
    if (!actor || !material) return;
    vtkProperty* property = actor->GetProperty();
    property->SetColor(displayColor.x, displayColor.y, displayColor.z);
    property->SetOpacity(material->getOpacity());
    property->SetAmbient(material->getAmbient());
    property->SetDiffuse(material->getDiffuse());
    property->SetSpecular(material->getSpecular());
    property->SetSpecularPower(material->getSpecularPower());
    property->SetEdgeVisibility(material->getEdgeVisibility());
    const auto edge = material->getEdgeColor();
    property->SetEdgeColor(edge.x, edge.y, edge.z);
    property->SetLineWidth(material->getLineWidth());
    switch (material->getRepresentation()) {
        case 0: property->SetRepresentationToPoints(); break;
        case 1: property->SetRepresentationToWireframe(); break;
        default: property->SetRepresentationToSurface(); break;
    }
}

double presentationOpacity(
    const MaterialDB* material,
    const ModelInstanceDB& instance,
    const RenderingContext& context) {
    const float authoredOpacity =
        (material ? material->getOpacity() : 1.0f) *
        instance.getOpacity();
    const float contextOpacity =
        context.getMaterialOverride().opacity;
    return std::clamp(
        static_cast<double>(
            contextOpacity >= 0.0f && contextOpacity <= 1.0f
                ? contextOpacity
                : authoredOpacity),
        0.0, 1.0);
}

}

void ModelInstanceDBSync::initialize(
    const DBInstanceID& id, vtkSmartPointer<vtkRenderer> renderer,
    std::shared_ptr<RenderThread> renderThread) {
    m_id = id;
    m_vtkRenderer = renderer;
    m_renderThread = std::move(renderThread);
    if (!m_renderThread) return;
    auto self = shared_from_this();
    dispatchRenderCommand([self](auto*, auto*) {
        self->rebuildParts();
    });
}

void ModelInstanceDBSync::cleanup(const DBInstanceID&) {
    m_nextProjectionToken.fetch_add(1, std::memory_order_acq_rel);
    if (!m_renderThread) {
        m_parts.clear();
        return;
    }
    auto self = shared_from_this();
    dispatchRenderCommand([self](auto*, auto*) {
        if (self->m_vtkRenderer) {
            for (auto& part : self->m_parts) {
                self->m_vtkRenderer->RemoveActor(part.actor);
            }
        }
        self->m_parts.clear();
        self->releaseNativeGlb();
    });
}

void ModelInstanceDBSync::update(
    const DBInstanceID& changedId, ChangeType changeType,
    const std::string& propertyName) {
    if (!m_renderThread) return;
    auto self = shared_from_this();
    if (changedId != m_id) return;
    if (changeType == ChangeType::OBJECT_DELETED) return;
    dispatchRenderCommand([self, propertyName](auto*, auto*) {
        if (propertyName.empty() || propertyName == "PartSet" ||
            propertyName == ModelInstanceDB::kObjectRelation) {
            self->rebuildParts();
        } else if (propertyName == "Transform") {
            self->updateInstanceTransform();
        } else if (propertyName == "Visible") {
            self->updateInstanceStyle(true);
        } else if (propertyName == "Opacity") {
            self->updateInstanceStyle(false);
        } else if (propertyName == "SlicingConfigDBId" ||
                   propertyName ==
                       ModelInstanceDB::kSlicingConfigRelation) {
            self->updateInstanceStyle(false);
            std::vector<DBInstanceID> partIds;
            partIds.reserve(self->m_parts.size());
            for (const auto& part : self->m_parts) {
                partIds.push_back(part.partId);
            }
            for (const auto partId : partIds) {
                self->scheduleSurfaceColorProjection(partId);
            }
        }
    });
}

void ModelInstanceDBSync::update(
    const DBInstanceID& id, ChangeType changeType, const trans::Prop& prop) {
    update(id, changeType, prop.name());
}

void ModelInstanceDBSync::rebuildParts() {
    m_nextProjectionToken.fetch_add(1, std::memory_order_acq_rel);
    if (m_vtkRenderer) {
        for (auto& part : m_parts) m_vtkRenderer->RemoveActor(part.actor);
    }
    m_parts.clear();
    releaseNativeGlb();
    const auto instance = documentDB<ModelInstanceDB>(m_id);
    if (!instance || !m_vtkRenderer) return;
    const auto modelParts = partsForInstance(*instance);
    const auto object = objectForInstance(*instance);
    if (object) {
        if (const auto sourceAppearance =
                GPlatform::Rendering::resolveModelSourceAppearance(
                    *object, modelParts)) {
            if (buildNativeGlb(*instance, sourceAppearance)) return;
        }
    }
    const auto slicingConfig = activeSlicingConfig();
    for (const auto& part : modelParts) {
        if (!part) continue;
        const auto mesh = geometryForPart(part->getDBInstanceID());
        const auto material = materialForPart(part->getDBInstanceID());
        if (!mesh || !mesh->isValid() || !material) continue;
        auto geometry = mesh->read();
        if (!geometry) continue;
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(
            const_cast<vtkPolyData*>(&geometry.polyData()));
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->SetPickable(instance->isPickable() ? 1 : 0);
        actor->SetUserTransform(vtkTransformFor(
            instance->getTransform().getMatrix() *
            part->getLocalTransform().getMatrix()));
        const Vector3 displayColor =
            GPlatform::Rendering::resolveModelDisplayColor(
            *part, material.get(), slicingConfig.get());
        applyMaterial(actor, material.get(), displayColor);
        m_vtkRenderer->AddActor(actor);
        PartPresentation presentation;
        presentation.partId = part->getDBInstanceID();
        presentation.geometryId = mesh->getDBInstanceID();
        presentation.materialId = material->getDBInstanceID();
        if (const auto colors = findSurfaceColors(
                part->getDBInstanceID())) {
            presentation.surfaceColorId = colors->getDBInstanceID();
        }
        presentation.geometry = std::move(geometry);
        presentation.actor = actor;
        presentation.mapper = mapper;
        m_parts.push_back(std::move(presentation));
    }
    updateInstanceStyle();
    std::vector<DBInstanceID> partIds;
    partIds.reserve(m_parts.size());
    for (const auto& part : m_parts) partIds.push_back(part.partId);
    for (const auto partId : partIds) scheduleSurfaceColorProjection(partId);
}

bool ModelInstanceDBSync::buildNativeGlb(
    const ModelInstanceDB& instance,
    std::shared_ptr<const GlbAssetSnapshot> snapshot) {
    if (!snapshot || !*snapshot || !m_vtkRenderer) return false;
    const auto& bytes = snapshot->asset->bytes();
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
            "ModelInstanceDBSync: native GLB produced no actors model={}",
            m_id.getValue());
        return false;
    }

    auto assembly = vtkSmartPointer<vtkAssembly>::New();
    importedActors->InitTraversal();
    while (auto* actor = importedActors->GetNextActor()) {
        m_nativeGlbActors.emplace_back(actor);
        m_nativeGlbAuthoredOpacities.push_back(
            actor->GetProperty() ? actor->GetProperty()->GetOpacity() : 1.0);
        assembly->AddPart(actor);
    }
    double bounds[6];
    assembly->GetBounds(bounds);
    m_nativeGlbSourceCenter = {
        (bounds[0] + bounds[1]) * 0.5,
        (bounds[2] + bounds[3]) * 0.5,
        (bounds[4] + bounds[5]) * 0.5};
    const auto modelParts = partsForInstance(instance);
    m_nativeGlbPartId = modelParts.empty()
        ? DBInstanceID{} : modelParts.front()->getDBInstanceID();
    m_nativeGlbImporter = std::move(importer);
    m_nativeGlbStream = std::move(stream);
    m_nativeGlbAssembly = std::move(assembly);
    m_nativeGlbSnapshot = std::move(snapshot);
    updateNativeGlbTransform(instance);
    updateNativeGlbStyle(instance);
    m_vtkRenderer->AddViewProp(m_nativeGlbAssembly);
    if (m_renderThread) markSceneChanged();
    return true;
}

void ModelInstanceDBSync::releaseNativeGlb() {
    if (m_vtkRenderer && m_nativeGlbAssembly) {
        m_vtkRenderer->RemoveViewProp(m_nativeGlbAssembly);
    }
    m_nativeGlbActors.clear();
    m_nativeGlbAuthoredOpacities.clear();
    m_nativeGlbAssembly = nullptr;
    m_nativeGlbImporter = nullptr;
    m_nativeGlbStream = nullptr;
    m_nativeGlbSourceCenter = {};
    m_nativeGlbSnapshot.reset();
    m_nativeGlbPartId = DBInstanceID{};
}

void ModelInstanceDBSync::updateNativeGlbTransform(
    const ModelInstanceDB& instance) {
    if (!m_nativeGlbAssembly) return;
    vtkNew<vtkMatrix4x4> instanceMatrix;
    const auto& sourceMatrix = instance.getTransformMatrix();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            instanceMatrix->SetElement(
                row, column, sourceMatrix(row, column));
        }
    }
    constexpr double kMetersToMillimeters = 1000.0;
    vtkNew<vtkMatrix4x4> sourceToModel;
    sourceToModel->Zero();
    sourceToModel->SetElement(0, 0, kMetersToMillimeters);
    sourceToModel->SetElement(
        0, 3, -m_nativeGlbSourceCenter[0] * kMetersToMillimeters);
    sourceToModel->SetElement(1, 2, -kMetersToMillimeters);
    sourceToModel->SetElement(
        1, 3, m_nativeGlbSourceCenter[2] * kMetersToMillimeters);
    sourceToModel->SetElement(2, 1, kMetersToMillimeters);
    sourceToModel->SetElement(
        2, 3, -m_nativeGlbSourceCenter[1] * kMetersToMillimeters);
    sourceToModel->SetElement(3, 3, 1.0);
    vtkNew<vtkMatrix4x4> composed;
    vtkMatrix4x4::Multiply4x4(instanceMatrix, sourceToModel, composed);
    m_nativeGlbAssembly->SetUserMatrix(composed);
}

void ModelInstanceDBSync::updateNativeGlbStyle(
    const ModelInstanceDB& instance,
    bool updateVisibility) {
    if (!m_nativeGlbAssembly) return;
    if (updateVisibility) {
        m_nativeGlbAssembly->SetVisibility(instance.getVisible() ? 1 : 0);
    }
    m_nativeGlbAssembly->SetPickable(instance.isPickable() ? 1 : 0);
    const float opacityOverride =
        getRenderingContext().getMaterialOverride().opacity;
    const bool hasOpacityOverride =
        opacityOverride >= 0.0f && opacityOverride <= 1.0f;
    const std::size_t count = std::min(
        m_nativeGlbActors.size(), m_nativeGlbAuthoredOpacities.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (auto* property = m_nativeGlbActors[index]->GetProperty()) {
            property->SetOpacity(hasOpacityOverride
                ? opacityOverride
                : m_nativeGlbAuthoredOpacities[index] * instance.getOpacity());
        }
    }
}

void ModelInstanceDBSync::updateInstanceTransform() {
    const auto instance = documentDB<ModelInstanceDB>(m_id);
    if (!instance) return;
    if (m_nativeGlbAssembly) {
        updateNativeGlbTransform(*instance);
        return;
    }
    for (auto& presentation : m_parts) {
        const auto part = documentDB<ModelPartDB>(presentation.partId);
        if (!part) continue;
        presentation.actor->SetUserTransform(vtkTransformFor(
            instance->getTransform().getMatrix() *
            part->getLocalTransform().getMatrix()));
    }
}

void ModelInstanceDBSync::updatePartTransform(DBInstanceID partId) {
    const auto instance = documentDB<ModelInstanceDB>(m_id);
    const auto part = documentDB<ModelPartDB>(partId);
    const auto found = std::find_if(
        m_parts.begin(), m_parts.end(),
        [partId](const auto& value) { return value.partId == partId; });
    if (!instance || !part || found == m_parts.end()) return;
    found->actor->SetUserTransform(vtkTransformFor(
        instance->getTransform().getMatrix() *
        part->getLocalTransform().getMatrix()));
}

void ModelInstanceDBSync::updatePartGeometry(DBInstanceID partId) {
    const auto part = documentDB<ModelPartDB>(partId);
    const auto mesh = geometryForPart(partId);
    if (!part || !mesh || !mesh->isValid()) return;
    const auto found = std::find_if(
        m_parts.begin(), m_parts.end(),
        [partId](const auto& value) { return value.partId == partId; });
    if (found == m_parts.end()) return;
    auto geometry = mesh->read();
    if (!geometry) return;
    clearSurfaceColorPresentation(partId);
    found->mapper->SetInputData(
        const_cast<vtkPolyData*>(&geometry.polyData()));
    found->mapper->Modified();
    found->geometry = std::move(geometry);
    found->geometryId = mesh->getDBInstanceID();
}

void ModelInstanceDBSync::updatePartMaterial(DBInstanceID partId) {
    const auto part = documentDB<ModelPartDB>(partId);
    const auto found = std::find_if(
        m_parts.begin(), m_parts.end(),
        [partId](const auto& value) { return value.partId == partId; });
    const auto material = materialForPart(partId);
    if (part && found != m_parts.end()) {
        const Vector3 displayColor =
            GPlatform::Rendering::resolveModelDisplayColor(
            *part, material.get(), activeSlicingConfig().get());
        applyMaterial(found->actor, material.get(), displayColor);
        if (const auto instance = documentDB<ModelInstanceDB>(m_id)) {
            found->actor->GetProperty()->SetOpacity(
                presentationOpacity(
                    material.get(), *instance, getRenderingContext()));
        }
        found->materialId = material
            ? material->getDBInstanceID() : DBInstanceID{};
    }
}

void ModelInstanceDBSync::updateInstanceStyle(bool updateVisibility) {
    const auto instance = documentDB<ModelInstanceDB>(m_id);
    if (!instance) return;
    if (m_nativeGlbAssembly) {
        updateNativeGlbStyle(*instance, updateVisibility);
        return;
    }
    const auto palette = activeFilamentPalette();
    const auto slicingConfig = activeSlicingConfig();
    for (auto& part : m_parts) {
        const auto partDb = documentDB<ModelPartDB>(part.partId);
        const auto material = materialForPart(part.partId);
        if (partDb) {
            const Vector3 displayColor =
                GPlatform::Rendering::resolveModelDisplayColor(
                *partDb, material.get(), slicingConfig.get());
            applyMaterial(part.actor, material.get(), displayColor);
        }
        if (part.surfaceColorProjection) {
            applySurfaceColorPalette(part.partId, palette, true);
        }
        if (updateVisibility) {
            part.actor->SetVisibility(instance->getVisible() ? 1 : 0);
        }
        part.actor->SetPickable(instance->isPickable() ? 1 : 0);
        part.actor->GetProperty()->SetOpacity(
            presentationOpacity(
                material.get(), *instance, getRenderingContext()));
    }
}

void ModelInstanceDBSync::setVisible(bool visible) {
    if (!m_renderThread) return;
    auto self = shared_from_this();
    dispatchRenderCommand([self, visible](auto*, auto*) {
        if (self->m_nativeGlbAssembly) {
            self->m_nativeGlbAssembly->SetVisibility(visible ? 1 : 0);
            return;
        }
        for (auto& part : self->m_parts) {
            part.actor->SetVisibility(visible ? 1 : 0);
        }
    });
}

vtkSmartPointer<vtkObject>
ModelInstanceDBSync::getPrimaryVTKObject() const {
    if (m_nativeGlbAssembly) return m_nativeGlbAssembly;
    return m_parts.empty()
        ? vtkSmartPointer<vtkObject>{}
        : vtkSmartPointer<vtkObject>(m_parts.front().actor);
}

bool ModelInstanceDBSync::ownsProp(vtkSmartPointer<vtkProp> prop) const {
    if (prop && (prop == m_nativeGlbAssembly.GetPointer() ||
                 std::any_of(
                     m_nativeGlbActors.begin(), m_nativeGlbActors.end(),
                     [prop](const auto& actor) {
                         return actor.GetPointer() == prop.GetPointer();
                     }))) {
        return true;
    }
    return std::any_of(m_parts.begin(), m_parts.end(),
                       [prop](const auto& part) { return part.actor == prop; });
}

DBInstanceID ModelInstanceDBSync::partIdForProp(
    vtkSmartPointer<vtkProp> prop) const {
    if (prop && (prop == m_nativeGlbAssembly.GetPointer() ||
                 std::any_of(
                     m_nativeGlbActors.begin(), m_nativeGlbActors.end(),
                     [prop](const auto& actor) {
                         return actor.GetPointer() == prop.GetPointer();
                     }))) {
        return m_nativeGlbPartId;
    }
    const auto it = std::find_if(
        m_parts.begin(), m_parts.end(),
        [prop](const auto& part) { return part.actor == prop; });
    return it == m_parts.end() ? DBInstanceID{} : it->partId;
}

void ModelInstanceDBSync::applyRenderingContext() {
    updateInstanceStyle(false);
}

std::shared_ptr<const ModelSurfaceColorDB>
ModelInstanceDBSync::findSurfaceColors(DBInstanceID partId) const {
    const auto& document = documentReadView();
    if (!document || !partId.isValid()) return {};
    const auto part = document->get<ModelPartDB>(partId);
    if (!part) return {};
    const auto ids = document->getOwnedChildren(
        partId, ModelPartDB::kSurfaceColorsRelation);
    return ids.empty() ? std::shared_ptr<const ModelSurfaceColorDB>{}
                       : document->get<ModelSurfaceColorDB>(ids.front());
}

std::vector<Color> ModelInstanceDBSync::activeFilamentPalette() const {
    const auto config = activeSlicingConfig();
    if (config) {
        std::vector<Color> palette = config->filamentColors();
        if (!palette.empty()) return palette;
    }
    return {};
}

std::shared_ptr<const GPlatform::SlicingConfigDB>
ModelInstanceDBSync::activeSlicingConfig() const {
    const auto instance = documentDB<ModelInstanceDB>(m_id);
    return instance
        ? documentDB<GPlatform::SlicingConfigDB>(
              instance->getSlicingConfigDBId())
        : nullptr;
}

ModelInstanceDBSync::PartPresentation*
ModelInstanceDBSync::findPartPresentation(DBInstanceID partId) {
    const auto found = std::find_if(
        m_parts.begin(), m_parts.end(),
        [partId](const auto& value) { return value.partId == partId; });
    return found == m_parts.end() ? nullptr : &*found;
}

const ModelInstanceDBSync::PartPresentation*
ModelInstanceDBSync::findPartPresentation(DBInstanceID partId) const {
    const auto found = std::find_if(
        m_parts.begin(), m_parts.end(),
        [partId](const auto& value) { return value.partId == partId; });
    return found == m_parts.end() ? nullptr : &*found;
}

void ModelInstanceDBSync::scheduleSurfaceColorProjection(DBInstanceID partId) {
    auto* presentation = findPartPresentation(partId);
    const auto part = documentDB<ModelPartDB>(partId);
    const auto colors = findSurfaceColors(partId);
    if (!presentation || !part || !colors ||
        colors->getSurfaceColorData().empty()) {
        clearSurfaceColorPresentation(partId);
        return;
    }

    presentation->surfaceColorId = colors->getDBInstanceID();
    const int revision = colors->getRevision();
    const auto palette = activeFilamentPalette();
    if (presentation->surfaceColorProjection &&
        presentation->surfaceColorRevision == revision) {
        applySurfaceColorPalette(partId, palette);
        return;
    }
    if (presentation->surfaceColorProjectionPending &&
        presentation->pendingSurfaceColorId == colors->getDBInstanceID() &&
        presentation->pendingSurfaceColorRevision == revision) {
        return;
    }

    const std::uint64_t token =
        m_nextProjectionToken.fetch_add(1, std::memory_order_acq_rel);
    presentation->projectionToken = token;

    const auto projectionKey = makeSurfaceColorRenderProjectionKey(
        colors->getTopologyFingerprint(), colors->getSurfaceColorData(),
        colors->getSourceTriangleCount());
    if (auto prepared = findSurfaceColorRenderProjection(projectionKey)) {
        applySurfaceColorProjection(
            partId, token, std::move(prepared), palette,
            colors->getDBInstanceID(), revision);
        return;
    }

    const auto geometry = geometryForPart(partId);
    if (!geometry) {
        clearSurfaceColorPresentation(partId);
        return;
    }
    const auto triangles = partTriangles(*geometry);
    const auto encodedState = colors->getSurfaceColorData();
    const auto fingerprint = colors->getTopologyFingerprint();
    const int triangleCount = colors->getSourceTriangleCount();
    const DBInstanceID colorsId = colors->getDBInstanceID();
    presentation->surfaceColorProjectionPending = true;
    presentation->pendingSurfaceColorId = colorsId;
    presentation->pendingSurfaceColorRevision = revision;
    std::weak_ptr<ModelInstanceDBSync> weakSelf = shared_from_this();
    requestSurfaceColorRenderProjection(
        {projectionKey,
         [triangles, encodedState, fingerprint, triangleCount]() {
             return SurfaceColorRenderProjectionInput{
                 triangles, encodedState, fingerprint, triangleCount};
         }},
        [weakSelf, partId, token, colorsId, revision](
            std::shared_ptr<const SurfaceColorRenderProjectionResult> projection) {
            const auto self = weakSelf.lock();
            if (!self || !self->m_renderThread) {
                return;
            }
            self->dispatchRenderCommand(
                [weakSelf, partId, token,
                 projection = std::move(projection),
                 colorsId, revision](auto*, auto*) {
                    if (const auto target = weakSelf.lock()) {
                        target->applySurfaceColorProjection(
                            partId, token, projection,
                            target->activeFilamentPalette(),
                            colorsId, revision);
                    }
                });
            self->requestRender();
        });
}

void ModelInstanceDBSync::applySurfaceColorProjection(
    DBInstanceID partId,
    std::uint64_t token,
    std::shared_ptr<const SurfaceColorRenderProjectionResult> projection,
    const std::vector<Color>& palette,
    DBInstanceID surfaceColorId,
    int revision) {
    auto* presentation = findPartPresentation(partId);
    if (!presentation || presentation->projectionToken != token) {
        return;
    }
    presentation->surfaceColorProjectionPending = false;
    presentation->pendingSurfaceColorId = DBInstanceID{};
    presentation->pendingSurfaceColorRevision = -1;
    if (!projection || !projection->valid || !m_vtkRenderer) {
        return;
    }

    static_assert(sizeof(vtkIdType) == sizeof(std::int64_t));
    auto data = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    auto pointValues = vtkSmartPointer<vtkFloatArray>::New();
    auto normals = vtkSmartPointer<vtkFloatArray>::New();
    auto labels = vtkSmartPointer<vtkUnsignedIntArray>::New();
    auto sourceFacets = vtkSmartPointer<vtkIntArray>::New();
    auto offsets = vtkSmartPointer<vtkIdTypeArray>::New();
    auto connectivity = vtkSmartPointer<vtkIdTypeArray>::New();

    labels->SetName(kSurfaceLabelArray);
    sourceFacets->SetName(kSourceFacetArray);
    normals->SetName("Normals");
    pointValues->SetNumberOfComponents(3);
    normals->SetNumberOfComponents(3);
    pointValues->SetArray(
        const_cast<float*>(projection->points.data()),
        static_cast<vtkIdType>(projection->points.size()), 1);
    normals->SetArray(
        const_cast<float*>(projection->normals.data()),
        static_cast<vtkIdType>(projection->normals.size()), 1);
    labels->SetArray(
        const_cast<std::uint32_t*>(projection->labels.data()),
        static_cast<vtkIdType>(projection->labels.size()), 1);
    sourceFacets->SetArray(
        const_cast<std::int32_t*>(projection->sourceFacets.data()),
        static_cast<vtkIdType>(projection->sourceFacets.size()), 1);
    offsets->SetArray(
        reinterpret_cast<vtkIdType*>(const_cast<std::int64_t*>(
            projection->cellOffsets.data())),
        static_cast<vtkIdType>(projection->cellOffsets.size()), 1);
    connectivity->SetArray(
        reinterpret_cast<vtkIdType*>(const_cast<std::int64_t*>(
            projection->connectivity.data())),
        static_cast<vtkIdType>(projection->connectivity.size()), 1);
    points->SetData(pointValues);
    cells->SetData(offsets, connectivity);
    data->SetPoints(points);
    data->SetPolys(cells);
    data->GetPointData()->SetNormals(normals);
    data->GetCellData()->AddArray(labels);
    data->GetCellData()->AddArray(sourceFacets);
    data->GetCellData()->SetActiveScalars(kSurfaceLabelArray);

    presentation->surfaceColorLookupTable = vtkSmartPointer<vtkLookupTable>::New();
    presentation->mapper->SetInputData(data);
    presentation->mapper->SetLookupTable(
        presentation->surfaceColorLookupTable);
    presentation->mapper->SetScalarModeToUseCellFieldData();
    presentation->mapper->SelectColorArray(kSurfaceLabelArray);
    presentation->mapper->SetColorModeToMapScalars();
    presentation->mapper->ScalarVisibilityOn();
    presentation->surfaceColorPolyData = data;
    presentation->surfaceColorProjection = std::move(projection);
    presentation->surfaceColorId = surfaceColorId;
    presentation->surfaceColorRevision = revision;
    presentation->surfaceColorPaletteInitialized = false;
    applySurfaceColorPalette(partId, palette, true);
    // Surface-color projection changes the mapper input and material only.
    // Preserve presentation visibility applied by temporary tools such as the
    // cut preview instead of resurrecting the source actor asynchronously.
    updateInstanceStyle(false);
}

void ModelInstanceDBSync::applySurfaceColorPalette(
    DBInstanceID partId, const std::vector<Color>& palette, bool force) {
    auto* presentation = findPartPresentation(partId);
    if (!presentation || !presentation->surfaceColorLookupTable ||
        !presentation->surfaceColorProjection ||
        (!force && presentation->surfaceColorPaletteInitialized &&
         palette == presentation->surfaceColorPalette)) {
        return;
    }
    const int tableSize = std::max<int>(
        static_cast<int>(presentation->surfaceColorProjection->maximumLabel) + 1,
        static_cast<int>(palette.size()) + 1);
    presentation->surfaceColorLookupTable->SetNumberOfTableValues(
        std::max(1, tableSize));
    presentation->surfaceColorLookupTable->SetTableRange(
        0.0, std::max(0, tableSize - 1));
    presentation->surfaceColorLookupTable->Build();
    Vector3 baseColor(0.8f, 0.8f, 0.8f);
    const auto part = documentDB<ModelPartDB>(partId);
    const auto material = materialForPart(partId);
    if (part) {
        baseColor = GPlatform::Rendering::resolveModelDisplayColor(
            *part, material.get(), activeSlicingConfig().get());
    }
    presentation->surfaceColorLookupTable->SetTableValue(
        0, baseColor.x, baseColor.y, baseColor.z, 1.0);
    for (int label = 1; label < tableSize; ++label) {
        const Color color = ModelSurfaceColorDB::colorForLabel(
            static_cast<std::uint32_t>(label), palette,
            {baseColor.x, baseColor.y, baseColor.z, 1.0f});
        presentation->surfaceColorLookupTable->SetTableValue(
            label, color.r, color.g, color.b, color.a);
    }
    presentation->surfaceColorLookupTable->Modified();
    presentation->mapper->SetScalarRange(0.0, std::max(0, tableSize - 1));
    presentation->mapper->Modified();
    presentation->surfaceColorPalette = palette;
    presentation->surfaceColorPaletteInitialized = true;
}

void ModelInstanceDBSync::clearSurfaceColorPresentation(DBInstanceID partId) {
    auto* presentation = findPartPresentation(partId);
    if (!presentation) return;
    presentation->projectionToken =
        m_nextProjectionToken.fetch_add(1, std::memory_order_acq_rel);
    const auto part = documentDB<ModelPartDB>(partId);
    const auto mesh = geometryForPart(partId);
    if (part && mesh && mesh->isValid()) {
        auto geometry = mesh->read();
        if (geometry) {
            presentation->mapper->SetInputData(
                const_cast<vtkPolyData*>(&geometry.polyData()));
            presentation->geometry = std::move(geometry);
        }
    }
    presentation->mapper->ScalarVisibilityOff();
    presentation->mapper->SetLookupTable(nullptr);
    presentation->mapper->SetScalarModeToDefault();
    presentation->mapper->SetColorModeToDefault();
    presentation->mapper->Modified();
    presentation->surfaceColorPolyData = nullptr;
    presentation->surfaceColorLookupTable = nullptr;
    presentation->surfaceColorProjection.reset();
    presentation->surfaceColorPalette.clear();
    presentation->surfaceColorPaletteInitialized = false;
    presentation->surfaceColorProjectionPending = false;
    presentation->pendingSurfaceColorId = DBInstanceID{};
    presentation->pendingSurfaceColorRevision = -1;
    presentation->surfaceColorId = DBInstanceID{};
    presentation->surfaceColorRevision = -1;
}

void registerModelInstanceDBSync() {
    VtkDBSyncFactory::getInstance().registerDBSync(
        trans::TypeID(static_cast<std::uint32_t>(TypeID::MODEL_INSTANCE_DB)),
        [] { return std::make_shared<ModelInstanceDBSync>(); },
        GPlatform::Rendering::RendererRoleSets::PrepareAndPreview);
}
REGISTER_VTK_BACKEND_ENTRY(registerModelInstanceDBSync())
