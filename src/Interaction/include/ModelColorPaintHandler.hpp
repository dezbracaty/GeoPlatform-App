#pragma once

#include "ManualSupportOrcaScaffold.hpp"
#include "MeshRayQuery.hpp"
#include "ModelColorPaintComputeCoordinator.hpp"
#include "StandardActionHandler.hpp"
#include "SurfaceBrushCursor.hpp"
#include "SurfaceBrushProjection.hpp"
#include "Transform.hpp"
#include <QColor>
#include <QPoint>
#include <QTimer>
#include <QVariantMap>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

class TransientPolyDataActorDB;
class ModelInstanceDB;
class ModelSurfaceColorDB;

/** Interactive application-side model surface color editor. */
class ModelColorPaintHandler final : public StandardActionHandler {
    Q_OBJECT

public:
    explicit ModelColorPaintHandler(QObject* parent = nullptr);
    ~ModelColorPaintHandler() override;

    bool isPersistent() const override { return true; }
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    bool onMousePressEvent(QMouseEvent* event) override;
    bool onMouseMoveEvent(QMouseEvent* event) override;
    bool onMouseReleaseEvent(QMouseEvent* event) override;
    bool onWheelEvent(QWheelEvent* event) override;
    bool onKeyPressEvent(QKeyEvent* event) override;

    bool applySettingsCommand(const QVariantMap& settings);
    bool clearPaintingCommand();
    bool performGapFillCommand();
    bool remapFilamentsCommand(const QVariantList& mapping);
    bool setDefaultFilamentSlotCommand(int slot);
    bool requestFinish();

private:
    enum class State { Inactive, Ready, Drawing, Finishing };

    struct QueuedStroke {
        ModelColorStrokeRequest request;
        std::vector<ManualSupportOrcaScaffold::LeafTriangle> previewLeaves;
        std::uint64_t token{0};
    };

    bool startSession();
    bool loadTargetMesh(const std::shared_ptr<ModelInstanceDB>& model);
    int paintedFacetCount() const;
    int brushCursorRadiusPx() const;
    int indexedFacetId(int sourceFacetId) const;
    bool updateHitFromRay(const QPoint& screenPos);
    bool updateProjectedBrushSelection(const SurfaceBrushProjection::ScreenContext& projection);
    bool appendStrokeSample(const QPoint& screenPos, bool erase);
    void refreshToolCursor();
    void ensureHoverActor();
    void refreshHoverPreview(const SurfaceBrushProjection::ScreenContext* projection = nullptr);
    void clearHoverPreview();
    void scheduleGapPreview();
    void requestGapPreview();
    void handleGapPreviewFinished(
        std::shared_ptr<const ModelColorGapPreviewResult> result);
    void updateHoverMaterial();
    void ensureStrokePreviewActor();
    void refreshStrokePreview();
    void clearStrokePreview();
    bool enqueueStroke(ModelColorStrokeRequest request,
                       std::vector<ManualSupportOrcaScaffold::LeafTriangle> previewLeaves = {});
    bool submitNextStroke();
    void handleStrokeFinished(std::shared_ptr<const ModelColorStrokeResult> result);
    void finishAfterPendingCommits();
    void handleCommittedModelDataChanged(qulonglong modelId);
    void startDocumentObservation();
    void stopDocumentObservation();
    void syncPaletteFromActiveFilaments();
    void ensureStateDB(bool createIfMissing);
    bool restoreState();
    bool persistState(const ManualSupportOrcaScaffold::TriangleSplittingData& state,
                      bool transaction,
                      const char* description);
    QColor colorForLabel(std::uint32_t labelId) const;
    static DBInstanceID parseModelId(const QVariant& value);
    static bool encodeState(const ManualSupportOrcaScaffold::TriangleSplittingData& state,
                            std::string* blob);
    static bool decodeState(const std::string& blob,
                            ManualSupportOrcaScaffold::TriangleSplittingData* state);

    State m_state{State::Inactive};
    DBInstanceID m_interactionViewId{INVALID_DB_ID};
    bool m_pointerDown{false};
    bool m_modelInputSuspended{false};
    bool m_finishRequested{false};
    QPoint m_lastPoint;
    QPoint m_strokeAnchor;
    DBInstanceID m_targetModelId;
    DBInstanceID m_targetPartId;
    std::shared_ptr<ModelInstanceDB> m_targetMesh;
    ManualSupportOrcaScaffold m_engine;
    MeshRayQuery m_meshRayQuery;
    bool m_meshRayQueryAccelerated{false};
    std::vector<int> m_sourceFacetToIndexedFacet;
    std::shared_ptr<const OrcaIndexedMesh> m_workerTopology;
    DBInstanceID m_loadedModelId;
    DBInstanceID m_loadedPartId;
    std::uint64_t m_loadedMeshRevision{0};
    Transform::Matrix4 m_loadedTransform;
    Transform::Matrix4 m_paintFromPart{Transform::Matrix4::Identity()};
    Vector3 m_paintOriginWorld{0.0f, 0.0f, 0.0f};
    bool m_hasLoadedTopology{false};
    int m_loadedStateRevision{-1};
    int m_sourceTriangleCount{0};
    std::string m_topologyFingerprint;
    ManualSupportOrcaScaffold::TriangleSplittingData m_committedState;
    std::vector<ModelColorStrokeSample> m_strokeSamples;
    std::vector<ManualSupportOrcaScaffold::LeafTriangle> m_strokePreviewLeaves;
    std::deque<QueuedStroke> m_queuedStrokes;
    ManualSupportOrcaScaffold::ScreenProjection m_currentScreenProjection;
    std::vector<int> m_currentProjectionSeedFacets;
    std::uint64_t m_pendingStrokeToken{0};
    std::uint64_t m_pendingGapPreviewToken{0};
    QTimer m_gapPreviewTimer;
    int m_cachedPaintedFacetCount{0};
    std::vector<QColor> m_palette;
    std::uint32_t m_currentLabelId{1};
    double m_brushSize{8.0};
    QString m_brushShape{QStringLiteral("circle")};
    bool m_edgeDetection{true};
    double m_smartFillAngle{30.0};
    double m_heightRange{0.2};
    double m_gapArea{5.0};
    bool m_verticalOnly{false};
    bool m_horizontalOnly{false};
    SurfaceBrushCursor m_surfaceBrushCursor;
    bool m_hasHoverHit{false};
    QVector3D m_hoverHit;
    QVector3D m_hoverCamera;
    int m_hoverFacetId{-1};
    float m_hoverRadiusWorld{0.0f};
    std::vector<ManualSupportOrcaScaffold::LeafTriangle> m_hoverPreviewLeaves;
    std::shared_ptr<TransientPolyDataActorDB> m_hoverActor;
    std::shared_ptr<TransientPolyDataActorDB> m_strokePreviewActor;
    std::shared_ptr<ModelSurfaceColorDB> m_stateDB;
    std::size_t m_documentListenerId{0};
};
