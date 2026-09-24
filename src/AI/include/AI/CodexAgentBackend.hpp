#pragma once

#include "AI/IAgentBackend.hpp"

#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QString>

namespace GPlatform::AI {

class CodexAppServerClient;

class CodexAgentBackend final : public QObject, public IAgentBackend {
public:
    static CodexAgentBackend& instance();

    AgentDescriptor descriptor() const override;
    AgentCapabilities capabilities() const noexcept override;
    bool isReady() const noexcept override;
    void ensureReady() override;
    void setEventSink(IAgentEventSink* sink) noexcept override;
    AgentTurnStartResult startTurn(const AgentTurnRequest& request) override;
    bool submitToolResult(const AgentToolResult& result) override;
    void cancelTurn() override;
    void resetSession() override;

private:
    explicit CodexAgentBackend(QObject* parent = nullptr);

    void completeTurn(bool success, const QString& text, const QString& error);

    CodexAppServerClient* m_client = nullptr;
    IAgentEventSink* m_eventSink = nullptr;
    QString m_applicationTurnId;
    QHash<QString, QJsonValue> m_toolRequestIds;
};

} // namespace GPlatform::AI
