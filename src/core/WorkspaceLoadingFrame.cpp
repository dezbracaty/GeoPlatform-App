#include "WorkspaceLoadingFrame.hpp"
#include "CoreRegistration.hpp"

#include <QQuickWindow>
#include <QSGSimpleRectNode>

GPLATFORM_QML_TYPE_REGISTRATION(
    GPlatform::Core::moduleRegistrations, "GPlatform", 1, 0,
    WorkspaceLoadingFrame, "WorkspaceLoadingFrame")

WorkspaceLoadingFrame::WorkspaceLoadingFrame(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::visibleChanged, this,
            &WorkspaceLoadingFrame::invalidatePresentation);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* window) {
        disconnect(m_windowVisibilityConnection);
        if (window) {
            m_windowVisibilityConnection = connect(
                window, &QWindow::visibilityChanged, this,
                &WorkspaceLoadingFrame::invalidatePresentation);
        }
        invalidatePresentation();
    });
}

void WorkspaceLoadingFrame::setColor(const QColor& color) {
    if (m_color == color) return;
    m_color = color;
    update();
    emit colorChanged();
}

void WorkspaceLoadingFrame::setArmed(bool armed) {
    if (m_armed == armed) return;
    m_armed = armed;
    invalidatePresentation();
    emit armedChanged();
}

void WorkspaceLoadingFrame::invalidatePresentation() {
    ++m_request;
    if (m_armed) update();
}

QSGNode* WorkspaceLoadingFrame::updatePaintNode(
    QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGSimpleRectNode*>(oldNode);
    if (!node) node = new QSGSimpleRectNode;
    node->setRect(boundingRect());
    node->setColor(m_color);

    auto* renderWindow = window();
    if (m_armed && m_request != m_syncedRequest && isVisible() && opacity() > 0 &&
        !boundingRect().isEmpty() && renderWindow && renderWindow->isVisible() &&
        renderWindow->windowState() != Qt::WindowMinimized) {
        // Synchronization runs with the GUI state frozen. Tie the callback
        // to the frame containing this actual node, then return to the GUI
        // thread before creating a workspace.
        const auto request = m_syncedRequest = m_request;
        connect(renderWindow, &QQuickWindow::afterFrameEnd, this, [this, request] {
            if (request == m_request && m_armed && isVisible() && window() &&
                window()->isVisible() && window()->windowState() != Qt::WindowMinimized) {
                emit framePresented();
            }
        }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    }
    return node;
}
