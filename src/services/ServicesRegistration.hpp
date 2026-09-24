#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::Services {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::Services

#define REGISTER_SERVICES_QML_SINGLETON_CUSTOM(                                 \
    ClassName, QmlName, Creator)                                                \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                \
        GPlatform::Services::moduleRegistrations, "GPlatform", 1, 0,           \
        ClassName, QmlName, Creator)

#define REGISTER_SERVICES_QML_TYPE(ClassName, QmlName)                          \
    GPLATFORM_QML_TYPE_REGISTRATION(                                             \
        GPlatform::Services::moduleRegistrations, "GPlatform", 1, 0,            \
        ClassName, QmlName)
