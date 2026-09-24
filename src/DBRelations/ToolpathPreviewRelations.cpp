#include "ToolpathPreviewRelations.hpp"

#include "DBRelationsRegistration.hpp"
#include "DocumentManager.hpp"

namespace ToolpathPreviewRelations {

void registerRelations() {
    // A preview is a print output for one bed, not a child of the first model
    // that happened to participate in the slice. More than one preview may
    // exist briefly while a replacement is committed.
    auto* document = DocumentManager::instance();
    if (!document) return;
    document->registerOwnershipRelation(
        TypeID::PRINT_BED_DB,
        TypeID::TOOLPATH_PREVIEW_DB,
        PrintBedPreviewsRelation,
        true,
        DBRelationDeletePolicy::CascadeDelete);
}

} // namespace ToolpathPreviewRelations

REGISTER_DB_RELATIONS(ToolpathPreviewRelations::registerRelations())
