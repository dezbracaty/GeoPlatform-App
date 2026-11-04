#include "FilamentDBSyncFactory.hpp"

#include "FilamentCameraDBSync.hpp"
#include "FilamentLightDBSync.hpp"
#include "FilamentGlbDBSync.hpp"
#include "FilamentManualSupportDBSync.hpp"
#include "FilamentPolyDataDBSync.hpp"
#include "FilamentModelInstanceDBSync.hpp"
#include "FilamentPrintBedDBSync.hpp"
#include "FilamentSkyboxDBSync.hpp"

#include "FilamentEnvironmentController.hpp"
#include "FilamentSceneAdapter.hpp"
#include <CameraDB.hpp>
#include <SkyboxDB.hpp>
#include <ThemeDB.hpp>
#include <StudioBackgroundResource.hpp>
#include <mutex>

namespace GPlatform::Rendering {
namespace {
constexpr RendererRoleMask kSceneRoles = RendererRoleSets::PrepareAndPreview | RendererRole::Realistic;
constexpr RendererRoleMask kPrepareRoles = RendererRoleSets::Prepare | RendererRole::Realistic;

void synchronizeDefaultEnvironment(FilamentSceneAdapter& adapter) {
    const auto& document = adapter.document();
    if (!document || !document->getDBs(TypeID::SKYBOX_DB).empty()) return;
    const auto themes = document->getDBs(TypeID::THEME_DB);
    const auto theme = themes.empty() ? nullptr :
        std::dynamic_pointer_cast<const ThemeDB>(themes.front());
    auto settings = FilamentEnvironmentController::defaultSettings();
    settings.backgroundPath = resolveStudioBackgroundResource(
        kPrintStudioEnvironmentUri, theme && theme->theme() == ThemeDB::Mode::Dark);
    adapter.environmentController().apply(settings);
}
}
static void registerFilamentSyncs() {
    auto& factory = FilamentDBSyncFactory::instance();
    factory.registerDBSync(TypeID::TRANSIENT_POLY_DATA_ACTOR_DB,
                   createFilamentPolyDataDBSync, RendererRoleSets::Prepare);
    factory.registerDBSync(
        TypeID::GLB_DB,
        [](DBInstanceID id, FilamentSceneAdapter& adapter) {
            return createFilamentGlbDBSync(id, TypeID::GLB_DB, adapter);
        },
        kSceneRoles);
    factory.registerDBSync(
        TypeID::MODEL_INSTANCE_DB,
        [](DBInstanceID id, FilamentSceneAdapter& adapter) {
            return createFilamentModelInstanceDBSync(id, adapter);
        },
        kSceneRoles);
    factory.registerDBSync(
        TypeID::MANUAL_SUPPORT_DB,
        createFilamentManualSupportDBSync,
        kPrepareRoles);
    factory.registerDBSync(
        TypeID::PRINT_BED_DB,
        createFilamentPrintBedDBSync,
        kSceneRoles);
    factory.registerDBSync(
        TypeID::LIGHT_DB,
        createFilamentLightDBSync,
        kSceneRoles);
    factory.registerDBSync(
        TypeID::CAMERA_DB,
        createFilamentCameraDBSync,
        kSceneRoles,
        [](const RendererInstanceContext& context,
           const std::shared_ptr<const AutoRegisterDB>& db) {
            return db && db->getDBInstanceID() == context.cameraId;
        });
    factory.registerDBSync(
        TypeID::SKYBOX_DB,
        createFilamentSkyboxDBSync,
        kSceneRoles);
    factory.setEnvironmentSynchronizer(synchronizeDefaultEnvironment);
}

void registerAppFilamentSyncs() {
    static std::once_flag once;
    std::call_once(once, registerFilamentSyncs);
}
} // namespace GPlatform::Rendering
