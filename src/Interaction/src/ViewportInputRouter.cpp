#include "ViewportInputRouter.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_QML_SINGLETON(ViewportInputRouter, "ViewportInputRouter")

#include "ActionManager.hpp"
#include "InteractionInputEvent.hpp"
#include "KeyboardShortcutManager.hpp"
#include "QuickActionController.hpp"
#include "Foundation/Log.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

ViewportInputRouter::ViewportInputRouter(QObject* parent)
    : QObject(parent),
      m_keyboardShortcutManager(KeyboardShortcutManager::getInstance()) {
}

void ViewportInputRouter::rememberPointer(
    std::uint64_t viewId,
    const QPointF& position) {
    if (viewId == 0) {
        return;
    }
    m_pointerPositions.insert(static_cast<qulonglong>(viewId), position);
    m_lastInputViewId = viewId;
}

QPointF ViewportInputRouter::pointerPosition(std::uint64_t viewId) const {
    return m_pointerPositions.value(static_cast<qulonglong>(viewId));
}

std::uint64_t ViewportInputRouter::lastInputViewId() const noexcept {
    return m_lastInputViewId;
}

void ViewportInputRouter::pushMouseEvent(
    qulonglong viewIdValue,
    const QString& type,
    Qt::MouseButton button,
    Qt::MouseButtons buttons,
    Qt::MouseEventFlags flags,
    Qt::KeyboardModifiers modifiers,
    bool wasHeld,
    qreal localX,
    qreal localY,
    qreal windowX,
    qreal windowY,
    qreal screenX,
    qreal screenY) {
    Q_UNUSED(flags)
    Q_UNUSED(wasHeld)

    QEvent::Type eventType = QEvent::None;
    if (type == QStringLiteral("move")) {
        eventType = QEvent::MouseMove;
    } else if (type == QStringLiteral("press")) {
        eventType = QEvent::MouseButtonPress;
    } else if (type == QStringLiteral("release")) {
        eventType = QEvent::MouseButtonRelease;
    } else {
        LOG_WARN("ViewportInputRouter: unknown mouse event type {}",
                 type.toStdString());
        return;
    }

    const auto viewId = static_cast<std::uint64_t>(viewIdValue);
    rememberPointer(viewId, QPointF(localX, localY));
    QMouseEvent event(
        eventType,
        QPointF(localX, localY),
        QPointF(windowX, windowY),
        QPointF(screenX, screenY),
        button,
        buttons,
        modifiers);

    auto* actions = ActionManager::getInstance();
    if (!actions) {
        return;
    }
    const MouseInputEvent input{viewId, &event};
    switch (eventType) {
    case QEvent::MouseButtonPress:
        actions->onMousePressEvent(input);
        break;
    case QEvent::MouseMove:
        actions->onMouseMoveEvent(input);
        break;
    case QEvent::MouseButtonRelease:
        actions->onMouseReleaseEvent(input);
        break;
    default:
        break;
    }
}

void ViewportInputRouter::pushKeyEvent(
    qulonglong viewIdValue,
    bool accepted,
    int count,
    bool isAutoRepeat,
    int key,
    int modifiers,
    unsigned int nativeScanCode,
    const QString& text,
    int pressedOrReleased) {
    Q_UNUSED(accepted)
    Q_UNUSED(nativeScanCode)

    const auto eventType = pressedOrReleased == 0
        ? QEvent::KeyPress
        : QEvent::KeyRelease;
    QKeyEvent event(
        eventType,
        key,
        static_cast<Qt::KeyboardModifiers>(modifiers),
        text,
        isAutoRepeat,
        count);

    const auto viewId = static_cast<std::uint64_t>(viewIdValue);
    m_lastInputViewId = viewId;
    if (auto* actions = ActionManager::getInstance()) {
        const KeyInputEvent input{viewId, &event};
        const bool consumed = pressedOrReleased == 0
            ? actions->onKeyPressEvent(input)
            : actions->onKeyReleaseEvent(input);
        if (consumed) {
            return;
        }
    }

    if (pressedOrReleased != 0 || isAutoRepeat) {
        return;
    }
    if (m_keyboardShortcutManager &&
        m_keyboardShortcutManager->handleKeyEvent(key, modifiers)) {
        return;
    }

    const bool isLetterOrDigit =
        (key >= Qt::Key_A && key <= Qt::Key_Z) ||
        (key >= Qt::Key_0 && key <= Qt::Key_9);
    const bool hasOnlyShift = modifiers == Qt::NoModifier ||
        modifiers == Qt::ShiftModifier;
    if (isLetterOrDigit && hasOnlyShift && !text.isEmpty()) {
        QuickActionController::getInstance()->handleKeyboardInput(
            text, QPointF(400, 300));
    }
}

void ViewportInputRouter::pushWheelEvent(
    qulonglong viewIdValue,
    const QPointF& pos,
    const QPointF& globalPos,
    QPoint pixelDelta,
    QPoint angleDelta,
    Qt::MouseButtons buttons,
    Qt::KeyboardModifiers modifiers,
    Qt::ScrollPhase phase,
    bool inverted,
    Qt::MouseEventSource source) {
    const auto viewId = static_cast<std::uint64_t>(viewIdValue);
    rememberPointer(viewId, pos);
    QWheelEvent event(
        pos,
        globalPos,
        pixelDelta,
        angleDelta,
        buttons,
        modifiers,
        phase,
        inverted,
        source);
    if (auto* actions = ActionManager::getInstance()) {
        actions->onWheelEvent(WheelInputEvent{viewId, &event});
    }
}
