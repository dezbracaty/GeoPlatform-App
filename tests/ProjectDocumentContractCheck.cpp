#include "AppDBRegistration.hpp"
#include "DBRelationsRegistration.hpp"
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <DocumentManager.hpp>
#include <ModelFilamentSlotResolver.hpp>
#include <ModelGraphUtil.hpp>
#include <PrintBedPlacementUtil.hpp>
#include <PrintBedTemplateDB.hpp>
#include <SlicingConfigDB.hpp>
#include <TransactionManager.hpp>
#include <TransDBSerialization.hpp>
#include <QCoreApplication>
#include <QScopeGuard>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vtkCubeSource.h>
#include <vtkTriangleFilter.h>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
ModelGraphUtil::CreateResult createTestBoxModel() {
    auto cube = vtkSmartPointer<vtkCubeSource>::New();
    cube->SetXLength(2); cube->SetYLength(2); cube->SetZLength(2);
    auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
    triangles->SetInputConnection(cube->GetOutputPort());
    triangles->Update();
    return ModelGraphUtil::createSinglePartObject(
        {"test model", "test://project", "test", true}, triangles->GetOutput(),
        ModelGraphUtil::InstanceCreateInfo{});
}
void checkSemantics() {
    TransactionGuard transaction("semantic fixture");
    auto graph = createTestBoxModel();
    require(bool(graph), "cannot create model fixture");
    graph.object->setDefaultFilamentSlot(2);
    graph.object->setProjectSemantics("{\"version\":1,\"scope\":\"object\",\"settings\":{}}");
    graph.parts.front()->setRole(static_cast<int>(ModelPartRole::ParameterModifier));
    graph.parts.front()->setProjectSemantics("part settings retained");
    graph.parts.front()->setSurfaceAnnotations("facet data");
    graph.parts.front()->setAnnotationsTopologyFingerprint("source topology");
    graph.parts.front()->setAnnotationsSourceTriangleCount(12);
    graph.parts.front()->setFilamentBindingMode(static_cast<int>(ModelFilamentBindingMode::InheritObject));
    auto config = GPlatform::SlicingConfigDB::createDefault();
    config->setDefaultFilamentSlot(1);
    require(GPlatform::Slicing::resolveModelFilamentSlot(*graph.parts.front(), config.get(), 2).effectiveSlot == 2,
            "part did not inherit object filament");
    graph.parts.front()->setFilamentBindingMode(static_cast<int>(ModelFilamentBindingMode::InheritProjectDefault));
    require(GPlatform::Slicing::resolveModelFilamentSlot(*graph.parts.front(), config.get(), 2).effectiveSlot == 1,
            "existing use-project-default behavior changed");
    auto objectCopy = std::dynamic_pointer_cast<ModelObjectDB>(graph.object->clone());
    auto partCopy = std::dynamic_pointer_cast<ModelPartDB>(graph.parts.front()->clone());
    require(objectCopy->getDefaultFilamentSlot() == 2 && objectCopy->getProjectSemantics() == graph.object->getProjectSemantics(),
            "object clone lost project semantics");
    require(partCopy->getSurfaceAnnotations() == "facet data" && partCopy->getRole() == 4,
            "part clone lost annotations or modifier role");
    auto serialized = graph.parts.front()->serialize();
    require(partCopy->deserialize(serialized) && partCopy->getRole() == static_cast<int>(ModelPartRole::ParameterModifier),
            "part property serialization lost project fields");
    const auto rawPart = trans::serializeState(*graph.parts.front());
    partCopy->setProjectSemantics("changed after capture");
    trans::deserializeState(*partCopy, rawPart);
    require(partCopy->getProjectSemantics() == "part settings retained" &&
            partCopy->getSurfaceAnnotations() == "facet data" &&
            partCopy->getRole() == 4,
            "existing raw DB serialization lost imported part state");
    transaction.commit();
}

