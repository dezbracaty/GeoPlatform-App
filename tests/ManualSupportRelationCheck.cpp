#include "DBRelationsRegistration.hpp"
#include "ManualSupportDB.hpp"
#include "ManualSupportRelations.hpp"

#include <DBCopyService.hpp>
#include <DBRelationRegistry.hpp>
#include <DocumentManager.hpp>
#include "TestModelGraphFixture.hpp"
#include <TransactionManager.hpp>

#include <iostream>
#include <memory>

namespace {
int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::shared_ptr<const IndexedTriangleMesh> triangle() {
    auto geometry = std::make_shared<IndexedTriangleMesh>();
    geometry->positions = {
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f)};
    geometry->indices = {0, 1, 2};
    geometry->rebuildVertexNormals();
    return geometry;
}
} // namespace

int main() {
    auto* document = DocumentManager::instance();
    auto& registry = DBRelationRegistry::instance();
    auto& transactions = TransactionManager::instance();
    registry.clear();
    transactions.clear();
    GPlatform::DBRelations::registerModule();

    auto model = createTestModelGraph().instance;
    if (!document || !model) return fail("failed to create support source model");

    std::shared_ptr<ManualSupportDB> support;
    {
        TransactionGuard guard("create manual support aggregate");
        support = trans::TransDB::create<ManualSupportDB>();
        if (!support || !document->attachOwnedChild(
                model->getDBInstanceID(), support->getDBInstanceID(),
                ManualSupportRelations::ManualSupportRelation)) {
            guard.rollback();
            return fail("failed to attach manual support aggregate");
        }
        support->replaceSelectionSnapshot("selection-v1", 1);
        guard.commit();
    }

    if (ManualSupportRelations::findForModel(model->getDBInstanceID()) !=
            support ||
        document->getOwner(support->getDBInstanceID()) !=
            model->getDBInstanceID()) {
        return fail("manual support ownership invariant was not established");
    }

    auto duplicate = trans::TransDB::create<ManualSupportDB>();
    if (document->attachOwnedChild(
            model->getDBInstanceID(), duplicate->getDBInstanceID(),
            ManualSupportRelations::ManualSupportRelation)) {
        return fail("single support ownership accepted a duplicate child");
    }
    document->unregisterDBInstance(duplicate->getDBInstanceID());

    const auto revisionOneGeometry = triangle();
    if (!support->setGeneratedGeometry(revisionOneGeometry, 1) ||
        !support->hasGeneratedGeometry()) {
        return fail("current-revision support geometry was rejected");
    }

    {
        TransactionGuard guard("edit manual support selection");
        support->replaceSelectionSnapshot("selection-v2", 2);
        guard.commit();
    }
    if (support->hasGeneratedGeometry() || support->getGeneratedGeometry()) {
        return fail("selection revision did not invalidate derived support geometry");
    }
    if (support->setGeneratedGeometry(revisionOneGeometry, 1)) {
        return fail("stale asynchronous support geometry was accepted");
    }
    if (!support->setGeneratedGeometry(triangle(), 2)) {
        return fail("new-revision support geometry was rejected");
    }

    if (!transactions.undo() || support->getRevision() != 1 ||
        support->getSelectionStateBlob() != "selection-v1" ||
        support->hasGeneratedGeometry()) {
        return fail("undo did not restore state and invalidate derived geometry");
    }
    if (!transactions.redo() || support->getRevision() != 2 ||
        support->getSelectionStateBlob() != "selection-v2" ||
        support->hasGeneratedGeometry()) {
        return fail("redo did not restore state and keep derived geometry invalid");
    }

    DBCopyResult copyResult;
    {
        TransactionGuard guard("copy manual support aggregate");
        DBCopyService copyService(*document);
        copyResult = copyService.copy({model->getDBInstanceID()});
        if (!copyResult.success) {
            guard.rollback();
            return fail("failed to copy manual support aggregate");
        }
        guard.commit();
    }
    const auto copiedModelIt = copyResult.idMap.find(model->getDBInstanceID());
    const auto copiedSupportIt = copyResult.idMap.find(support->getDBInstanceID());
    if (copiedModelIt == copyResult.idMap.end() ||
        copiedSupportIt == copyResult.idMap.end()) {
        return fail("copy omitted a manual support aggregate node");
    }
    auto copiedSupport = std::dynamic_pointer_cast<ManualSupportDB>(
        document->getDBInstance(copiedSupportIt->second));
    auto copiedModel = document->getDB<ModelInstanceDB>(copiedModelIt->second);
    if (!copiedSupport || !copiedModel ||
        ManualSupportRelations::findForModel(copiedModelIt->second) !=
            copiedSupport ||
        document->getOwner(copiedSupportIt->second) != copiedModelIt->second ||
        copiedSupport->getSelectionStateBlob() != "selection-v2" ||
        copiedSupport->hasGeneratedGeometry()) {
        return fail("copy did not remap ownership or correctly discard derived geometry");
    }

    {
        TransactionGuard guard("delete manual support source model");
        if (!document->unregisterDBInstance(model->getDBInstanceID())) {
            guard.rollback();
            return fail("failed to delete manual support source model");
        }
        guard.commit();
    }
    if (document->getDBInstance(support->getDBInstanceID())) {
        return fail("manual support aggregate survived cascade model deletion");
    }
    if (!document->getDBInstance(copiedModelIt->second) ||
        !document->getDBInstance(copiedSupportIt->second)) {
        return fail("deleting source aggregate also deleted its independent copy");
    }

    return 0;
}
