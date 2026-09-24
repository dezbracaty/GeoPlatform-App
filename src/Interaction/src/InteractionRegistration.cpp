#include "InteractionRegistration.hpp"

namespace GPlatform::Interaction {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    moduleRegistrations().execute();
}

} // namespace GPlatform::Interaction
