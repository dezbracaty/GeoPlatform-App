#pragma once

#include <AutoRegisterDB.hpp>

#include <memory>
#include <utility>
#include <vector>

/**
 * Owns the registration lifetime of a group of temporary DB instances.
 * Instances use their normal concrete DB types and the normal DocumentManager;
 * only their negative instance IDs distinguish their lifetime.
 */
class TempDBScope final {
public:
    TempDBScope() = default;
    ~TempDBScope();

    TempDBScope(const TempDBScope&) = delete;
    TempDBScope& operator=(const TempDBScope&) = delete;

    template <typename T, typename... Args>
    std::shared_ptr<T> create(Args&&... args) {
        return AutoRegisterDB::createTempTracked<T>(
            m_ids, std::forward<Args>(args)...);
    }

    void clear();
    const std::vector<DBInstanceID>& ids() const noexcept { return m_ids; }

private:
    std::vector<DBInstanceID> m_ids;
};
