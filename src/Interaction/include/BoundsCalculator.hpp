#pragma once

#include "AlignmentTypes.hpp"
#include <SystemTypes.hpp>
#include <memory>
#include <vector>

// 前向声明
class AutoRegisterDB;

/**
 * @brief 边界框计算器
 *
 * 负责按当前 DB 状态计算模型的世界坐标边界框
 */
class BoundsCalculator {
public:
    using BoundingBox = Alignment::BoundingBox;
    using AxisType = Alignment::Axis;

    BoundsCalculator();
    ~BoundsCalculator();

    // 计算模型的世界坐标边界框
    BoundingBox calculateWorldBounds(const DBInstanceID& modelId) const;

    // 计算多个模型的组合边界框
    BoundingBox calculateCombinedBounds(
        const std::vector<DBInstanceID>& modelIds) const;

    // 获取模型在指定轴向的最小/最大值
    float getAxisMinValue(const DBInstanceID& modelId, AxisType axis) const;
    float getAxisMaxValue(const DBInstanceID& modelId, AxisType axis) const;
    float getAxisCenter(const DBInstanceID& modelId, AxisType axis) const;

private:
    // 获取模型数据库对象
    std::shared_ptr<AutoRegisterDB> getModelDBObject(const DBInstanceID& modelId) const;

};
