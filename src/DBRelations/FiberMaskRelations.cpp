#include "DBRelationsRegistration.hpp"
#include <DocumentManager.hpp>
#include <FiberMaskDB.hpp>

namespace {
void registerFiberMaskRelations() {
    auto* document = DocumentManager::instance();
    if (!document)
        return;
    document->registerOwnershipRelation(TypeID::MODEL_INSTANCE_DB, TypeID::FIBER_MASK_DB,
                                        FiberMaskDB::kOwnerRelation, true,
                                        DBRelationDeletePolicy::CascadeDelete);
}
} // namespace
REGISTER_DB_RELATIONS(registerFiberMaskRelations())
