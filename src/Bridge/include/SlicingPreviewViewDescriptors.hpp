#pragma once

#include "ToolpathPreviewTypes.hpp"

#include <QString>
#include <algorithm>
#include <array>
#include <optional>

namespace GPlatform {

struct SlicingPreviewViewDescriptor {
    ToolpathPreviewViewType type;
    ToolpathPreviewColorMode colorMode;
    const char* id;
    const char* label;
    const char* layoutKind;
    int preferredWidth;
};

inline constexpr std::array<SlicingPreviewViewDescriptor, 16> kSlicingPreviewViewDescriptors{{
    {ToolpathPreviewViewType::Summary, ToolpathPreviewColorMode::PreviewColor, "summary", "Summary", "summary", 330},
    {ToolpathPreviewViewType::FeatureType, ToolpathPreviewColorMode::FeatureType, "lineType", "Line Type", "featureTable", 520},
    {ToolpathPreviewViewType::Filament, ToolpathPreviewColorMode::PreviewColor, "filament", "Filament", "filamentTable", 420},
    {ToolpathPreviewViewType::Speed, ToolpathPreviewColorMode::Speed, "speed", "Speed", "range", 330},
    {ToolpathPreviewViewType::ActualSpeed, ToolpathPreviewColorMode::ActualSpeed, "actualSpeed", "Actual Speed", "range", 330},
    {ToolpathPreviewViewType::Acceleration, ToolpathPreviewColorMode::Acceleration, "acceleration", "Acceleration", "range", 330},
    {ToolpathPreviewViewType::Jerk, ToolpathPreviewColorMode::Jerk, "jerk", "Jerk", "range", 330},
    {ToolpathPreviewViewType::LayerHeight, ToolpathPreviewColorMode::LayerHeight, "layerHeight", "Layer Height", "range", 330},
    {ToolpathPreviewViewType::LineWidth, ToolpathPreviewColorMode::LineWidth, "lineWidth", "Line Width", "range", 330},
    {ToolpathPreviewViewType::VolumetricFlow, ToolpathPreviewColorMode::VolumetricFlow, "volumetricFlow", "Volumetric Flow", "range", 330},
    {ToolpathPreviewViewType::ActualVolumetricFlow, ToolpathPreviewColorMode::ActualVolumetricFlow, "actualVolumetricFlow", "Actual Volumetric Flow", "range", 330},
    {ToolpathPreviewViewType::LayerTime, ToolpathPreviewColorMode::LayerTime, "layerTime", "Layer Time", "range", 330},
    {ToolpathPreviewViewType::LayerTimeLogarithmic, ToolpathPreviewColorMode::LayerTimeLogarithmic, "layerTimeLog", "Layer Time (log)", "range", 330},
    {ToolpathPreviewViewType::FanSpeed, ToolpathPreviewColorMode::FanSpeed, "fanSpeed", "Fan Speed", "range", 330},
    {ToolpathPreviewViewType::Temperature, ToolpathPreviewColorMode::Temperature, "temperature", "Temperature", "range", 330},
    {ToolpathPreviewViewType::PressureAdvance, ToolpathPreviewColorMode::PressureAdvance, "pressureAdvance", "Pressure Advance", "range", 330},
}};

inline const SlicingPreviewViewDescriptor* slicingPreviewViewDescriptorForId(
    const QString& id) {
    const auto it = std::find_if(kSlicingPreviewViewDescriptors.begin(),
                                 kSlicingPreviewViewDescriptors.end(),
                                 [&id](const auto& descriptor) {
                                     return id == QLatin1String(descriptor.id);
                                 });
    return it == kSlicingPreviewViewDescriptors.end() ? nullptr : &*it;
}

inline const SlicingPreviewViewDescriptor* slicingPreviewViewDescriptorForType(
    ToolpathPreviewViewType type) {
    const auto it = std::find_if(kSlicingPreviewViewDescriptors.begin(),
                                 kSlicingPreviewViewDescriptors.end(),
                                 [type](const auto& descriptor) {
                                     return descriptor.type == type;
                                 });
    return it == kSlicingPreviewViewDescriptors.end() ? nullptr : &*it;
}

inline std::optional<ToolpathPreviewColorMode> slicingPreviewColorModeForViewType(
    const QString& viewType) {
    const auto* descriptor = slicingPreviewViewDescriptorForId(viewType);
    return descriptor ? std::optional{descriptor->colorMode} : std::nullopt;
}

} // namespace GPlatform
