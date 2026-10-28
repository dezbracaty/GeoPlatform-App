#include "QtTrayNotificationBackend.hpp"
#include <QApplication>
#include <QSystemTrayIcon>
#include <QThread>
#include <utility>

namespace GPlatform::Notifications {
QtTrayNotificationBackend::QtTrayNotificationBackend(QIcon icon) : m_icon(std::move(icon)) {}
QtTrayNotificationBackend::~QtTrayNotificationBackend() { stop(); }
void QtTrayNotificationBackend::start() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_tray || m_icon.isNull() || !qobject_cast<QApplication*>(QCoreApplication::instance())) return;
    m_tray = std::make_unique<QSystemTrayIcon>(m_icon);
    m_tray->setToolTip(QGuiApplication::applicationDisplayName());
    connect(m_tray.get(), &QSystemTrayIcon::messageClicked, this, &IDesktopNotificationBackend::activationRequested);
    connect(m_tray.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit activationRequested();
    });
    m_tray->show();
}
void QtTrayNotificationBackend::stop() {
    Q_ASSERT(QThread::currentThread() == thread());
    m_tray.reset();
}
bool QtTrayNotificationBackend::isAvailable() const {
    Q_ASSERT(QThread::currentThread() == thread());
    return m_tray && m_tray->isVisible() && !m_tray->icon().isNull()
        && QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
}
bool QtTrayNotificationBackend::send(const Notification& notification) {
    if (!isAvailable()) return false;
    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    if (notification.severity == Severity::Warning) icon = QSystemTrayIcon::Warning;
    if (notification.severity == Severity::Error) icon = QSystemTrayIcon::Critical;
    m_tray->showMessage(notification.title, notification.body, icon, 10000);
    return true;
}
}
