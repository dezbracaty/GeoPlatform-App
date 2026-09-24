#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::BaseInteraction {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::BaseInteraction

#define REGISTER_BASE_INTERACTION_QML_SINGLETON(ClassName, QmlName)               \
    GPLATFORM_QML_SINGLETON_REGISTRATION(                                         \
        GPlatform::BaseInteraction::moduleRegistrations,                          \
        "GPlatform", 1, 0, ClassName, QmlName)
