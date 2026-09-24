#include "ServicesRegistration.hpp"

namespace GPlatform::Services {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    moduleRegistrations().execute();
}

} // namespace GPlatform::Services
