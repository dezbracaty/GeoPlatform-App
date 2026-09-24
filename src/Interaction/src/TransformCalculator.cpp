#include "TransformCalculator.hpp"
#include "Foundation/Log.h"

TransformCalculator::TransformCalculator() = default;

TransformCalculator::~TransformCalculator() = default;

Transform TransformCalculator::calculateAlignToBedTransform(
    const BoundingBox& currentBounds,
    const Transform& currentTransform,
    float bedBaseZ) const {

    if (!currentBounds.valid) {
        LOG_WARN("Invalid bounds for align to bed calculation");
        return currentTransform;
    }

    const float deltaZ = bedBaseZ - currentBounds.min.z;

    // 创建新的变换，只修改Z坐标
    Transform newTransform = currentTransform;
    newTransform.translate(Vector3(0, 0, deltaZ));

    LOG_DEBUG("Align to bed: deltaZ = {:.3f}", deltaZ);
    return newTransform;
}

Transform TransformCalculator::calculateAxisAlignTransform(
    const BoundingBox& currentBounds,
    const Transform& currentTransform,
    float targetValue,
    AxisType axis) const {

    if (!currentBounds.valid) {
        LOG_WARN("Invalid bounds for axis align calculation");
        return currentTransform;
    }

    Vector3 currentCenter = currentBounds.getCenter();
    Vector3 deltaMove(0, 0, 0);

    switch (axis) {
        case AxisType::X:
            deltaMove.x = targetValue - currentCenter.x;
            break;
        case AxisType::Y:
            deltaMove.y = targetValue - currentCenter.y;
            break;
        case AxisType::Z:
            deltaMove.z = targetValue - currentCenter.z;
            break;
    }

    Transform newTransform = currentTransform;
    newTransform.translate(deltaMove);

    LOG_DEBUG("Axis align transform: axis={}, target={:.3f}, delta=({:.3f}, {:.3f}, {:.3f})",
              static_cast<int>(axis), targetValue, deltaMove.x, deltaMove.y, deltaMove.z);

    return newTransform;
}
