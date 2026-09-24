#pragma once

#include <QObject>
#include <QHash>
#include <QPointF>
#include <QString>
#include <qqmlregistration.h>
#include "stdafx.h"

class KeyboardShortcutManager;

class ViewportInputRouter final : public QObject {
    Q_OBJECT
    QML_SINGLETON

public:
    SINGLETON(ViewportInputRouter)

    Q_INVOKABLE void pushMouseEvent(
        qulonglong viewId,
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
        qreal screenY);

    Q_INVOKABLE void pushKeyEvent(
        qulonglong viewId,
        bool accepted,
        int count,
        bool isAutoRepeat,
        int key,
        int modifiers,
        unsigned int nativeScanCode,
        const QString& text,
        int pressedOrReleased);

    Q_INVOKABLE void pushWheelEvent(
        qulonglong viewId,
        const QPointF& pos,
        const QPointF& globalPos,
        QPoint pixelDelta,
        QPoint angleDelta,
        Qt::MouseButtons buttons,
        Qt::KeyboardModifiers modifiers,
        Qt::ScrollPhase phase,
        bool inverted,
        Qt::MouseEventSource source = Qt::MouseEventNotSynthesized);

    Q_INVOKABLE void pushPointerExited(qulonglong viewId) { emit pointerExited(viewId); }

signals:
    void pointerExited(qulonglong viewId);

public:
    QPointF pointerPosition(std::uint64_t viewId) const;
    std::uint64_t lastInputViewId() const noexcept;

private:
    explicit ViewportInputRouter(QObject* parent = nullptr);

    void rememberPointer(std::uint64_t viewId, const QPointF& position);

    KeyboardShortcutManager* m_keyboardShortcutManager{nullptr};
    QHash<qulonglong, QPointF> m_pointerPositions;
    std::uint64_t m_lastInputViewId{0};
};
