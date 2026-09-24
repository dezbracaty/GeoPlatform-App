#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GPlatform::AI {

enum class AgentCapability : std::uint32_t {
    StreamingText = 1U << 0U,
    ToolCalls = 1U << 1U,
    ToolResults = 1U << 2U,
    Cancellation = 1U << 3U,
    SessionReset = 1U << 4U,
};

using AgentCapabilities = std::uint32_t;

constexpr AgentCapabilities capability(AgentCapability value) noexcept {
    return static_cast<AgentCapabilities>(value);
}

constexpr bool hasCapability(AgentCapabilities capabilities,
                             AgentCapability value) noexcept {
    return (capabilities & capability(value)) != 0U;
}

struct AgentDescriptor {
    std::string id;
    std::string displayName;
};

struct AgentToolDefinition {
    std::string name;
    std::string description;
    std::string inputSchemaJson;
};

struct AgentTurnRequest {
    std::string applicationTurnId;
    std::string input;
    std::string instructions;
    std::vector<AgentToolDefinition> tools;
};

struct AgentTurnStartResult {
    bool accepted = false;
    std::string error;
};

struct AgentTextDelta {
    std::string applicationTurnId;
    std::string text;
};

struct AgentToolCall {
    std::string applicationTurnId;
    std::string callId;
    std::string name;
    std::string argumentsJson;
};

struct AgentToolResult {
    std::string callId;
    bool success = false;
    std::string payloadJson;
};

struct AgentTurnResult {
    std::string applicationTurnId;
    bool success = false;
    std::string text;
    std::string error;
};

class IAgentEventSink {
public:
    virtual ~IAgentEventSink() = default;

    virtual void onAgentTextDelta(const AgentTextDelta& delta) = 0;
    virtual void onAgentToolCall(const AgentToolCall& call) = 0;
    virtual void onAgentTurnFinished(const AgentTurnResult& result) = 0;
};

class IAgentBackend {
public:
    virtual ~IAgentBackend() = default;

    virtual AgentDescriptor descriptor() const = 0;
    virtual AgentCapabilities capabilities() const noexcept = 0;
    virtual bool isReady() const noexcept = 0;
    virtual void ensureReady() = 0;
    virtual void setEventSink(IAgentEventSink* sink) noexcept = 0;
    virtual AgentTurnStartResult startTurn(const AgentTurnRequest& request) = 0;
    virtual bool submitToolResult(const AgentToolResult& result) = 0;
    virtual void cancelTurn() = 0;
    virtual void resetSession() = 0;
};

} // namespace GPlatform::AI
