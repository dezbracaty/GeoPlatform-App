#pragma once

#include <string>
#include <unordered_map>

namespace GPlatform::AI {

class IAgentBackend;

class AgentBackendCatalog final {
public:
    static AgentBackendCatalog& instance();

    IAgentBackend* backend(const std::string& backendId) const noexcept;

private:
    AgentBackendCatalog();

    bool registerBackend(IAgentBackend& backend);

    std::unordered_map<std::string, IAgentBackend*> m_backends;
};

} // namespace GPlatform::AI
