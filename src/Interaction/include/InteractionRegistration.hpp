#pragma once

#include "ActionRegistration.hpp"
#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::Interaction {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::Interaction

#define REGISTER_INTERACTION_ACTION(HandlerClass, ...)                            \
    GPLATFORM_ACTION_REGISTRATION(                                                \
        GPlatform::Interaction::moduleRegistrations, HandlerClass, __VA_ARGS__)

#define REGISTER_PERSISTENT_INTERACTION_ACTION(HandlerClass, ...)                 \
    GPLATFORM_PERSISTENT_ACTION_REGISTRATION(                                     \
        GPlatform::Interaction::moduleRegistrations, HandlerClass, __VA_ARGS__)

#define REGISTER_INTERACTION_QML_SINGLETON(ClassName, QmlName)                    \
    GPLATFORM_QML_SINGLETON_REGISTRATION(                                         \
        GPlatform::Interaction::moduleRegistrations, "GPlatform", 1, 0,          \
        ClassName, QmlName)

#define REGISTER_INTERACTION_QML_SINGLETON_CUSTOM(ClassName, QmlName, Creator)    \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                  \
        GPlatform::Interaction::moduleRegistrations, "GPlatform", 1, 0,         \
        ClassName, QmlName, Creator)
