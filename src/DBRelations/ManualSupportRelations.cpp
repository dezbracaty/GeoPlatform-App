#include "ManualSupportRelations.hpp"

#include "DBRelationsRegistration.hpp"
#include "DocumentManager.hpp"
#include "ManualSupportDB.hpp"

namespace ManualSupportRelations {

void registerRelations() {
    auto* document = DocumentManager::instance();
    if (!document) return;
    document->registerOwnershipRelation(
        TypeID::MODEL_INSTANCE_DB,
        TypeID::MANUAL_SUPPORT_DB,
        ManualSupportRelation,
        false,
        DBRelationDeletePolicy::CascadeDelete);
}

std::shared_ptr<ManualSupportDB> findForModel(DBInstanceID modelId) {
    auto* document = DocumentManager::instance();
    if (!document || !modelId.isValid()) return {};
    const auto children = document->getOwnedChildren(
        modelId, ManualSupportRelation);
    return children.empty()
        ? std::shared_ptr<ManualSupportDB>{}
        : document->getDB<ManualSupportDB>(children.front());
}

} // namespace ManualSupportRelations

REGISTER_DB_RELATIONS(ManualSupportRelations::registerRelations())
