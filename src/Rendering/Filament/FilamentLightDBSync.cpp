#include "FilamentLightDBSync.hpp"

#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"

#include <IDocumentReadView.hpp>
#include <LightDB.hpp>

#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/Scene.h>
#include <utils/EntityManager.h>

#include <algorithm>

namespace GPlatform::Rendering {
namespace {

class FilamentLightDBSync final : public IFilamentDBSync {
public:
    FilamentLightDBSync(DBInstanceID id, FilamentSceneAdapter& adapter)
        : m_id(id), m_adapter(adapter) {
    }

    ~FilamentLightDBSync() override { shutdown(); }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override { return TypeID::LIGHT_DB; }
    void initialize() override { rebuild(); }
    void applyChanges(std::span<const DocumentChange>) override { rebuild(); }
    void synchronize(const QSize&) override {}

    void shutdown() noexcept override {
        if (!m_entity) return;
        m_adapter.scene().remove(m_entity);
        m_adapter.engine().destroy(m_entity);
        utils::EntityManager::get().destroy(m_entity);
        m_entity = {};
    }

private:
    void rebuild() {
        shutdown();
        if (!m_adapter.document()) return;
        const auto light = m_adapter.document()->get<LightDB>(m_id);
        if (!light || !light->getEnabled()) return;
        const Vector3 position = light->getPosition();
        const Vector3 focalPoint = light->getFocalPoint();
        filament::math::float3 direction = toFilamentSpace(Vector3(
                focalPoint.x - position.x,
                focalPoint.y - position.y,
                focalPoint.z - position.z));
        using namespace filament::math;
        direction = length2(direction) < 1.0e-12f
            ? float3{0.0f, 0.0f, -1.0f}
            : normalize(direction);
        m_entity = utils::EntityManager::get().create();
        const Vector3 color = light->getColor();
        const auto result = filament::LightManager::Builder(
                filament::LightManager::Type::DIRECTIONAL)
            .direction(direction)
            .color({color.x, color.y, color.z})
            .intensity(std::max(0.0f, light->getIntensity()) * 10000.0f)
            .castShadows(true)
            .build(m_adapter.engine(), m_entity);
        if (result != filament::LightManager::Builder::Result::Success) {
            utils::EntityManager::get().destroy(m_entity);
            m_entity = {};
            return;
        }
        m_adapter.scene().addEntity(m_entity);
    }

    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    utils::Entity m_entity;
};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentLightDBSync(
    DBInstanceID id,
    FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentLightDBSync>(id, adapter);
}

} // namespace GPlatform::Rendering
