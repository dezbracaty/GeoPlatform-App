#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::Printer {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::Printer

#define REGISTER_PRINTER_QML_SINGLETON_CUSTOM(ClassName, QmlName, Creator)        \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                  \
        GPlatform::Printer::moduleRegistrations,                                  \
        "GPlatform", 1, 0, ClassName, QmlName, Creator)
