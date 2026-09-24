#pragma once

#include "AI/IAgentBackend.hpp"
#include <BridgeBase.hpp>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>
#include <vector>

class AIChatBridge : public bridge::BridgeBase,
                     private GPlatform::AI::IAgentEventSink {
    Q_OBJECT
    Q_PROPERTY(bool panelVisible READ panelVisible WRITE setPanelVisible NOTIFY panelVisibleChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    static AIChatBridge* instance();
    static AIChatBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    bool panelVisible() const {
        return m_panelVisible;
    }
    void setPanelVisible(bool visible);

    QVariantList messages() const {
        return m_messages;
    }

    bool streaming() const {
        return m_streaming;
    }

    Q_INVOKABLE void openPanel();
    Q_INVOKABLE void closePanel();
    Q_INVOKABLE void togglePanel();

    Q_INVOKABLE void clearMessages();
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE bool retryTurn(const QString& turnId = QString());
    Q_INVOKABLE bool cancelStreamingRequest(const QString& reason = QString());
    Q_INVOKABLE QVariantMap queryTurn(const QString& turnId = QString()) const;
    Q_INVOKABLE QString activeTurnId() const {
        return m_activeTurnId;
    }
    Q_INVOKABLE QString latestCompletedTurnId() const {
        return m_latestCompletedTurnId;
    }
    Q_INVOKABLE QString latestRetryableTurnId() const {
        return m_latestRetryableTurnId;
    }
    Q_INVOKABLE QVariantMap lastActionExecution() const {
        return m_lastActionExecution;
    }

signals:
    void panelVisibleChanged();
    void messagesChanged();
    void streamingChanged();

private:
    explicit AIChatBridge(QObject* parent = nullptr);
    ~AIChatBridge() override = default;

    AIChatBridge(const AIChatBridge&) = delete;
    AIChatBridge& operator=(const AIChatBridge&) = delete;
    AIChatBridge(AIChatBridge&&) = delete;
    AIChatBridge& operator=(AIChatBridge&&) = delete;

    void appendMessage(const QString& role,
                       const QString& text,
                       const QVariantMap& extra = QVariantMap{});
    void upsertToolTraceMessage(const QString& traceId,
                                const QString& toolName,
                                const QString& status,
                                const QString& arguments,
                                const QString& resultPayload,
                                const QString& errorMessage);
    void setStreaming(bool streaming);
    void requestModelReply();
    QVariantMap loadAiRuntimeConfigFile() const;
    GPlatform::AI::IAgentBackend* resolveConfiguredBackend() const;
    std::vector<GPlatform::AI::AgentToolDefinition> buildToolDefinitions() const;
    QVariantMap executeToolCall(const QString& functionName,
                                const QVariantMap& arguments,
                                const QString& callId,
                                QVariantList& actionTrace,
                                QVariantMap& latestActionExecution);
    QString buildSystemPrompt() const;
    QVariantMap buildCapabilitySnapshot() const;
    QVariantMap finalizeActiveTurn(const QString& status,
                                   const QString& error = QString(),
                                   const QVariantMap& actionExecution = QVariantMap{});
    QString beginTurn(const QString& userText);
    bool rollbackUndoSteps(qulonglong undoSteps,
                           const QString& turnId,
                           int& undoneCount,
                           QString& error);
    std::vector<qulonglong> parseTurnTransactionIds(const QVariantMap& turn) const;
    bool isTurnRetryable(const QVariantMap& turn) const;
    QString latestUserMessage() const;
    void submitToolResult(const QString& callId, const QVariantMap& result);

    void onAgentTextDelta(const GPlatform::AI::AgentTextDelta& delta) override;
    void onAgentToolCall(const GPlatform::AI::AgentToolCall& call) override;
    void onAgentTurnFinished(const GPlatform::AI::AgentTurnResult& result) override;

    bool m_panelVisible = false;
    bool m_streaming = false;
    QVariantList m_messages;
    QVariantMap m_lastActionExecution;
    QHash<QString, QVariantMap> m_turnLedger;
    QString m_activeTurnId;
    QString m_latestRetryableTurnId;
    QString m_latestCompletedTurnId;
    GPlatform::AI::IAgentBackend* m_agentBackend = nullptr;
    bool m_agentTurnInFlight = false;
    QVariantList m_agentActionTrace;
    QVariantMap m_agentLatestActionExecution;
    QHash<QString, QVariantMap> m_pendingAgentInvocations;
};
