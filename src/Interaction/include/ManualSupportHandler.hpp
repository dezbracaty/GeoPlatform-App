#pragma once

#include "StandardActionHandler.hpp"
#include "PickService.hpp"
#include "MeshRayQuery.hpp"
#include "ManualSupportOrcaScaffold.hpp"
#include "SurfaceBrushCursor.hpp"
#include <ChangeTypes.hpp>
#include <QCursor>
#include <QPoint>
#include <QString>
#include <QVector3D>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <vtkSmartPointer.h>

class DebugActorDB;
class ManualSupportDB;
class vtkPolyData;

/**
 * @brief Manual support editing handler (framework only)
 *
 * This handler provides the interaction skeleton for manual support painting:
 * - enter/exit manual support editing session
 * - collect stroke points from pick results (mouse press/move/release)
 * - keep committed strokes in memory for future geometry generation
 *
 * NOTE:
 * The actual support mesh generation is intentionally left as follow-up work.
 */
class ManualSupportHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit ManualSupportHandler(QObject* parent = nullptr);
    ~ManualSupportHandler() override = default;

    bool isPersistent() const override { return true; }

    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;

    bool onMousePressEvent(QMouseEvent* event) override;
    bool onMouseMoveEvent(QMouseEvent* event) override;
    bool onMouseReleaseEvent(QMouseEvent* event) override;
    bool onWheelEvent(QWheelEvent* event) override;

    // In-session commands (called by bridge directly)
    bool applySettingsCommand(const QVariantMap& settings, QString* errorMessage = nullptr);
    bool applyDebugVisibilityCommand(const QVariantMap& visibility, QString* errorMessage = nullptr);
    bool clearStrokesCommand(QString* errorMessage = nullptr);
    bool commitStrokeCommand(QString* errorMessage = nullptr);
Q_SIGNALS:
    void committedSupportMeshReady(const QString& modelId,
                                   bool success,
                                   int pointCount = 0,
                                   int polygonCount = 0);