void checkPublication() {
    auto* doc = DocumentManager::instance();
    auto& history = TransactionManager::instance();
    ModelGraphUtil::CreateResult original;
    { TransactionGuard transaction("old project"); original = createTestBoxModel(); transaction.commit(); }
    { TransactionGuard edit("old edit"); original.object->setDefaultFilamentSlot(2); edit.commit(); }
    require(history.undo(), "cannot prepare redo history");
    doc->flushPendingChanges();
    const auto oldUndo = history.getUndoStackSize(), oldRedo = history.getRedoStackSize();
    const auto originalId = original.object->getDBInstanceID();
    int events = 0, callbacks = 0;
    bool checkingCommit = false, coherent = true;
    ModelGraphUtil::CreateResult published;
    std::shared_ptr<GPlatform::SlicingConfigDB> publishedConfig;
    std::shared_ptr<PrintBedDB> publishedBed;
    const auto publishedStateCoherent = [&] {
        return !doc->getDBInstance(originalId) &&
            published.object && published.parts.front()->object() == published.object &&
            published.parts.front()->getProjectSemantics() == "published parameters" &&
            publishedConfig && publishedBed &&
            publishedConfig->getActivePrintBedDBId() == publishedBed->getDBInstanceID() &&
            publishedConfig->getDefaultFilamentSlot() == 2 &&
            published.instance->getSlicingConfigDBId() == publishedConfig->getDBInstanceID();
    };
    const auto listener = doc->addChangeListener([&](const auto&) {
        ++events;
        if (checkingCommit) coherent = coherent && publishedStateCoherent();
    });
    const auto callback = history.addTransactionCommittedListener([&](const auto&) {
        ++callbacks;
        if (checkingCommit) coherent = coherent && publishedStateCoherent();
    });
    {
        auto retainedObjects = doc->getDBInstancesByType(TypeID::UNKNOWN);
        const auto retention = doc->addChangeListener([&](const auto& change) {
            if (change.changeType == ChangeType::OBJECT_CREATED)
                retainedObjects.push_back(doc->getDBInstance(change.id));
        });
        const auto stopRetention = qScopeGuard([&] { doc->removeChangeListener(retention); });
        TransactionGuard transaction("rejected project");
        original.object->setDefaultFilamentSlot(3);
        require(ModelGraphUtil::removeObject(originalId), "cannot stage old project deletion");
        auto added = createTestBoxModel();
        require(bool(added), "cannot stage project graph");
        require(events > 0 && callbacks == 0, "normal lifecycle notifications were suppressed");
        const std::weak_ptr<ModelObjectDB> removedBeforeRollback = added.object;
        require(ModelGraphUtil::removeObject(added.object->getDBInstanceID()), "cannot clean up the rejected graph");
        added = {};
        transaction.rollback();
        require(!removedBeforeRollback.expired(), "factory cleanup destroyed an object with pending raw notifications");
        doc->rebuildRelationsFromReferenceFields();
        doc->flushPendingChanges();
    }
    require(callbacks == 0, "rollback emitted a committed transaction");
    require(doc->getDBInstance(originalId) == original.object && original.parts.front()->object() == original.object,
            "rollback did not restore old graph ownership");
    require(original.object->getDefaultFilamentSlot() == 0, "rollback did not restore old property");
    require(history.getUndoStackSize() == oldUndo && history.getRedoStackSize() == oldRedo,
            "rollback changed old undo/redo history");
    {
        TransactionGuard transaction("committed project");
        auto bed = trans::TransDB::create<PrintBedDB>();
        publishedBed = bed;
        publishedConfig = GPlatform::SlicingConfigDB::createDefault();
        publishedConfig->setActivePrintBedDBId(bed->getDBInstanceID());
        publishedConfig->setDefaultFilamentSlot(2);
        require(history.isInTransaction() && history.getUndoStackSize() == oldUndo,
                "bed initialization prematurely committed the project transaction");
        require(ModelGraphUtil::removeObject(originalId), "cannot replace old project");
        auto graph = createTestBoxModel();
        graph.parts.front()->setProjectSemantics("published parameters");
        published = graph;
        graph.instance->setSlicingConfigDBId(publishedConfig->getDBInstanceID());
        checkingCommit = true;
        transaction.commit();
        require(events > 0 && callbacks == 1, "normal notifications or transaction callback were lost");
        history.clear();
        doc->flushPendingChanges();
        require(!history.canUndo() && !history.canRedo(), "project boundary retained old history");
    }
    require(events > 0 && callbacks == 1, "App publication duplicated transaction callbacks or lost document events");
    require(coherent, "observer saw a mixed project graph/configuration projection");
    history.removeTransactionCommittedListener(callback);
    doc->removeChangeListener(listener);
}

void checkCoordinates() {
    auto* doc = DocumentManager::instance();
    TransactionGuard transaction("second plate");
    auto definition = trans::TransDB::create<PrintBedTemplateDB>();
    definition->setWidth(200); definition->setHeight(200); definition->setOrigin({10,20,0});
    auto bed = trans::TransDB::create<PrintBedDB>();
    bed->setTemplateDBId(definition->getDBInstanceID());
    bed->setDisplayName("Imported plate name");
    bed->setProjectSemantics("plate settings");
    const auto clone = std::dynamic_pointer_cast<PrintBedDB>(bed->clone());
    require(clone && clone->getDisplayName() == "Imported plate name" &&
            clone->getProjectSemantics() == "plate settings",
            "plate clone lost its name or slicing settings");
    const AutoRegisterDB& documentObject = *bed;
    const auto properties = documentObject.serialize();
    require(properties.value("displayName").toString() == "Imported plate name" &&
            !properties.contains("PlateName") && !properties.contains("PlateLocked"),
            "plate state retains duplicate names or an unused lock flag");
    Transform bedTransform; bedTransform.translate({240,0,0}); bed->setTransform(bedTransform);
    auto graph = createTestBoxModel();
    graph.instance->setParentPrintBedDBId(bed->getDBInstanceID());
    Transform pose; pose.translate({270,50,1}); graph.instance->setTransform(pose);
    const auto local = PrintBedPlacementUtil::instancePlateMatrix(*graph.instance);
    require(std::abs(local(0,3)-30) < 1e-5 && std::abs(local(1,3)-50) < 1e-5,
            "slice matrix retained plate display offset");
    require(graph.instance->worldBounds().min.x > 260, "world bounds removed plate display offset");
    auto config = GPlatform::SlicingConfigDB::createDefault();
    config->setActivePrintBedDBId(bed->getDBInstanceID());
    doc->flushPendingChanges();
    require(!config->reportRelations().empty(), "active plate reference omitted from copy graph");
    transaction.commit();
}


}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    GPlatform::AppDB::registerModule();
    GPlatform::DBRelations::registerModule();
    try {
        checkSemantics(); checkPublication(); checkCoordinates();
        std::cout << "Project document semantics, rollback/publication and plate coordinates verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
