#include "NotificationManager.h"
#include "BaseUIRegistration.hpp"
#include "Foundation/Log.h"
#include <QThread>
#include <utility>

using namespace GPlatform::Notifications;

std::atomic<NotificationManager*> NotificationManager::s_instance{nullptr};

NotificationManager::NotificationManager(QObject* parent) : QObject(parent) {
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
NotificationManager::~NotificationManager() {
    closeDesktopNotifications();
    s_instance.store(nullptr, std::memory_order_release);
}
void NotificationManager::initialize(QCoreApplication& app) {
    Q_ASSERT(QThread::currentThread() == app.thread());
    if (s_instance.load(std::memory_order_acquire)) return;
    s_instance.store(new NotificationManager(&app), std::memory_order_release);
}
NotificationManager* NotificationManager::instance() {
    auto* manager = s_instance.load(std::memory_order_acquire);
    if (!manager) qFatal("NotificationManager::initialize must run before notification producers");
    return manager;
}
void NotificationManager::dispatch(std::function<void()> operation) {
    if (QThread::currentThread() == thread()) operation();
    else QMetaObject::invokeMethod(this, std::move(operation), Qt::QueuedConnection);
}
void NotificationManager::configureDesktopNotifications(
    std::unique_ptr<IDesktopNotificationBackend> backend, std::function<bool()> isForeground) {
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(!backend || (!backend->parent() && backend->thread() == thread()));
    if (m_closed.load()) return;
    if (m_desktopBackend) m_desktopBackend->stop();
    m_desktopBackend = std::move(backend);
    m_isForeground = std::move(isForeground);
    if (m_desktopBackend) {
        connect(m_desktopBackend.get(), &IDesktopNotificationBackend::activationRequested,
                this, [this] { if (!m_closed.load()) emit activationRequested(); });
        m_desktopBackend->start();
    }
    refreshDesktopStatus();
}
void NotificationManager::setDesktopNotificationsEnabled(bool enabled) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_desktopEnabled == enabled) return;
    m_desktopEnabled = enabled;
    emit desktopNotificationsEnabledChanged();
}
void NotificationManager::refreshDesktopStatus() {
    Q_ASSERT(QThread::currentThread() == thread());
    const bool available = !m_closed.load() && m_desktopBackend && m_desktopBackend->isAvailable();
    if (m_desktopAvailable == available) return;
    m_desktopAvailable = available;
    emit desktopStatusChanged();
}
NotificationManager::SendResult NotificationManager::notify(
    const QString& title, const QString& body, Severity severity) {
    if (m_closed.load()) return Closed;
    const Notification notification{title, body, severity};
    if (QThread::currentThread() == thread()) return deliverNotification(notification);
    const bool queued = QMetaObject::invokeMethod(this, [this, notification] {
        const auto result = deliverNotification(notification);
        LOG_DEBUG("Notification submission result={}", int(result));
    }, Qt::QueuedConnection);
    return queued ? Queued : Closed;
}
NotificationManager::SendResult NotificationManager::deliverNotification(const Notification& notification) {
    if (m_closed.load()) return Closed;
    if ((notification.title.trimmed().isEmpty() && notification.body.trimmed().isEmpty())
        || notification.severity < Severity::Info || notification.severity > Severity::Error)
        return Invalid;
    switch (notification.severity) {
    case Severity::Info: showInfo(notification.title, notification.body); break;
    case Severity::Success: showSuccess(notification.title, notification.body); break;
    case Severity::Warning: showWarning(notification.title, notification.body); break;
    case Severity::Error: showError(notification.title, notification.body); break;
    }
    // In-app observers may change settings or initiate shutdown synchronously.
    refreshDesktopStatus();
    if (m_closed.load()) return Closed;
    if (!m_desktopEnabled) return Disabled;
    if (!m_isForeground || m_isForeground()) return Foreground;
    if (m_closed.load()) return Closed;
    if (!m_desktopBackend || !m_desktopBackend->isAvailable()) return Unavailable;
    return m_desktopBackend->send(notification) ? SubmittedToQt : Unavailable;
}
void NotificationManager::closeDesktopNotifications() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed.exchange(true)) return;
    if (m_desktopBackend) m_desktopBackend->stop();
    m_desktopBackend.reset();
    m_isForeground = {};
    refreshDesktopStatus();
}

void NotificationManager::showError(const QString& title, const QString& message) {
    dispatch([this, title, message] {
        LOG_ERROR("Error notification: {} - {}", title.toStdString(), message.toStdString());
        m_lastErrorTitle = title;
        m_lastErrorMessage = message;
        emit errorOccurred(title, message);
    });
}

void NotificationManager::showWarning(const QString& title, const QString& message) {
    dispatch([this, title, message] {
        LOG_WARN("Warning notification: {} - {}", title.toStdString(), message.toStdString());
        emit warningOccurred(title, message);
    });
}

void NotificationManager::showInfo(const QString& title, const QString& message) {
    dispatch([this, title, message] {
        LOG_INFO("Info notification: {} - {}", title.toStdString(), message.toStdString());
        emit infoOccurred(title, message);
    });
}

void NotificationManager::showSuccess(const QString& title, const QString& message) {
    dispatch([this, title, message] {
        LOG_INFO("Success notification: {} - {}", title.toStdString(), message.toStdString());
        emit successOccurred(title, message);
    });
}

void NotificationManager::showDialog(const QString& title, const QString& message, bool critical) {
    dispatch([this, title, message, critical] {
        LOG_INFO("Dialog requested: {} - {} (critical: {})",
                 title.toStdString(), message.toStdString(), critical);
        emit dialogRequested(title, message, critical);
    });
}

void NotificationManager::showSliceConfigurationError(
    const QString& presetName, const QString& message,
    const QString& optionKey) {
    dispatch([this, presetName, message, optionKey] {
        LOG_WARN(
            "Slice configuration error requested: preset='{}' option='{}' diagnostic='{}'",
            presetName.toStdString(), optionKey.toStdString(), message.toStdString());
        emit sliceConfigurationErrorRequested(presetName, message, optionKey);
    });
}

void NotificationManager::showVerificationFailure(int stepNumber, const QString& stepDesc,
                                                 double similarity, double threshold,
                                                 const QString& expectedImg, const QString& actualImg,
                                                 const QString& diffImg, const QString& reportPath) {
    dispatch([this, stepNumber, stepDesc, similarity, threshold, expectedImg, actualImg, diffImg, reportPath] {
        LOG_INFO("Verification failure at step {}: similarity {:.2f}% < threshold {:.2f}%",
                 stepNumber, similarity * 100, threshold * 100);
        LOG_INFO("Expected image: {}", expectedImg.toStdString());
        LOG_INFO("Actual image: {}", actualImg.toStdString());
        LOG_INFO("Diff image: {}", diffImg.toStdString());
        LOG_INFO("Report path: {}", reportPath.toStdString());
        emit verificationFailureOccurred(stepNumber, stepDesc, similarity, threshold,
                                        expectedImg, actualImg, diffImg, reportPath);
    });
}

void NotificationManager::showProgress(const QString& message, int value) {
    dispatch([this, message, value] {
        LOG_DEBUG("Progress shown: {} ({}%)", message.toStdString(), value);
        emit progressShown(message, value);
    });
}

void NotificationManager::hideProgress() {
    dispatch([this] {
        LOG_DEBUG("Progress hidden");
        emit progressHidden();
    });
}

REGISTER_BASE_UI_QML_SINGLETON_CUSTOM(
    NotificationManager, "NotificationManager", &NotificationManager::create)
