#include "AI/CodexAgentBackend.hpp"

#include "AI/CodexAppServerClient.hpp"
#include "Foundation/Log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>

namespace GPlatform::AI {
namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

} // namespace

CodexAgentBackend& CodexAgentBackend::instance() {
    static CodexAgentBackend backend;
    return backend;
}

CodexAgentBackend::CodexAgentBackend(QObject* parent)
    : QObject(parent),
      m_client(CodexAppServerClient::instance()) {
    connect(m_client,
            &CodexAppServerClient::conversationAgentDelta,
            this,
            [this](const QString& delta) {
                if (m_eventSink && !m_applicationTurnId.isEmpty()) {
                    m_eventSink->onAgentTextDelta(
                        AgentTextDelta{toUtf8(m_applicationTurnId), toUtf8(delta)});
                }
            });
    connect(m_client,
            &CodexAppServerClient::dynamicToolCall,
            this,
            [this](const QJsonValue& requestId,
                   const QString& serverCallId,
                   const QString& toolName,
                   const QJsonValue& arguments) {
                QString callId = serverCallId.trimmed();
                if (callId.isEmpty()) {
                    callId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                }
                if (m_toolRequestIds.contains(callId)) {
                    m_client->respondToDynamicToolCall(
                        requestId,
                        false,
                        QJsonArray{QJsonObject{{"type", "inputText"},
                                               {"text", "Duplicate tool call id."}}});
                    return;
                }

                QByteArray argumentsBytes;
                if (arguments.isObject()) {
                    argumentsBytes = QJsonDocument(arguments.toObject())
                                         .toJson(QJsonDocument::Compact);
                } else if (arguments.isArray()) {
                    argumentsBytes = QJsonDocument(arguments.toArray())
                                         .toJson(QJsonDocument::Compact);
                } else {
                    argumentsBytes = QJsonDocument(QJsonArray{arguments})
                                         .toJson(QJsonDocument::Compact);
                }

                if (!m_eventSink || m_applicationTurnId.isEmpty()) {
                    m_client->respondToDynamicToolCall(
                        requestId,
                        false,
                        QJsonArray{QJsonObject{{"type", "inputText"},
                                               {"text", "No active GPlatform Agent turn."}}});
                    return;
                }
                m_toolRequestIds.insert(callId, requestId);
                m_eventSink->onAgentToolCall(
                    AgentToolCall{
                        toUtf8(m_applicationTurnId),
                        toUtf8(callId),
                        toUtf8(toolName),
                        std::string(argumentsBytes.constData(),
                                    static_cast<std::size_t>(argumentsBytes.size()))});
            });
    connect(m_client,
            &CodexAppServerClient::conversationTurnCompleted,
            this,
            [this](bool success, const QString& text, const QString& error) {
                completeTurn(success, text, error);
            });
}

AgentDescriptor CodexAgentBackend::descriptor() const {
    return AgentDescriptor{"codex_app_server", "ChatGPT/Codex"};
}

AgentCapabilities CodexAgentBackend::capabilities() const noexcept {
    return capability(AgentCapability::StreamingText) |
           capability(AgentCapability::ToolCalls) |
           capability(AgentCapability::ToolResults) |
           capability(AgentCapability::Cancellation) |
           capability(AgentCapability::SessionReset);
}

bool CodexAgentBackend::isReady() const noexcept {
    return m_client->isReady();
}

void CodexAgentBackend::ensureReady() {
    m_client->ensureStarted();
}

void CodexAgentBackend::setEventSink(IAgentEventSink* sink) noexcept {
    m_eventSink = sink;
}

AgentTurnStartResult CodexAgentBackend::startTurn(const AgentTurnRequest& request) {
    const QString applicationTurnId = fromUtf8(request.applicationTurnId).trimmed();
    if (applicationTurnId.isEmpty()) {
        return {false, "Agent turn requires an applicationTurnId."};
    }
    if (!m_eventSink) {
        return {false, "Agent event sink is not attached."};
    }
    if (!m_client->isReady()) {
        return {false, "Codex App Server is not ready."};
    }
    if (!m_client->isSignedIn()) {
        return {false, "ChatGPT/Codex account is not signed in."};
    }
    if (!m_applicationTurnId.isEmpty()) {
        return {false, "An Agent turn is already active."};
    }

    QJsonArray dynamicTools;
    for (const AgentToolDefinition& tool : request.tools) {
        const QString name = fromUtf8(tool.name).trimmed();
        const QString description = fromUtf8(tool.description).trimmed();
        QJsonParseError parseError;
        const QJsonDocument schemaDocument = QJsonDocument::fromJson(
            QByteArray::fromStdString(tool.inputSchemaJson), &parseError);
        if (name.isEmpty() || description.isEmpty() ||
            parseError.error != QJsonParseError::NoError ||
            !schemaDocument.isObject()) {
            return {false,
                    toUtf8(QStringLiteral("Agent tool definition is malformed: '%1'.")
                               .arg(name))};
        }
        dynamicTools.append(QJsonObject{{"type", "function"},
                                        {"name", name},
                                        {"description", description},
                                        {"inputSchema", schemaDocument.object()}});
    }

    m_applicationTurnId = applicationTurnId;
    if (!m_client->startConversationTurn(fromUtf8(request.input),
                                         fromUtf8(request.instructions),
                                         dynamicTools)) {
        m_applicationTurnId.clear();
        return {false, "Codex App Server rejected the conversation turn."};
    }
    return {true, {}};
}

bool CodexAgentBackend::submitToolResult(const AgentToolResult& result) {
    const QString callId = fromUtf8(result.callId).trimmed();
    const auto requestIt = m_toolRequestIds.constFind(callId);
    if (callId.isEmpty() || requestIt == m_toolRequestIds.cend()) {
        LOG_ERROR("Codex tool result references unknown callId '{}'.", result.callId);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument payloadDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(result.payloadJson), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        (!payloadDocument.isObject() && !payloadDocument.isArray())) {
        LOG_ERROR("Codex tool result for callId '{}' contains invalid JSON.",
                  result.callId);
        return false;
    }

    const QJsonValue requestId = requestIt.value();
    m_toolRequestIds.remove(callId);
    m_client->respondToDynamicToolCall(
        requestId,
        result.success,
        QJsonArray{QJsonObject{{"type", "inputText"},
                               {"text", fromUtf8(result.payloadJson)}}});
    return true;
}

void CodexAgentBackend::cancelTurn() {
    m_client->interruptConversationTurn();
}

void CodexAgentBackend::resetSession() {
    m_applicationTurnId.clear();
    m_toolRequestIds.clear();
    m_client->resetConversation();
}

void CodexAgentBackend::completeTurn(bool success,
                                     const QString& text,
                                     const QString& error) {
    const QString applicationTurnId = m_applicationTurnId;
    m_applicationTurnId.clear();
    m_toolRequestIds.clear();
    if (m_eventSink && !applicationTurnId.isEmpty()) {
        m_eventSink->onAgentTurnFinished(
            AgentTurnResult{toUtf8(applicationTurnId),
                            success,
                            toUtf8(text),
                            toUtf8(error)});
    }
}

} // namespace GPlatform::AI
