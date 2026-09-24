#include "DBRelationsRegistration.hpp"
#include "ModelGraphRelations.hpp"

namespace GPlatform::DBRelations {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    ModelGraphRelations::registerRelations();
    moduleRegistrations().execute();
}

} // namespace GPlatform::DBRelations
