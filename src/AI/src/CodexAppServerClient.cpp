#include "AI/CodexAppServerClient.hpp"

#include "Foundation/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

namespace GPlatform::AI {
namespace {
constexpr int kStartupTimeoutMs = 10000;

QString responseErrorMessage(const QJsonObject& errorObject) {
    const QString message = errorObject.value("message").toString().trimmed();
    if (!message.isEmpty()) {
        return message;
    }
    return QStringLiteral("Codex App Server returned an unspecified protocol error.");
}
} // namespace

CodexAppServerClient* CodexAppServerClient::instance() {
    static auto* client = new CodexAppServerClient();
    return client;
}

CodexAppServerClient::CodexAppServerClient(QObject* parent)
    : QObject(parent) {
    m_process.setProcessChannelMode(QProcess::SeparateChannels);

    connect(&m_process, &QProcess::started, this, [this]() {
        m_startupTimer.stop();
        setLifecycleState(LifecycleState::Initializing);

        QJsonObject clientInfo{
            {"name", "gplatform"},
            {"title", "GPlatform"},
            {"version", QCoreApplication::applicationVersion().isEmpty()
                            ? QStringLiteral("1.0.0")
                            : QCoreApplication::applicationVersion()},
        };
        QJsonObject capabilities{{"experimentalApi", true}};
        sendRequest("initialize",
                    QJsonObject{{"clientInfo", clientInfo}, {"capabilities", capabilities}});
        sendNotification("initialized");
    });

    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &CodexAppServerClient::processStdout);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray bytes = m_process.readAllStandardError();
        if (!bytes.isEmpty()) {
            LOG_DEBUG("Codex App Server wrote {} stderr bytes", bytes.size());
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_stoppingProcess ||
            (error == QProcess::Crashed && m_lifecycleState == LifecycleState::Stopped)) {
            return;
        }
        failLifecycle(QStringLiteral("Codex App Server process error: %1")
                          .arg(m_process.errorString()));
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_stoppingProcess || m_lifecycleState == LifecycleState::Stopped) {
                    return;
                }
                failLifecycle(
                    QStringLiteral("Codex App Server exited unexpectedly (code %1, status %2).")
                        .arg(exitCode)
                        .arg(exitStatus == QProcess::NormalExit ? "normal" : "crashed"));
            });

    m_startupTimer.setSingleShot(true);
    connect(&m_startupTimer, &QTimer::timeout, this, [this]() {
        failLifecycle(QStringLiteral("Timed out while starting Codex App Server."));
    });
}

CodexAppServerClient::~CodexAppServerClient() {
    stopProcess();
}

CodexAppServerClient::LifecycleState CodexAppServerClient::lifecycleState() const {
    return m_lifecycleState;
}

bool CodexAppServerClient::isReady() const {
    return m_lifecycleState == LifecycleState::Ready;
}

bool CodexAppServerClient::isSignedIn() const {
    return m_signedIn;
}

QString CodexAppServerClient::accountEmail() const {
    return m_accountEmail;
}

QString CodexAppServerClient::accountPlanType() const {
    return m_accountPlanType;
}

QString CodexAppServerClient::activeLoginId() const {
    return m_activeLoginId;
}

QString CodexAppServerClient::resolveExecutable(QString& error) const {
    error.clear();
    const QString configured = qEnvironmentVariable("GP_CODEX_EXECUTABLE").trimmed();
    if (!configured.isEmpty()) {
        const QFileInfo info(configured);
        if (!info.exists() || !info.isFile() || !info.isExecutable()) {
            error = QStringLiteral("GP_CODEX_EXECUTABLE is not an executable file: %1")
                        .arg(configured);
            return {};
        }
        return info.absoluteFilePath();
    }

    const QString discovered = QStandardPaths::findExecutable(QStringLiteral("codex"));
    if (discovered.isEmpty()) {
        error = QStringLiteral(
            "Codex CLI was not found in PATH. Install Codex CLI or set GP_CODEX_EXECUTABLE.");
        return {};
    }
    return discovered;
}

void CodexAppServerClient::ensureStarted() {
    if (m_lifecycleState == LifecycleState::Ready ||
        m_lifecycleState == LifecycleState::Starting ||
        m_lifecycleState == LifecycleState::Initializing) {
        return;
    }

    QString error;
    const QString executable = resolveExecutable(error);
    if (executable.isEmpty()) {
        failLifecycle(error);
        return;
    }

    m_pendingRequests.clear();
    m_stdoutBuffer.clear();
    setLifecycleState(LifecycleState::Starting);
    m_process.setProgram(executable);
    m_process.setArguments({QStringLiteral("app-server"), QStringLiteral("--stdio")});
    m_process.start();
    m_startupTimer.start(kStartupTimeoutMs);
    LOG_INFO("Starting Codex App Server from {}", executable.toStdString());
}

