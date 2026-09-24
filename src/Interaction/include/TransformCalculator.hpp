#pragma once

#include "AlignmentTypes.hpp"
#include <Transform.hpp>

/**
 * @brief 变换计算器
 *
 * 负责计算对齐操作所需的变换矩阵
 */
class TransformCalculator {
public:
    using BoundingBox = Alignment::BoundingBox;
    using AxisType = Alignment::Axis;

    TransformCalculator();
    ~TransformCalculator();

    // 计算对齐到平台的变换
    Transform calculateAlignToBedTransform(
        const BoundingBox& currentBounds,
        const Transform& currentTransform,
        float bedBaseZ
    ) const;

    // 计算轴向对齐变换
    Transform calculateAxisAlignTransform(
        const BoundingBox& currentBounds,
        const Transform& currentTransform,
        float targetValue,
        AxisType axis
    ) const;

};
