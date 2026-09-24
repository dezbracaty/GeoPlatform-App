#include "ToolpathPreviewUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_set>

namespace GPlatform::ToolpathPreviewUtils {
namespace {

constexpr std::uint16_t kInvalidSmallId = 0xffffu;

Vector3 roleColor(std::uint8_t role, ToolpathMotionKind motion) {
    // Ported 1:1 from OrcaSlicer / libvgcode DEFAULT_EXTRUSION_ROLES_COLORS + DEFAULT_OPTIONS_COLORS
    // (RGB 0-255), keyed by the Slic3r ExtrusionRole byte, so the preview matches OrcaSlicer exactly.
    const auto c = [](int r, int g, int b) { return Vector3(r / 255.0f, g / 255.0f, b / 255.0f); };
    if (motion == ToolpathMotionKind::Travel || motion == ToolpathMotionKind::Wipe) {
        return c(56, 72, 155);  // Travels
    }
    switch (role) {
    case 0:  return c(230, 179, 179);  // Undefined
    case 1:  return c(255, 230, 77);   // Inner wall (Perimeter)
    case 2:  return c(255, 125, 56);   // Outer wall (External perimeter)
    case 3:  return c(31, 31, 255);    // Overhang wall
    case 4:  return c(176, 48, 41);    // Sparse infill (Internal infill)
    case 5:  return c(150, 84, 204);   // Internal solid infill
    case 6:  return c(240, 64, 64);    // Top surface
    case 7:  return c(102, 92, 199);   // Bottom surface
    case 8:  return c(255, 140, 105);  // Ironing
    case 9:  return c(77, 128, 186);   // Bridge
    case 10: return c(77, 128, 186);   // Internal bridge (same as Bridge in OrcaSlicer)
    case 11: return c(255, 255, 255);  // Gap infill
    case 12: return c(0, 135, 110);    // Skirt
    case 13: return c(0, 59, 110);     // Brim
    case 14: return c(0, 255, 0);      // Support
    case 15: return c(0, 128, 0);      // Support interface
    case 16: return c(0, 64, 0);       // Support transition
    case 17: return c(179, 227, 171);  // Prime tower (Wipe tower)
    case 18: return c(94, 209, 148);   // Custom
    case 19: return c(128, 128, 128);  // Multiple (Mixed)
    case 20: return c(26, 173, 196);   // Continuous fiber contour
    case 21: return c(0, 104, 122);    // Continuous fiber infill
    case 22: return c(220, 150, 95);   // Resin infill
    default: return c(128, 128, 128);
    }
}

Vector3 heatColor(float value, float minValue, float maxValue) {
    // OrcaSlicer / libvgcode DEFAULT_RANGES_COLORS: 11 stops (dark-blue -> teal -> green -> yellow ->
    // orange -> dark-red), continuously interpolated like libvgcode ColorRange::get_color_at. The
    // legend strip uses the same 11 stops, so 3D shading and legend match (and match OrcaSlicer).
    static const Vector3 palette[] = {
        Vector3(11.0f / 255.0f, 44.0f / 255.0f, 122.0f / 255.0f),
        Vector3(19.0f / 255.0f, 89.0f / 255.0f, 133.0f / 255.0f),
        Vector3(28.0f / 255.0f, 136.0f / 255.0f, 145.0f / 255.0f),
        Vector3(4.0f / 255.0f, 214.0f / 255.0f, 15.0f / 255.0f),
        Vector3(170.0f / 255.0f, 242.0f / 255.0f, 0.0f / 255.0f),
        Vector3(252.0f / 255.0f, 249.0f / 255.0f, 3.0f / 255.0f),
        Vector3(245.0f / 255.0f, 206.0f / 255.0f, 10.0f / 255.0f),
        Vector3(227.0f / 255.0f, 136.0f / 255.0f, 32.0f / 255.0f),
        Vector3(209.0f / 255.0f, 104.0f / 255.0f, 48.0f / 255.0f),
        Vector3(194.0f / 255.0f, 82.0f / 255.0f, 60.0f / 255.0f),
        Vector3(148.0f / 255.0f, 38.0f / 255.0f, 22.0f / 255.0f),
    };
    constexpr int kStops = 11;
    if (maxValue <= minValue) {
        return palette[0];
    }
    const float t = std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
    const float g = t * static_cast<float>(kStops - 1);
    const int i = std::min(static_cast<int>(g), kStops - 2);
    const float k = g - static_cast<float>(i);
    const Vector3& a = palette[i];
    const Vector3& b = palette[i + 1];
    return Vector3(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k);
}

Vector3 logarithmicHeatColor(float value, float minValue, float maxValue) {
    return heatColor(std::log1p(std::max(value, 0.0f)),
                     std::log1p(std::max(minValue, 0.0f)),
                     std::log1p(std::max(maxValue, 0.0f)));
}

Vector3 toolColor(std::uint16_t toolId) {
    static const Vector3 colors[] = {
        Vector3(0.25f, 0.66f, 0.93f),
        Vector3(0.95f, 0.46f, 0.35f),
        Vector3(0.35f, 0.82f, 0.48f),
        Vector3(0.78f, 0.50f, 0.95f),
        Vector3(0.95f, 0.78f, 0.25f)
    };
    return colors[toolId == kInvalidSmallId ? 0 : toolId % (sizeof(colors) / sizeof(colors[0]))];
}

Vector4 rgbaColor(const Vector3& color) {
    return Vector4(color.x, color.y, color.z, 1.0f);
}

RgbColor rgbColor(const Vector3& color) {
    return {
        static_cast<unsigned char>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f)
    };
}

RgbColor rgbColor(const Vector4& color) {
    return {
        static_cast<unsigned char>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f)
    };
}

