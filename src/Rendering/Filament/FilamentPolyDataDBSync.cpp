#include "FilamentPolyDataDBSync.hpp"
#include "FilamentActorPresentation.hpp"
#include "FilamentRenderableResource.hpp"
#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"
#include <TransientPolyDataActorDB.hpp>
#include <filament/Engine.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkPointData.h>
#include <vtkTriangleFilter.h>
#include <array>
#include <map>

namespace GPlatform::Rendering {
namespace {
class FilamentPolyDataDBSync final : public IFilamentDBSync {
public:
    FilamentPolyDataDBSync(DBInstanceID id, FilamentSceneAdapter& adapter)
        : m_id(id), m_adapter(adapter),
          m_renderable(adapter.engine(), adapter.scene(), adapter.materialLibrary()) {}
    ~FilamentPolyDataDBSync() override {
        shutdown();
    }
    DBInstanceID objectId() const noexcept override {
        return m_id;
    }
    TypeID dbType() const noexcept override {
        const auto source = db();
        return source ? source->getTypeID() : TypeID::TRANSIENT_POLY_DATA_ACTOR_DB;
    }
    void initialize() override {
        rebuild();
    }
    void applyChanges(std::span<const DocumentChange> changes) override {
        bool geometryChanged = false, transformChanged = false, visibilityChanged = false;
        for (const auto& change : changes) {
            const auto& property = change.propertyName;
            if (change.changeType != ChangeType::PROPERTY_CHANGED || property.empty())
                geometryChanged = true;
            else if (property == "Transform")
                transformChanged = true;
            else if (property == "Visible")
                visibilityChanged = true;
            else if (property != "WorldBounds" && property != "DisplayName")
                geometryChanged = true;
        }
        if (geometryChanged) {
            rebuild();
            return;
        }
        const auto source = db();
        if (!source)
            return;
        if (visibilityChanged) {
            if (source->getVisible() && !m_renderable.valid()) {
                rebuild();
                return;
            }
            if (m_renderable.setVisible(source->getVisible()))
                m_adapter.markPickGeometryChanged();
        }
        if (transformChanged && m_renderable.updateTransform(worldTransform(*source)))
            m_adapter.markPickGeometryChanged();
    }
    void synchronize(const QSize&) override {}
    void shutdown() noexcept override {
        clear();
    }

private:
    static Transform::Matrix4 worldTransform(const TransientPolyDataActorDB& source) {
        return source.getTransformMatrix();
    }
    std::shared_ptr<const TransientPolyDataActorDB> db() const {
        return m_adapter.document() ? m_adapter.document()->get<TransientPolyDataActorDB>(m_id)
                                    : nullptr;
    }
    void clear() noexcept {
        if (auto entity = m_renderable.entityId())
            m_adapter.unbindPickIdentity(entity);
        if (m_renderable.reset())
            m_adapter.markPickGeometryChanged();
    }
    void rebuild() {
        const auto source = db();
        if (!source || !source->getVisible()) {
            clear();
            return;
        }
        const auto data = source->getGeometry();
        if (!data || data->GetNumberOfCells() == 0) {
            clear();
            return;
        }
        auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
        triangles->SetInputData(data);
        auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
        normals->SetInputConnection(triangles->GetOutputPort());
        normals->SplittingOff();
        normals->ConsistencyOn();
        normals->ComputePointNormalsOn();
        normals->Update();
        auto mesh = normals->GetOutput();
        auto normalData = mesh->GetPointData()->GetNormals();
        auto colors = source->usesCellColors() ? mesh->GetCellData()->GetScalars() : nullptr;
        FilamentGeometry geometry;
        for (vtkIdType i = 0; i < mesh->GetNumberOfPoints(); ++i) {
            double p[3], n[3]{0, 0, 1};
            mesh->GetPoint(i, p);
            if (normalData)
                normalData->GetTuple(i, n);
            geometry.vertices.push_back({toFilamentSpace(Vector3(p[0], p[1], p[2])),
                                         toFilamentTangentFrame(Vector3(n[0], n[1], n[2]))});
        }
        std::map<std::array<int, 4>, std::vector<std::uint32_t>> groups;
        const auto material = source->getMaterial();
        const Vector3 base = material ? material->getDiffuseColor() : Vector3(1, 1, 1);
        for (vtkIdType cell = 0; cell < mesh->GetNumberOfCells(); ++cell) {
            vtkIdType count = 0;
            const vtkIdType* ids = nullptr;
            mesh->GetCellPoints(cell, count, ids);
            if (count != 3)
                continue;
            std::array<int, 4> rgba{int(base.x * 255), int(base.y * 255), int(base.z * 255), 255};
            if (colors && colors->GetNumberOfComponents() >= 3 &&
                cell < colors->GetNumberOfTuples())
                for (int c = 0; c < std::min(4, colors->GetNumberOfComponents()); ++c)
                    rgba[c] = int(colors->GetComponent(cell, c));
            auto& indices = groups[rgba];
            for (int v = 0; v < 3; ++v)
                indices.push_back(std::uint32_t(ids[v]));
        }
        std::vector<FilamentMaterialDescriptor> materials;
        for (const auto& [rgba, indices] : groups) {
            const Vector3 color(rgba[0] / 255.f, rgba[1] / 255.f, rgba[2] / 255.f);
            geometry.primitives.push_back(
                {0, std::uint32_t(geometry.indices.size()), std::uint32_t(indices.size()), color});
            geometry.indices.insert(geometry.indices.end(), indices.begin(), indices.end());
            auto descriptor = filamentMaterialDescriptor(*source, color);
            descriptor.alpha *= rgba[3] / 255.f;
            descriptor.transparentOverlay = descriptor.alpha < 1.f;
            materials.push_back(descriptor);
        }
        geometry.valid = !geometry.indices.empty();
        const auto transform = worldTransform(*source);
        clear();
        if (geometry.valid && m_renderable.rebuild(std::move(geometry), materials, transform)) {
            auto& manager = m_adapter.engine().getRenderableManager();
            const auto instance = manager.getInstance(utils::Entity::import(m_renderable.entityId()));
            if (source->drawsOnTop()) manager.setPriority(instance, 7);
            for (std::size_t i = 0; i < materials.size(); ++i) {
                if (source->drawsOnTop()) {
                    auto* materialInstance = manager.getMaterialInstanceAt(instance, i);
                    materialInstance->setDepthCulling(false);
                    materialInstance->setDepthWrite(false);
                }
                if (materials[i].alpha < 1.f)
                    manager.getMaterialInstanceAt(instance, i)->setPolygonOffset(-1.f, -1.f);
            }
            m_adapter.bindPickIdentity(m_renderable.entityId(), m_id);
            m_adapter.markPickGeometryChanged();
        }
    }
    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    FilamentRenderableResource m_renderable;
};
} // namespace
std::unique_ptr<IFilamentDBSync> createFilamentPolyDataDBSync(DBInstanceID id,
                                                              FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentPolyDataDBSync>(id, adapter);
}
} // namespace GPlatform::Rendering
