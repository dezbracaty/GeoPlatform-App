#include "BaseInteractionRegistration.hpp"

namespace GPlatform::BaseInteraction {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    moduleRegistrations().execute();
}

} // namespace GPlatform::BaseInteraction
