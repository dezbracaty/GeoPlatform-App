#include "ThemeDB.hpp"

#include <DocumentManager.hpp>
#include <ImmediateNotifyGuard.hpp>

ThemeDB::Mode ThemeDB::theme() const noexcept {
    return m_theme.load(std::memory_order_relaxed);
}

bool ThemeDB::setTheme(Mode mode) {
    if (mode != Mode::Light && mode != Mode::Dark) return false;
    if (m_theme.exchange(mode, std::memory_order_relaxed) == mode) return false;

    // A user's long-running transaction must not delay a UI theme change.
    ImmediateNotifyGuard immediate;
    DocumentManager::instance()->notifyChange(
        this, ChangeType::PROPERTY_CHANGED, "EffectiveTheme");
    return true;
}
