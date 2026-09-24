#include "BoundsCalculator.hpp"
#include <DocumentManager.hpp>
#include <ActorDB.hpp>
#include "Foundation/Log.h"
#include <algorithm>

BoundsCalculator::BoundsCalculator() = default;

BoundsCalculator::~BoundsCalculator() = default;

BoundsCalculator::BoundingBox BoundsCalculator::calculateWorldBounds(const DBInstanceID& modelId) const {
    const auto actor = std::dynamic_pointer_cast<ActorDB>(
        getModelDBObject(modelId));
    if (!actor) {
        LOG_WARN("Failed to resolve actor bounds for model: {}", modelId.getValue());
        return {};
    }
    return actor->worldBounds();
}

BoundsCalculator::BoundingBox BoundsCalculator::calculateCombinedBounds(
    const std::vector<DBInstanceID>& modelIds) const {

    BoundingBox combined;
    combined.valid = false;

    for (const auto& modelId : modelIds) {
        auto bounds = calculateWorldBounds(modelId);
        if (!bounds.valid) {
            continue;
        }

        if (!combined.valid) {
            combined = bounds;
        } else {
            combined.min.x = std::min(combined.min.x, bounds.min.x);
            combined.min.y = std::min(combined.min.y, bounds.min.y);
            combined.min.z = std::min(combined.min.z, bounds.min.z);
            combined.max.x = std::max(combined.max.x, bounds.max.x);
            combined.max.y = std::max(combined.max.y, bounds.max.y);
            combined.max.z = std::max(combined.max.z, bounds.max.z);
        }
    }

    return combined;
}

float BoundsCalculator::getAxisMinValue(
    const DBInstanceID& modelId,
    AxisType axis) const {
    auto bounds = calculateWorldBounds(modelId);
    if (!bounds.valid) {
        return 0.0f;
    }

    switch (axis) {
        case AxisType::X: return bounds.min.x;
        case AxisType::Y: return bounds.min.y;
        case AxisType::Z: return bounds.min.z;
        default: return bounds.min.x;
    }
}

float BoundsCalculator::getAxisMaxValue(
    const DBInstanceID& modelId,
    AxisType axis) const {
    auto bounds = calculateWorldBounds(modelId);
    if (!bounds.valid) {
        return 0.0f;
    }

    switch (axis) {
        case AxisType::X: return bounds.max.x;
        case AxisType::Y: return bounds.max.y;
        case AxisType::Z: return bounds.max.z;
        default: return bounds.max.x;
    }
}

float BoundsCalculator::getAxisCenter(
    const DBInstanceID& modelId,
    AxisType axis) const {
    auto bounds = calculateWorldBounds(modelId);
    if (!bounds.valid) {
        return 0.0f;
    }

    auto center = bounds.getCenter();
    switch (axis) {
        case AxisType::X: return center.x;
        case AxisType::Y: return center.y;
        case AxisType::Z: return center.z;
        default: return center.x;
    }
}

std::shared_ptr<AutoRegisterDB> BoundsCalculator::getModelDBObject(const DBInstanceID& modelId) const {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("DocumentManager not available");
        return nullptr;
    }

    return docManager->getDBInstance(modelId);
}
