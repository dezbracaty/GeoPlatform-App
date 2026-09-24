#pragma once

#include "ToolpathPreviewTypes.hpp"

namespace libslicer {
struct ToolpathPreview;
}

namespace GPlatform {

// The sole anti-corruption boundary between libslicer's public result contract and
// the application's persistent preview document.
ToolpathPreviewData adaptLibSlicerToolpath(const libslicer::ToolpathPreview& preview);

} // namespace GPlatform
