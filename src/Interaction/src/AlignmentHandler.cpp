#include "AlignmentHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_PERSISTENT_INTERACTION_ACTION(
    AlignmentHandler, "align.to_bed", "align.center", "align.smart", "align.axis")
#include "ActionHandlerRegistry.hpp"
#include "BoundsCalculator.hpp"
#include "TransformCalculator.hpp"
#include "ActionContext.hpp"
#include <DocumentManager.hpp>
#include <ActorDB.hpp>
#include <ModelInstanceDB.hpp>
#include <PrintBedCollisionDetector.hpp>
#include <PrintBedDB.hpp>
#include <TransactionManager.hpp>
#include <SelectionBridge.hpp>
#include "Foundation/Log.h"
#include "NotificationManager.h"
#include "AIDescriptorHelper.hpp"
#include <QVariantList>
#include <algorithm>
#include <limits>
// #include <QMessageBox> // 移除：在QML环境中不能使用QMessageBox

namespace {
std::vector<DBInstanceID> parseModelIdsForAI(const QVariantMap& params) {
    std::vector<DBInstanceID> modelIds;
    const auto appendId = [&modelIds](int rawId) {
        if (rawId > 0) {
            modelIds.emplace_back(rawId);
        }
    };

    appendId(params.value("modelId", params.value("dbId", 0)).toInt());
    for (const auto& value : params.value("modelIds").toList()) {
        appendId(value.toInt());
    }
    for (const auto& value : params.value("dbIds").toList()) {
        appendId(value.toInt());
    }

    std::sort(modelIds.begin(), modelIds.end(),
              [](const DBInstanceID& a, const DBInstanceID& b) {
                  return a.getValue() < b.getValue();
              });
    modelIds.erase(std::unique(modelIds.begin(), modelIds.end(),
                               [](const DBInstanceID& a, const DBInstanceID& b) {
                                   return a.getValue() == b.getValue();
                               }),
                   modelIds.end());
    return modelIds;
}

} // namespace

AlignmentHandler::AlignmentHandler(QObject* parent)
    : StandardActionHandler(parent)
    , m_boundsCalculator(std::make_unique<BoundsCalculator>())
    , m_transformCalculator(std::make_unique<TransformCalculator>()) {
}

AlignmentHandler::~AlignmentHandler() = default;

void AlignmentHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    QString actionCode = getActionCode();
    auto params = getParams();

    LOG_DEBUG("AlignmentHandler processing action: {}", actionCode.toStdString());

    try {
        if (actionCode == "align.to_bed") {
            auto selectedIds = getSelectedModelIds();
            if (selectedIds.empty()) {
                showError("Align to Bed", "No models selected. Please select one or more models first.");
                return;
            }

            bool preserveRelative = params.value("preserveRelative", false).toBool();

            auto result = alignToBed(selectedIds, preserveRelative);
            if (result.success) {
                showSuccess("Align to Bed", result.processedCount);
            } else {
                showError("Align to Bed", result.errorMessage);
            }

        } else if (actionCode == "align.center") {
            auto selectedIds = getSelectedModelIds();
            if (selectedIds.empty()) {
                showError("Center", "No models selected. Please select one or more models first.");
                return;
            }

            auto bed = getBedDimensions(selectedIds);

            QString modeStr = params.value("mode", "xy").toString();
            CenterMode mode = CenterMode::XY;
            if (modeStr == "x") mode = CenterMode::XOnly;
            else if (modeStr == "y") mode = CenterMode::YOnly;

            auto result = centerToBed(selectedIds, bed, mode);
            if (result.success) {
                showSuccess("Center", result.processedCount);
            } else {
                showError("Center", result.errorMessage);
            }

        } else if (actionCode == "align.smart") {
            auto selectedIds = getSelectedModelIds();
            if (selectedIds.empty()) {
                showError("Smart Place", "No models selected. Please select one or more models first.");
                return;
            }

            auto bed = getBedDimensions(selectedIds);

            auto result = smartPlace(selectedIds, bed);
            if (result.success) {
                showSuccess("Smart Place", result.processedCount);
            } else {
                showError("Smart Place", result.errorMessage);
            }

        } else if (actionCode == "align.axis") {
            auto selectedIds = getSelectedModelIds();
            if (selectedIds.empty()) {
                showError("Axis Alignment", "No models selected. Please select one or more models first.");
                return;
            }

            QString axisStr = params.value("axis").isValid() ? params.value("axis").toString() : "x";
            QString modeStr = params.value("mode").isValid() ? params.value("mode").toString() : "center";

            AxisType axis;
            if (axisStr == "y") axis = AxisType::Y;
            else if (axisStr == "z") axis = AxisType::Z;
            else axis = AxisType::X;

            AlignMode mode;
            if (modeStr == "min") mode = AlignMode::Min;
            else if (modeStr == "max") mode = AlignMode::Max;
            else if (modeStr == "first") mode = AlignMode::First;
            else mode = AlignMode::Center;

            auto result = alignOnAxis(selectedIds, axis, mode);
            if (result.success) {
                showSuccess("Axis Alignment", result.processedCount);
            } else {
                showError("Axis Alignment", result.errorMessage);
            }

        } else {
            LOG_WARN("Unknown alignment action: {}", actionCode.toStdString());
        }

    } catch (const std::exception& e) {
        LOG_ERROR("AlignmentHandler exception: {}", e.what());
        showError("Alignment Operation", QString("Unexpected error: %1").arg(e.what()));
    }
}

void AlignmentHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    if (!context) {
        StandardActionHandler::onExit();
        return;
    }

    const QString actionCode = getActionCode();
    const QVariantMap params = getParams();

    std::vector<DBInstanceID> targetIds = parseModelIdsForAI(params);
    if (targetIds.empty()) {
        targetIds = getSelectedModelIds();
    }
    if (targetIds.empty()) {
        context->setError(ActionErrorCode::TargetRequired, "No target model ids and no current selection");
        showError("AI Alignment", context->getError());
        StandardActionHandler::onExit();
        return;
    }

    AlignmentResult result;
    bool handled = true;
    if (actionCode == "align.to_bed") {
        const bool preserveRelative = params.value("preserveRelative", false).toBool();
        result = alignToBed(targetIds, preserveRelative);
    } else if (actionCode == "align.center") {
        const BedDimensions bed = getBedDimensions(targetIds);
        const QString modeStr = params.value("mode", "xy").toString().trimmed().toLower();
        CenterMode mode = CenterMode::XY;
        if (modeStr == "x") {
            mode = CenterMode::XOnly;
        } else if (modeStr == "y") {
            mode = CenterMode::YOnly;
        } else if (modeStr != "xy") {
            context->setError(ActionErrorCode::InvalidParams, "align.center mode must be one of x/y/xy");
            handled = false;
        }
        if (handled) {
            result = centerToBed(targetIds, bed, mode);
        }
    } else if (actionCode == "align.smart") {
        const BedDimensions bed = getBedDimensions(targetIds);
        result = smartPlace(targetIds, bed);
    } else if (actionCode == "align.axis") {
        const QString axisStr = params.value("axis", "x").toString().trimmed().toLower();
        const QString modeStr = params.value("mode", "center").toString().trimmed().toLower();

        AxisType axis = AxisType::X;
        if (axisStr == "y") {
            axis = AxisType::Y;
        } else if (axisStr == "z") {
            axis = AxisType::Z;
        } else if (axisStr != "x") {
            context->setError(ActionErrorCode::InvalidParams, "align.axis axis must be one of x/y/z");
            handled = false;
        }

        AlignMode mode = AlignMode::Center;
        if (handled) {
            if (modeStr == "min") {
                mode = AlignMode::Min;
            } else if (modeStr == "max") {
                mode = AlignMode::Max;
            } else if (modeStr == "first") {
                mode = AlignMode::First;
            } else if (modeStr != "center") {
                context->setError(ActionErrorCode::InvalidParams, "align.axis mode must be one of center/min/max/first");
                handled = false;
            }
        }

        DBInstanceID referenceModel;
        if (handled) {
            const int referenceId = params.value("referenceModelId",
                                                 params.value("referenceDbId", 0))
                                        .toInt();
            if (referenceId > 0) {
                referenceModel = DBInstanceID(referenceId);
            }
            result = alignOnAxis(targetIds, axis, mode, referenceModel);
        }
    } else {
        handled = false;
        context->setError(ActionErrorCode::InvalidParams,
                          QString("AI invoke not supported for action '%1'").arg(actionCode));
    }

    if (!handled) {
        showError("AI Alignment", context->getError());
        StandardActionHandler::onExit();
        return;
    }

    if (!result.success) {
        context->setError(ActionErrorCode::Internal, result.errorMessage.isEmpty() ? "Alignment failed" : result.errorMessage);
        showError("AI Alignment", context->getError());
        StandardActionHandler::onExit();
        return;
    }

    context->setResult(QVariantMap{
        {"actionCode", actionCode},
        {"processedCount", result.processedCount}
    });
    showSuccess("AI Alignment", result.processedCount);
    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& AlignmentHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"align.to_bed", makeDescriptor(
            "align.to_bed",
            "Align To Bed",
            "Align selected or specified models to print bed.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"preserveRelative", "bool", false}
            },
            {"align", "layout", "scene"}
        )},
        {"align.center", makeDescriptor(
            "align.center",
            "Center Models",
            "Center selected or specified models on print bed.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"mode", "string", false, QVariant("xy"), {"x", "y", "xy"}}
            },
            {"align", "layout", "scene"}
        )},
        {"align.smart", makeDescriptor(
            "align.smart",
            "Smart Place",
            "Align to bed then center selected or specified models.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false}
            },
            {"align", "layout", "scene"}
        )},
        {"align.axis", makeDescriptor(
            "align.axis",
            "Axis Align",
            "Align selected or specified models on x/y/z by mode center|min|max|first.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"axis", "string", false, QVariant("x"), {"x", "y", "z"}},
                {"mode", "string", false, QVariant("center"), {"center", "min", "max", "first"}},
                {"referenceModelId", "number", false},
                {"referenceDbId", "number", false}
            },
            {"align", "layout", "scene"}
        )}
    };

    return table;
}