std::string makeId(const char* prefix, std::uint32_t rawId) {
    return std::string(prefix) + std::to_string(rawId);
}

std::optional<std::uint32_t> parseIdWithPrefix(const std::string& itemId, const char* prefix) {
    const std::string prefixString(prefix);
    if (itemId.rfind(prefixString, 0) != 0) {
        return std::nullopt;
    }
    const std::string value = itemId.substr(prefixString.size());
    if (value.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

const char* extrusionRoleName(std::uint8_t role) {
    // Labels match OrcaSlicer's legend (ExtrusionEntity::role_to_string): the "wall / infill" naming,
    // not the old PrusaSlicer "perimeter" naming.
    switch (role) {
    case 0: return "Undefined";
    case 1: return "Inner wall";
    case 2: return "Outer wall";
    case 3: return "Overhang wall";
    case 4: return "Sparse infill";
    case 5: return "Internal solid infill";
    case 6: return "Top surface";
    case 7: return "Bottom surface";
    case 8: return "Ironing";
    case 9: return "Bridge";
    case 10: return "Internal Bridge";
    case 11: return "Gap infill";
    case 12: return "Skirt";
    case 13: return "Brim";
    case 14: return "Support";
    case 15: return "Support interface";
    case 16: return "Support transition";
    case 17: return "Prime tower";
    case 18: return "Custom";
    case 19: return "Multiple";
    case 20: return "Continuous fiber contour";
    case 21: return "Continuous fiber infill";
    case 22: return "Resin infill";
    default: return nullptr;
    }
}

std::string fallbackRoleName(std::uint8_t role) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "Role %u", static_cast<unsigned int>(role));
    return buffer;
}

std::string colorLabel(const ToolpathPreviewColor& color) {
    if (!color.name.empty()) {
        return color.name;
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "Color %u", static_cast<unsigned int>(color.id));
    return buffer;
}

std::string toolLabel(std::uint16_t toolId) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "Tool %u", static_cast<unsigned int>(toolId));
    return buffer;
}

} // namespace

bool isTravelSegment(const ToolpathPreviewSegment& segment) {
    return segment.deposition == ToolpathDepositionKind::None;
}

