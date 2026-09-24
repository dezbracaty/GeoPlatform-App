#pragma once

#include "BridgeBase.hpp"
#include "GCodeLineModel.hpp"
#include <QObject>
#include <QJSEngine>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>
#include <cstdint>
#include <memory>
#include <optional>

namespace GPlatform {
    enum class ToolpathPreviewColorMode : int;
    class ToolpathPreviewDB;
}

class SlicingPreviewBridge : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* gcodeLines READ gcodeLines CONSTANT)
    Q_PROPERTY(bool previewInspectModifierPressed READ previewInspectModifierPressed NOTIFY previewInspectModifierPressedChanged)
    Q_PROPERTY(bool previewCursorSnapEnabled READ previewCursorSnapEnabled WRITE setPreviewCursorSnapEnabled NOTIFY previewCursorSnapEnabledChanged)
    Q_PROPERTY(bool previewInspectorOpen READ previewInspectorOpen WRITE setPreviewInspectorOpen NOTIFY previewInspectorOpenChanged)
    Q_PROPERTY(QString previewInspectorTab READ previewInspectorTab WRITE setPreviewInspectorTab NOTIFY previewInspectorTabChanged)

    Q_PROPERTY(int currentLayer READ currentLayer NOTIFY currentLayerChanged)
    Q_PROPERTY(int currentStep READ currentStep WRITE setCurrentStep NOTIFY currentStepChanged)
    Q_PROPERTY(int totalLayers READ totalLayers NOTIFY totalLayersChanged)
    Q_PROPERTY(int totalSteps READ totalSteps NOTIFY totalStepsChanged)
    Q_PROPERTY(int layerRangeStart READ layerRangeStart NOTIFY layerRangeChanged)
    Q_PROPERTY(int layerRangeEnd READ layerRangeEnd NOTIFY layerRangeChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying WRITE setIsPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(float playbackSpeed READ playbackSpeed WRITE setPlaybackSpeed NOTIFY playbackSpeedChanged)
    Q_PROPERTY(bool isLooping READ isLooping WRITE setIsLooping NOTIFY isLoopingChanged)
    Q_PROPERTY(int currentModelId READ currentModelId WRITE setCurrentModelId NOTIFY currentModelChanged)
    Q_PROPERTY(QVariant slicedModelList READ slicedModelList NOTIFY slicedModelListChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QString previewArtifactPath READ previewArtifactPath NOTIFY previewArtifactPathChanged)
    Q_PROPERTY(QString printOutputPath READ printOutputPath NOTIFY printOutputPathChanged)
    Q_PROPERTY(QString printPackagePath READ printPackagePath NOTIFY printOutputPathChanged)
    Q_PROPERTY(bool printOutputReady READ printOutputReady NOTIFY printOutputPathChanged)
    Q_PROPERTY(bool printPackageExportAvailable READ printPackageExportAvailable NOTIFY printOutputPathChanged)
    Q_PROPERTY(bool resliceAvailable READ resliceAvailable NOTIFY currentToolpathPreviewChanged)
    Q_PROPERTY(bool previewPanelVisible READ previewPanelVisible WRITE setPreviewPanelVisible NOTIFY previewPanelVisibleChanged)
    Q_PROPERTY(bool previewPanelFolded READ previewPanelFolded WRITE setPreviewPanelFolded NOTIFY previewPanelFoldedChanged)
    Q_PROPERTY(QString previewPanelViewType READ previewPanelViewType NOTIFY previewPanelViewTypeChanged)
    Q_PROPERTY(bool previewDetailsVisible READ previewDetailsVisible WRITE setPreviewDetailsVisible NOTIFY previewDetailsVisibleChanged)
    Q_PROPERTY(QVariantList previewPanelViewTypes READ previewPanelViewTypes NOTIFY statisticsChanged)
    Q_PROPERTY(float totalTime READ totalTime NOTIFY statisticsChanged)
    Q_PROPERTY(float totalExtrusion READ totalExtrusion NOTIFY statisticsChanged)
    Q_PROPERTY(float totalPrintDistance READ totalPrintDistance NOTIFY statisticsChanged)
    Q_PROPERTY(float totalTravelDistance READ totalTravelDistance NOTIFY statisticsChanged)
    Q_PROPERTY(int colorMode READ colorMode WRITE setColorMode NOTIFY colorModeChanged)
    Q_PROPERTY(bool showTravel READ showTravel WRITE setShowTravel NOTIFY pathVisibilityChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    GCodeLineModel* gcodeLines() const { return m_gcodeLines; }
    static SlicingPreviewBridge* instance();
    static SlicingPreviewBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    int currentLayer() const;
    int currentStep() const;
    void setCurrentStep(int step);
    int totalLayers() const;
    int totalSteps() const;
    int layerRangeStart() const;
    int layerRangeEnd() const;
    bool isPlaying() const { return m_isPlaying; }
    void setIsPlaying(bool playing);
    float playbackSpeed() const { return m_playbackSpeed; }
    void setPlaybackSpeed(float speed);
    bool isLooping() const { return m_isLooping; }
    void setIsLooping(bool looping);
    int currentModelId() const { return m_currentModelId; }
    void setCurrentModelId(int modelId);
    QVariant slicedModelList() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setLayerRange(int start, int end);
    Q_INVOKABLE QVariantMap layerInfo(int layer) const;

    void beginPreviewLoad(const QString& artifactPath);
    void completePreviewLoad(const QString& artifactPath,
                             const QString& printOutputPath,
                             int modelId,
                             const QString& modelName,
                             std::shared_ptr<GPlatform::ToolpathPreviewDB> preview);
    void failPreviewLoad(const QString& artifactPath, const QString& errorMessage);
    void cancelPreviewLoad();
    Q_INVOKABLE void clearPreview();
    Q_INVOKABLE bool showFiberFillDiagnostics(const QString& previewToken);
    void clearPreviewPreservingModel(int modelId);

    Q_INVOKABLE bool canExportPrintOutput(const QString& formatId) const;
    Q_INVOKABLE bool exportCurrentPrintOutputAs(const QString& formatId, const QString& savePath);
    Q_INVOKABLE QString defaultPrintOutputFileName(const QString& formatId) const;
    Q_INVOKABLE QString defaultExportPrintOutputFileUrl(const QString& formatId) const;
    Q_INVOKABLE QStringList printOutputNameFilters(const QString& formatId) const;
    Q_INVOKABLE bool canResliceCurrentModel() const;

    bool isLoading() const { return m_isLoading; }
    QString previewArtifactPath() const;
    QString printOutputPath() const;
    QString printPackagePath() const;
    bool printOutputReady() const;
    bool printPackageExportAvailable() const;
    bool resliceAvailable() const;
    bool previewInspectModifierPressed() const { return m_previewInspectModifierPressed; }
    void setPreviewInspectModifierPressed(bool pressed);
    bool previewCursorSnapEnabled() const { return m_previewCursorSnapEnabled; }
    void setPreviewCursorSnapEnabled(bool enabled);
    bool previewInspectorOpen() const { return m_previewInspectorOpen; }
    void setPreviewInspectorOpen(bool open);
    QString previewInspectorTab() const { return m_previewInspectorTab; }
    void setPreviewInspectorTab(const QString& tab);
    Q_INVOKABLE void togglePreviewInspector();
    bool previewPanelVisible() const { return m_previewPanelVisible; }
    Q_INVOKABLE void setPreviewPanelVisible(bool visible);
    bool previewPanelFolded() const { return m_previewPanelFolded; }
    Q_INVOKABLE void setPreviewPanelFolded(bool folded);
    QString previewPanelViewType() const { return m_previewPanelViewType; }
    Q_INVOKABLE bool setPreviewPanelViewType(const QString& viewType);
    static std::optional<GPlatform::ToolpathPreviewColorMode> previewPanelColorModeForViewType(
        const QString& viewType);
    bool previewDetailsVisible() const { return m_previewDetailsVisible; }
    Q_INVOKABLE void setPreviewDetailsVisible(bool visible);
    QVariantList previewPanelViewTypes() const;
    Q_INVOKABLE QVariantMap previewPanelData(const QString& viewType) const;
    Q_INVOKABLE bool setPreviewPanelRowVisible(const QString& viewType,
                                               const QString& rowId,
                                               bool visible);
    Q_INVOKABLE QVariantList previewLegendItems(const QString& viewType) const;
    Q_INVOKABLE QVariantMap previewGradientRange(const QString& viewType) const;
    Q_INVOKABLE bool setPreviewLegendItemVisible(const QString& viewType, const QString& itemId, bool visible);
    Q_INVOKABLE bool setPreviewLegendGroupVisible(const QString& viewType, bool visible);
    Q_INVOKABLE bool resetPreviewLegendVisibility();
    Q_INVOKABLE void togglePreviewPanelVisible();
    Q_INVOKABLE void togglePreviewPanelFolded();
    Q_INVOKABLE void togglePreviewDetailsVisible();

    float totalTime() const;
    float totalExtrusion() const;
    float totalPrintDistance() const;
    float totalTravelDistance() const;
    int colorMode() const;
    void setColorMode(int mode);
    bool showTravel() const;
    void setShowTravel(bool show);
    std::shared_ptr<GPlatform::ToolpathPreviewDB> currentToolpathPreview() const;
    void updatePlaybackPlaying(bool playing);
    void updatePlaybackSpeed(float speed);
    void updatePlaybackLooping(bool looping);
    void addToolpathPreview(int modelId, const QString& modelName, std::shared_ptr<GPlatform::ToolpathPreviewDB> preview);
    void removeToolpathPreview(int modelId);
    void clearAllToolpathPreviews();
    void clearAllDBReferences();

signals:
    void currentLayerChanged();
    void currentStepChanged();
    void totalLayersChanged();
    void totalStepsChanged();
    void layerRangeChanged();
    void isPlayingChanged();
    void playbackSpeedChanged();
    void isLoopingChanged();
    void currentModelChanged();
    void slicedModelListChanged();
    void isLoadingChanged();
    void previewArtifactPathChanged();
    void printOutputPathChanged();
    void previewInspectModifierPressedChanged();
    void previewCursorSnapEnabledChanged();
    void previewInspectorOpenChanged();
    void previewInspectorTabChanged();
    void previewPanelVisibleChanged();
    void previewPanelFoldedChanged();
    void previewPanelViewTypeChanged();
    void previewDetailsVisibleChanged();
    void previewLoadError(const QString& error);
    void previewLoadFinished(bool success, const QString& artifactPath);
    void statisticsChanged();
    void colorModeChanged();
    void pathVisibilityChanged();
    void previewLegendItemsChanged();
    void playbackPlayRequested();
    void playbackPauseRequested();
    void playbackStopRequested();
    void playbackSpeedChangeRequested(float speed);
    void playbackLoopingChangeRequested(bool looping);
    void currentToolpathPreviewChanged();

private:
    explicit SlicingPreviewBridge(QObject* parent = nullptr);
    ~SlicingPreviewBridge() override;

    SlicingPreviewBridge(const SlicingPreviewBridge&) = delete;
    SlicingPreviewBridge& operator=(const SlicingPreviewBridge&) = delete;
    SlicingPreviewBridge(SlicingPreviewBridge&&) = delete;
    SlicingPreviewBridge& operator=(SlicingPreviewBridge&&) = delete;

    std::shared_ptr<GPlatform::ToolpathPreviewDB> getCurrentPreview() const;
    void refreshAlgorithmAuditCache(
        const std::shared_ptr<GPlatform::ToolpathPreviewDB>& preview) const;
    QString resolveCurrentPrintOutputPath() const;
    QString resolveCurrentPrintOutputPath(const QString& formatId) const;
    void clearAllToolpathPreviewsInternal(int preservedModelId);
    void navigateGCode(bool layerStart);
    bool isValidPreviewPanelViewType(const QString& viewType) const;
    bool applyPreviewPanelRenderStateForViewType(const QString& viewType,
                                                 bool restoreFeatureVisibility,
                                                 bool logWhenUnavailable);
    std::shared_ptr<GPlatform::ToolpathPreviewDB> m_activePreview;
    mutable std::weak_ptr<GPlatform::ToolpathPreviewDB> m_cachedAuditPreview;
    mutable std::uint64_t m_cachedAuditPreviewGeneration{0};
    mutable QVariantList m_cachedAuditStages;
    QString m_activePreviewName;
    int m_currentModelId = -1;

    GCodeLineModel* m_gcodeLines{nullptr};
    bool m_previewInspectModifierPressed{false};
    bool m_previewCursorSnapEnabled{false};
    bool m_previewInspectorOpen{true};
    QString m_previewInspectorTab{QStringLiteral("types")};
    bool m_isLoading = false;
    QString m_previewArtifactPath;
    QString m_printOutputPath;
    bool m_previewPanelVisible = false;
    bool m_previewPanelFolded = false;
    QString m_previewPanelViewType = QStringLiteral("summary");
    bool m_previewDetailsVisible = false;
    bool m_isPlaying = false;
    float m_playbackSpeed = 1.0f;
    bool m_isLooping = false;
};
