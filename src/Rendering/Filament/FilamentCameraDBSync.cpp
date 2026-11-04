#include "FilamentCameraDBSync.hpp"

#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"

#include "Foundation/Log.h"

#include <CameraDB.hpp>
#include <IDocumentReadView.hpp>

#include <filament/Camera.h>

namespace GPlatform::Rendering {
namespace {

class FilamentCameraDBSync final : public IFilamentDBSync {
public:
    FilamentCameraDBSync(DBInstanceID id, FilamentSceneAdapter& adapter)
        : m_id(id), m_adapter(adapter) {
    }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override { return TypeID::CAMERA_DB; }
    void initialize() override { m_dirty = true; }
    void applyChanges(std::span<const DocumentChange>) override {
        m_dirty = true;
    }

    void synchronize(const QSize& viewport) override {
        if (!m_adapter.document() || viewport.height() <= 0 ||
            (!m_dirty && viewport == m_lastViewport)) {
            return;
        }
        const auto cameraDb = m_adapter.document()->get<CameraDB>(m_id);
        if (!cameraDb) return;
        const auto state = cameraDb->getRenderStateSnapshot();
        const double aspect = static_cast<double>(viewport.width()) /
            static_cast<double>(viewport.height());
        if (state.projectionType ==
            static_cast<int>(CameraDB::ProjectionType::ORTHOGRAPHIC)) {
            const double halfHeight = state.orthographicSize * 0.5;
            m_adapter.camera().setProjection(
                filament::Camera::Projection::ORTHO,
                -halfHeight * aspect, halfHeight * aspect,
                -halfHeight, halfHeight,
                state.nearClippingPlane, state.farClippingPlane);
        } else {
            m_adapter.camera().setProjection(
                state.fieldOfView, aspect,
                state.nearClippingPlane, state.farClippingPlane,
                filament::Camera::Fov::VERTICAL);
        }
        m_adapter.camera().lookAt(
            toFilamentSpace(state.position),
            toFilamentSpace(state.target),
            toFilamentSpace(state.upVector));
        if (m_lastViewport.isEmpty()) {
            LOG_INFO(
                "FilamentCameraDBSync: camera {} position=({}, {}, {}) target=({}, {}, {}) up=({}, {}, {}) fov={} clip=({}, {}) viewport={}x{}",
                m_id.toString(), state.position.x, state.position.y,
                state.position.z, state.target.x, state.target.y,
                state.target.z, state.upVector.x, state.upVector.y,
                state.upVector.z, state.fieldOfView,
                state.nearClippingPlane, state.farClippingPlane,
                viewport.width(), viewport.height());
        }
        m_dirty = false;
        m_lastViewport = viewport;
        m_adapter.markPickGeometryChanged();
    }

    void shutdown() noexcept override {
        m_dirty = false;
        m_lastViewport = {};
    }

private:
    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    QSize m_lastViewport;
    bool m_dirty{true};
};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentCameraDBSync(
    DBInstanceID id,
    FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentCameraDBSync>(id, adapter);
}

} // namespace GPlatform::Rendering
