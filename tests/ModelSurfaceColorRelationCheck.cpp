#include "DBRelationsRegistration.hpp"
#include "ModelGraphRelations.hpp"
#include "ModelSurfaceColorDB.hpp"
#include <ChainUpdateSystem.hpp>
#include <DBCopyService.hpp>
#include <DBRelationRegistry.hpp>
#include <DocumentManager.hpp>
#include "TestModelGraphFixture.hpp"
#include <TransactionManager.hpp>
#include <QString>
#include <algorithm>
#include <iostream>

namespace {
int fail(const char* message) {
    std::cerr << message << std::endl;
    return 1;
}
}

int main() {
    auto* document = DocumentManager::instance();
    auto& registry = DBRelationRegistry::instance();
    auto& chain = ChainUpdateSystem::instance();
    auto& transactions = TransactionManager::instance();

    registry.clear();
    chain.clear();
    transactions.clear();
    GPlatform::DBRelations::registerModule();

    auto graph = createTestModelGraph();
    auto mesh = graph.instance;
    auto part = graph.part;
    if (!document || !mesh || !part) return fail("failed to create model");

    transactions.beginTransaction("create model surface colors");
    auto colors = trans::TransDB::create<ModelSurfaceColorDB>();
    if (!colors) return fail("failed to create surface colors");
    if (!document->attachOwnedChild(
            part->getDBInstanceID(), colors->getDBInstanceID(),
            ModelPartDB::kSurfaceColorsRelation) ||
        part->surfaceColors() != colors) {
        return fail("concrete surface-color relationship failed to attach");
    }
    colors->setSurfaceColorData("encoded-colors");
    colors->setRevision(1);
    transactions.commitTransaction();

    if (document->getOwner(colors->getDBInstanceID()) != part->getDBInstanceID()) {
        return fail("surface colors were not owned by their part");
    }
    const auto children = document->getOwnedChildren(
        part->getDBInstanceID(), ModelPartDB::kSurfaceColorsRelation);
    if (children.size() != 1 || children.front() != colors->getDBInstanceID()) {
        return fail("surface color ownership cardinality mismatch");
    }
    const auto dependencyEdges = document->getDependencyRelationEdges();
    if (std::any_of(dependencyEdges.begin(), dependencyEdges.end(),
                    [&](const auto& edge) {
                        return edge.sourceId == colors->getDBInstanceID() &&
                            edge.targetId == part->getDBInstanceID() &&
                            edge.relationName ==
                                ModelPartDB::kSurfaceColorsRelation;
                    })) {
        return fail("owned surface colors created a duplicate dependency edge");
    }

    int partInvalidations = 0;
    int objectInvalidations = 0;
    int instanceInvalidations = 0;
    const auto listenerId = document->addChangeListener(
        [&](const DocumentManager::ChangeNotification& change) {
            if (change.changeType != ChangeType::PROPERTY_CHANGED) return;
            if (change.id == part->getDBInstanceID() &&
                change.propertyName == ModelPartDB::kSurfaceColorsRelation) {
                ++partInvalidations;
            } else if (change.id == graph.object->getDBInstanceID() &&
                       change.propertyName == ModelPartDB::kObjectRelation) {
                ++objectInvalidations;
            } else if (change.id == mesh->getDBInstanceID() &&
                       change.propertyName ==
                           ModelInstanceDB::kObjectRelation) {
                ++instanceInvalidations;
            }
        });
    colors->setRevision(2);
    document->removeChangeListener(listenerId);
    if (partInvalidations == 0 || objectInvalidations == 0 ||
        instanceInvalidations == 0) {
        return fail("surface color change did not propagate to its instance");
    }

    document->detachOwnedChild(colors->getDBInstanceID());
    if (document->getOwner(colors->getDBInstanceID()).isValid() ||
        part->surfaceColors()) {
        return fail("concrete surface-color relationship failed to detach");
    }

    document->attachOwnedChild(
        part->getDBInstanceID(), colors->getDBInstanceID(),
        ModelPartDB::kSurfaceColorsRelation);
    if (document->getOwner(colors->getDBInstanceID()) != part->getDBInstanceID()) {
        return fail("concrete surface-color relationship failed to reconcile");
    }

    const auto serializedPart =
        std::static_pointer_cast<AutoRegisterDB>(part)->serialize();
    const auto serialized =
        std::static_pointer_cast<AutoRegisterDB>(colors)->serialize();
    if (serializedPart.contains("SurfaceColorDBId") ||
        serialized.contains("SourcePartId") ||
        serialized.value("SurfaceColorData").toString() != QStringLiteral("encoded-colors")) {
        return fail("surface color ownership/data did not serialize");
    }

    DBCopyResult copyResult;
    {
        TransactionGuard guard("copy colored model aggregate");
        DBCopyService copyService(*document);
        copyResult = copyService.copy({graph.object->getDBInstanceID()});
        if (!copyResult.success) {
            guard.rollback();
            std::cerr << copyResult.error << std::endl;
            return fail("failed to copy colored model aggregate");
        }
        guard.commit();
    }

    const auto copiedObjectIdIt = copyResult.idMap.find(
        graph.object->getDBInstanceID());
    const auto copiedMeshIdIt = copyResult.idMap.find(mesh->getDBInstanceID());
    const auto copiedPartIdIt = copyResult.idMap.find(part->getDBInstanceID());
    const auto copiedColorsIdIt = copyResult.idMap.find(colors->getDBInstanceID());
    if (copyResult.rootIds.size() != 1 ||
        copiedObjectIdIt == copyResult.idMap.end() ||
        copiedMeshIdIt == copyResult.idMap.end() ||
        copiedPartIdIt == copyResult.idMap.end() ||
        copiedColorsIdIt == copyResult.idMap.end() ||
        copyResult.rootIds.front() != copiedObjectIdIt->second) {
        return fail("copy result did not contain the complete aggregate mapping");
    }

    auto copiedMesh = std::dynamic_pointer_cast<ModelInstanceDB>(
        document->getDBInstance(copiedMeshIdIt->second));
    auto copiedPart = document->getDB<ModelPartDB>(copiedPartIdIt->second);
    auto copiedColors = std::dynamic_pointer_cast<ModelSurfaceColorDB>(
        document->getDBInstance(copiedColorsIdIt->second));
    if (!copiedMesh || !copiedPart || !copiedColors ||
        copiedMesh->getDBInstanceID() == mesh->getDBInstanceID() ||
        copiedColors->getDBInstanceID() == colors->getDBInstanceID()) {
        return fail("copy operation did not create distinct DB nodes");
    }
    if (copiedPart->surfaceColors() != copiedColors ||
        document->getOwner(copiedColors->getDBInstanceID()) != copiedPartIdIt->second) {
        return fail("copy operation did not remap the surface-color ownership relation");
    }
    if (copiedColors->getSurfaceColorData() != colors->getSurfaceColorData() ||
        copiedColors->getRevision() != colors->getRevision()) {
        return fail("copy operation did not preserve surface-color payload");
    }

    transactions.beginTransaction("delete colored model");
    if (!document->unregisterDBInstance(graph.object->getDBInstanceID())) {
        return fail("failed to delete colored model");
    }
    transactions.commitTransaction();
    if (document->getDBInstance(colors->getDBInstanceID())) {
        return fail("surface colors survived cascade model deletion");
    }
    if (!document->getDBInstance(copiedMesh->getDBInstanceID()) ||
        !document->getDBInstance(copiedColors->getDBInstanceID())) {
        return fail("deleting the source aggregate also deleted its copy");
    }

    if (!transactions.undo()) return fail("failed to undo colored model deletion");
    if (!document->getDBInstance(mesh->getDBInstanceID()) ||
        !document->getDBInstance(colors->getDBInstanceID()) ||
        document->getOwner(colors->getDBInstanceID()) != part->getDBInstanceID()) {
        return fail("undo did not restore the model surface color aggregate");
    }

    if (!transactions.undo()) return fail("failed to undo aggregate copy");
    if (document->getDBInstance(copiedMesh->getDBInstanceID()) ||
        document->getDBInstance(copiedColors->getDBInstanceID())) {
        return fail("undo did not remove the copied aggregate");
    }
    if (!document->getDBInstance(mesh->getDBInstanceID()) ||
        !document->getDBInstance(colors->getDBInstanceID())) {
        return fail("undoing the copy removed the source aggregate");
    }

    if (!transactions.redo()) return fail("failed to redo aggregate copy");
    if (!document->getDBInstance(copiedMesh->getDBInstanceID()) ||
        !document->getDBInstance(copiedColors->getDBInstanceID()) ||
        copiedPart->surfaceColors() != copiedColors ||
        document->getOwner(copiedColors->getDBInstanceID()) != copiedPartIdIt->second) {
        return fail("redo did not restore the copied aggregate and its relation");
    }

    return 0;
}