void CodexAppServerClient::readAccount() {
    if (!isReady()) {
        emit requestFailed("account/read", "Codex App Server is not ready.");
        return;
    }
    sendRequest("account/read", QJsonObject{{"refreshToken", false}});
}

void CodexAppServerClient::startDeviceCodeLogin() {
    if (!isReady()) {
        emit requestFailed("account/login/start", "Codex App Server is not ready.");
        return;
    }
    if (!m_activeLoginId.isEmpty()) {
        emit requestFailed("account/login/start", "A Device Code login is already active.");
        return;
    }
    sendRequest("account/login/start", QJsonObject{{"type", "chatgptDeviceCode"}});
}

void CodexAppServerClient::cancelDeviceCodeLogin() {
    if (!isReady()) {
        emit requestFailed("account/login/cancel", "Codex App Server is not ready.");
        return;
    }
    if (m_activeLoginId.isEmpty()) {
        emit requestFailed("account/login/cancel", "No Device Code login is active.");
        return;
    }
    sendRequest("account/login/cancel", QJsonObject{{"loginId", m_activeLoginId}});
}

void CodexAppServerClient::logout() {
    if (!isReady()) {
        emit requestFailed("account/logout", "Codex App Server is not ready.");
        return;
    }
    sendRequest("account/logout");
}

bool CodexAppServerClient::startConversationTurn(const QString& text,
                                                  const QString& baseInstructions,
                                                  const QJsonArray& dynamicTools) {
    if (!isReady()) {
        emit requestFailed("turn/start", "Codex App Server is not ready.");
        return false;
    }
    if (!m_signedIn) {
        emit requestFailed("turn/start", "ChatGPT/Codex account is not signed in.");
        return false;
    }
    if (!m_activeTurnId.isEmpty() || !m_pendingTurnText.isEmpty()) {
        emit requestFailed("turn/start", "A Codex conversation turn is already active.");
        return false;
    }

    m_pendingTurnText = text.trimmed();
    m_pendingBaseInstructions = baseInstructions;
    m_pendingDynamicTools = dynamicTools;
    m_agentText.clear();
    m_turnError.clear();

    if (m_pendingTurnText.isEmpty()) {
        m_pendingBaseInstructions.clear();
        m_pendingDynamicTools = {};
        emit requestFailed("turn/start", "Conversation input is empty.");
        return false;
    }

    if (m_threadId.isEmpty()) {
        startThreadForPendingTurn();
    } else {
        sendPendingTurn();
    }
    return true;
}

void CodexAppServerClient::respondToDynamicToolCall(const QJsonValue& requestId,
                                                     bool success,
                                                     const QJsonArray& contentItems) {
    QJsonObject response{{"id", requestId},
                         {"result", QJsonObject{{"success", success},
                                                {"contentItems", contentItems}}}};
    writeMessage(response);
}

void CodexAppServerClient::interruptConversationTurn() {
    if (!isReady() || m_threadId.isEmpty() || m_activeTurnId.isEmpty()) {
        return;
    }
    sendRequest("turn/interrupt", QJsonObject{{"threadId", m_threadId},
                                              {"turnId", m_activeTurnId}});
}

void CodexAppServerClient::resetConversation() {
    if (!m_activeTurnId.isEmpty()) {
        interruptConversationTurn();
    }
    m_threadId.clear();
    m_activeTurnId.clear();
    m_pendingTurnText.clear();
    m_pendingBaseInstructions.clear();
    m_pendingDynamicTools = {};
    m_agentText.clear();
    m_turnError.clear();
}

void CodexAppServerClient::setLifecycleState(LifecycleState state) {
    if (m_lifecycleState == state) {
        return;
    }
    m_lifecycleState = state;
    emit lifecycleStateChanged(state);
}

qint64 CodexAppServerClient::sendRequest(const QString& method, const QJsonObject& params) {
    const qint64 id = m_nextRequestId++;
    QJsonObject request{{"method", method}, {"id", id}, {"params", params}};
    if (!writeMessage(request)) {
        emit requestFailed(method, "Failed to write request to Codex App Server.");
        return -1;
    }
    m_pendingRequests.insert(id, method);
    return id;
}

