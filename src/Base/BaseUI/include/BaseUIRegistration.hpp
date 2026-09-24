#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::BaseUI {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::BaseUI

#define REGISTER_BASE_UI_QML_SINGLETON_CUSTOM(ClassName, QmlName, Creator)       \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                 \
        GPlatform::BaseUI::moduleRegistrations, "GPlatform", 1, 0,             \
        ClassName, QmlName, Creator)
