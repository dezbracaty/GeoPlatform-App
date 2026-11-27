#pragma once

#include <QColor>
#include <QQuickItem>

// The viewport's loading background also acknowledges its first Qt frame.
class WorkspaceLoadingFrame : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool armed READ armed WRITE setArmed NOTIFY armedChanged)

public:
    explicit WorkspaceLoadingFrame(QQuickItem* parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& color);
    bool armed() const { return m_armed; }
    void setArmed(bool armed);

signals:
    void colorChanged();
    void armedChanged();
    void framePresented();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    void invalidatePresentation();

    QColor m_color;
    bool m_armed = false;
    quint64 m_request = 1;
    quint64 m_syncedRequest = 0;
    QMetaObject::Connection m_windowVisibilityConnection;
};
