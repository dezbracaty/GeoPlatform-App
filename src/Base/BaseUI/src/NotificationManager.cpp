#include "NotificationManager.h"
#include "BaseUIRegistration.hpp"
#include "Foundation/Log.h"

NotificationManager* NotificationManager::s_instance = nullptr;

NotificationManager::NotificationManager(QObject* parent)
    : QObject(parent) {
}

NotificationManager* NotificationManager::instance() {
    if (!s_instance) {
        s_instance = new NotificationManager();
    }
    return s_instance;
}

void NotificationManager::showError(const QString& title, const QString& message) {
    LOG_ERROR("Error notification: {} - {}", title.toStdString(), message.toStdString());
    m_lastErrorTitle = title;
    m_lastErrorMessage = message;
    emit errorOccurred(title, message);
}

void NotificationManager::showWarning(const QString& title, const QString& message) {
    LOG_WARN("Warning notification: {} - {}", title.toStdString(), message.toStdString());
    emit warningOccurred(title, message);
}

void NotificationManager::showInfo(const QString& title, const QString& message) {
    LOG_INFO("Info notification: {} - {}", title.toStdString(), message.toStdString());
    emit infoOccurred(title, message);
}

void NotificationManager::showSuccess(const QString& title, const QString& message) {
    LOG_INFO("Success notification: {} - {}", title.toStdString(), message.toStdString());
    emit successOccurred(title, message);
}

void NotificationManager::showDialog(const QString& title, const QString& message, bool critical) {
    LOG_INFO("Dialog requested: {} - {} (critical: {})",
             title.toStdString(), message.toStdString(), critical);
    emit dialogRequested(title, message, critical);
}

void NotificationManager::showSliceConfigurationError(
    const QString& presetName, const QString& message,
    const QString& optionKey) {
    LOG_WARN(
        "Slice configuration error requested: preset='{}' option='{}' diagnostic='{}'",
        presetName.toStdString(), optionKey.toStdString(), message.toStdString());
    emit sliceConfigurationErrorRequested(presetName, message, optionKey);
}

void NotificationManager::showVerificationFailure(int stepNumber, const QString& stepDesc,
                                                 double similarity, double threshold,
                                                 const QString& expectedImg, const QString& actualImg,
                                                 const QString& diffImg, const QString& reportPath) {
    LOG_INFO("Verification failure at step {}: similarity {:.2f}% < threshold {:.2f}%",
             stepNumber, similarity * 100, threshold * 100);
    LOG_INFO("Expected image: {}", expectedImg.toStdString());
    LOG_INFO("Actual image: {}", actualImg.toStdString());
    LOG_INFO("Diff image: {}", diffImg.toStdString());
    LOG_INFO("Report path: {}", reportPath.toStdString());
    emit verificationFailureOccurred(stepNumber, stepDesc, similarity, threshold,
                                    expectedImg, actualImg, diffImg, reportPath);
}

void NotificationManager::showProgress(const QString& message, int value) {
    LOG_DEBUG("Progress shown: {} ({}%)", message.toStdString(), value);
    emit progressShown(message, value);
}

void NotificationManager::hideProgress() {
    LOG_DEBUG("Progress hidden");
    emit progressHidden();
}

REGISTER_BASE_UI_QML_SINGLETON_CUSTOM(
    NotificationManager, "NotificationManager", &NotificationManager::create)
