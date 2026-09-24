#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace GPlatform::AI {

class CodexAppServerClient final : public QObject {
    Q_OBJECT

public:
    enum class LifecycleState {
        Stopped,
        Starting,
        Initializing,
        Ready,
        Failed,
    };
    Q_ENUM(LifecycleState)

    static CodexAppServerClient* instance();

    LifecycleState lifecycleState() const;
    bool isReady() const;
    bool isSignedIn() const;
    QString accountEmail() const;
    QString accountPlanType() const;
    QString activeLoginId() const;

    void ensureStarted();
    void readAccount();
    void startDeviceCodeLogin();
    void cancelDeviceCodeLogin();
    void logout();

    bool startConversationTurn(const QString& text,
                               const QString& baseInstructions,
                               const QJsonArray& dynamicTools);
    void respondToDynamicToolCall(const QJsonValue& requestId,
                                  bool success,
                                  const QJsonArray& contentItems);
    void interruptConversationTurn();
    void resetConversation();

signals:
    void lifecycleStateChanged(GPlatform::AI::CodexAppServerClient::LifecycleState state);
    void ready();
    void accountChanged(bool signedIn, const QString& email, const QString& planType);
    void deviceCodeReady(const QString& loginId,
                         const QString& verificationUrl,
                         const QString& userCode);
    void loginCompleted(bool success, const QString& error);
    void requestFailed(const QString& method, const QString& error);

    void conversationTurnStarted(const QString& turnId);
    void conversationAgentDelta(const QString& delta);
    void dynamicToolCall(const QJsonValue& requestId,
                         const QString& callId,
                         const QString& tool,
                         const QJsonValue& arguments);
    void conversationTurnCompleted(bool success,
                                   const QString& text,
                                   const QString& error);

private:
    explicit CodexAppServerClient(QObject* parent = nullptr);
    ~CodexAppServerClient() override;

    CodexAppServerClient(const CodexAppServerClient&) = delete;
    CodexAppServerClient& operator=(const CodexAppServerClient&) = delete;

    QString resolveExecutable(QString& error) const;
    void setLifecycleState(LifecycleState state);
    qint64 sendRequest(const QString& method, const QJsonObject& params = {});
    bool sendNotification(const QString& method, const QJsonObject& params = {});
    bool writeMessage(const QJsonObject& message);
    void processStdout();
    void processMessage(const QJsonObject& message);
    void processResponse(const QJsonObject& message);
    void processNotification(const QString& method, const QJsonObject& params);
    void processServerRequest(const QJsonValue& requestId,
                              const QString& method,
                              const QJsonObject& params);
    void processAccountResult(const QJsonObject& result);
    void updateAccount(bool signedIn, const QString& email, const QString& planType);
    void startThreadForPendingTurn();
    void sendPendingTurn();
    void failLifecycle(const QString& error);
    void stopProcess();

    QProcess m_process;
    QTimer m_startupTimer;
    QByteArray m_stdoutBuffer;
    LifecycleState m_lifecycleState = LifecycleState::Stopped;
    qint64 m_nextRequestId = 1;
    QHash<qint64, QString> m_pendingRequests;

    bool m_signedIn = false;
    QString m_accountEmail;
    QString m_accountPlanType;
    QString m_activeLoginId;

    QString m_threadId;
    QString m_activeTurnId;
    QString m_pendingTurnText;
    QString m_pendingBaseInstructions;
    QJsonArray m_pendingDynamicTools;
    QString m_agentText;
    QString m_turnError;
    bool m_stoppingProcess = false;
};

} // namespace GPlatform::AI
