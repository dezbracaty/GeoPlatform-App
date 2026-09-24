#include "DocumentRegistration.hpp"

namespace GPlatform::Document {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    moduleRegistrations().execute();
}

} // namespace GPlatform::Document
