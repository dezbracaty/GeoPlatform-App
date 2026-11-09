#include "ToolpathPreviewRelations.hpp"

#include "DBRelationsRegistration.hpp"
#include "DocumentManager.hpp"
#include <DBRelationRegistry.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <PrintBedDB.hpp>
#include <SliceOutputRecord.hpp>
#include <TransactionManager.hpp>
#include <QCoreApplication>
#include <QThread>
#include <set>

namespace ToolpathPreviewRelations {

bool sourceModelsAvailable(const std::vector<DBInstanceID>& ids, DBInstanceID bedId) {
    auto* document = DocumentManager::instance();
    if (ids.empty() || !document->getDB<PrintBedDB>(bedId)) return false;
    for (const auto id : ids) {
        const auto instance = document->getDB<ModelInstanceDB>(id);
        if (!instance || instance->getParentPrintBedDBId() != bedId) return false;
    }
    return true;
}

bool sourceObjectSettingsMatch(const GPlatform::SliceOutputRecord& output) {
    if (output.sourceObjectSemantics.empty())
        return true;
    if (output.sourceObjectSemantics.size() != output.sourceModelIds.size())
        return false;
    if (!output.sourceObjectFilamentSlots.empty() && output.sourceObjectFilamentSlots.size() != output.sourceModelIds.size())
        return false;
    const bool hasPartSnapshot = !output.sourcePartSemantics.empty();
    if (hasPartSnapshot && output.sourcePartSemantics.size() != output.sourceModelIds.size())
        return false;
    auto* document = DocumentManager::instance();
    for (std::size_t i = 0; i < output.sourceModelIds.size(); ++i) {
        const auto instance = document->getDB<ModelInstanceDB>(output.sourceModelIds[i]);
        const auto object = instance ? instance->object() : nullptr;
        if (!object || object->getProjectSemantics() != output.sourceObjectSemantics[i])
            return false;
        if (!output.sourceObjectFilamentSlots.empty() && object->getDefaultFilamentSlot() != output.sourceObjectFilamentSlots[i])
            return false;
        if (hasPartSnapshot) {
            std::size_t partIndex = 0;
            const auto& snapshots = output.sourcePartSemantics[i];
            for (const auto& part : instance->parts()) {
                if (!part || !part->hasValidMesh())
                    continue;
                if (partIndex >= snapshots.size())
                    return false;
                const auto& [id, semantics, binding, slot] = snapshots[partIndex];
                if (part->getDBInstanceID().getValue() != id || part->getProjectSemantics() != semantics ||
                    part->getFilamentBindingMode() != binding ||
                    (binding == int(ModelFilamentBindingMode::ExplicitSlot) && part->getDefaultFilamentSlot() != slot))
                    return false;
                ++partIndex;
            }
            if (partIndex != snapshots.size())
                return false;
        }
    }
    return true;
}

bool sourceEdgesMatch(DBInstanceID previewId, const std::vector<DBInstanceID>& expectedIds) {
    std::set<DBInstanceID> actual;
    for (const auto& edge : DocumentManager::instance()->getDependencyRelationEdges())
        if (edge.targetId == previewId && edge.relationName == SourceModelsRelation)
            actual.insert(edge.sourceId);
    return actual == std::set<DBInstanceID>(expectedIds.begin(), expectedIds.end());
}

namespace {
void sourceDetached(DBRelationDetachContext& context) {
    if (context.reason != DBRelationDetachReason::SourceDeleted) return;
    auto* application = QCoreApplication::instance();
    if (!application) return;
    const auto id = context.targetId;
    // Current document edits are synchronous and never run a nested event loop.
    // Recheck after that operation returns, so rollback can restore the source.
    QMetaObject::invokeMethod(application, [id] {
        Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
        Q_ASSERT(!TransactionManager::instance().isInTransaction());
        auto* document = DocumentManager::instance();
        const auto preview = document->getDB<GPlatform::ToolpathPreviewDB>(id);
        if (!preview) return;
        const auto scope = preview->getSourceScope();
        if (scope != GPlatform::ToolpathPreviewSourceScope::SingleModel &&
            scope != GPlatform::ToolpathPreviewSourceScope::Plate) return;
        if (sourceModelsAvailable(preview->getSourceModelDBIds(), document->getOwner(id))) return;
        // Both DBs must outlive queued raw-pointer notifications.
        const auto material = preview->getMaterial();
        TransientUpdateGuard derived;
        document->flushPendingChanges();
        document->unregisterDBInstance(id);
        document->flushPendingChanges();
    }, Qt::QueuedConnection);
}
}

void registerRelations() {
    // A preview is a print output for one bed, not a child of the first model
    // that happened to participate in the slice. More than one preview may
    // exist briefly while a replacement is committed.
    auto* document = DocumentManager::instance();
    if (!document) return;
    auto& registry = DBRelationRegistry::instance();
    registry.registerDependency(TypeID::MODEL_INSTANCE_DB, TypeID::TOOLPATH_PREVIEW_DB,
        SourceModelsRelation, "invalidate-toolpath-on-source-delete", {},
        [](DBRelationUpdateContext&) {}, nullptr, sourceDetached);
    registry.registerDependencyReference(TypeID::TOOLPATH_PREVIEW_DB,
        "SourceModelDBIds", TypeID::MODEL_INSTANCE_DB, SourceModelsRelation);
    document->registerOwnershipRelation(
        TypeID::PRINT_BED_DB,
        TypeID::TOOLPATH_PREVIEW_DB,
        PrintBedPreviewsRelation,
        true,
        DBRelationDeletePolicy::CascadeDelete);
}

} // namespace ToolpathPreviewRelations

REGISTER_DB_RELATIONS(ToolpathPreviewRelations::registerRelations())
