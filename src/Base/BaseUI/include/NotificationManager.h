#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>

// Process-wide UI notification port exposed to C++ producers and QML.
class NotificationManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastErrorTitle READ lastErrorTitle NOTIFY errorOccurred)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY errorOccurred)

public:
    static NotificationManager* instance();

    // For QML registration
    static NotificationManager* create(QQmlEngine *qmlEngine, QJSEngine *jsEngine) {
        Q_UNUSED(qmlEngine)
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
    ~NotificationManager() = default;

    static NotificationManager* s_instance;

    QString m_lastErrorTitle;
    QString m_lastErrorMessage;
};
