#include "../include/ImmediateNotifyGuard.hpp"

// 静态成员定义
thread_local bool ImmediateNotifyGuard::s_forceImmediate = false;

ImmediateNotifyGuard::ImmediateNotifyGuard()
    : m_previousMode(s_forceImmediate) {
    s_forceImmediate = true;
}

ImmediateNotifyGuard::~ImmediateNotifyGuard() {
    s_forceImmediate = m_previousMode;
}

bool ImmediateNotifyGuard::isForceImmediate() {
    return s_forceImmediate;
}