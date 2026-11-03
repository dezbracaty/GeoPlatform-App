#pragma once

#include <BaseID.hpp>

#include <memory>

namespace GPlatform::Rendering {
class FilamentSceneAdapter;
class IFilamentDBSync;

/** Creates the Filament presentation owned by one ModelInstanceDB. */
std::unique_ptr<IFilamentDBSync> createFilamentModelInstanceDBSync(
    DBInstanceID id, FilamentSceneAdapter& adapter);
}
