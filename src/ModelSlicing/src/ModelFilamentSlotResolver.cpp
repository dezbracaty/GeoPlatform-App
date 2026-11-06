#include "ModelFilamentSlotResolver.hpp"

#include <ModelPartDB.hpp>
#include <ModelObjectDB.hpp>
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
    const bool inheritObject = part.getFilamentBindingMode() ==
        static_cast<int>(ModelFilamentBindingMode::InheritObject);
    if (!result.explicitlyBound && !inheritObject) return result;
    const auto object = inheritObject ? part.object() : nullptr;
    const int requested = inheritObject
        ? (object ? object->getDefaultFilamentSlot() : 0)
        : part.getDefaultFilamentSlot();
    if (inheritObject && requested == 0) return result;
    result.requestedSlotValid = requested >= 1 &&
        (availableFilamentSlots == 0 ||
         static_cast<std::size_t>(requested) <= availableFilamentSlots);
    if (result.requestedSlotValid) result.effectiveSlot = requested;
    return result;
}

} // namespace GPlatform::Slicing
