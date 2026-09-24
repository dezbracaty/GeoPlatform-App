#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::Bridge {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::Bridge

#define REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(ClassName, QmlName, Creator)         \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                  \
        GPlatform::Bridge::moduleRegistrations, "GPlatform", 1, 0,               \
        ClassName, QmlName, Creator)

#define REGISTER_BRIDGE_QML_TYPE(ClassName, QmlName)                              \
    GPLATFORM_QML_TYPE_REGISTRATION(                                              \
        GPlatform::Bridge::moduleRegistrations, "GPlatform", 1, 0,               \
        ClassName, QmlName)
