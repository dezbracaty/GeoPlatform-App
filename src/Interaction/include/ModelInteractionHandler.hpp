#pragma once

#include "IActionHandlerBase.hpp"
#include "PickService.hpp"

#include <SystemTypes.hpp>

#include <QHash>
#include <QPoint>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class ActorDB;
class TransactionGuard;

/**
 * Owns ordinary model pointer interaction: synchronous CPU selection and
 * direct XY dragging.
 */
class ModelInteractionHandler final : public IActionHandlerBase {
    Q_OBJECT

public:
    explicit ModelInteractionHandler(QObject* parent = nullptr);
    ~ModelInteractionHandler() override;

    HandlerType getHandlerType() const override {
        return HandlerType::Middleware;
    }

    bool isPersistent() const override {
        return true;
    }

    bool supportsEnvironment(const QString& environment) const override;

    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    void onSuspend() override;
    void onResume() override;
    std::optional<QVariantMap> getAIDescriptor(
        const QString& actionCode) const override;

private:
    struct DragState {
        bool armed{false};
        DBInstanceID viewId{INVALID_DB_ID};
        DBInstanceID actorId{INVALID_DB_ID};
        QPoint pressPosition;
        QPoint latestPosition;
        Qt::KeyboardModifiers latestModifiers{Qt::NoModifier};
        Vector3 initialActorPosition;
        Vector3 anchorWorldPosition;
        bool requiresThreshold{true};
        std::unique_ptr<TransactionGuard> transaction;
    };

    struct BlankClickCandidate {
        DBInstanceID viewId{INVALID_DB_ID};
        QPoint pressPosition;
    };

    bool onMousePressEvent(QMouseEvent* event) override;
    bool onMouseMoveEvent(QMouseEvent* event) override;
    bool onMouseReleaseEvent(QMouseEvent* event) override;
    bool onKeyPressEvent(QKeyEvent* event) override;

    void handleAction(const std::shared_ptr<ActionContext>& context);
    void executeSelectionAction(const std::shared_ptr<ActionContext>& context);
    const QHash<QString, QVariantMap>& aiDescriptorTable() const;

    void selectObject(DBInstanceID id, bool additive);
    bool beginModelGesture(const std::shared_ptr<ActorDB>& actor,
                           DBInstanceID viewId,
                           const QPoint& pressPosition,
                           Qt::KeyboardModifiers modifiers);
    void applySelection(const std::vector<DBInstanceID>& selectedIds);
    void clearSelection();
    void hideSelectionVisuals();
    void restoreSelectionVisuals();
    void applySelectionVisual(DBInstanceID id);
    void clearSelectionVisual(DBInstanceID id);
    void createSelectionBox(DBInstanceID actorId);
    void destroySelectionBox(DBInstanceID actorId);

    void updateDrag(const QPoint& screenPosition,
                    Qt::KeyboardModifiers modifiers);
    std::optional<Vector3> intersectDragPlane(
        const QPoint& screenPosition) const;
    std::shared_ptr<ActorDB> resolveDraggedActor() const;
    bool draggedActorIsStillSelected() const;
    void finishDrag(bool commit);

    DragState m_drag;
    std::optional<BlankClickCandidate> m_blankClick;
    std::unordered_map<DBInstanceID, DBInstanceID> m_selectionBoxes;
    bool m_selectionInputSuspended{false};
};
