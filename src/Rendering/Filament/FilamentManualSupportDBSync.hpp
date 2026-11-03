#pragma once

#include "IFilamentDBSync.hpp"
#include <memory>

namespace GPlatform::Rendering {

class FilamentSceneAdapter;

std::unique_ptr<IFilamentDBSync> createFilamentManualSupportDBSync(
    DBInstanceID id,
    FilamentSceneAdapter& adapter);

} // namespace GPlatform::Rendering