bool CodexAppServerClient::sendNotification(const QString& method, const QJsonObject& params) {
    return writeMessage(QJsonObject{{"method", method}, {"params", params}});
}

bool CodexAppServerClient::writeMessage(const QJsonObject& message) {
    if (m_process.state() != QProcess::Running) {
        return false;
    }
    QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    return m_process.write(bytes) == bytes.size();
}

void CodexAppServerClient::processStdout() {
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
    while (true) {
        const qsizetype newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            failLifecycle(QStringLiteral("Invalid JSON received from Codex App Server: %1")
                              .arg(parseError.errorString()));
            return;
        }
        processMessage(document.object());
    }
}

void CodexAppServerClient::processMessage(const QJsonObject& message) {
    if (message.contains("id") && message.contains("method")) {
        processServerRequest(message.value("id"),
                             message.value("method").toString(),
                             message.value("params").toObject());
        return;
    }
    if (message.contains("id")) {
        processResponse(message);
        return;
    }
    processNotification(message.value("method").toString(),
                        message.value("params").toObject());
}

void CodexAppServerClient::processResponse(const QJsonObject& message) {
    const qint64 id = message.value("id").toVariant().toLongLong();
    const QString method = m_pendingRequests.take(id);
    if (method.isEmpty()) {
        LOG_WARN("Received Codex App Server response for unknown request id {}", id);
        return;
    }

    if (message.value("error").isObject()) {
        const QString error = responseErrorMessage(message.value("error").toObject());
        emit requestFailed(method, error);
        if (method == "initialize") {
            failLifecycle(error);
        }
        if (method == "thread/start" || method == "turn/start") {
            m_pendingTurnText.clear();
            m_pendingBaseInstructions.clear();
            m_pendingDynamicTools = {};
            emit conversationTurnCompleted(false, {}, error);
        }
        return;
    }

    const QJsonObject result = message.value("result").toObject();
    if (method == "initialize") {
        setLifecycleState(LifecycleState::Ready);
        emit ready();
        readAccount();
        return;
    }
    if (method == "account/read") {
        processAccountResult(result);
        return;
    }
    if (method == "account/login/start") {
        m_activeLoginId = result.value("loginId").toString();
        const QString verificationUrl = result.value("verificationUrl").toString();
        const QString userCode = result.value("userCode").toString();
        if (m_activeLoginId.isEmpty() || verificationUrl.isEmpty() || userCode.isEmpty()) {
            emit requestFailed(method, "Device Code login response is incomplete.");
            m_activeLoginId.clear();
            return;
        }
        emit deviceCodeReady(m_activeLoginId, verificationUrl, userCode);
        return;
    }
    if (method == "account/logout") {
        resetConversation();
        updateAccount(false, {}, {});
        return;
    }
    if (method == "thread/start") {
        m_threadId = result.value("thread").toObject().value("id").toString();
        if (m_threadId.isEmpty()) {
            const QString error = QStringLiteral("Codex App Server returned no thread id.");
            m_pendingTurnText.clear();
            emit conversationTurnCompleted(false, {}, error);
            return;
        }
        sendPendingTurn();
        return;
    }
    if (method == "turn/start") {
        m_activeTurnId = result.value("turn").toObject().value("id").toString();
        m_pendingTurnText.clear();
        m_pendingBaseInstructions.clear();
        m_pendingDynamicTools = {};
        if (!m_activeTurnId.isEmpty()) {
            emit conversationTurnStarted(m_activeTurnId);
        }
    }
}

void CodexAppServerClient::processNotification(const QString& method,
                                                const QJsonObject& params) {
    if (method == "account/login/completed") {
        const bool success = params.value("success").toBool();
        const QString error = params.value("error").toString();
        m_activeLoginId.clear();
        emit loginCompleted(success, error);
        if (success) {
            readAccount();
        }
        return;
    }
    if (method == "account/updated") {
        const QString authMode = params.value("authMode").toString();
        const bool signedIn = authMode == "chatgpt" || authMode == "chatgptAuthTokens";
        updateAccount(signedIn,
                      signedIn ? m_accountEmail : QString{},
                      signedIn ? params.value("planType").toString() : QString{});
        if (signedIn && m_accountEmail.isEmpty()) {
            readAccount();
        }
        return;
    }
    if (method == "item/agentMessage/delta") {
        const QString delta = params.value("delta").toString();
        m_agentText += delta;
        emit conversationAgentDelta(delta);
        return;
    }
    if (method == "item/completed") {
        const QJsonObject item = params.value("item").toObject();
        if (item.value("type").toString() == "agentMessage") {
            const QString finalText = item.value("text").toString();
            if (!finalText.isEmpty()) {
                m_agentText = finalText;
            }
        }
        return;
    }
    if (method == "error") {
        m_turnError = params.value("error").toObject().value("message").toString();
        return;
    }
    if (method == "turn/completed") {
        const QJsonObject turn = params.value("turn").toObject();
        const QString status = turn.value("status").toString();
        const bool success = status == "completed";
        QString error = m_turnError;
        if (!success && error.isEmpty()) {
            error = QStringLiteral("Codex turn finished with status '%1'.").arg(status);
        }
        const QString text = m_agentText.trimmed();
        m_activeTurnId.clear();
        m_agentText.clear();
        m_turnError.clear();
        emit conversationTurnCompleted(success, text, error);
    }
}

