#pragma once

#include <ActorDB.hpp>

namespace Alignment {

using BoundingBox = ActorDB::BoundingBox;

struct BedDimensions {
    float width{0.0f};
    float height{0.0f};
    float centerX{0.0f};
    float centerY{0.0f};
    float baseZ{0.0f};
};

enum class CenterMode {
    XOnly,
    YOnly,
    XY
};

enum class Axis {
    X,
    Y,
    Z
};

enum class AlignMode {
    Min,
    Max,
    Center,
    First
};

} // namespace Alignment
