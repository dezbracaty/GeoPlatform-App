#pragma once

#include "ModuleRegistration.hpp"
#include "QmlRegistration.hpp"

namespace GPlatform::Document {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::Document

#define REGISTER_DOCUMENT_QML_SINGLETON_CUSTOM(ClassName, QmlName, Creator)       \
    GPLATFORM_QML_SINGLETON_CUSTOM_REGISTRATION(                                  \
        GPlatform::Document::moduleRegistrations, "GPlatform", 1, 0,             \
        ClassName, QmlName, Creator)
