#include "ModelGraphRelations.hpp"

#include "DBRelationsRegistration.hpp"
#include <ActorDB.hpp>
#include <DBRelationRegistry.hpp>
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>

namespace {

void invalidateDependencyTarget(DBRelationUpdateContext& context) {
    if (auto* document = DocumentManager::instance()) {
        if (const auto target = document->getDBInstance(context.targetId)) {
            document->notifyChange(
                target.get(), ChangeType::PROPERTY_CHANGED,
                context.relationName);
        }
    }
}

void invalidateAttachedDependencyTarget(DBRelationAttachContext& context) {
    if (auto* document = DocumentManager::instance()) {
        if (const auto target = document->getDBInstance(context.targetId)) {
            document->notifyChange(
                target.get(), ChangeType::PROPERTY_CHANGED,
                context.relationName);
        }
    }
}

void invalidateDetachedDependencyTarget(DBRelationDetachContext& context) {
    if (auto* document = DocumentManager::instance()) {
        if (const auto target = document->getDBInstance(context.targetId)) {
            document->notifyChange(
                target.get(), ChangeType::PROPERTY_CHANGED,
                context.relationName);
        }
    }
}

void observeOwnedModelChanges(DocumentManager* document) {
    static DocumentManager::ListenerID listenerId = 0;
    if (!document || listenerId != 0) return;

    listenerId = document->addChangeListener(
        [](const DocumentManager::ChangeNotification& change) {
            if (change.changeType != ChangeType::PROPERTY_CHANGED) return;
            auto* current = DocumentManager::instance();
            if (!current) return;

            auto notify = [current](
                DBInstanceID id, const char* relationName) {
                const auto target = current->getDBInstance(id);
                if (target) {
                    current->notifyChange(
                        target.get(), ChangeType::PROPERTY_CHANGED,
                        relationName);
                }
            };

            if (change.dbType == TypeID::MODEL_GEOMETRY_DB) {
                notify(current->getOwner(change.id),
                       ModelPartDB::kGeometryRelation);
                return;
            }
            if (change.dbType == TypeID::MODEL_SURFACE_COLOR_DB) {
                notify(current->getOwner(change.id),
                       ModelPartDB::kSurfaceColorsRelation);
                return;
            }
            if (change.dbType == TypeID::MATERIAL_DB) {
                const DBInstanceID ownerId = current->getOwner(change.id);
                if (current->getDB<ModelPartDB>(ownerId)) {
                    notify(ownerId, ModelPartDB::kMaterialRelation);
                } else if (current->getDB<ActorDB>(ownerId)) {
                    notify(ownerId, ActorDB::kMaterialRelation);
                }
                return;
            }
            if (change.dbType == TypeID::MODEL_PART_DB) {
                notify(current->getOwner(change.id),
                       ModelPartDB::kObjectRelation);
                return;
            }
            if (change.dbType == TypeID::MODEL_OBJECT_DB) {
                for (const DBInstanceID instanceId :
                     current->getOwnedChildren(
                         change.id, ModelInstanceDB::kObjectRelation)) {
                    notify(instanceId, ModelInstanceDB::kObjectRelation);
                }
            }
        });
}

} // namespace

namespace ModelGraphRelations {
void registerRelations() {
    auto* document = DocumentManager::instance();
    if (!document) return;
    auto& registry = DBRelationRegistry::instance();
    document->registerOwnershipRelation(
        TypeID::MODEL_OBJECT_DB, TypeID::MODEL_PART_DB,
        ModelPartDB::kObjectRelation, true,
        DBRelationDeletePolicy::CascadeDelete);
    document->registerOwnershipRelation(
        TypeID::MODEL_OBJECT_DB, TypeID::MODEL_INSTANCE_DB,
        ModelInstanceDB::kObjectRelation, true,
        DBRelationDeletePolicy::CascadeDelete);
    document->registerOwnershipRelation(
        TypeID::MODEL_PART_DB, TypeID::MODEL_GEOMETRY_DB,
        ModelPartDB::kGeometryRelation, false,
        DBRelationDeletePolicy::CascadeDelete);
    document->registerOwnershipRelation(
        TypeID::MODEL_PART_DB, TypeID::MATERIAL_DB,
        ModelPartDB::kMaterialRelation, false,
        DBRelationDeletePolicy::CascadeDelete);
    document->registerOwnershipRelation(
        TypeID::MODEL_PART_DB, TypeID::MODEL_SURFACE_COLOR_DB,
        ModelPartDB::kSurfaceColorsRelation, false,
        DBRelationDeletePolicy::CascadeDelete);
    observeOwnedModelChanges(document);

    // SlicingConfig is shared reference data rather than an owned child. The
    // Instance stores the single reference ID; the live dependency is derived
    // from that field and projects palette changes back onto the Instance.
    registry.registerDependency(
        TypeID::SLICING_CONFIG_DB, TypeID::MODEL_INSTANCE_DB,
        ModelInstanceDB::kSlicingConfigRelation,
        "slicing-config-invalidates-instance", {},
        invalidateDependencyTarget,
        invalidateAttachedDependencyTarget,
        invalidateDetachedDependencyTarget);
    registry.registerDependencyReference(
        TypeID::MODEL_INSTANCE_DB, "SlicingConfigDBId",
        TypeID::SLICING_CONFIG_DB,
        ModelInstanceDB::kSlicingConfigRelation);
}
}

REGISTER_DB_RELATIONS(ModelGraphRelations::registerRelations())
