#include <ChainUpdateSystem.hpp>
#include <DBRelationRegistry.hpp>
#include "DBRelationsRegistration.hpp"
#include <DocumentManager.hpp>
#include "TestModelGraphFixture.hpp"
#include <PrintBedDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include <TransactionManager.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
int fail(const char* message) {
    std::cerr << message << std::endl;
    return 1;
}
}

int main() {
    auto* document = DocumentManager::instance();
    auto& chain = ChainUpdateSystem::instance();
    auto& registry = DBRelationRegistry::instance();
    registry.clear();
    chain.clear();
    TransactionManager::instance().clear();
    GPlatform::DBRelations::registerModule();

    auto definition = trans::TransDB::create<PrintBedTemplateDB>();
    auto bed = trans::TransDB::create<PrintBedDB>();
    const auto modelGraph = createTestModelGraph();
    auto mesh = modelGraph.instance;
    if (!document || !definition || !bed || !mesh) {
        return fail("failed to create print bed relation fixture");
    }

    definition->setModelId("guider-3-ultra");
    definition->setVariantId("0.4");
    definition->setWidth(300.0f);
    definition->setHeight(330.0f);
    definition->setPrintHeight(600.0f);
    definition->setOrigin(Vector3(-150.0f, -165.0f, 0.0f));

    int templateProjectionChanges = 0;
    const auto listener = document->addChangeListener(
        [&](const DocumentManager::ChangeNotification& change) {
            if (change.id == bed->getDBInstanceID() &&
                change.propertyName == "Width") {
                ++templateProjectionChanges;
            }
        });

    bed->setTemplateDBId(definition->getDBInstanceID());
    mesh->setParentPrintBedDBId(bed->getDBInstanceID());

    if (bed->getTemplateDB() != definition ||
        std::fabs(bed->getWidth() - 300.0f) > 0.001f ||
        std::fabs(bed->getHeight() - 330.0f) > 0.001f) {
        return fail("real print bed did not resolve its template definition");
    }
    if (document->getOwner(bed->getDBInstanceID()).isValid()) {
        return fail("template incorrectly owns the real print bed instance");
    }
    if (document->getOwner(mesh->getDBInstanceID()) !=
        modelGraph.object->getDBInstanceID()) {
        return fail("model instance lost its object owner");
    }
    if (mesh->getParentPrintBedDBId() != bed->getDBInstanceID()) {
        return fail("model did not retain its parent print bed DBID");
    }
    const auto bedEdges = document->getDependencyRelationEdges();
    if (std::any_of(bedEdges.begin(), bedEdges.end(), [&](const auto& edge) {
            return edge.sourceId == bed->getDBInstanceID() &&
                edge.targetId == mesh->getDBInstanceID();
        })) {
        return fail("print bed created a no-op dependency to its model");
    }

    mesh->setPosition(Vector3(200.0f, 0.0f, 0.0f));
    if (!bed->isWallCollisionSet(1)) {
        return fail("model transform did not update its print bed collision state");
    }
    mesh->setPosition(Vector3(0.0f, 0.0f, 0.0f));
    if (bed->getCollisionEffectsEnabled()) {
        return fail("returning a model inside the bed did not clear collision state");
    }

    // Non-printing volumes may extend outside the model or below the bed.
    // They remain part of selection bounds, but must not turn the bed red.
    auto modifier = createTestBoxModel({0, 0, -5}, {400, 400, 10});
    modifier.part->setRole(static_cast<int>(ModelPartRole::ParameterModifier));
    if (!document->detachOwnedChild(modifier.part->getDBInstanceID()) ||
        !document->attachOwnedChild(modelGraph.object->getDBInstanceID(),
            modifier.part->getDBInstanceID(), ModelPartDB::kObjectRelation)) {
        return fail("cannot attach an oversized modifier to the printable model");
    }
    for (const auto role : {ModelPartRole::ParameterModifier,
                           ModelPartRole::NegativeVolume,
                           ModelPartRole::SupportEnforcer,
                           ModelPartRole::SupportBlocker}) {
        modifier.part->setRole(static_cast<int>(role));
        document->flushPendingChanges();
        if (bed->getCollisionEffectsEnabled()) {
            return fail("non-printing volume incorrectly collides with the build volume");
        }
    }
    if (mesh->worldBounds().min.z >= 0) {
        return fail("collision filtering changed the model's selection bounds");
    }
    modifier.part->setRole(static_cast<int>(ModelPartRole::Model));
    document->flushPendingChanges();
    if (!(bed->getCollisionMask() & PrintBedDB::CollisionBottom) ||
        !(bed->getCollisionMask() & PrintBedDB::CollisionRight)) {
        return fail("out-of-volume model part did not set the collision mask");
    }
    modifier.part->setRole(static_cast<int>(ModelPartRole::ParameterModifier));
    document->flushPendingChanges();
    if (bed->getCollisionEffectsEnabled()) {
        return fail("changing a part back to modifier did not clear collision state");
    }
    mesh->setPosition({0, 0, -1});
    document->flushPendingChanges();
    if (!(bed->getCollisionMask() & PrintBedDB::CollisionBottom)) {
        return fail("actual model below the bed was not detected");
    }
    mesh->setPosition({0, 0, 0});
    document->flushPendingChanges();

    definition->setWidth(305.0f);
    if (std::fabs(bed->getWidth() - 305.0f) > 0.001f ||
        templateProjectionChanges == 0) {
        return fail("template update did not invalidate the real bed projection");
    }

    const auto serializedBed = std::static_pointer_cast<AutoRegisterDB>(bed)->serialize();
    const auto serializedMesh = std::static_pointer_cast<AutoRegisterDB>(mesh)->serialize();
    if (!serializedBed.contains("TemplateDBId") ||
        serializedBed.value("TemplateDBId").toULongLong() !=
            definition->getDBInstanceID().getValue()) {
        return fail("template relation was not serialized on the real bed");
    }
    if (!serializedMesh.contains("ParentPrintBedDBId") ||
        serializedMesh.value("ParentPrintBedDBId").toULongLong() !=
            bed->getDBInstanceID().getValue()) {
        return fail("real bed relation was not serialized on the model");
    }

    const auto bedClone = std::dynamic_pointer_cast<PrintBedDB>(bed->clone());
    const auto meshClone = std::dynamic_pointer_cast<ModelInstanceDB>(mesh->clone());
    if (!bedClone || bedClone->getTemplateDBId().isValid() ||
        !meshClone || meshClone->getParentPrintBedDBId().isValid()) {
        return fail("node clone retained relation ids before remapping");
    }

    document->removeChangeListener(listener);
    return 0;
}