void CodexAppServerClient::processServerRequest(const QJsonValue& requestId,
                                                 const QString& method,
                                                 const QJsonObject& params) {
    if (method == "item/tool/call") {
        emit dynamicToolCall(requestId,
                             params.value("callId").toString(),
                             params.value("tool").toString(),
                             params.value("arguments"));
        return;
    }

    writeMessage(QJsonObject{
        {"id", requestId},
        {"error", QJsonObject{{"code", -32601},
                              {"message", QStringLiteral("Unsupported server request: %1")
                                              .arg(method)}}},
    });
}

void CodexAppServerClient::processAccountResult(const QJsonObject& result) {
    const QJsonValue accountValue = result.value("account");
    if (!accountValue.isObject()) {
        updateAccount(false, {}, {});
        return;
    }
    const QJsonObject account = accountValue.toObject();
    const QString type = account.value("type").toString();
    const bool signedIn = type == "chatgpt" || type == "chatgptAuthTokens";
    updateAccount(signedIn,
                  signedIn ? account.value("email").toString() : QString{},
                  signedIn ? account.value("planType").toString() : QString{});
}

void CodexAppServerClient::updateAccount(bool signedIn,
                                          const QString& email,
                                          const QString& planType) {
    const bool changed = m_signedIn != signedIn || m_accountEmail != email ||
                         m_accountPlanType != planType;
    m_signedIn = signedIn;
    m_accountEmail = email;
    m_accountPlanType = planType;
    if (!signedIn) {
        resetConversation();
    }
    if (changed) {
        emit accountChanged(m_signedIn, m_accountEmail, m_accountPlanType);
    }
}

void CodexAppServerClient::startThreadForPendingTurn() {
    QJsonObject params{
        {"serviceName", "gplatform"},
        {"ephemeral", true},
        {"approvalPolicy", "never"},
        {"sandbox", "read-only"},
        {"baseInstructions", m_pendingBaseInstructions},
        {"developerInstructions",
         "You are embedded inside GPlatform. Use only the supplied GPlatform dynamic tools for "
         "application queries and mutations. Do not use shell, filesystem, web, or code-editing tools."},
        {"dynamicTools", m_pendingDynamicTools},
    };
    sendRequest("thread/start", params);
}

void CodexAppServerClient::sendPendingTurn() {
    if (m_threadId.isEmpty() || m_pendingTurnText.isEmpty()) {
        const QString error = QStringLiteral("Cannot start Codex turn without thread and input.");
        m_pendingTurnText.clear();
        emit conversationTurnCompleted(false, {}, error);
        return;
    }
    QJsonArray input{QJsonObject{{"type", "text"}, {"text", m_pendingTurnText}}};
    sendRequest("turn/start", QJsonObject{{"threadId", m_threadId}, {"input", input}});
}

void CodexAppServerClient::failLifecycle(const QString& error) {
    if (error.trimmed().isEmpty()) {
        return;
    }
    m_startupTimer.stop();
    LOG_ERROR("Codex App Server failure: {}", error.toStdString());
    setLifecycleState(LifecycleState::Failed);
    emit requestFailed("app-server", error);
    if (!m_activeTurnId.isEmpty() || !m_pendingTurnText.isEmpty()) {
        emit conversationTurnCompleted(false, {}, error);
    }
    stopProcess();
}

void CodexAppServerClient::stopProcess() {
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    m_stoppingProcess = true;
    m_process.terminate();
    if (!m_process.waitForFinished(1500)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_stoppingProcess = false;
}

} // namespace GPlatform::AI
