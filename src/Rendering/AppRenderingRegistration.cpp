#include "AppRenderingRegistration.hpp"
#include <VtkBackendRegistration.hpp>
#ifdef GPLATFORM_ENABLE_FILAMENT
namespace GPlatform::Rendering { void registerAppFilamentSyncs(); }
#endif

namespace GPlatform::Rendering {
void registerAppRendering() {
    registerVtkBackendModule();
#ifdef GPLATFORM_ENABLE_FILAMENT
    registerAppFilamentSyncs();
#endif
}
}
