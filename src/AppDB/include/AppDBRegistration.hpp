#pragma once

#include "ModuleRegistration.hpp"

#include <TransDBSerialization.hpp>

namespace GPlatform::AppDB {

ModuleRegistrationList& moduleRegistrations();
void registerModule();

} // namespace GPlatform::AppDB

// A FIELD value codec belongs to its data module, not to a DB constructor.
// Keep the stable type key first so template types containing commas can be
// passed through __VA_ARGS__ without aliases made only for the macro.
#define REGISTER_APPDB_FIELD_VALUE_CODEC(StableTypeKey, ...)                    \
    GPLATFORM_ADD_MODULE_REGISTRATION(                                          \
        GPlatform::AppDB::moduleRegistrations,                                  \
        trans::registerPropertyType<__VA_ARGS__>(StableTypeKey))
