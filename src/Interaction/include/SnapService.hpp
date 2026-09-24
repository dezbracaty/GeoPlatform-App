#pragma once
#include "SnapTypes.hpp"

namespace GPlatform::Interaction {

// Stateless calculation entry point. Geometry/indexes belong to DBs; operation
// history and presentation belong to the caller. No scene discovery or writes.
class SnapService final {
public:
    std::optional<SnapResult> query(
        const SnapPointerQuery& query, const std::vector<SnapTarget>& targets,
        const std::optional<SnapResult>& previous = std::nullopt) const;
    std::optional<SnapResult> query(
        const SnapQuery& query, const std::vector<SnapTarget>& targets,
        const std::optional<SnapResult>& previous = std::nullopt) const;
};

} // namespace GPlatform::Interaction
