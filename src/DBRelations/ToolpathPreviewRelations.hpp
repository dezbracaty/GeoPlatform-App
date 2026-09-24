#pragma once

#include <BaseID.hpp>

namespace ToolpathPreviewRelations {

inline constexpr const char* PrintBedPreviewsRelation =
    "PrintBedToolpathPreviews";

void registerRelations();

} // namespace ToolpathPreviewRelations
