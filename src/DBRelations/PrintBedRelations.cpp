#include "PrintBedRelations.hpp"
#include "DBRelationsRegistration.hpp"

#include <DBRelationRegistry.hpp>
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <PrintBedCollisionDetector.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include "DBRelationMacros.hpp"
#include <cstdint>

namespace GPlatform::DBRelations::PrintBedRelations {
namespace {

constexpr const char* TemplateProjectionRule = "PrintBedTemplateProjection";
void updateCollisionMask(const std::shared_ptr<PrintBedDB>& bed) {
    auto* document = DocumentManager::instance();
    if (!document || !bed) return;

    const auto volume = PrintBedCollisionDetector::buildVolume(*bed);
    std::uint32_t mask = PrintBedDB::CollisionNone;
    for (const auto& object : document->getDBInstancesByType(
             TypeID::MODEL_INSTANCE_DB)) {
        const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(object);
        if (!instance || instance->getParentPrintBedDBId() !=
                             bed->getDBInstanceID()) continue;
        mask |= PrintBedCollisionDetector::detect(
            *instance, instance->getTransform(), volume);
    }
    bed->setCollisionMask(mask);
}

void updateAllCollisionMasks(DocumentManager& document) {
    for (const auto& object : document.getDBInstancesByType(
             TypeID::PRINT_BED_DB)) {
        updateCollisionMask(std::dynamic_pointer_cast<PrintBedDB>(object));
    }
}

void observeOwnedModelPlacement(DocumentManager* document) {
    static DocumentManager::ListenerID listenerId = 0;
    if (!document || listenerId != 0) return;

    listenerId = document->addChangeListener(
        [](const DocumentManager::ChangeNotification& change) {
            if (change.dbType == TypeID::PRINT_BED_DB) {
                const bool relevantBedChange =
                    change.changeType == ChangeType::OBJECT_CREATED ||
                    (change.changeType == ChangeType::PROPERTY_CHANGED &&
                     (change.propertyName == "Width" ||
                      change.propertyName == "Height" ||
                      change.propertyName == "PrintHeight" ||
                      change.propertyName == "Origin" ||
                      change.propertyName == "TemplateDBId"));
                if (relevantBedChange) {
                    auto* current = DocumentManager::instance();
                    updateCollisionMask(current
                        ? current->getDB<PrintBedDB>(change.id)
                        : nullptr);
                }
                return;
            }
            auto* currentDocument = DocumentManager::instance();
            if (!currentDocument) return;

            if (!isPrintableModelType(change.dbType)) return;
            const bool relevant =
                change.changeType == ChangeType::OBJECT_CREATED ||
                change.changeType == ChangeType::OBJECT_DELETED ||
                (change.changeType == ChangeType::PROPERTY_CHANGED &&
                 (change.propertyName == "Transform" ||
                  change.propertyName == "ParentPrintBedDBId" ||
                  change.propertyName == ModelInstanceDB::kObjectRelation));
            if (!relevant) return;

            if (change.changeType == ChangeType::OBJECT_DELETED ||
                change.propertyName == "ParentPrintBedDBId") {
                updateAllCollisionMasks(*currentDocument);
                return;
            }

            const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(
                currentDocument->getDBInstance(change.id));
            const DBInstanceID bedId = instance
                ? instance->getParentPrintBedDBId()
                : DBInstanceID{};
            if (!bedId.isValid()) return;
            updateCollisionMask(std::dynamic_pointer_cast<PrintBedDB>(
                currentDocument->getDBInstance(bedId)));
        });
}

void invalidateBedProjection(DBRelationUpdateContext& context) {
    if (const auto bed = context.targetAs<PrintBedDB>()) {
        std::string propertyName = context.propertyName;
        if (propertyName == "Vendor") propertyName = "BedVendor";
        if (propertyName == "PrinterModel") propertyName = "BedPrinterModel";
        if (propertyName == "ModelId" || propertyName == "VariantId") {
            propertyName = "Template";
        }
        bed->notifyTemplateChanged(propertyName);
    }
}

void initializeBedProjection(DBRelationAttachContext& context) {
    if (const auto bed = context.targetAs<PrintBedDB>()) {
        bed->notifyTemplateChanged("TemplateDBId");
    }
}

} // namespace

void registerRelations() {
    auto* document = DocumentManager::instance();

    DBRelationRegistry::instance().registerDependencyReference(
        TypeID::PRINT_BED_DB,
        "TemplateDBId",
        TypeID::PRINT_BED_TEMPLATE_DB,
        TemplateRelation);

    auto& registry = DBRelationRegistry::instance();
    registry.registerDependency(
        TypeID::PRINT_BED_TEMPLATE_DB,
        TypeID::PRINT_BED_DB,
        TemplateRelation,
        TemplateProjectionRule,
        WATCH_PROPS(),
        invalidateBedProjection,
        initializeBedProjection);

    observeOwnedModelPlacement(document);
}

} // namespace GPlatform::DBRelations::PrintBedRelations

REGISTER_DB_RELATIONS(GPlatform::DBRelations::PrintBedRelations::registerRelations())
