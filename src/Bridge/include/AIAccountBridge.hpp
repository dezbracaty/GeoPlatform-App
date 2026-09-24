#pragma once

#include <BridgeBase.hpp>

#include <QJSEngine>
#include <QQmlEngine>
#include <QString>
#include <qqmlregistration.h>

class AIAccountBridge final : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(AuthState authState READ authState NOTIFY authStateChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY accountChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY authStateChanged)
    Q_PROPERTY(QString email READ email NOTIFY accountChanged)
    Q_PROPERTY(QString planType READ planType NOTIFY accountChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY deviceCodeChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY deviceCodeChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    enum AuthState {
        Checking,
        SignedOut,
        Authorizing,
        SignedIn,
        Error,
    };
    Q_ENUM(AuthState)

    static AIAccountBridge* instance();
    static AIAccountBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    AuthState authState() const;
    bool signedIn() const;
    bool busy() const;
    QString email() const;
    QString planType() const;
    QString verificationUrl() const;
    QString userCode() const;
    QString errorMessage() const;

    Q_INVOKABLE void refreshAccount();
    Q_INVOKABLE void startDeviceCodeLogin();
    Q_INVOKABLE void cancelDeviceCodeLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void openAccountCenter(const QString& section = QStringLiteral("ai"));

signals:
    void authStateChanged();
    void accountChanged();
    void deviceCodeChanged();
    void errorMessageChanged();
    void accountCenterRequested(const QString& section);

private:
    explicit AIAccountBridge(QObject* parent = nullptr);
    ~AIAccountBridge() override = default;

    AIAccountBridge(const AIAccountBridge&) = delete;
    AIAccountBridge& operator=(const AIAccountBridge&) = delete;

    void setAuthState(AuthState state);
    void setErrorMessage(const QString& error);
    void clearDeviceCode();
    void applyAccount(bool signedIn, const QString& email, const QString& planType);

    AuthState m_authState = Checking;
    QString m_email;
    QString m_planType;
    QString m_verificationUrl;
    QString m_userCode;
    QString m_errorMessage;
    bool m_cancelRequested = false;
};
