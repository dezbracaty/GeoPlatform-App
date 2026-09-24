#pragma once

#include "StandardActionHandler.hpp"
#include "ActionContext.hpp"
#include "AlignmentTypes.hpp"
#include <SystemTypes.hpp>
#include <Transform.hpp>
#include <memory>
#include <vector>
#include <QString>

// 前向声明
class BoundsCalculator;
class TransformCalculator;
class PrintBedDB;

/**
 * @brief 对齐工具处理器
 *
 * 提供3D打印模型的对齐功能，包括：
 * - 对齐到平台 (Align to Bed)
 * - 居中 (Center to Bed)
 * - 智能摆放 (Smart Place)
 * - 轴向对齐 (Axis Align)
 */
class AlignmentHandler : public StandardActionHandler {
    Q_OBJECT

public:
    using BoundingBox = Alignment::BoundingBox;
    using BedDimensions = Alignment::BedDimensions;
    using CenterMode = Alignment::CenterMode;
    using AxisType = Alignment::Axis;
    using AlignMode = Alignment::AlignMode;

    // 对齐操作结果
    struct AlignmentResult {
        bool success = false;
        QString errorMessage;
        std::vector<Transform> newTransforms;
        BoundingBox combinedBounds;
        int processedCount = 0;
    };


public:
    explicit AlignmentHandler(QObject* parent = nullptr);
    virtual ~AlignmentHandler();

    // 重写StandardActionHandler方法
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;

protected:
    // AI 描述符表
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

    // 主要对齐操作方法
    AlignmentResult alignToBed(
        const std::vector<DBInstanceID>& modelIds,
        bool preserveRelativePositions = false
    );

    AlignmentResult centerToBed(
        const std::vector<DBInstanceID>& modelIds,
        const BedDimensions& bed,
        CenterMode mode = CenterMode::XY
    );

    AlignmentResult smartPlace(
        const std::vector<DBInstanceID>& modelIds,
        const BedDimensions& bed
    );

    AlignmentResult alignOnAxis(
        const std::vector<DBInstanceID>& modelIds,
        AxisType axis,
        AlignMode mode,
        const DBInstanceID& referenceModel = DBInstanceID()
    );

private:
    // 辅助方法
    std::vector<DBInstanceID> getSelectedModelIds() const;
    BedDimensions getBedDimensions(
        const std::vector<DBInstanceID>& modelIds) const;
    Transform getModelTransform(const DBInstanceID& modelId) const;
    bool applyTransformToModel(const DBInstanceID& modelId, const Transform& transform);
    BoundingBox getModelBounds(const DBInstanceID& modelId) const;
    bool isValidModelId(const DBInstanceID& modelId) const;
    std::shared_ptr<PrintBedDB> getPrintBedForModels(
        const std::vector<DBInstanceID>& modelIds) const;
    bool canApplyTransforms(
        const std::vector<DBInstanceID>& modelIds,
        const std::vector<Transform>& transforms,
        QString& error) const;

    // 错误处理
    void showError(const QString& operation, const QString& error) const;
    void showSuccess(const QString& operation, int modelCount) const;

private:
    std::unique_ptr<BoundsCalculator> m_boundsCalculator;
    std::unique_ptr<TransformCalculator> m_transformCalculator;
};