AlignmentHandler::AlignmentResult AlignmentHandler::alignToBed(
    const std::vector<DBInstanceID>& modelIds,
    bool preserveRelativePositions) {

    AlignmentResult result;
    result.success = false;
    result.processedCount = 0;

    if (modelIds.empty()) {
        result.errorMessage = "No models selected";
        return result;
    }

    try {
        TransactionGuard guard("Align to Bed");
        const BedDimensions bed = getBedDimensions(modelIds);

        // 计算所有模型的边界框
        std::vector<BoundingBox> currentBounds;
        std::vector<Transform> currentTransforms;

        for (const auto& modelId : modelIds) {
            if (!isValidModelId(modelId)) {
                result.errorMessage = QString("Invalid model ID: %1").arg(modelId.getValue());
                return result;
            }

            auto bounds = getModelBounds(modelId);
            auto transform = getModelTransform(modelId);

            // 对齐前数据记录（仅在调试模式下）
            LOG_DEBUG("Align to bed - Model ID: {}, Position: ({:.2f}, {:.2f}, {:.2f})",
                     modelId.getValue(), transform.getPosition().x, transform.getPosition().y, transform.getPosition().z);

            currentBounds.push_back(bounds);
            currentTransforms.push_back(transform);
        }

        if (currentBounds.empty()) {
            result.errorMessage = "No valid models found";
            return result;
        }

        // 计算新的变换
        std::vector<Transform> newTransforms;
        newTransforms.reserve(modelIds.size());

        if (preserveRelativePositions && modelIds.size() > 1) {
            // 保持相对位置：将整体最低点对齐到平台
            float globalMinZ = std::numeric_limits<float>::max();
            for (const auto& bounds : currentBounds) {
                if (bounds.valid) {
                    globalMinZ = std::min(globalMinZ, bounds.min.z);
                }
            }

            float deltaZ = bed.baseZ - globalMinZ;

            for (size_t i = 0; i < currentTransforms.size(); ++i) {
                auto newTransform = currentTransforms[i];
                newTransform.translate(Vector3(0, 0, deltaZ));
                newTransforms.push_back(newTransform);
            }
        } else {
            // 独立对齐：每个模型独立对齐到平台
            for (size_t i = 0; i < currentBounds.size(); ++i) {
                auto newTransform = m_transformCalculator->calculateAlignToBedTransform(
                    currentBounds[i], currentTransforms[i], bed.baseZ);
                newTransforms.push_back(newTransform);
            }
        }

        if (!canApplyTransforms(modelIds, newTransforms, result.errorMessage)) {
            return result;
        }

        // 应用变换
        for (size_t i = 0; i < modelIds.size() && i < newTransforms.size(); ++i) {
            if (applyTransformToModel(modelIds[i], newTransforms[i])) {
                result.processedCount++;
            }
        }

        result.newTransforms = newTransforms;
        result.combinedBounds = m_boundsCalculator->calculateCombinedBounds(modelIds);
        result.success = (result.processedCount == modelIds.size());

        guard.commit();

    } catch (const std::exception& e) {
        result.errorMessage = QString("Alignment failed: %1").arg(e.what());
        LOG_ERROR("AlignToBed failed: {}", e.what());
    }

    return result;
}

