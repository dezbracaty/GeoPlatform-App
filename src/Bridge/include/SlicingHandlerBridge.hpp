#pragma once

#include "BridgeBase.hpp"
#include <QObject>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <vector>

// 前向声明
class ActorDB;
class ModelInstanceDB;
namespace GPlatform {
    class ToolpathPreviewDB;
}
class DebugActorDB;

/**
 * @brief SlicingHandlerBridge - QML 访问切片调试功能的桥接类
 *
 * 提供 QML 可访问的切片调试模式控制和状态查询
 */
class SlicingHandlerBridge : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(bool debugMode READ debugMode WRITE setDebugMode NOTIFY debugModeChanged)
    Q_PROPERTY(int debugTargetLayer READ debugTargetLayer WRITE setDebugTargetLayer NOTIFY debugTargetLayerChanged)
    Q_PROPERTY(bool hasDebugActors READ hasDebugActors NOTIFY hasDebugActorsChanged)

    // 可见性控制属性
    Q_PROPERTY(bool showOriginalModel READ showOriginalModel WRITE setShowOriginalModel NOTIFY showOriginalModelChanged)
    Q_PROPERTY(bool showSlicingModel READ showSlicingModel WRITE setShowSlicingModel NOTIFY showSlicingModelChanged)
    Q_PROPERTY(bool showDebugData READ showDebugData WRITE setShowDebugData NOTIFY showDebugDataChanged)

    // 手动支撑状态与参数（UI 与 Handler 共享的真实状态）
    Q_PROPERTY(bool manualSupportActive READ manualSupportActive WRITE setManualSupportActive NOTIFY manualSupportActiveChanged)

    Q_PROPERTY(QString manualSupportPaintMode READ manualSupportPaintMode WRITE setManualSupportPaintMode NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(QString manualSupportBrushShape READ manualSupportBrushShape WRITE setManualSupportBrushShape NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(double manualSupportBrushSizeMm READ manualSupportBrushSizeMm WRITE setManualSupportBrushSizeMm NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(double manualSupportDensityPercent READ manualSupportDensityPercent WRITE setManualSupportDensityPercent NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(bool manualSupportSmartFill READ manualSupportSmartFill WRITE setManualSupportSmartFill NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(bool manualSupportClipToOverhang READ manualSupportClipToOverhang WRITE setManualSupportClipToOverhang NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(double manualSupportSmartFillAngleDeg READ manualSupportSmartFillAngleDeg WRITE setManualSupportSmartFillAngleDeg NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(double manualSupportGapArea READ manualSupportGapArea WRITE setManualSupportGapArea NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(double manualSupportSectionViewRatio READ manualSupportSectionViewRatio WRITE setManualSupportSectionViewRatio NOTIFY manualSupportSettingsChanged)
    Q_PROPERTY(bool manualSupportHasContactPatch READ manualSupportHasContactPatch NOTIFY manualSupportInspectionChanged)
    Q_PROPERTY(int manualSupportContactTriangleCount READ manualSupportContactTriangleCount NOTIFY manualSupportInspectionChanged)
    Q_PROPERTY(int manualSupportLowerModelHitSampleCount READ manualSupportLowerModelHitSampleCount NOTIFY manualSupportInspectionChanged)
    Q_PROPERTY(int manualSupportTotalBottomSampleCount READ manualSupportTotalBottomSampleCount NOTIFY manualSupportInspectionChanged)
    Q_PROPERTY(double manualSupportMaxLowerModelHitHeightMm READ manualSupportMaxLowerModelHitHeightMm NOTIFY manualSupportInspectionChanged)

    Q_PROPERTY(bool manualDebugShowActor READ manualDebugShowActor WRITE setManualDebugShowActor NOTIFY manualSupportDebugVisibilityChanged)
    Q_PROPERTY(bool manualDebugShowStrokePolyline READ manualDebugShowStrokePolyline WRITE setManualDebugShowStrokePolyline NOTIFY manualSupportDebugVisibilityChanged)
    Q_PROPERTY(bool manualDebugShowStrokePoints READ manualDebugShowStrokePoints WRITE setManualDebugShowStrokePoints NOTIFY manualSupportDebugVisibilityChanged)
    Q_PROPERTY(bool manualDebugShowPickNormals READ manualDebugShowPickNormals WRITE setManualDebugShowPickNormals NOTIFY manualSupportDebugVisibilityChanged)
    Q_PROPERTY(bool manualDebugShowProjectedRing READ manualDebugShowProjectedRing WRITE setManualDebugShowProjectedRing NOTIFY manualSupportDebugVisibilityChanged)
    Q_PROPERTY(bool manualDebugShowMergedSelectionSurfaceOutline READ manualDebugShowMergedSelectionSurfaceOutline WRITE setManualDebugShowMergedSelectionSurfaceOutline NOTIFY manualSupportDebugVisibilityChanged)
    Q_PROPERTY(bool manualDebugShowContactPatch READ manualDebugShowContactPatch WRITE setManualDebugShowContactPatch NOTIFY manualSupportDebugVisibilityChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    // 单例模式
    static SlicingHandlerBridge* instance();

    // QML 单例提供函数
    static SlicingHandlerBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // 属性访问器
    bool debugMode() const { return m_debugMode; }
    void setDebugMode(bool enabled);

    int debugTargetLayer() const { return m_debugTargetLayer; }
    void setDebugTargetLayer(int layer);

    bool hasDebugActors() const { return m_debugActorCount > 0; }

    // 可见性控制访问器
    bool showOriginalModel() const { return m_showOriginalModel; }
    void setShowOriginalModel(bool show);

    bool showSlicingModel() const { return m_showSlicingModel; }
    void setShowSlicingModel(bool show);

    bool showDebugData() const { return m_showDebugData; }
    void setShowDebugData(bool show);

    bool manualSupportActive() const { return m_manualSupportActive; }
    void setManualSupportActive(bool active);

    QString manualSupportPaintMode() const { return m_manualSupportPaintMode; }
    void setManualSupportPaintMode(const QString& mode);

    QString manualSupportBrushShape() const { return m_manualSupportBrushShape; }
    void setManualSupportBrushShape(const QString& shape);

    double manualSupportBrushSizeMm() const { return m_manualSupportBrushSizeMm; }
    void setManualSupportBrushSizeMm(double sizeMm);

    double manualSupportDensityPercent() const { return m_manualSupportDensityPercent; }
    void setManualSupportDensityPercent(double densityPercent);

    bool manualSupportSmartFill() const { return m_manualSupportSmartFill; }
    void setManualSupportSmartFill(bool enabled);

    bool manualSupportClipToOverhang() const { return m_manualSupportClipToOverhang; }
    void setManualSupportClipToOverhang(bool enabled);

    double manualSupportSmartFillAngleDeg() const { return m_manualSupportSmartFillAngleDeg; }
    void setManualSupportSmartFillAngleDeg(double degree);

    double manualSupportGapArea() const { return m_manualSupportGapArea; }
    void setManualSupportGapArea(double area);

    double manualSupportSectionViewRatio() const { return m_manualSupportSectionViewRatio; }
    void setManualSupportSectionViewRatio(double ratio);

    bool manualSupportHasContactPatch() const { return m_manualSupportHasContactPatch; }
    int manualSupportContactTriangleCount() const { return m_manualSupportContactTriangleCount; }
    int manualSupportLowerModelHitSampleCount() const { return m_manualSupportLowerModelHitSampleCount; }
    int manualSupportTotalBottomSampleCount() const { return m_manualSupportTotalBottomSampleCount; }
    double manualSupportMaxLowerModelHitHeightMm() const { return m_manualSupportMaxLowerModelHitHeightMm; }

    bool manualDebugShowActor() const { return m_manualDebugShowActor; }
    void setManualDebugShowActor(bool enabled);

    bool manualDebugShowStrokePolyline() const { return m_manualDebugShowStrokePolyline; }
    void setManualDebugShowStrokePolyline(bool enabled);

    bool manualDebugShowStrokePoints() const { return m_manualDebugShowStrokePoints; }
    void setManualDebugShowStrokePoints(bool enabled);

    bool manualDebugShowPickNormals() const { return m_manualDebugShowPickNormals; }
    void setManualDebugShowPickNormals(bool enabled);

    bool manualDebugShowProjectedRing() const { return m_manualDebugShowProjectedRing; }
    void setManualDebugShowProjectedRing(bool enabled);

    bool manualDebugShowMergedSelectionSurfaceOutline() const { return m_manualDebugShowMergedSelectionSurfaceOutline; }
    void setManualDebugShowMergedSelectionSurfaceOutline(bool enabled);

    bool manualDebugShowContactPatch() const { return m_manualDebugShowContactPatch; }
    void setManualDebugShowContactPatch(bool enabled);

    // QML 可调用方法
    Q_INVOKABLE void clearDebugActors();
    Q_INVOKABLE bool manualSupportApplySettings(const QVariantMap& settings);
    Q_INVOKABLE bool manualSupportSetDebugVisibility(const QVariantMap& visibility);
    Q_INVOKABLE bool manualSupportClearStrokes();
    Q_INVOKABLE bool manualSupportCommitStroke();
    Q_INVOKABLE bool manualSupportHasCommittedMesh(qulonglong modelId) const;

    // 内部更新方法（由 SlicingHandler 调用）
    void updateDebugActorCount(int count);
    void setManualSupportContactDiagnostics(bool hasContactPatch,
                                            int contactTriangleCount,
                                            int lowerModelHitSampleCount,
                                            int totalBottomSampleCount,
                                            double maxLowerModelHitHeightMm);

    // DB 对象管理方法（由 SlicingHandler 调用）
    void setOriginalModelActor(std::shared_ptr<ActorDB> actor);
    void setModelInstance(std::shared_ptr<ModelInstanceDB> instance);
    void setToolpathPreviewDB(std::shared_ptr<GPlatform::ToolpathPreviewDB> preview);
    void setDebugActors(const std::vector<std::shared_ptr<DebugActorDB>>& debugActors);

    // 清除所有 DB 引用
    void clearAllDBReferences();

signals:
    void debugModeChanged();
    void debugTargetLayerChanged();
    void hasDebugActorsChanged();
    void showOriginalModelChanged();
    void showSlicingModelChanged();
    void showDebugDataChanged();
    void manualSupportActiveChanged();
    void manualSupportSettingsChanged();
    void manualSupportInspectionChanged();
    void manualSupportDebugVisibilityChanged();
    void clearDebugActorsRequested();
    void manualSupportApplySettingsRequested(const QVariantMap& settings);
    void manualSupportDebugVisibilityRequested(const QVariantMap& visibility);
    void manualSupportClearStrokesRequested();
    void manualSupportCommitStrokeRequested();

private:
    SlicingHandlerBridge(QObject* parent = nullptr);
    ~SlicingHandlerBridge() = default;

    // 禁用拷贝和移动
    SlicingHandlerBridge(const SlicingHandlerBridge&) = delete;
    SlicingHandlerBridge& operator=(const SlicingHandlerBridge&) = delete;
    SlicingHandlerBridge(SlicingHandlerBridge&&) = delete;
    SlicingHandlerBridge& operator=(SlicingHandlerBridge&&) = delete;

    // 成员变量
    bool m_debugMode = false;  // 默认禁用调试模式
    int m_debugTargetLayer = -1;  // -1 表示所有层
    int m_debugActorCount = 0;

    // 可见性控制状态
    bool m_showOriginalModel = true;   // 默认显示原始模型
    bool m_showSlicingModel = false;   // 默认隐藏切片模型（调试模式）
    bool m_showDebugData = true;       // 默认显示调试数据

    // 手动支撑状态/参数镜像（与 Handler 保持同步）
    bool m_manualSupportActive = false;
    QString m_manualSupportPaintMode = "enforce";
    QString m_manualSupportBrushShape = "circle";
    double m_manualSupportBrushSizeMm = 1.0;
    double m_manualSupportDensityPercent = 100.0;
    bool m_manualSupportSmartFill = false;
    bool m_manualSupportClipToOverhang = true;
    double m_manualSupportSmartFillAngleDeg = 30.0;
    double m_manualSupportGapArea = 1.0;
    double m_manualSupportSectionViewRatio = 0.0;
    bool m_manualSupportHasContactPatch = false;
    int m_manualSupportContactTriangleCount = 0;
    int m_manualSupportLowerModelHitSampleCount = 0;
    int m_manualSupportTotalBottomSampleCount = 0;
    double m_manualSupportMaxLowerModelHitHeightMm = 0.0;

    bool m_manualDebugShowActor = true;
    bool m_manualDebugShowStrokePolyline = true;
    bool m_manualDebugShowStrokePoints = true;
    bool m_manualDebugShowPickNormals = true;
    bool m_manualDebugShowProjectedRing = true;
    bool m_manualDebugShowMergedSelectionSurfaceOutline = true;
    bool m_manualDebugShowContactPatch = true;

    // DB 对象引用（由 SlicingHandler 设置）
    std::shared_ptr<ActorDB> m_currentOriginalModelActor;
    std::shared_ptr<GPlatform::ToolpathPreviewDB> m_currentPreview;
    std::vector<std::shared_ptr<DebugActorDB>> m_debugActors;
};