private:
    enum class State {
        Inactive,
        Ready,
        Drawing
    };

    enum class ToolPickPhase {
        Press,
        Move,
        Release
    };

    struct PendingToolPick {
        ToolPickPhase phase{ToolPickPhase::Move};
        QPoint screenPos;
        Qt::KeyboardModifiers modifiers{Qt::NoModifier};
        std::uint64_t gesture{0};
    };

    struct StrokePoint {
        QPoint screenPos;
        DBInstanceID dbId;
        Vector3 worldPos;
        int cellId = -1;
        int pointId = -1;
    };

    struct SupportContactDiagnostics {
        bool hasContactPatch{false};
        int contactTriangleCount{0};
        int lowerModelHitSampleCount{0};
        int totalBottomSampleCount{0};
        double maxLowerModelHitHeightMm{0.0};
    };

    struct SupportRegionComputationRequest {
        quint64 generation{0};
        int sourceRevision{0};
        bool committed{false};
        float groundZ{0.0f};
        QString brushShape;
        double brushSizeMm{1.0};
        double densityPercent{100.0};
        ManualSupportOrcaScaffold::SelectedSurfaceMesh surfaceMesh;
        std::shared_ptr<MeshRayQuery> meshRayQuery;
    };

    struct SupportRegionComputationResult {
        quint64 generation{0};
        int sourceRevision{0};
        bool committed{false};
        vtkSmartPointer<vtkPolyData> candidatePolyData;
        vtkSmartPointer<vtkPolyData> contactPolyData;
        SupportContactDiagnostics diagnostics;
    };

    using Stroke = std::vector<StrokePoint>;

    // Action dispatchers
    void handleEnterAction();
    void handleLeaveAction();

    // Stroke helpers
    std::uint64_t requestToolPick(const QPoint& screenPos,
                                  Qt::KeyboardModifiers modifiers,
                                  ToolPickPhase phase,
                                  PickDelivery delivery);
    void handleToolPickCompleted(const PickSnapshot& snapshot);
    void cancelPendingToolPicks();
    bool beginStrokeFromPick(const PickResult& pick,
                             const QPoint& screenPos,
                             Qt::KeyboardModifiers modifiers);
    void moveStrokeFromPick(const PickResult& pick,
                            const QPoint& screenPos,
                            Qt::KeyboardModifiers modifiers);
    void finishStrokeFromPick(const PickResult& pick,
                              const QPoint& screenPos,
                              Qt::KeyboardModifiers modifiers);
    void abortPendingStroke();
    bool appendPointFromPickResult(const PickResult& pick,
                                   const QPoint& expectedScreenPos,
                                   bool enforceDistanceCheck);
    bool commitCurrentStroke(bool requireAtLeastTwoPoints = true);
    void resetCurrentStroke();
    void clearAllStrokes();
    void ensureManualSupportDB();
    void registerSupportStateChangeListener();
    void unregisterSupportStateChangeListener();
    bool applySupportStateFromDb(bool forceApply);
    bool persistSupportStateSnapshot(bool useTransaction, const char* description);
    void handleSupportStateChange(const DBInstanceID& id, ChangeType changeType, const std::string& propertyName);
    static bool encodeSupportStateSnapshot(const ManualSupportOrcaScaffold::TriangleSplittingData& state,
                                           std::string* encodedBlob);
    static bool decodeSupportStateSnapshot(const std::string& encodedBlob,
                                           ManualSupportOrcaScaffold::TriangleSplittingData* state);

    // Debug/return helpers
    QVariantMap buildStateResult() const;
    static QString stateToString(State state);
    static DBInstanceID parseTargetModelId(const QVariant& rawModelId);
    void ensureDebugActorCreated();
    void applyDebugVisibility();
    void resetCommittedSupportMeshState();
    void updateDebugPickLayer(const StrokePoint& point, const QVector3D& cameraPos);
    void updateDebugSelectionLayer(bool committed);
    void refreshSelectionDebugSnapshot();
    static QVariantList toVariantList(const std::vector<int>& values);
    void clearDebugLayers();
    void applySettingsFromParams(const QVariantMap& params);
    void applyDebugVisibilityFromParams(const QVariantMap& params);
    void queueSupportRegionComputation(bool committed);
    void startSupportRegionComputation(const SupportRegionComputationRequest& request);
    void finishSupportRegionComputation(const SupportRegionComputationResult& result);
    void maybeStartPendingSupportRegionComputation();
    void cancelSupportRegionComputation();
    void refreshManualCursor();
    void clearManualCursor();
    Qt::CursorShape cursorShapeForCurrentTool() const;
    int brushCursorRadiusPx() const;
    bool tryComputeProjectedBrushRadiusWorld(const QPoint& requestedScreenPos, float* radiusWorld) const;
    float resolvePrintBedTopZ() const;
    vtkSmartPointer<vtkPolyData> buildProjectedSupportCandidatePolyData(
        const ManualSupportOrcaScaffold::SelectedSurfaceMesh* surfaceMesh = nullptr,
        SupportContactDiagnostics* diagnostics = nullptr) const;
    vtkSmartPointer<vtkPolyData> buildContactSurfacePolyData(
        const ManualSupportOrcaScaffold::SelectedSurfaceMesh* surfaceMesh = nullptr) const;
    static vtkSmartPointer<vtkPolyData> buildProjectedSupportCandidatePolyDataForTask(
        const ManualSupportOrcaScaffold::SelectedSurfaceMesh& surfaceMesh,
        const MeshRayQuery* meshRayQuery,
        float groundZ,
        const QString& brushShape,
        double brushSizeMm,
        double densityPercent,
        SupportContactDiagnostics* diagnostics,
        const std::function<void(float, const QString&)>& progressCallback = {},
        const std::function<bool()>& shouldCancel = {});
    static vtkSmartPointer<vtkPolyData> buildContactSurfacePolyDataForTask(
        const ManualSupportOrcaScaffold::SelectedSurfaceMesh& surfaceMesh,
        const std::function<void(float, const QString&)>& progressCallback = {},
        const std::function<bool()>& shouldCancel = {});
    void updateMergedSurfaceOutlineLayer(bool committed);
    bool shouldComputeMergedSurfaceOutline() const;
    void syncScaffoldOverhangFilter();
    void initializeAutoSupportSelection(bool resetSession);
    void refreshCommittedSelectionVisualization();
    void syncContactDiagnosticsToBridge() const;
