#include "FilamentManualSupportDBSync.hpp"

#include "FilamentActorPresentation.hpp"
#include "FilamentRenderableResource.hpp"
#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"
#include "ManualSupportDB.hpp"

namespace GPlatform::Rendering {
namespace {

class FilamentManualSupportDBSync final : public IFilamentDBSync {
public:
    FilamentManualSupportDBSync(
        DBInstanceID id,
        FilamentSceneAdapter& adapter)
        : m_id(id),
          m_adapter(adapter),
          m_renderable(
              adapter.engine(), adapter.scene(), adapter.materialLibrary()) {
    }

    ~FilamentManualSupportDBSync() override { shutdown(); }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override {
        return TypeID::MANUAL_SUPPORT_DB;
    }

    void initialize() override { rebuild(); }

    void applyChanges(std::span<const DocumentChange> changes) override {
        const auto support = db();
        if (!support) {
            clear();
            return;
        }
        bool transformOnly = !changes.empty();
        for (const auto& change : changes) {
            transformOnly = transformOnly &&
                change.changeType == ChangeType::PROPERTY_CHANGED &&
                change.propertyName == "Transform";
        }
        if (transformOnly && m_renderable.updateTransform(
                                 support->getTransformMatrix())) {
            m_adapter.markPickGeometryChanged();
            return;
        }
        rebuild();
    }

    void synchronize(const QSize&) override {}

    void shutdown() noexcept override { clear(); }

private:
    std::shared_ptr<const ManualSupportDB> db() const {
        return m_adapter.document()
            ? m_adapter.document()->get<ManualSupportDB>(m_id)
            : std::shared_ptr<const ManualSupportDB>{};
    }

    FilamentGeometry geometryFor(const IndexedTriangleMesh& source) const {
        IndexedTriangleMesh mesh = source;
        if (mesh.normals.size() != mesh.positions.size()) {
            mesh.rebuildVertexNormals();
        }
        FilamentGeometry result;
        if (!mesh.hasValidTopology()) {
            result.error = "manual-support geometry is invalid";
            return result;
        }
        result.vertices.reserve(mesh.positions.size());
        for (std::size_t index = 0; index < mesh.positions.size(); ++index) {
            result.vertices.push_back({
                toFilamentSpace(mesh.positions[index]),
                toFilamentTangentFrame(mesh.normals[index])});
        }
        result.indices = mesh.indices;
        result.primitives.push_back({
            0, 0, static_cast<std::uint32_t>(result.indices.size()), {}});
        result.valid = true;
        return result;
    }

    void rebuild() {
        const auto support = db();
        if (!support || !support->getVisible() ||
            !support->hasGeneratedGeometry()) {
            clear();
            return;
        }
        auto geometry = geometryFor(*support->getGeneratedGeometry());
        if (!geometry.valid) {
            clear();
            return;
        }
        if (const auto material = support->getMaterial()) {
            geometry.primitives.front().authoredColor =
                material->getDiffuseColor();
        }
        const auto materials = filamentMaterialDescriptors(*support, geometry);
        replace(*support, std::move(geometry), materials);
    }

    void clear() noexcept {
        const auto entity = m_renderable.entityId();
        if (entity != 0) m_adapter.unbindPickIdentity(entity);
        if (m_renderable.reset()) m_adapter.markPickGeometryChanged();
    }

    bool replace(
        const ManualSupportDB& support,
        FilamentGeometry geometry,
        const std::vector<FilamentMaterialDescriptor>& materials) {
        clear();
        if (!m_renderable.rebuild(
                std::move(geometry), materials,
                support.getTransformMatrix())) {
            return false;
        }
        m_adapter.bindPickIdentity(m_renderable.entityId(), m_id);
        m_adapter.markPickGeometryChanged();
        return true;
    }

    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    FilamentRenderableResource m_renderable;
};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentManualSupportDBSync(
    DBInstanceID id,
    FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentManualSupportDBSync>(id, adapter);
}

} // namespace GPlatform::Rendering