AlignmentHandler::AlignmentResult AlignmentHandler::centerToBed(
    const std::vector<DBInstanceID>& modelIds,
    const BedDimensions& bed,
    CenterMode mode) {

    AlignmentResult result;
    result.success = false;
    result.processedCount = 0;

    if (modelIds.empty()) {
        result.errorMessage = "No models selected";
        return result;
    }

    try {
        TransactionGuard guard("Center to Bed");

        // 计算组合边界框
        auto combinedBounds = m_boundsCalculator->calculateCombinedBounds(modelIds);
        if (!combinedBounds.valid) {
            result.errorMessage = "Failed to calculate combined bounds";
            return result;
        }

        // 计算目标中心位置
        Vector3 bedCenter(bed.centerX, bed.centerY, 0);
        Vector3 modelCenter = combinedBounds.getCenter();

        // 计算移动距离
        Vector3 deltaMove(0, 0, 0);
        switch (mode) {
            case CenterMode::XOnly:
                deltaMove.x = bedCenter.x - modelCenter.x;
                break;
            case CenterMode::YOnly:
                deltaMove.y = bedCenter.y - modelCenter.y;
                break;
            case CenterMode::XY:
                deltaMove.x = bedCenter.x - modelCenter.x;
                deltaMove.y = bedCenter.y - modelCenter.y;
                break;
        }

        // 应用移动到所有模型
        std::vector<Transform> newTransforms;
        for (const auto& modelId : modelIds) {
            if (!isValidModelId(modelId)) {
                result.errorMessage = QString("Invalid model ID: %1").arg(modelId.getValue());
                return result;
            }

            auto currentTransform = getModelTransform(modelId);
            currentTransform.translate(deltaMove);
            newTransforms.push_back(currentTransform);
        }

        if (!canApplyTransforms(modelIds, newTransforms, result.errorMessage)) {
            return result;
        }

        // 应用变换
        for (size_t i = 0; i < modelIds.size() && i < newTransforms.size(); ++i) {
            if (applyTransformToModel(modelIds[i], newTransforms[i])) {
                result.processedCount++;
            }
        }

        result.newTransforms = newTransforms;
        result.combinedBounds = m_boundsCalculator->calculateCombinedBounds(modelIds);
        result.success = (result.processedCount == modelIds.size());

        guard.commit();

    } catch (const std::exception& e) {
        result.errorMessage = QString("Centering failed: %1").arg(e.what());
        LOG_ERROR("CenterToBed failed: {}", e.what());
    }

    return result;
}

AlignmentHandler::AlignmentResult AlignmentHandler::smartPlace(
    const std::vector<DBInstanceID>& modelIds,
    const BedDimensions& bed) {

    AlignmentResult result;
    result.success = false;
    result.processedCount = 0;

    if (modelIds.empty()) {
        result.errorMessage = "No models selected";
        return result;
    }

    try {
        TransactionGuard guard("Smart Place");

        // 首先对齐到平台
        auto alignResult = alignToBed(modelIds, true); // 保持相对位置
        if (!alignResult.success) {
            result.errorMessage = QString("Align to bed failed: %1").arg(alignResult.errorMessage);
            return result;
        }

        // 然后居中到打印床
        auto centerResult = centerToBed(modelIds, bed, CenterMode::XY);
        if (!centerResult.success) {
            result.errorMessage = QString("Center to bed failed: %1").arg(centerResult.errorMessage);
            return result;
        }

        // 最终验证
        auto finalBounds = m_boundsCalculator->calculateCombinedBounds(modelIds);

        result.success = true;
        result.processedCount = modelIds.size();
        result.combinedBounds = finalBounds;
        result.newTransforms = centerResult.newTransforms;

        guard.commit();

    } catch (const std::exception& e) {
        result.errorMessage = QString("Smart place failed: %1").arg(e.what());
        LOG_ERROR("SmartPlace failed: {}", e.what());
    }

    return result;
}

