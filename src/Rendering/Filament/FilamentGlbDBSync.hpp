#pragma once

#include "IFilamentDBSync.hpp"

#include <memory>

namespace GPlatform::Rendering {

class FilamentSceneAdapter;

std::unique_ptr<IFilamentDBSync> createFilamentGlbDBSync(
    DBInstanceID id,
    TypeID type,
    FilamentSceneAdapter& adapter);

} // namespace GPlatform::Rendering
