#pragma once

#include <cstddef>

class ModelPartDB;

namespace GPlatform {
class SlicingConfigDB;

namespace Slicing {

struct ResolvedModelFilamentSlot {
    int effectiveSlot{1};
    bool explicitlyBound{false};
    bool requestedSlotValid{true};
};

/** Resolve the one-based slot consumed by painting and slicing workflows. */
ResolvedModelFilamentSlot resolveModelFilamentSlot(
    const ModelPartDB& part,
    const GPlatform::SlicingConfigDB* slicingConfig,
    std::size_t availableFilamentSlots = 0);

} // namespace Slicing
} // namespace GPlatform
