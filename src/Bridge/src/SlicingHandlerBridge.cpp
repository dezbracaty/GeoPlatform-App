#include "SlicingHandlerBridge.hpp"
#include "BridgeRegistration.hpp"
#include "ActorDB.hpp"
#include "ModelInstanceDB.hpp"
#include "ToolpathPreviewDB.hpp"
#include "DebugActorDB.hpp"
#include "ManualSupportDB.hpp"
#include "ManualSupportRelations.hpp"
#include "DocumentManager.hpp"
#include "TransactionManager.hpp"
#include "Foundation/Log.h"
#include <algorithm>
#include <QtGlobal>

SlicingHandlerBridge::SlicingHandlerBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    LOG_INFO("SlicingHandlerBridge created");
}

SlicingHandlerBridge* SlicingHandlerBridge::instance() {
    static SlicingHandlerBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new SlicingHandlerBridge();
    }
    return s_instance;
}

SlicingHandlerBridge* SlicingHandlerBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    SlicingHandlerBridge* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

void SlicingHandlerBridge::setDebugMode(bool enabled) {
    if (m_debugMode != enabled) {
        m_debugMode = enabled;
        LOG_INFO("🔧 调试模式设置为: {}", enabled ? "启用" : "禁用");
        emit debugModeChanged();
    }
}

void SlicingHandlerBridge::setDebugTargetLayer(int layer) {
    if (m_debugTargetLayer != layer) {
        m_debugTargetLayer = layer;
        LOG_INFO("🎯 调试目标层设置为: {}", layer);
        emit debugTargetLayerChanged();
    }
}

void SlicingHandlerBridge::clearDebugActors() {
    LOG_INFO("🗑️ 请求清除调试可视化对象");
    emit clearDebugActorsRequested();
}

bool SlicingHandlerBridge::manualSupportApplySettings(const QVariantMap& settings) {
    emit manualSupportApplySettingsRequested(settings);
    return true;
}

bool SlicingHandlerBridge::manualSupportSetDebugVisibility(const QVariantMap& visibility) {
    emit manualSupportDebugVisibilityRequested(visibility);
    return true;
}

bool SlicingHandlerBridge::manualSupportClearStrokes() {
    emit manualSupportClearStrokesRequested();
    return true;
}

bool SlicingHandlerBridge::manualSupportCommitStroke() {
    emit manualSupportCommitStrokeRequested();
    return true;
}

bool SlicingHandlerBridge::manualSupportHasCommittedMesh(qulonglong modelId) const {
    if (modelId == 0) {
        return false;
    }

    auto* document = DocumentManager::instance();
    const auto support = ManualSupportRelations::findForModel(
        DBInstanceID(static_cast<std::uint64_t>(modelId)));
    return support && support->hasGeneratedGeometry();
}

void SlicingHandlerBridge::updateDebugActorCount(int count) {
    bool hadActors = (m_debugActorCount > 0);
    m_debugActorCount = count;
    bool hasActors = (m_debugActorCount > 0);

    if (hadActors != hasActors) {
        emit hasDebugActorsChanged();
    }
}

void SlicingHandlerBridge::setShowOriginalModel(bool show) {
    const bool changed = (m_showOriginalModel != show);

    LOG_INFO("🎨 原始模型可见性: {}", show ? "显示" : "隐藏");

    TransactionGuard guard(tr("Toggle original model visibility").toStdString());
    m_showOriginalModel = show;
    if (m_currentOriginalModelActor) {
        m_currentOriginalModelActor->setVisible(show);
    }

    if (changed) {
        emit showOriginalModelChanged();
    }
}

void SlicingHandlerBridge::setShowSlicingModel(bool show) {
    if (m_showSlicingModel == show) return;

    LOG_INFO("🎨 toolpath preview visibility: {}", show ? "show" : "hide");

    TransactionGuard guard(tr("Toggle sliced model visibility").toStdString());
    if (m_currentPreview) {
        m_currentPreview->setVisible(show);
    }

    m_showSlicingModel = show;
    emit showSlicingModelChanged();
}