float segmentSpeed(const ToolpathPreviewSegment& segment) {
    return segment.actualFeedrateMmS > 0.0f ? segment.actualFeedrateMmS : segment.feedrateMmS;
}

PreviewColorMap buildPreviewColorMap(const std::vector<ToolpathPreviewColor>& previewColors) {
    PreviewColorMap previewColorById;
    previewColorById.reserve(previewColors.size());
    for (const auto& previewColor : previewColors) {
        previewColorById.emplace(previewColor.id, previewColor.colorRgba);
    }
    return previewColorById;
}

Vector4 featureTypeColorRgba(std::uint8_t role, ToolpathMotionKind motion) {
    return rgbaColor(roleColor(role, motion));
}

Vector4 toolColorRgba(std::uint16_t toolId) {
    return rgbaColor(toolColor(toolId));
}

Vector4 rangeColorRgba(float normalizedValue) {
    return rgbaColor(heatColor(normalizedValue, 0.0f, 1.0f));
}

std::optional<ToolpathPreviewLegendGroupKind> legendGroupForViewType(const std::string& viewType) {
    if (viewType == "filament") {
        return ToolpathPreviewLegendGroupKind::Filament;
    }
    if (viewType == "lineType") {
        return ToolpathPreviewLegendGroupKind::FeatureType;
    }
    if (viewType == "tool") {
        return ToolpathPreviewLegendGroupKind::Tool;
    }
    return std::nullopt;
}

const char* legendGroupViewType(ToolpathPreviewLegendGroupKind group) {
    switch (group) {
    case ToolpathPreviewLegendGroupKind::Filament: return "filament";
    case ToolpathPreviewLegendGroupKind::FeatureType: return "lineType";
    case ToolpathPreviewLegendGroupKind::Tool: return "tool";
    case ToolpathPreviewLegendGroupKind::MoveType: return "lineType";
    default: return "";
    }
}

const char* legendGroupName(ToolpathPreviewLegendGroupKind group) {
    switch (group) {
    case ToolpathPreviewLegendGroupKind::Filament: return "filament";
    case ToolpathPreviewLegendGroupKind::FeatureType: return "featureType";
    case ToolpathPreviewLegendGroupKind::Tool: return "tool";
    case ToolpathPreviewLegendGroupKind::MoveType: return "moveType";
    default: return "unknown";
    }
}

