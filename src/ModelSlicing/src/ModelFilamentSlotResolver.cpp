#include "ModelFilamentSlotResolver.hpp"

#include <ModelPartDB.hpp>
#include <SlicingConfigDB.hpp>

#include <algorithm>

namespace GPlatform::Slicing {
namespace {

int validProjectDefault(
    const GPlatform::SlicingConfigDB* config,
    std::size_t availableSlots) {
    const int requested = config ? config->getDefaultFilamentSlot() : 1;
    if (availableSlots == 0) return std::max(1, requested);
    return requested >= 1 &&
            static_cast<std::size_t>(requested) <= availableSlots
        ? requested
        : 1;
}

} // namespace

ResolvedModelFilamentSlot resolveModelFilamentSlot(
    const ModelPartDB& part,
    const GPlatform::SlicingConfigDB* slicingConfig,
    std::size_t availableFilamentSlots) {
    if (availableFilamentSlots == 0 && slicingConfig) {
        availableFilamentSlots = slicingConfig->getFilaments().size();
    }

    ResolvedModelFilamentSlot result;
    result.effectiveSlot = validProjectDefault(
        slicingConfig, availableFilamentSlots);
    result.explicitlyBound = part.getFilamentBindingMode() ==
        static_cast<int>(ModelFilamentBindingMode::ExplicitSlot);
    if (!result.explicitlyBound) return result;

    const int requested = part.getDefaultFilamentSlot();
    result.requestedSlotValid = requested >= 1 &&
        (availableFilamentSlots == 0 ||
         static_cast<std::size_t>(requested) <= availableFilamentSlots);
    if (result.requestedSlotValid) result.effectiveSlot = requested;
    return result;
}

} // namespace GPlatform::Slicing
