#pragma once

#include "ModuleRegistration.hpp"

namespace GPlatform::DBRelations {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::DBRelations

#define REGISTER_DB_RELATIONS(RegistrationCall)                                  \
    GPLATFORM_ADD_MODULE_REGISTRATION(                                            \
        GPlatform::DBRelations::moduleRegistrations, RegistrationCall)
