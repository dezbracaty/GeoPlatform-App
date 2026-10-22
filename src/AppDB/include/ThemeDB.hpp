#pragma once

#include <AutoRegisterDB.hpp>

#include <atomic>

// Application-owned presentation state, shared by every viewport.
class ThemeDB final : public AutoRegisterDB {
public:
    enum class Mode { Light, Dark };

    TypeID getTypeID() const override { return TypeID::THEME_DB; }
    bool needsVTKSync() const override { return false; }

    Mode theme() const noexcept;
    bool setTheme(Mode mode);

    PropertyMap serialize() const override { return {}; }
    bool deserialize(const PropertyMap&) override { return false; }

private:
    std::atomic<Mode> m_theme{Mode::Light};
};
