#pragma once

#include <BaseID.hpp>
#include <ToolpathPreviewDB.hpp>
#include <vector>

namespace ToolpathPreviewRelations {

inline constexpr const char* PrintBedPreviewsRelation =
    "PrintBedToolpathPreviews";

inline constexpr const char* SourceModelsRelation =
    GPlatform::ToolpathPreviewDB::kSourceModelsRelation;

// Document-thread checks shared by publication and derived-result cleanup.
bool sourceModelsAvailable(const std::vector<DBInstanceID>& ids, DBInstanceID bedId);
bool sourceEdgesMatch(DBInstanceID previewId, const std::vector<DBInstanceID>& expectedIds);
void registerRelations();

} // namespace ToolpathPreviewRelations
