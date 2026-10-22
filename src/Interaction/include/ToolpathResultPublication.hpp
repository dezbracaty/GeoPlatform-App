#pragma once
#include "ToolpathPreviewPreparation.hpp"
#include <SliceOutputRecord.hpp>
#include <ToolpathPreviewDB.hpp>

namespace GPlatform {
enum class PreviewPublicationStage { Create, Register, Attach, Commit, RemovePrevious, AfterRemovePrevious };
struct ToolpathPublicationResult {
    bool accepted = false;
    QString error;
    std::shared_ptr<ToolpathPreviewDB> preview;
};
// Main-thread acceptance boundary shared by slicing and G-code import.
// The optional operation observer can inject failures without changing SDK data.
ToolpathPublicationResult publishToolpathResult(std::shared_ptr<SliceOutputRecord> output,
    PreparedToolpathPreview prepared, std::uint64_t epoch,
    const std::function<void(PreviewPublicationStage)>& beforeOperation = {});
}
