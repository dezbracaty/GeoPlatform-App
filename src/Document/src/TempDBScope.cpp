#include "TempDBScope.hpp"

#include "DocumentManager.hpp"
#include "Foundation/Log.h"

#include <exception>

TempDBScope::~TempDBScope() {
    try {
        clear();
    } catch (const std::exception& exception) {
        LOG_ERROR("TempDBScope cleanup failed: {}", exception.what());
        auto* document = DocumentManager::instance();
        for (auto it = m_ids.rbegin(); it != m_ids.rend(); ++it) {
            document->unregisterDBInstance(*it);
        }
        m_ids.clear();
    } catch (...) {
        LOG_ERROR("TempDBScope cleanup failed with an unknown error");
        auto* document = DocumentManager::instance();
        for (auto it = m_ids.rbegin(); it != m_ids.rend(); ++it) {
            document->unregisterDBInstance(*it);
        }
        m_ids.clear();
    }
}

void TempDBScope::clear() {
    auto* document = DocumentManager::instance();
    for (auto it = m_ids.rbegin(); it != m_ids.rend(); ++it) {
        document->unregisterDBInstance(*it);
    }
    m_ids.clear();
}
