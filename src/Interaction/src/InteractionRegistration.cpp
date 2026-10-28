#include "InteractionRegistration.hpp"
#include "KeyboardShortcutManager.hpp"

#include <mutex>

namespace GPlatform::Interaction {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    static std::once_flag once;
    std::call_once(once, [] {
        moduleRegistrations().execute();
        KeyboardShortcutManager::getInstance()->loadConfiguration();
    });
}

} // namespace GPlatform::Interaction
