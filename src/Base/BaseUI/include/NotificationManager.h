#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>
#include <QCoreApplication>
#include <atomic>
#include <functional>
#include <memory>
#include "Notifications/IDesktopNotificationBackend.hpp"

// Process-wide UI notification port exposed to C++ producers and QML.
class NotificationManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastErrorTitle READ lastErrorTitle NOTIFY errorOccurred)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY errorOccurred)

public:
    static void initialize(QCoreApplication& app);
    static NotificationManager* instance();
    enum SendResult { Queued, SubmittedToQt, Disabled, Foreground, Unavailable, Invalid, Closed };
    Q_ENUM(SendResult)

    // GUI-thread setup. The application supplies platform UI and foreground state.
    void configureDesktopNotifications(
        std::unique_ptr<GPlatform::Notifications::IDesktopNotificationBackend> backend,
        std::function<bool()> isForeground);
    // One-shot notification: existing in-app signals plus desktop delivery in the background.
    // Worker calls return Queued; GUI calls return the desktop submission result.
    Q_INVOKABLE SendResult notify(const QString& title, const QString& body,
        GPlatform::Notifications::Severity severity = GPlatform::Notifications::Severity::Info);
    void closeDesktopNotifications();
    bool desktopNotificationsEnabled() const { return m_desktopEnabled; }
    void setDesktopNotificationsEnabled(bool enabled);
    bool desktopAvailable() const { return m_desktopAvailable; }
    Q_INVOKABLE void refreshDesktopStatus();
    Q_PROPERTY(bool desktopNotificationsEnabled READ desktopNotificationsEnabled
               WRITE setDesktopNotificationsEnabled NOTIFY desktopNotificationsEnabledChanged)
    Q_PROPERTY(bool desktopAvailable READ desktopAvailable NOTIFY desktopStatusChanged)

    // For QML registration
    static NotificationManager* create(QQmlEngine *qmlEngine, QJSEngine *jsEngine) {
        Q_ASSERT(qmlEngine && qmlEngine->thread() == instance()->thread());
        Q_UNUSED(jsEngine)
        return instance();
    }

    QString lastErrorTitle() const { return m_lastErrorTitle; }
    QString lastErrorMessage() const { return m_lastErrorMessage; }

    // C++ API for showing notifications (also callable from QML)
    Q_INVOKABLE void showError(const QString& title, const QString& message);
    Q_INVOKABLE void showWarning(const QString& title, const QString& message);
    Q_INVOKABLE void showInfo(const QString& title, const QString& message);
    Q_INVOKABLE void showSuccess(const QString& title, const QString& message);

    // For dialogs
    Q_INVOKABLE void showDialog(const QString& title, const QString& message, bool critical = false);
    Q_INVOKABLE void showSliceConfigurationError(
        const QString& presetName, const QString& message,
        const QString& optionKey);

    // For verification failures
    void showVerificationFailure(int stepNumber, const QString& stepDesc,
                                double similarity, double threshold,
                                const QString& expectedImg, const QString& actualImg,
                                const QString& diffImg, const QString& reportPath);

    // For progress
    void showProgress(const QString& message, int value = -1);
    void hideProgress();

signals:
    void desktopNotificationsEnabledChanged();
    void desktopStatusChanged();
    void activationRequested();
    void fiberFillDebugAvailable(const QString& previewToken, int rejectedCount);
    // Notification signals for QML
    void errorOccurred(const QString& title, const QString& message);
    void warningOccurred(const QString& title, const QString& message);
    void infoOccurred(const QString& title, const QString& message);
    void successOccurred(const QString& title, const QString& message);

    // Dialog signal
    void dialogRequested(const QString& title, const QString& message, bool critical);
    void sliceConfigurationErrorRequested(
        const QString& presetName, const QString& message,
        const QString& optionKey);

    // Verification failure signal
    void verificationFailureOccurred(int stepNumber, const QString& stepDesc,
                                    double similarity, double threshold,
                                    const QString& expectedImg, const QString& actualImg,
                                    const QString& diffImg, const QString& reportPath);

    // Progress signals
    void progressShown(const QString& message, int value);
    void progressHidden();

private:
    NotificationManager(QObject* parent = nullptr);
    ~NotificationManager() override;
    void dispatch(std::function<void()> operation);
    SendResult deliverNotification(const GPlatform::Notifications::Notification& notification);

    static std::atomic<NotificationManager*> s_instance;
    std::unique_ptr<GPlatform::Notifications::IDesktopNotificationBackend> m_desktopBackend;
    std::function<bool()> m_isForeground;
    std::atomic_bool m_closed{false};
    bool m_desktopEnabled = false;
    bool m_desktopAvailable = false;

    QString m_lastErrorTitle;
    QString m_lastErrorMessage;
};
