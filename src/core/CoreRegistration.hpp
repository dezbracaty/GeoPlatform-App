#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::Core {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::Core

#define REGISTER_CORE_QML_SINGLETON(ClassName, QmlName)                         \
    GPLATFORM_QML_SINGLETON_REGISTRATION(                                       \
        GPlatform::Core::moduleRegistrations, "GPlatform", 1, 0,               \
        ClassName, QmlName)

#define REGISTER_CORE_QML_SINGLETON_CUSTOM(ClassName, QmlName, Creator)         \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                \
        GPlatform::Core::moduleRegistrations, "GPlatform", 1, 0,               \
        ClassName, QmlName, Creator)
