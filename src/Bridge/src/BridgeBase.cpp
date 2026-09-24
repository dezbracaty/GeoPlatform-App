#include <BridgeBase.hpp>
#include "Foundation/Log.h"

namespace bridge {

BridgeBase::BridgeBase(QObject* parent)
    : QObject(parent) {
    LOG_DEBUG("BridgeBase constructed");
}

BridgeBase::~BridgeBase() {
    LOG_DEBUG("BridgeBase destructor called");

    if (m_initialized) {
        emit aboutToDestroy();
        cleanup();
    }
}

void BridgeBase::initialize() {
    if (m_initialized) {
        LOG_WARN("BridgeBase already initialized");
        return;
    }

    LOG_DEBUG("BridgeBase initializing");
    m_initialized = true;
    emit initialized();
}

void BridgeBase::cleanup() {
    LOG_DEBUG("BridgeBase cleanup");
    m_initialized = false;
}

} // namespace bridge