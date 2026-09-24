#include "AI/AgentBackendCatalog.hpp"

#include "AI/CodexAgentBackend.hpp"
#include "AI/IAgentBackend.hpp"
#include "Foundation/Log.h"

#include <algorithm>
#include <cctype>

namespace GPlatform::AI {
namespace {

std::string normalizedBackendId(std::string value) {
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

AgentBackendCatalog::AgentBackendCatalog() {
    registerBackend(CodexAgentBackend::instance());
}

AgentBackendCatalog& AgentBackendCatalog::instance() {
    static AgentBackendCatalog catalog;
    return catalog;
}

IAgentBackend* AgentBackendCatalog::backend(const std::string& backendId) const noexcept {
    const auto found = m_backends.find(normalizedBackendId(backendId));
    return found == m_backends.end() ? nullptr : found->second;
}

bool AgentBackendCatalog::registerBackend(IAgentBackend& backend) {
    const AgentDescriptor descriptor = backend.descriptor();
    const std::string id = normalizedBackendId(descriptor.id);
    if (id.empty()) {
        LOG_ERROR("Cannot register an Agent backend with an empty id.");
        return false;
    }
    if (m_backends.find(id) != m_backends.end()) {
        LOG_ERROR("Agent backend id '{}' is already registered.", id);
        return false;
    }
    m_backends.emplace(id, &backend);
    LOG_INFO("Registered Agent backend '{}' ({})", id, descriptor.displayName);
    return true;
}

} // namespace GPlatform::AI
