#pragma once

#include "ToolpathPreviewDB.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace GPlatform::ToolpathPreviewUtils {

using PreviewColorMap = std::unordered_map<std::uint16_t, Vector4>;
using RgbColor = std::array<unsigned char, 3>;

bool isTravelSegment(const ToolpathPreviewSegment& segment);
float segmentSpeed(const ToolpathPreviewSegment& segment);
PreviewColorMap buildPreviewColorMap(const std::vector<ToolpathPreviewColor>& previewColors);

std::vector<ToolpathPreviewLegendItem> buildLegendItems(const ToolpathPreviewDB& preview,
                                                        ToolpathPreviewLegendGroupKind group);
std::optional<ToolpathPreviewLegendGroupKind> legendGroupForViewType(const std::string& viewType);
const char* legendGroupViewType(ToolpathPreviewLegendGroupKind group);
const char* legendGroupName(ToolpathPreviewLegendGroupKind group);
std::optional<std::uint32_t> parseLegendItemRawId(ToolpathPreviewLegendGroupKind group,
                                                  const std::string& itemId);
Vector4 featureTypeColorRgba(std::uint8_t role, ToolpathMotionKind motion);
Vector4 toolColorRgba(std::uint16_t toolId);
Vector4 rangeColorRgba(float normalizedValue);

std::optional<RgbColor> resolveSegmentColor(const ToolpathPreviewSegment& segment,
                                            ToolpathPreviewColorMode colorMode,
                                            const PreviewColorMap& previewColorById,
                                            const ToolpathPreviewStats& stats);

// Per-segment visibility that depends ONLY on the filter state (travel / hidden role / hidden
// filament colour / hidden tool) — NOT on the layer slider. 255 = passes filters, 0 = filtered out.
// This is the layer-independent half of visibility; it only needs recomputing when a filter changes.
std::vector<unsigned char> collectFilterPassAlpha(const ToolpathPreviewDB& preview);

// One [begin, end) source-segment span per visible layer. CurrentStep is a shared cap: at Step N,
// every layer in ShowLayerRangeStart/End contributes at most its first N + 1 segments. The result is
// intentionally non-contiguous when more than one layer is visible.
struct VisibleSegmentSpan {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};
std::vector<VisibleSegmentSpan> computeVisibleSegmentSpans(const ToolpathPreviewDB& preview);
bool isEventVisibleAtSharedStep(const ToolpathPreviewDB& preview,
                                const ToolpathPreviewOption& event);

const char* colorModeName(ToolpathPreviewColorMode mode);

} // namespace GPlatform::ToolpathPreviewUtils