private:
    State m_state{State::Inactive};
    bool m_isPointerDown{false};
    bool m_modelInputSuspended{false};
    bool m_scaffoldPointerDown{false};
    std::uint64_t m_gestureSequence{0};
    std::uint64_t m_pressPickRequestId{0};
    std::uint64_t m_latestMovePickRequestId{0};
    std::uint64_t m_previewPickRequestId{0};
    std::map<std::uint64_t, PendingToolPick> m_pendingToolPicks;
    std::optional<std::pair<PendingToolPick, PickSnapshot>> m_deferredReleasePick;
    QPoint m_lastSampleScreenPos;
    int m_minSampleDistancePx{3};

    DBInstanceID m_targetModelId;
    DBInstanceID m_interactionViewId{INVALID_DB_ID};
    QString m_returnEnvironment{"slicing"};
    bool m_switchEnvironmentOnStart{true};
    SurfaceBrushCursor m_surfaceBrushCursor;

    // Paint settings (Orca-like UI options; generation not implemented yet)
    QString m_paintMode{"enforce"};      // enforce/block/erase
    QString m_brushShape{"circle"};      // circle/sphere/fill/gap
    double m_brushSizeMm{1.0};
    double m_densityPercent{100.0};
    bool m_smartFill{false};
    bool m_clipToOverhang{true};
    int m_overhangThresholdDeg{std::numeric_limits<int>::max()};
    double m_smartFillAngleDeg{30.0};
    double m_gapArea{1.0};
    double m_sectionViewRatio{0.0};

    // Debug visibility options
    bool m_debugActorVisible{true};
    bool m_showStrokePolyline{true};
    bool m_showStrokePoints{true};
    bool m_showPickNormals{true};
    bool m_showProjectedRing{true};
    bool m_showMergedSelectionSurfaceOutline{true};
    bool m_showContactPatch{true};

    std::shared_ptr<DebugActorDB> m_debugActor;
    std::shared_ptr<DebugActorDB> m_debugActorPick;
    std::shared_ptr<DebugActorDB> m_debugActorContact;
    std::shared_ptr<DebugActorDB> m_debugActorMergedOutline;
    bool m_mergedSurfaceOutlineDirty{true};
    ManualSupportOrcaScaffold m_orcaScaffold;
    std::shared_ptr<MeshRayQuery> m_meshRayQuery;
    std::vector<QVector3D> m_targetMeshVertices;
    std::vector<std::array<int, 3>> m_targetMeshTriangles;
    std::vector<QVector3D> m_targetFacetNormals;
    std::map<std::pair<std::uint64_t, std::uint64_t>, int>
        m_partPrimitiveToScaffoldFacet;
    int m_lastStampStartFacetId{-1};
    std::vector<int> m_lastSelectedSourceFacetIds;

    Stroke m_currentStroke;
    ManualSupportOrcaScaffold::TriangleSplittingData m_baseSelectionState;
    ManualSupportOrcaScaffold::TriangleSplittingData m_selectionStateBeforeStroke;
    bool m_hasBaseSelectionState{false};
    bool m_hasSelectionStateBeforeStroke{false};
    std::shared_ptr<ManualSupportDB> m_manualSupportDB;
    std::size_t m_supportStateListenerId{0};
    bool m_supportStateInitialized{false};
    bool m_ignoreSupportStateNotifications{false};
    bool m_isApplyingSupportState{false};
    std::string m_lastAppliedSupportStateBlob;
    SupportContactDiagnostics m_contactDiagnostics;
    bool m_supportRegionTaskActive{false};
    quint64 m_supportRegionRequestSequence{0};
    quint64 m_latestSupportRegionRequestGeneration{0};
    quint64 m_runningSupportRegionGeneration{0};
    QString m_supportRegionTaskId;
    bool m_hasPendingSupportRegionRequest{false};
    SupportRegionComputationRequest m_pendingSupportRegionRequest;
    std::shared_ptr<std::atomic<quint64>> m_supportRegionGenerationToken;
};
