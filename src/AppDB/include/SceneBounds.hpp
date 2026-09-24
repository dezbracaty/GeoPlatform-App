#pragma once

#include "ActorDB.hpp"
#include <cmath>

namespace SceneBounds {

struct Box {
    Vector3 min{0, 0, 0};
    Vector3 max{0, 0, 0};
    bool valid = false;

    Vector3 getCenter() const {
        return Vector3((min.x + max.x) * 0.5f,
                       (min.y + max.y) * 0.5f,
                       (min.z + max.z) * 0.5f);
    }

    Vector3 getSize() const {
        return Vector3(max.x - min.x, max.y - min.y, max.z - min.z);
    }

    float getDiagonal() const {
        if (!valid) {
            return 0.0f;
        }
        const Vector3 size = getSize();
        return std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    }
};

Box computeSceneBounds();

} // namespace SceneBounds