AlignmentHandler::AlignmentResult AlignmentHandler::alignOnAxis(
    const std::vector<DBInstanceID>& modelIds,
    AxisType axis,
    AlignMode mode,
    const DBInstanceID& referenceModel) {

    AlignmentResult result;
    result.success = false;
    result.processedCount = 0;

    if (modelIds.size() < 2) {
        result.errorMessage = "Need at least 2 models for axis alignment";
        return result;
    }

    try {
        TransactionGuard guard("Axis Alignment");

        // 计算目标位置
        float targetValue = 0.0f;

        switch (mode) {
            case AlignMode::Min: {
                targetValue = std::numeric_limits<float>::max();
                for (const auto& modelId : modelIds) {
                    float value = m_boundsCalculator->getAxisMinValue(modelId, axis);
                    targetValue = std::min(targetValue, value);
                }
                break;
            }
            case AlignMode::Max: {
                targetValue = std::numeric_limits<float>::lowest();
                for (const auto& modelId : modelIds) {
                    float value = m_boundsCalculator->getAxisMaxValue(modelId, axis);
                    targetValue = std::max(targetValue, value);
                }
                break;
            }
            case AlignMode::Center: {
                float minVal = std::numeric_limits<float>::max();
                float maxVal = std::numeric_limits<float>::lowest();
                for (const auto& modelId : modelIds) {
                    float centerVal = m_boundsCalculator->getAxisCenter(modelId, axis);
                    minVal = std::min(minVal, centerVal);
                    maxVal = std::max(maxVal, centerVal);
                }
                targetValue = (minVal + maxVal) / 2.0f;
                break;
            }
            case AlignMode::First: {
                if (referenceModel.isValid()) {
                    targetValue = m_boundsCalculator->getAxisCenter(referenceModel, axis);
                } else if (!modelIds.empty()) {
                    targetValue = m_boundsCalculator->getAxisCenter(modelIds[0], axis);
                }
                break;
            }
        }

        // 计算每个模型的新变换
        std::vector<Transform> newTransforms;
        for (const auto& modelId : modelIds) {
            if (!isValidModelId(modelId)) {
                result.errorMessage = QString("Invalid model ID: %1").arg(modelId.getValue());
                return result;
            }

            auto currentBounds = getModelBounds(modelId);
            auto currentTransform = getModelTransform(modelId);

            auto newTransform = m_transformCalculator->calculateAxisAlignTransform(
                currentBounds, currentTransform, targetValue, axis);

            newTransforms.push_back(newTransform);
        }

        if (!canApplyTransforms(modelIds, newTransforms, result.errorMessage)) {
            return result;
        }

        // 应用变换
        for (size_t i = 0; i < modelIds.size() && i < newTransforms.size(); ++i) {
            if (applyTransformToModel(modelIds[i], newTransforms[i])) {
                result.processedCount++;
            }
        }

        result.success = (result.processedCount == modelIds.size());
        result.newTransforms = newTransforms;
        result.combinedBounds = m_boundsCalculator->calculateCombinedBounds(modelIds);

        guard.commit();

    } catch (const std::exception& e) {
        result.errorMessage = QString("Axis alignment failed: %1").arg(e.what());
        LOG_ERROR("AlignOnAxis failed: {}", e.what());
    }

    return result;
}

// 辅助方法实现
std::vector<DBInstanceID> AlignmentHandler::getSelectedModelIds() const {
    auto* selectionBridge = SelectionBridge::instance();
    if (!selectionBridge) {
        LOG_WARN("SelectionBridge not available");
        return {};
    }

    const auto& selectedIds = selectionBridge->getSelectedIds();
    LOG_DEBUG("Getting selected model IDs: {} models selected", selectedIds.size());

    return selectedIds;
}

AlignmentHandler::BedDimensions AlignmentHandler::getBedDimensions(
    const std::vector<DBInstanceID>& modelIds) const {
    BedDimensions bed;
    const auto printBed = getPrintBedForModels(modelIds);
    if (!printBed) return bed;

    bed.width = printBed->getWidth();
    bed.height = printBed->getHeight();
    bed.centerX = printBed->getCenter().x;
    bed.centerY = printBed->getCenter().y;
    bed.baseZ = printBed->getOrigin().z;
    return bed;
}

std::shared_ptr<PrintBedDB> AlignmentHandler::getPrintBedForModels(
    const std::vector<DBInstanceID>& modelIds) const {
    auto* document = DocumentManager::instance();
    if (!document || modelIds.empty()) return nullptr;

    DBInstanceID bedId;
    for (const DBInstanceID& modelId : modelIds) {
        const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(
            document->getDBInstance(modelId));
        if (!instance || !instance->getParentPrintBedDBId().isValid()) {
            return nullptr;
        }
        if (bedId.isValid() && bedId != instance->getParentPrintBedDBId()) {
            return nullptr;
        }
        bedId = instance->getParentPrintBedDBId();
    }

    return std::dynamic_pointer_cast<PrintBedDB>(
        document->getDBInstance(bedId));
}

