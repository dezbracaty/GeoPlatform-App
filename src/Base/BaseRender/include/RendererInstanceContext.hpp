#pragma once

#include "RendererRole.hpp"

#include <BaseID.hpp>

namespace GPlatform::Rendering {

struct RendererInstanceContext {
    RendererRole role{RendererRole::Compatibility};
    DBInstanceID windowId{INVALID_DB_ID};
    DBInstanceID cameraId{INVALID_DB_ID};
};

} // namespace GPlatform::Rendering
