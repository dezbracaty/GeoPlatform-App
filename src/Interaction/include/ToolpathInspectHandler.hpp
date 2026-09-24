#pragma once
#include "IActionHandlerBase.hpp"
#include "SnapPreviewHandler.hpp"
#include <QTimer>

// Owns preview inspection input and queries, then presents the returned hit and
// maps its authored element to G-code. Rendering is delegated to the preview.
class ToolpathInspectHandler final : public IActionHandlerBase {
    Q_OBJECT
public:
    explicit ToolpathInspectHandler(QObject* parent = nullptr);
    ~ToolpathInspectHandler() override;
    HandlerType getHandlerType() const override { return HandlerType::Background; }
    void onEnter(std::shared_ptr<ActionContext>) override;
    void onExit() override;
    void onSuspend() override;
    void onResume() override;
    bool onKeyPressEvent(QKeyEvent*) override;
    bool onKeyReleaseEvent(QKeyEvent*) override;
    bool onMouseMoveEvent(QMouseEvent*) override;
    bool onMousePressEvent(QMouseEvent*) override;
    bool onMouseReleaseEvent(QMouseEvent*) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct SnapContext {
        GPlatform::Interaction::SnapPointerQuery query;
        std::vector<GPlatform::Interaction::SnapTarget> targets;
    };
    bool canQuery() const;
    void syncEnabled();
    void requestRefresh();
    void refresh();
    void clearSnap();
    void leaveViewport();
    void leave();
    void setModifiers(Qt::KeyboardModifiers modifiers);
    void handleKey(const QKeyEvent& event);
    std::optional<SnapContext> snapContext(DBInstanceID viewId);
    void inspectResult();
    GPlatform::Interaction::SnapPreviewHandler m_snapPreview;
    std::optional<GPlatform::Interaction::SnapResult> m_snapResult;
    DBInstanceID m_viewId;
    QPointF m_position;
    Qt::KeyboardModifiers m_modifiers{Qt::NoModifier};
    bool m_inside{false}, m_dragging{false}, m_updatesAllowed{true};
    QPointer<QWindow> m_pointerWindow;
    QTimer m_refresh;
    std::size_t m_listener{0};
};
