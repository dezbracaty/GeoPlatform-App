#include "AIAccountBridge.hpp"
#include "BridgeRegistration.hpp"

#include "AI/CodexAppServerClient.hpp"
#include "Foundation/Log.h"

using GPlatform::AI::CodexAppServerClient;

AIAccountBridge::AIAccountBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    auto* client = CodexAppServerClient::instance();

    connect(client, &CodexAppServerClient::ready, this, [client]() {
        client->readAccount();
    });
    connect(client,
            &CodexAppServerClient::accountChanged,
            this,
            [this](bool signedIn, const QString& email, const QString& planType) {
                applyAccount(signedIn, email, planType);
            });
    connect(client,
            &CodexAppServerClient::deviceCodeReady,
            this,
            [this](const QString&, const QString& verificationUrl, const QString& userCode) {
                m_verificationUrl = verificationUrl;
                m_userCode = userCode;
                setErrorMessage({});
                setAuthState(Authorizing);
                emit deviceCodeChanged();
            });
    connect(client,
            &CodexAppServerClient::loginCompleted,
            this,
            [this, client](bool success, const QString& error) {
                clearDeviceCode();
                if (success) {
                    m_cancelRequested = false;
                    setAuthState(Checking);
                    client->readAccount();
                    return;
                }
                if (m_cancelRequested) {
                    m_cancelRequested = false;
                    setErrorMessage({});
                    setAuthState(SignedOut);
                    return;
                }
                setErrorMessage(error.isEmpty()
                                    ? QStringLiteral("ChatGPT/Codex login failed.")
                                    : error);
                setAuthState(Error);
            });
    connect(client,
            &CodexAppServerClient::requestFailed,
            this,
            [this](const QString& method, const QString& error) {
                if (method != "app-server" && !method.startsWith("account/")) {
                    return;
                }
                clearDeviceCode();
                setErrorMessage(error);
                setAuthState(Error);
            });
}

AIAccountBridge* AIAccountBridge::instance() {
    static auto* bridge = new AIAccountBridge();
    return bridge;
}

AIAccountBridge* AIAccountBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

AIAccountBridge::AuthState AIAccountBridge::authState() const {
    return m_authState;
}

bool AIAccountBridge::signedIn() const {
    return m_authState == SignedIn;
}

bool AIAccountBridge::busy() const {
    return m_authState == Checking || m_authState == Authorizing;
}

QString AIAccountBridge::email() const {
    return m_email;
}

QString AIAccountBridge::planType() const {
    return m_planType;
}

QString AIAccountBridge::verificationUrl() const {
    return m_verificationUrl;
}

QString AIAccountBridge::userCode() const {
    return m_userCode;
}

QString AIAccountBridge::errorMessage() const {
    return m_errorMessage;
}

void AIAccountBridge::refreshAccount() {
    // Keep an already resolved account state visible while revalidating it. The
    // App Server suppresses duplicate accountChanged signals, so forcing the UI
    // back to Checking here could otherwise leave it there indefinitely.
    if (m_authState == Authorizing) {
        return;
    }
    m_cancelRequested = false;
    clearDeviceCode();
    setErrorMessage({});
    if (m_authState == Error) {
        setAuthState(Checking);
    }

    auto* client = CodexAppServerClient::instance();
    if (client->isReady()) {
        client->readAccount();
        return;
    }
    client->ensureStarted();
}

void AIAccountBridge::startDeviceCodeLogin() {
    if (m_authState != SignedOut && m_authState != Error) {
        return;
    }
    auto* client = CodexAppServerClient::instance();
    if (!client->isReady()) {
        setErrorMessage(QStringLiteral("Codex App Server is not ready. Retry account detection first."));
        setAuthState(Error);
        return;
    }

    m_cancelRequested = false;
    clearDeviceCode();
    setErrorMessage({});
    setAuthState(Authorizing);
    client->startDeviceCodeLogin();
}

void AIAccountBridge::cancelDeviceCodeLogin() {
    if (m_authState != Authorizing) {
        return;
    }
    m_cancelRequested = true;
    CodexAppServerClient::instance()->cancelDeviceCodeLogin();
}

void AIAccountBridge::logout() {
    if (m_authState != SignedIn) {
        return;
    }
    setErrorMessage({});
    setAuthState(Checking);
    CodexAppServerClient::instance()->logout();
}

void AIAccountBridge::openAccountCenter(const QString& section) {
    const QString target = section.trimmed().isEmpty() ? QStringLiteral("ai") : section.trimmed();
    emit accountCenterRequested(target);
}

void AIAccountBridge::setAuthState(AuthState state) {
    if (m_authState == state) {
        return;
    }
    m_authState = state;
    emit authStateChanged();
}

void AIAccountBridge::setErrorMessage(const QString& error) {
    if (m_errorMessage == error) {
        return;
    }
    m_errorMessage = error;
    emit errorMessageChanged();
}

void AIAccountBridge::clearDeviceCode() {
    if (m_verificationUrl.isEmpty() && m_userCode.isEmpty()) {
        return;
    }
    m_verificationUrl.clear();
    m_userCode.clear();
    emit deviceCodeChanged();
}

void AIAccountBridge::applyAccount(bool signedIn,
                                   const QString& email,
                                   const QString& planType) {
    const bool accountDataChanged = m_email != email || m_planType != planType;
    m_email = signedIn ? email : QString{};
    m_planType = signedIn ? planType : QString{};
    clearDeviceCode();
    setErrorMessage({});
    setAuthState(signedIn ? SignedIn : SignedOut);
    if (accountDataChanged || signedIn) {
        emit accountChanged();
    }
    LOG_INFO("Codex account state updated: signedIn={} plan={}",
             signedIn,
             m_planType.toStdString());
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    AIAccountBridge, "AIAccountBridge", &AIAccountBridge::create)
