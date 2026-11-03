#include "FilamentGlbDBSync.hpp"

#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"
#include "Foundation/Log.h"

#include <GlbDB.hpp>

#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/materials/uberarchive.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace GPlatform::Rendering {
namespace {

class FilamentGlbDBSync final : public IFilamentDBSync {
public:
    FilamentGlbDBSync(
        DBInstanceID id,
        TypeID type,
        FilamentSceneAdapter& adapter)
        : m_id(id), m_type(type), m_adapter(adapter) {}

    ~FilamentGlbDBSync() override { shutdown(); }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override { return m_type; }

    void initialize() override {
        LOG_INFO("FilamentGlbDBSync: initialize model={}", m_id.getValue());
        reload();
    }

    void applyChanges(std::span<const DocumentChange> changes) override {
        bool sourceDirty = false;
        bool transformDirty = false;
        bool visibilityDirty = false;
        for (const auto& change : changes) {
            if (change.id != m_id) continue;
            if (change.changeType == ChangeType::OBJECT_CREATED) {
                sourceDirty = true;
                continue;
            }
            if (change.changeType != ChangeType::PROPERTY_CHANGED) continue;
            const std::string_view property(change.propertyName);
            sourceDirty |= property == "AssetRevision";
            transformDirty |= property == "Transform";
            visibilityDirty |= property == "Visible";
        }
        if (sourceDirty || visibilityDirty) {
            const auto glb = db();
            if (glb && glb->getVisible()) reload();
            else releaseAsset();
            m_adapter.requestFrame();
        } else {
            if (transformDirty) updateTransform();
            if (transformDirty) m_adapter.requestFrame();
        }
    }

    void synchronize(const QSize&) override {}

    void shutdown() noexcept override {
        if (m_shutdown) return;
        m_shutdown = true;
        releaseAsset();
    }

private:
    std::shared_ptr<const GlbDB> db() const {
        return m_adapter.document()
            ? m_adapter.document()->get<GlbDB>(m_id)
            : std::shared_ptr<const GlbDB>{};
    }

