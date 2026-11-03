#pragma once
#include "IFilamentDBSync.hpp"
namespace GPlatform::Rendering {
class FilamentSceneAdapter;
std::unique_ptr<IFilamentDBSync> createFilamentPolyDataDBSync(DBInstanceID id,
                                                              FilamentSceneAdapter& adapter);
} // namespace GPlatform::Rendering
