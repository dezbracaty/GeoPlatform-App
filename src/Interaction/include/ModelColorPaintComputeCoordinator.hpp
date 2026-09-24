#pragma once

#include "BaseID.hpp"
#include "ManualSupportOrcaScaffold.hpp"
#include "OrcaIndexedMesh.hpp"
#include "SurfaceColorRenderProjection.hpp"
#include <QColor>
#include <QObject>
#include <QPoint>
#include <QThread>
#include <QVector3D>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class ModelColorPaintTool {
    Brush,
    Triangle,
    Fill,
    HeightRange,
    GapFill
};

struct ModelColorStrokeSample {
    QPoint screenPosition;
    int facetId{-1};
    QVector3D paintHit;
    QVector3D cameraPaintPosition;
    float radiusWorld{0.0f};
    ManualSupportOrcaScaffold::CursorType cursorType{
        ManualSupportOrcaScaffold::CursorType::Circle};
    ManualSupportOrcaScaffold::ScreenProjection screenProjection;
    std::vector<int> projectionSeedFacets;
    ModelColorPaintTool tool{ModelColorPaintTool::Brush};
    float smartFillAngleDeg{30.0f};
    float heightRange{0.2f};
    float gapArea{5.0f};
    bool edgeDetection{true};
    bool erase{false};
};

struct ModelColorStrokeRequest {
    std::uint64_t token{0};
    std::uint64_t generation{0};
    DBInstanceID modelId;
    DBInstanceID partId;
    std::uint64_t meshRevision{0};
    int baseRevision{0};
    int sourceTriangleCount{0};
    std::string topologyFingerprint;
    std::shared_ptr<const OrcaIndexedMesh> topology;
    // Formal paint leaves live in Paint Space: world axes with the model's
    // translation removed. This matrix projects them back into Part-local data.
    SurfaceColorMatrix4 localFromPaint{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};
    ManualSupportOrcaScaffold::TriangleSplittingData baseState;
    ManualSupportOrcaScaffold::FacetLabelId labelId{1};
    std::vector<QColor> palette;
    std::vector<ModelColorStrokeSample> samples;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct ModelColorStrokeResult {
    std::uint64_t token{0};
    DBInstanceID modelId;
    DBInstanceID partId;
    std::uint64_t meshRevision{0};
    int baseRevision{0};
    int committedRevision{0};
    bool success{false};
    bool changed{false};
    bool reusedWorkerSession{false};
    QString error;
    qint64 computeMs{0};
    std::string encodedState;
    ManualSupportOrcaScaffold::TriangleSplittingData state;
    std::shared_ptr<const SurfaceColorRenderProjectionResult> renderProjection;
    int paintedFacetCount{0};
    int usedColorCount{0};
    double paintedSurfaceArea{0.0};
};

struct ModelColorGapPreviewRequest {
    std::uint64_t token{0};
    std::uint64_t generation{0};
    DBInstanceID modelId;
    DBInstanceID partId;
    std::shared_ptr<const OrcaIndexedMesh> topology;
    ManualSupportOrcaScaffold::TriangleSplittingData baseState;
    float gapArea{0.0f};
};

struct ModelColorGapPreviewResult {
    std::uint64_t token{0};
    DBInstanceID modelId;
    DBInstanceID partId;
    bool success{false};
    QString error;
    qint64 computeMs{0};
    std::vector<ManualSupportOrcaScaffold::LeafTriangle> candidates;
};

struct ModelColorPaintWorkerState;

/**
 * Serial application service for formal surface-color computation.
 *
 * Worker code receives immutable value snapshots and never touches the
 * document, DB objects, actors or GUI objects. Results are validated and
 * committed on this object's GUI thread.
 */
class ModelColorPaintComputeCoordinator final : public QObject {
    Q_OBJECT

public:
    static ModelColorPaintComputeCoordinator* instance();
    ~ModelColorPaintComputeCoordinator() override;

    std::uint64_t submit(ModelColorStrokeRequest request);
    std::uint64_t submitGapPreview(ModelColorGapPreviewRequest request);
    void invalidateGapPreview(const DBInstanceID& modelId);
    void invalidateModel(const DBInstanceID& modelId);
    bool hasPending(const DBInstanceID& modelId) const;

signals:
    void strokeFinished(std::shared_ptr<const ModelColorStrokeResult> result);
    void gapPreviewFinished(std::shared_ptr<const ModelColorGapPreviewResult> result);

private:
    explicit ModelColorPaintComputeCoordinator(QObject* parent = nullptr);
    void finishOnGuiThread(std::shared_ptr<ModelColorStrokeRequest> request,
                           std::shared_ptr<ModelColorStrokeResult> result);

    QThread m_workerThread;
    QObject* m_workerContext{nullptr};
    std::unique_ptr<ModelColorPaintWorkerState> m_workerState;
    std::uint64_t m_nextToken{1};
    std::unordered_map<std::uint64_t, std::uint64_t> m_generations;
    std::unordered_map<std::uint64_t, std::uint64_t> m_pendingTokens;
    std::unordered_map<std::uint64_t, std::shared_ptr<std::atomic_bool>> m_activeCancellations;
    std::unordered_map<std::uint64_t, std::uint64_t> m_gapPreviewGenerations;
    std::unordered_map<std::uint64_t, std::uint64_t> m_latestGapPreviewTokens;
};
