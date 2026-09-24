#include "SceneBounds.hpp"

#include <DocumentManager.hpp>
#include <algorithm>

namespace SceneBounds {

Box computeSceneBounds() {
    Box scene;
    auto* document = DocumentManager::instance();
    if (!document) {
        return scene;
    }

    bool first = true;
    for (const auto& id : document->getAllDBInstanceIds()) {
        auto actor = std::dynamic_pointer_cast<ActorDB>(document->getDBInstance(id));
        if (!actor || !actor->getVisible() || actor->getTypeID() == TypeID::TOOLPATH_PREVIEW_DB) {
            continue;
        }

        const auto transformed = actor->worldBounds();
        if (!transformed.valid) {
            continue;
        }

        if (first) {
            scene.min = transformed.min;
            scene.max = transformed.max;
            scene.valid = true;
            first = false;
            continue;
        }

        scene.min.x = std::min(scene.min.x, transformed.min.x);
        scene.min.y = std::min(scene.min.y, transformed.min.y);
        scene.min.z = std::min(scene.min.z, transformed.min.z);
        scene.max.x = std::max(scene.max.x, transformed.max.x);
        scene.max.y = std::max(scene.max.y, transformed.max.y);
        scene.max.z = std::max(scene.max.z, transformed.max.z);
    }

    return scene;
}

} // namespace SceneBounds