std::optional<std::uint32_t> parseLegendItemRawId(ToolpathPreviewLegendGroupKind group,
                                                  const std::string& itemId) {
    switch (group) {
    case ToolpathPreviewLegendGroupKind::Filament:
        return parseIdWithPrefix(itemId, "filament.color.");
    case ToolpathPreviewLegendGroupKind::FeatureType:
        return parseIdWithPrefix(itemId, "feature.role.");
    case ToolpathPreviewLegendGroupKind::Tool:
        return parseIdWithPrefix(itemId, "tool.");
    case ToolpathPreviewLegendGroupKind::MoveType:
        if (itemId == "move.travel") {
            return static_cast<std::uint32_t>(ToolpathMotionKind::Travel);
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::vector<ToolpathPreviewLegendItem> buildLegendItems(const ToolpathPreviewDB& preview,
                                                        ToolpathPreviewLegendGroupKind group) {
    std::vector<ToolpathPreviewLegendItem> result;
    const auto& segments = preview.getSegments();

    if (group == ToolpathPreviewLegendGroupKind::Filament) {
        std::map<std::uint16_t, int> counts;
        for (const auto& segment : segments) {
            if (segment.cpColorId != kInvalidSmallId &&
                !isTravelSegment(segment)) {
                ++counts[segment.cpColorId];
            }
        }
        for (const auto& color : preview.getColors()) {
            const auto countIt = counts.find(color.id);
            if (countIt == counts.end() || countIt->second <= 0) {
                continue;
            }
            result.push_back({
                makeId("filament.color.", color.id),
                colorLabel(color),
                color.colorRgba,
                ToolpathPreviewLegendGroupKind::Filament,
                color.id,
                countIt->second,
                preview.isPreviewColorIdVisible(color.id)
            });
        }
        return result;
    }

    if (group == ToolpathPreviewLegendGroupKind::FeatureType) {
        std::map<std::uint8_t, int> counts;
        for (const auto& segment : segments) {
            if (!isTravelSegment(segment)) {
                ++counts[segment.extrusionRole];
            }
        }
        for (const auto& [role, count] : counts) {
            const char* roleName = extrusionRoleName(role);
            result.push_back({
                makeId("feature.role.", role),
                roleName ? std::string(roleName) : fallbackRoleName(role),
                featureTypeColorRgba(role, ToolpathMotionKind::Extrusion),
                ToolpathPreviewLegendGroupKind::FeatureType,
                role,
                count,
                preview.isExtrusionRoleVisible(role)
            });
        }
        return result;
    }

    if (group == ToolpathPreviewLegendGroupKind::Tool) {
        std::map<std::uint16_t, int> counts;
        for (const auto& segment : segments) {
            if (segment.toolId != kInvalidSmallId) {
                ++counts[segment.toolId];
            }
        }
        std::unordered_set<std::uint16_t> emitted;
        for (const auto& tool : preview.getTools()) {
            const auto countIt = counts.find(tool.id);
            if (countIt == counts.end() || countIt->second <= 0) {
                continue;
            }
            emitted.insert(tool.id);
            result.push_back({
                makeId("tool.", tool.id),
                toolLabel(tool.id),
                toolColorRgba(tool.id),
                ToolpathPreviewLegendGroupKind::Tool,
                tool.id,
                countIt->second,
                preview.isToolIdVisible(tool.id)
            });
        }
        for (const auto& [toolId, count] : counts) {
            if (emitted.find(toolId) != emitted.end()) {
                continue;
            }
            result.push_back({
                makeId("tool.", toolId),
                toolLabel(toolId),
                toolColorRgba(toolId),
                ToolpathPreviewLegendGroupKind::Tool,
                toolId,
                count,
                preview.isToolIdVisible(toolId)
            });
        }
        return result;
    }

    if (group == ToolpathPreviewLegendGroupKind::MoveType) {
        int travelCount = 0;
        for (const auto& segment : segments) {
            if (isTravelSegment(segment)) {
                ++travelCount;
            }
        }
        if (travelCount > 0) {
            result.push_back({
                "move.travel",
                "Travel",
                featureTypeColorRgba(0, ToolpathMotionKind::Travel),
                ToolpathPreviewLegendGroupKind::MoveType,
                static_cast<std::uint32_t>(ToolpathMotionKind::Travel),
                travelCount,
                preview.getShowTravel()
            });
        }
    }

    return result;
}

std::optional<RgbColor> resolveSegmentColor(const ToolpathPreviewSegment& segment,
                                            ToolpathPreviewColorMode colorMode,
                                            const PreviewColorMap& previewColorById,
                                            const ToolpathPreviewStats& stats) {
    switch (colorMode) {
    case ToolpathPreviewColorMode::PreviewColor:
        if (isTravelSegment(segment)) {
            return rgbColor(roleColor(segment.extrusionRole, isTravelSegment(segment) ? segment.motion : ToolpathMotionKind::Extrusion));
        }
        if (const auto colorIt = previewColorById.find(segment.cpColorId); colorIt != previewColorById.end()) {
            return rgbColor(colorIt->second);
        }
        return std::nullopt;
    case ToolpathPreviewColorMode::Speed:
        return rgbColor(heatColor(segment.feedrateMmS, stats.minSpeedMmS, stats.maxSpeedMmS));
    case ToolpathPreviewColorMode::ActualSpeed:
        return rgbColor(heatColor(segment.actualFeedrateMmS,
                                  stats.minActualSpeedMmS, stats.maxActualSpeedMmS));
    case ToolpathPreviewColorMode::Acceleration:
        return rgbColor(heatColor(segment.accelerationMmS2,
                                  stats.minAccelerationMmS2, stats.maxAccelerationMmS2));
    case ToolpathPreviewColorMode::Jerk:
        return rgbColor(heatColor(segment.jerkMmS, stats.minJerkMmS, stats.maxJerkMmS));
    case ToolpathPreviewColorMode::LayerHeight:
        return rgbColor(heatColor(segment.heightMm, stats.minLayerHeightMm, stats.maxLayerHeightMm));
    case ToolpathPreviewColorMode::LineWidth:
        return rgbColor(heatColor(segment.widthMm, stats.minWidthMm, stats.maxWidthMm));
    case ToolpathPreviewColorMode::VolumetricFlow:
        return rgbColor(heatColor(segment.mm3PerMm * segment.feedrateMmS,
                                  stats.minVolumetricFlow, stats.maxVolumetricFlow));
    case ToolpathPreviewColorMode::ActualVolumetricFlow:
        return rgbColor(heatColor(segment.mm3PerMm * segment.actualFeedrateMmS,
                                  stats.minActualVolumetricFlow,
                                  stats.maxActualVolumetricFlow));
    case ToolpathPreviewColorMode::LayerTime:
        return rgbColor(heatColor(segment.layerDurationS,
                                  stats.minLayerTimeS, stats.maxLayerTimeS));
    case ToolpathPreviewColorMode::LayerTimeLogarithmic:
        return rgbColor(logarithmicHeatColor(segment.layerDurationS,
                                             stats.minLayerTimeS, stats.maxLayerTimeS));
    case ToolpathPreviewColorMode::FanSpeed:
        return rgbColor(heatColor(segment.fanSpeedPercent,
                                  stats.minFanSpeedPercent, stats.maxFanSpeedPercent));
    case ToolpathPreviewColorMode::Temperature:
        return rgbColor(heatColor(segment.temperatureC,
                                  stats.minTemperatureC, stats.maxTemperatureC));
    case ToolpathPreviewColorMode::PressureAdvance:
        return rgbColor(heatColor(segment.pressureAdvance,
                                  stats.minPressureAdvance, stats.maxPressureAdvance));
    case ToolpathPreviewColorMode::Tool:
        return rgbColor(toolColor(segment.toolId));
    case ToolpathPreviewColorMode::FeatureType:
    default:
        return rgbColor(roleColor(segment.extrusionRole, isTravelSegment(segment) ? segment.motion : ToolpathMotionKind::Extrusion));
    }
}

std::vector<unsigned char> collectFilterPassAlpha(const ToolpathPreviewDB& preview) {
    const auto& segments = preview.getSegments();
    const bool showTravel = preview.getShowTravel();
    // Fetch the hidden lists once, not per segment: getHiddenXxx() returns a fresh vector copy each
    // call, so a per-segment lookup would allocate 3 vectors for every one of millions of segments.
    const auto hiddenRoles = preview.getHiddenExtrusionRoles();
    const auto hiddenColorIds = preview.getHiddenPreviewColorIds();
    const auto hiddenToolIds = preview.getHiddenToolIds();
    const bool hasHiddenRoles = !hiddenRoles.empty();
    const bool hasHiddenColorIds = !hiddenColorIds.empty();
    const bool hasHiddenToolIds = !hiddenToolIds.empty();
    const auto isHiddenId = [](const auto& ids, auto id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };

    std::vector<unsigned char> alpha(segments.size(), 0);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const bool travel = isTravelSegment(segment);
        const bool finish = segment.fiberPhase == ToolpathFiberPhase::Finish;
        if ((finish && !preview.getShowFiberProcess()) || (travel && !finish && !showTravel)) {
            continue;
        }
        if (!travel && hasHiddenRoles && isHiddenId(hiddenRoles, segment.extrusionRole)) {
            continue;
        }
        if (segment.cpColorId != kInvalidSmallId &&
            hasHiddenColorIds && isHiddenId(hiddenColorIds, segment.cpColorId)) {
            continue;
        }
        if (segment.toolId != kInvalidSmallId &&
            hasHiddenToolIds && isHiddenId(hiddenToolIds, segment.toolId)) {
            continue;
        }
        alpha[i] = 255;
    }
    return alpha;
}

std::vector<VisibleSegmentSpan> computeVisibleSegmentSpans(const ToolpathPreviewDB& preview) {
    const auto& layers = preview.getLayers();
    if (layers.empty()) {
        return {};
    }
    const int maxLayerIdx = static_cast<int>(layers.size()) - 1;
    const int startLayer = std::clamp(preview.getShowLayerRangeStart(), 0, maxLayerIdx);
    const int endLayer = std::clamp(
        std::max(startLayer, preview.getShowLayerRangeEnd()), startLayer, maxLayerIdx);
    const int sharedStep = preview.getCurrentStep();

    std::vector<VisibleSegmentSpan> spans;
    spans.reserve(static_cast<std::size_t>(endLayer - startLayer + 1));
    for (int layer = startLayer; layer <= endLayer; ++layer) {
        const auto& item = layers[static_cast<std::size_t>(layer)];
        std::uint32_t count = item.segmentCount;
        if (sharedStep >= 0) {
            count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(sharedStep) + 1u, count));
        }
        if (count > 0) {
            spans.push_back({item.segmentBegin, item.segmentBegin + count});
        }
    }
    return spans;
}

