#include "FilamentSkyboxDBSync.hpp"

#include "FilamentEnvironmentController.hpp"
#include "FilamentSceneAdapter.hpp"

#include <IDocumentReadView.hpp>
#include <SkyboxDB.hpp>
#include <ThemeDB.hpp>
#include <StudioBackgroundResource.hpp>

#include <algorithm>

namespace GPlatform::Rendering {
namespace {

class FilamentSkyboxDBSync final : public IFilamentDBSync {
public:
    FilamentSkyboxDBSync(DBInstanceID id, FilamentSceneAdapter& adapter)
        : m_id(id), m_adapter(adapter) {
    }

    ~FilamentSkyboxDBSync() override { shutdown(); }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override { return TypeID::SKYBOX_DB; }
    void initialize() override { rebuild(); }
    void applyChanges(std::span<const DocumentChange>) override { rebuild(); }
    void synchronize(const QSize&) override { rebuild(); }
    void shutdown() noexcept override {
        if (!m_active) return;
        m_active = false;
        auto settings = FilamentEnvironmentController::defaultSettings();
        settings.backgroundPath = resolveStudioBackgroundResource(
            kPrintStudioEnvironmentUri, darkTheme());
        m_adapter.environmentController().apply(settings);
    }

private:
    bool darkTheme() {
        const auto& document = m_adapter.document();
        if (!m_themeDiscovered && document) {
            m_themeDiscovered = true;
            const auto themes = document->getDBs(TypeID::THEME_DB);
            if (!themes.empty() && themes.front()) {
                m_themeId = themes.front()->getDBInstanceID();
            }
        }
        const auto theme = document && m_themeId.isValid()
            ? document->get<ThemeDB>(m_themeId) : nullptr;
        return theme && theme->theme() == ThemeDB::Mode::Dark;
    }

    void rebuild() {
        if (!m_adapter.document()) return;
        const auto skybox = m_adapter.document()->get<SkyboxDB>(m_id);
        if (!skybox) {
            shutdown();
            return;
        }
        auto settings = FilamentEnvironmentController::defaultSettings();
        settings.backgroundVisible = skybox->getVisible();
        settings.imageBasedLighting = skybox->getUseImageBasedLighting();
        settings.lightingIntensity =
            std::max(0.0f, skybox->getLightingIntensity());
        const auto& legacyPath = skybox->getHdrTexturePath();
        const auto& lightingPath = skybox->getLightingTexturePath();
        const auto& backgroundPath = skybox->getBackgroundTexturePath();
        if (!lightingPath.empty() || !legacyPath.empty()) {
            settings.lightingPath = lightingPath.empty()
                ? legacyPath
                : lightingPath;
        }
        settings.backgroundPath = backgroundPath.empty()
            ? legacyPath
            : backgroundPath;
        if (skybox->getPresetTypeEnum() !=
            SkyboxDB::PresetType::HDR_TEXTURE) {
            settings.backgroundPath.clear();
            const Vector3 color =
                skybox->getPresetTypeEnum() == SkyboxDB::PresetType::SOLID_COLOR
                ? skybox->getBackgroundColor()
                : skybox->getSkyColorBottom();
            settings.fallbackColor = Vector3(
                std::clamp(color.x / 255.0f, 0.0f, 1.0f),
                std::clamp(color.y / 255.0f, 0.0f, 1.0f),
                std::clamp(color.z / 255.0f, 0.0f, 1.0f));
        }
        settings.backgroundPath = resolveStudioBackgroundResource(
            settings.backgroundPath, darkTheme());
        m_adapter.environmentController().apply(settings);
        m_active = true;
    }

    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    bool m_active{false};
    DBInstanceID m_themeId;
    bool m_themeDiscovered{false};
};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentSkyboxDBSync(
    DBInstanceID id,
    FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentSkyboxDBSync>(id, adapter);
}

} // namespace GPlatform::Rendering