bool AlignmentHandler::canApplyTransforms(
    const std::vector<DBInstanceID>& modelIds,
    const std::vector<Transform>& transforms,
    QString& error) const {
    if (modelIds.size() != transforms.size()) {
        error = "Model and transform counts do not match";
        return false;
    }

    auto* document = DocumentManager::instance();
    const auto printBed = getPrintBedForModels(modelIds);
    if (!document || !printBed) {
        error = "Print bed is not available";
        return false;
    }

    const auto volume = PrintBedCollisionDetector::buildVolume(*printBed);
    for (std::size_t i = 0; i < modelIds.size(); ++i) {
        const auto actor = std::dynamic_pointer_cast<ActorDB>(
            document->getDBInstance(modelIds[i]));
        if (!actor) {
            error = QString("Invalid model ID: %1").arg(modelIds[i].getValue());
            return false;
        }

        const auto currentBounds = actor->worldBounds();
        const auto candidateBounds = actor->worldBoundsAt(
            transforms[i].getMatrix());
        if (!currentBounds.valid || !candidateBounds.valid) {
            error = QString("Model %1 has no valid bounds").arg(modelIds[i].getValue());
            return false;
        }

        const std::uint32_t currentMask =
            PrintBedCollisionDetector::detect(currentBounds, volume);
        const std::uint32_t candidateMask =
            PrintBedCollisionDetector::detect(candidateBounds, volume);
        if ((candidateMask & ~currentMask) != PrintBedDB::CollisionNone) {
            error = QString("Alignment would move model %1 outside the build volume")
                        .arg(modelIds[i].getValue());
            return false;
        }
    }

    return true;
}

Transform AlignmentHandler::getModelTransform(const DBInstanceID& modelId) const {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("DocumentManager not available");
        return Transform();
    }

    auto dbObject = docManager->getDBInstance(modelId);
    if (!dbObject) {
        LOG_ERROR("Model not found: {}", modelId.getValue());
        return Transform();
    }

    auto actorDB = std::dynamic_pointer_cast<ActorDB>(dbObject);
    if (!actorDB) {
        LOG_ERROR("Object is not an ActorDB: {}", modelId.getValue());
        return Transform();
    }

    return actorDB->getTransform();
}

bool AlignmentHandler::applyTransformToModel(const DBInstanceID& modelId, const Transform& transform) {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("DocumentManager not available");
        return false;
    }

    auto dbObject = docManager->getDBInstance(modelId);
    if (!dbObject) {
        LOG_ERROR("Model not found: {}", modelId.getValue());
        return false;
    }

    auto actorDB = std::dynamic_pointer_cast<ActorDB>(dbObject);
    if (!actorDB) {
        LOG_ERROR("Object is not an ActorDB: {}", modelId.getValue());
        return false;
    }

    try {
        actorDB->setTransform(transform);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to apply transform to model {}: {}", modelId.getValue(), e.what());
        return false;
    }
}

AlignmentHandler::BoundingBox AlignmentHandler::getModelBounds(const DBInstanceID& modelId) const {
    return m_boundsCalculator->calculateWorldBounds(modelId);
}

bool AlignmentHandler::isValidModelId(const DBInstanceID& modelId) const {
    auto docManager = DocumentManager::instance();
    if (!docManager) return false;

    auto dbObject = docManager->getDBInstance(modelId);
    return dbObject && std::dynamic_pointer_cast<ActorDB>(dbObject);
}

void AlignmentHandler::showError(const QString& operation, const QString& error) const {
    LOG_ERROR("Alignment error - {}: {}", operation.toStdString(), error.toStdString());

    // 使用NotificationManager显示错误toast通知
    NotificationManager::instance()->showError(operation, error);
}

void AlignmentHandler::showSuccess(const QString& operation, int modelCount) const {
    QString message = QString("Successfully %1 %2 model%3")
                     .arg(operation.toLower())
                     .arg(modelCount)
                     .arg(modelCount > 1 ? "s" : "");

    LOG_INFO("Alignment success: {}", message.toStdString());

    // 使用NotificationManager显示成功toast通知
    NotificationManager::instance()->showSuccess(operation, message);
}