bool isEventVisibleAtSharedStep(const ToolpathPreviewDB& preview,
                                const ToolpathPreviewOption& event) {
    const auto& layers = preview.getLayers();
    if (layers.empty()) {
        return false;
    }
    const int maxLayerIdx = static_cast<int>(layers.size()) - 1;
    const int startLayer = std::clamp(preview.getShowLayerRangeStart(), 0, maxLayerIdx);
    const int endLayer = std::clamp(
        std::max(startLayer, preview.getShowLayerRangeEnd()), startLayer, maxLayerIdx);
    const int sharedStep = preview.getCurrentStep();
    for (int layer = startLayer; layer <= endLayer; ++layer) {
        const auto& item = layers[static_cast<std::size_t>(layer)];
        if (item.id != event.layerId) {
            continue;
        }
        std::uint64_t visibleEnd =
            static_cast<std::uint64_t>(item.segmentBegin) + item.segmentCount;
        if (sharedStep >= 0) {
            visibleEnd = static_cast<std::uint64_t>(item.segmentBegin) +
                std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(sharedStep) + 1u, item.segmentCount);
        }
        return event.afterSegment <= visibleEnd;
    }
    return false;
}

const char* colorModeName(ToolpathPreviewColorMode mode) {
    switch (mode) {
    case ToolpathPreviewColorMode::PreviewColor: return "PreviewColor";
    case ToolpathPreviewColorMode::FeatureType: return "FeatureType";
    case ToolpathPreviewColorMode::Speed: return "Speed";
    case ToolpathPreviewColorMode::ActualSpeed: return "ActualSpeed";
    case ToolpathPreviewColorMode::Acceleration: return "Acceleration";
    case ToolpathPreviewColorMode::Jerk: return "Jerk";
    case ToolpathPreviewColorMode::LayerHeight: return "LayerHeight";
    case ToolpathPreviewColorMode::Tool: return "Tool";
    case ToolpathPreviewColorMode::LineWidth: return "LineWidth";
    case ToolpathPreviewColorMode::VolumetricFlow: return "VolumetricFlow";
    case ToolpathPreviewColorMode::ActualVolumetricFlow: return "ActualVolumetricFlow";
    case ToolpathPreviewColorMode::LayerTime: return "LayerTime";
    case ToolpathPreviewColorMode::LayerTimeLogarithmic: return "LayerTimeLogarithmic";
    case ToolpathPreviewColorMode::FanSpeed: return "FanSpeed";
    case ToolpathPreviewColorMode::Temperature: return "Temperature";
    case ToolpathPreviewColorMode::PressureAdvance: return "PressureAdvance";
    default: return "Unknown";
    }
}

} // namespace GPlatform::ToolpathPreviewUtils
