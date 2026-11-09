#pragma once

#include <BaseID.hpp>
#include <ToolpathPreviewDB.hpp>
#include <vector>

namespace GPlatform { struct SliceOutputRecord; }

namespace ToolpathPreviewRelations {

inline constexpr const char* PrintBedPreviewsRelation =
    "PrintBedToolpathPreviews";

inline constexpr const char* SourceModelsRelation =
    GPlatform::ToolpathPreviewDB::kSourceModelsRelation;

// Document-thread checks shared by publication and derived-result cleanup.
bool sourceModelsAvailable(const std::vector<DBInstanceID>& ids, DBInstanceID bedId);
bool sourceObjectSettingsMatch(const GPlatform::SliceOutputRecord& output);
bool sourceEdgesMatch(DBInstanceID previewId, const std::vector<DBInstanceID>& expectedIds);
void registerRelations();

} // namespace ToolpathPreviewRelations
