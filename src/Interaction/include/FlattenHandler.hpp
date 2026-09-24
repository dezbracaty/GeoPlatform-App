#pragma once

#include "MeshRayQuery.hpp"
#include "PlaneGrouping.hpp"
#include "StandardActionHandler.hpp"
#include <TempDBScope.hpp>
#include <Transform.hpp>

#include <QPoint>
#include <memory>
#include <vector>

class ModelInstanceDB;
class TransientPolyDataActorDB;

/** Manual place-on-face tool backed by renderer-neutral temporary actors. */
class FlattenHandler final : public StandardActionHandler {
    Q_OBJECT

public:
    explicit FlattenHandler(QObject* parent = nullptr);
    ~FlattenHandler() override;

    bool isPersistent() const override { return true; }
    bool supportsEnvironment(const QString& environment) const override;

    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    void onSuspend() override { onExit(); }

    bool onMousePressEvent(QMouseEvent* event) override;
    bool onMouseMoveEvent(QMouseEvent* event) override;
    bool onMouseReleaseEvent(QMouseEvent* event) override;
    bool onKeyPressEvent(QKeyEvent* event) override;

private:
    void startCandidateBuild(const std::shared_ptr<ModelInstanceDB>& instance);
    void clearCandidateState();
    bool updateHoveredGroup(const QPoint& screenPosition);
    void setHoveredGroup(int groupIndex);
    void rebuildHoveredPreview();
    bool flattenGroup(int groupIndex);

    static DBInstanceID requestedModelId(const QVariantMap& params);

    TempDBScope m_previewScope;
    std::shared_ptr<TransientPolyDataActorDB> m_candidatePreview;
    std::shared_ptr<TransientPolyDataActorDB> m_hoverPreview;
    MeshRayQuery m_candidateRayQuery;
    placement::PlaneGroupingResult m_planeGroups;
    placement::PlaneOverlayMesh m_planeOverlay;
    DBInstanceID m_modelId{INVALID_DB_ID};
    std::uint64_t m_meshRevision{0};
    Transform::Matrix4 m_candidateTransform{Transform::Matrix4::Identity()};
    std::uint64_t m_buildGeneration{0};
    int m_hoveredGroup{-1};
    int m_pendingGroup{-1};
    bool m_candidatesReady{false};
    bool m_modelInputSuspended{false};
    bool m_leftDown{false};
    bool m_pressedOnFace{false};
    int m_pressedGroup{-1};
    QPoint m_pressPosition;
};
