#include "ModelRenderAppearanceResolver.hpp"
#include <ModelFilamentSlotResolver.hpp>

#include <MaterialDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <SlicingConfigDB.hpp>

#include <QColor>

namespace GPlatform::Rendering {

Vector3 resolveModelDisplayColor(
    const ModelPartDB& part,
    const MaterialDB* material,
    const GPlatform::SlicingConfigDB* slicingConfig) {
    const Vector3 authoredColor = material
        ? material->getDiffuseColor()
        : Vector3(0.8f, 0.8f, 0.8f);
    if (part.getFilamentBindingMode() ==
            static_cast<int>(ModelFilamentBindingMode::InheritProjectDefault) ||
        !slicingConfig) {
        return authoredColor;
    }

    const auto binding = GPlatform::Slicing::resolveModelFilamentSlot(part, slicingConfig);
    if (!binding.requestedSlotValid) return authoredColor;
    const int requestedSlot = binding.effectiveSlot;
    if (requestedSlot < 1) return authoredColor;
    const auto& filaments = slicingConfig->getFilaments();
    const std::size_t index = static_cast<std::size_t>(requestedSlot - 1);
    if (index >= filaments.size() || !filaments[index].color.isValid()) {
        return authoredColor;
    }

    const QColor& color = filaments[index].color;
    return {color.redF(), color.greenF(), color.blueF()};
}

std::shared_ptr<const GlbAssetSnapshot> resolveModelSourceAppearance(
    const ModelObjectDB& object,
    const std::vector<std::shared_ptr<const ModelPartDB>>& parts) {
    if (!object.getUseSourceAppearance()) return {};
    const auto snapshot = object.acquireGlbSourceAssetSnapshot();
    if (!snapshot || !*snapshot) return {};

    for (const auto& part : parts) {
        if (!part || part->getFilamentBindingMode() ==
                static_cast<int>(ModelFilamentBindingMode::ExplicitSlot)) {
            return {};
        }
    }
    return snapshot;
}

} // namespace GPlatform::Rendering