    void reload() {
        const auto glb = db();
        if (!glb || !glb->getVisible()) {
            // Visibility is the scene-membership contract. Invisible GLBs
            // keep their DB payload without allocating Filament resources.
            releaseAsset();
            return;
        }
        const auto snapshot = glb ? glb->acquireAssetSnapshot() : nullptr;
        if (!snapshot || !*snapshot) {
            LOG_DEBUG(
                "FilamentGlbDBSync: DB asset snapshot unavailable model={} db={}",
                m_id.getValue(), glb ? "ready" : "missing");
            releaseAsset();
            return;
        }
        if (m_asset && snapshot->revision == m_loadedRevision) {
            updateTransform();
            updateVisibility();
            return;
        }
        releaseAsset();
        m_shutdown = false;
        m_sourceSnapshot = snapshot;
        const auto& bytes = snapshot->asset->bytes();
        LOG_INFO(
            "FilamentGlbDBSync: parsing DB GLB model={} revision={} bytes={}",
            m_id.getValue(), snapshot->revision, bytes.size());
        if (bytes.empty() ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            LOG_ERROR(
                "FilamentGlbDBSync: invalid DB GLB bytes model={} bytes={}",
                m_id.getValue(), bytes.size());
            m_sourceSnapshot.reset();
            return;
        }

        auto& engine = m_adapter.engine();
        m_materials = filament::gltfio::createUbershaderProvider(
            &engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
        if (!m_materials) {
            LOG_ERROR(
                "FilamentGlbDBSync: failed to create material provider model={}",
                m_id.getValue());
            releaseAsset();
            return;
        }

        filament::gltfio::AssetConfiguration assetConfig{};
        assetConfig.engine = &engine;
        assetConfig.materials = m_materials;
        m_loader = filament::gltfio::AssetLoader::create(assetConfig);
        if (!m_loader) {
            LOG_ERROR(
                "FilamentGlbDBSync: failed to create AssetLoader model={}",
                m_id.getValue());
            releaseAsset();
            return;
        }

        m_asset = m_loader->createAsset(
            bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        if (!m_asset) {
            LOG_ERROR(
                "FilamentGlbDBSync: DB GLB parsing failed model={} revision={}",
                m_id.getValue(), snapshot->revision);
            releaseAsset();
            return;
        }

        filament::gltfio::ResourceConfiguration resourceConfig{};
        resourceConfig.engine = &engine;
        resourceConfig.normalizeSkinningWeights = true;
        m_resources = std::make_unique<filament::gltfio::ResourceLoader>(
            resourceConfig);
        m_stbTextures = filament::gltfio::createStbProvider(&engine);
        m_ktx2Textures = filament::gltfio::createKtx2Provider(&engine);
        if (m_stbTextures) {
            m_resources->addTextureProvider("image/png", m_stbTextures);
            m_resources->addTextureProvider("image/jpeg", m_stbTextures);
        }
        if (m_ktx2Textures) {
            m_resources->addTextureProvider("image/ktx2", m_ktx2Textures);
        }
        if (!m_resources->loadResources(m_asset)) {
            LOG_ERROR(
                "FilamentGlbDBSync: resource loading failed model={} revision={}",
                m_id.getValue(), snapshot->revision);
            releaseAsset();
            return;
        }
        m_asset->releaseSourceData();

        const auto bounds = m_asset->getBoundingBox();
        m_sourceCenter = bounds.isEmpty()
            ? filament::math::float3{}
            : bounds.center();
        bindPickIdentities();
        updateTransform();
        updateVisibility();
        m_adapter.markPickGeometryChanged();
        m_adapter.requestFrame();
        m_loadedRevision = snapshot->revision;
        LOG_INFO(
            "FilamentGlbDBSync: loaded GLB from DB model={} revision={} renderables={} bytes={}",
            m_id.getValue(), m_loadedRevision,
            m_asset->getRenderableEntityCount(), bytes.size());
    }

    void bindPickIdentities() {
        if (!m_asset) return;
        const auto* entities = m_asset->getRenderableEntities();
        for (std::size_t index = 0;
             index < m_asset->getRenderableEntityCount(); ++index) {
            m_adapter.bindPickIdentity(entities[index].getId(), m_id);
        }
    }

    void unbindPickIdentities() noexcept {
        if (!m_asset) return;
        const auto* entities = m_asset->getRenderableEntities();
        for (std::size_t index = 0;
             index < m_asset->getRenderableEntityCount(); ++index) {
            m_adapter.unbindPickIdentity(entities[index].getId());
        }
    }

    void updateTransform() {
        const auto mesh = db();
        if (!mesh || !m_asset) return;
        auto& transforms = m_adapter.engine().getTransformManager();
        const auto instance = transforms.getInstance(m_asset->getRoot());
        if (!instance) return;
        const auto centerTranslation = filament::math::mat4f::translation(
            -m_sourceCenter);
        constexpr float kMetersToMillimeters = 1000.0f;
        const filament::math::mat4f sourceScale =
            filament::math::mat4f::scaling(
                filament::math::float3{kMetersToMillimeters});
        transforms.setTransform(
            instance,
            toFilamentTransform(mesh->getTransformMatrix()) *
                sourceScale * centerTranslation);
        m_adapter.markPickGeometryChanged();
    }

    void updateVisibility() {
        const auto mesh = db();
        if (!mesh || !m_asset) return;
        const bool shouldBeVisible = mesh->getVisible();
        if (shouldBeVisible == m_inScene) return;
        const auto* entities = m_asset->getRenderableEntities();
        const auto count = m_asset->getRenderableEntityCount();
        if (shouldBeVisible) {
            m_adapter.scene().addEntities(entities, count);
        } else {
            m_adapter.scene().removeEntities(entities, count);
        }
        m_inScene = shouldBeVisible;
        m_adapter.markPickGeometryChanged();
    }

    void releaseAsset() noexcept {
        if (m_asset) {
            if (m_inScene) {
                m_adapter.scene().removeEntities(
                    m_asset->getRenderableEntities(),
                    m_asset->getRenderableEntityCount());
                m_inScene = false;
            }
            unbindPickIdentities();
        }
        if (m_loader && m_asset) m_loader->destroyAsset(m_asset);
        m_asset = nullptr;
        m_resources.reset();
        if (m_materials) {
            m_materials->destroyMaterials();
            delete m_materials;
            m_materials = nullptr;
        }
        delete m_ktx2Textures;
        m_ktx2Textures = nullptr;
        delete m_stbTextures;
        m_stbTextures = nullptr;
        if (m_loader) filament::gltfio::AssetLoader::destroy(&m_loader);
        m_sourceSnapshot.reset();
        m_loadedRevision = 0;
        m_adapter.markPickGeometryChanged();
    }

    DBInstanceID m_id;
    TypeID m_type;
    FilamentSceneAdapter& m_adapter;
    std::shared_ptr<const GlbAssetSnapshot> m_sourceSnapshot;
    filament::gltfio::MaterialProvider* m_materials{nullptr};
    filament::gltfio::AssetLoader* m_loader{nullptr};
    std::unique_ptr<filament::gltfio::ResourceLoader> m_resources;
    filament::gltfio::TextureProvider* m_stbTextures{nullptr};
    filament::gltfio::TextureProvider* m_ktx2Textures{nullptr};
    filament::gltfio::FilamentAsset* m_asset{nullptr};
    filament::math::float3 m_sourceCenter{};
    std::uint64_t m_loadedRevision{0};
    bool m_inScene{false};
    bool m_shutdown{false};
};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentGlbDBSync(
    DBInstanceID id,
    TypeID type,
    FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentGlbDBSync>(id, type, adapter);
}

} // namespace GPlatform::Rendering