void SlicingHandlerBridge::setShowDebugData(bool show) {
    if (m_showDebugData == show) return;

    LOG_INFO("🎨 调试数据可见性: {}", show ? "显示" : "隐藏");

    TransactionGuard guard(tr("Toggle debug data visibility").toStdString());
    for (auto& debugActor : m_debugActors) {
        if (debugActor) {
            debugActor->setVisible(show);
        }
    }

    m_showDebugData = show;
    emit showDebugDataChanged();
}

void SlicingHandlerBridge::setManualSupportActive(bool active) {
    if (m_manualSupportActive == active) {
        return;
    }
    m_manualSupportActive = active;
    emit manualSupportActiveChanged();
}

void SlicingHandlerBridge::setManualSupportPaintMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    if (m_manualSupportPaintMode == normalized) {
        return;
    }
    m_manualSupportPaintMode = normalized;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportBrushShape(const QString& shape) {
    const QString normalized = shape.trimmed().toLower();
    if (m_manualSupportBrushShape == normalized) {
        return;
    }
    m_manualSupportBrushShape = normalized;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportBrushSizeMm(double sizeMm) {
    const double clamped = std::clamp(sizeMm, 1.0, 30.0);
    if (m_manualSupportBrushSizeMm == clamped) {
        return;
    }
    m_manualSupportBrushSizeMm = clamped;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportDensityPercent(double densityPercent) {
    const double clamped = std::clamp(densityPercent, 0.0, 100.0);
    if (m_manualSupportDensityPercent == clamped) {
        return;
    }
    m_manualSupportDensityPercent = clamped;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportSmartFill(bool enabled) {
    if (m_manualSupportSmartFill == enabled) {
        return;
    }
    m_manualSupportSmartFill = enabled;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportClipToOverhang(bool enabled) {
    if (m_manualSupportClipToOverhang == enabled) {
        return;
    }
    m_manualSupportClipToOverhang = enabled;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportSmartFillAngleDeg(double degree) {
    const double clamped = std::clamp(degree, 0.0, 90.0);
    if (m_manualSupportSmartFillAngleDeg == clamped) {
        return;
    }
    m_manualSupportSmartFillAngleDeg = clamped;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportGapArea(double area) {
    const double clamped = std::clamp(area, 0.0, 5.0);
    if (m_manualSupportGapArea == clamped) {
        return;
    }
    m_manualSupportGapArea = clamped;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportSectionViewRatio(double ratio) {
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    if (m_manualSupportSectionViewRatio == clamped) {
        return;
    }
    m_manualSupportSectionViewRatio = clamped;
    emit manualSupportSettingsChanged();
}

void SlicingHandlerBridge::setManualSupportContactDiagnostics(bool hasContactPatch,
                                                              int contactTriangleCount,
                                                              int lowerModelHitSampleCount,
                                                              int totalBottomSampleCount,
                                                              double maxLowerModelHitHeightMm) {
    const int clampedContactTriangleCount = std::max(contactTriangleCount, 0);
    const int clampedLowerModelHitSampleCount = std::max(lowerModelHitSampleCount, 0);
    const int clampedTotalBottomSampleCount = std::max(totalBottomSampleCount, 0);
    const double clampedMaxLowerModelHitHeightMm = std::max(maxLowerModelHitHeightMm, 0.0);

    if (m_manualSupportHasContactPatch == hasContactPatch &&
        m_manualSupportContactTriangleCount == clampedContactTriangleCount &&
        m_manualSupportLowerModelHitSampleCount == clampedLowerModelHitSampleCount &&
        m_manualSupportTotalBottomSampleCount == clampedTotalBottomSampleCount &&
        qFuzzyCompare(m_manualSupportMaxLowerModelHitHeightMm + 1.0,
                      clampedMaxLowerModelHitHeightMm + 1.0)) {
        return;
    }

    m_manualSupportHasContactPatch = hasContactPatch;
    m_manualSupportContactTriangleCount = clampedContactTriangleCount;
    m_manualSupportLowerModelHitSampleCount = clampedLowerModelHitSampleCount;
    m_manualSupportTotalBottomSampleCount = clampedTotalBottomSampleCount;
    m_manualSupportMaxLowerModelHitHeightMm = clampedMaxLowerModelHitHeightMm;
    emit manualSupportInspectionChanged();
}

void SlicingHandlerBridge::setManualDebugShowActor(bool enabled) {
    if (m_manualDebugShowActor == enabled) {
        return;
    }
    m_manualDebugShowActor = enabled;
    emit manualSupportDebugVisibilityChanged();
}

void SlicingHandlerBridge::setManualDebugShowStrokePolyline(bool enabled) {
    if (m_manualDebugShowStrokePolyline == enabled) {
        return;
    }
    m_manualDebugShowStrokePolyline = enabled;
    emit manualSupportDebugVisibilityChanged();
}

void SlicingHandlerBridge::setManualDebugShowStrokePoints(bool enabled) {
    if (m_manualDebugShowStrokePoints == enabled) {
        return;
    }
    m_manualDebugShowStrokePoints = enabled;
    emit manualSupportDebugVisibilityChanged();
}

void SlicingHandlerBridge::setManualDebugShowPickNormals(bool enabled) {
    if (m_manualDebugShowPickNormals == enabled) {
        return;
    }
    m_manualDebugShowPickNormals = enabled;
    emit manualSupportDebugVisibilityChanged();
}

void SlicingHandlerBridge::setManualDebugShowProjectedRing(bool enabled) {
    if (m_manualDebugShowProjectedRing == enabled) {
        return;
    }
    m_manualDebugShowProjectedRing = enabled;
    emit manualSupportDebugVisibilityChanged();
}

void SlicingHandlerBridge::setManualDebugShowMergedSelectionSurfaceOutline(bool enabled) {
    if (m_manualDebugShowMergedSelectionSurfaceOutline == enabled) {
        return;
    }
    m_manualDebugShowMergedSelectionSurfaceOutline = enabled;
    emit manualSupportDebugVisibilityChanged();
}

void SlicingHandlerBridge::setManualDebugShowContactPatch(bool enabled) {
    if (m_manualDebugShowContactPatch == enabled) {
        return;
    }
    m_manualDebugShowContactPatch = enabled;
    emit manualSupportDebugVisibilityChanged();
}

// ============================================================================
// DB 对象管理方法
// ============================================================================

void SlicingHandlerBridge::setOriginalModelActor(std::shared_ptr<ActorDB> actor) {
    m_currentOriginalModelActor = actor;
    if (m_currentOriginalModelActor) {
        TransactionGuard guard(tr("Synchronize original model visibility").toStdString());
        m_currentOriginalModelActor->setVisible(m_showOriginalModel);
    }
}

void SlicingHandlerBridge::setModelInstance(
    std::shared_ptr<ModelInstanceDB> instance) {
    setOriginalModelActor(std::move(instance));
}

void SlicingHandlerBridge::setToolpathPreviewDB(std::shared_ptr<GPlatform::ToolpathPreviewDB> preview) {
    m_currentPreview = preview;
}

void SlicingHandlerBridge::setDebugActors(
    const std::vector<std::shared_ptr<DebugActorDB>>& debugActors) {
    m_debugActors = debugActors;
}

void SlicingHandlerBridge::clearAllDBReferences() {
    m_currentOriginalModelActor.reset();
    m_currentPreview.reset();
    m_debugActors.clear();
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    SlicingHandlerBridge, "SlicingHandlerBridge", &SlicingHandlerBridge::create)
