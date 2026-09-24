#include "ModelGraphUtil.hpp"

#include <DocumentManager.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <MaterialDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelSurfaceColorDB.hpp>
#include <TransactionManager.hpp>

#include <algorithm>
#include <memory>
#include <set>

#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace {
void setError(std::string* output, const std::string& message) {
    if (output) *output = message;
}

std::unique_ptr<TransactionGuard> transactionIfNeeded(
    const std::string& description) {
    return TransactionManager::instance().isInTransaction()
        ? nullptr
        : std::make_unique<TransactionGuard>(description);
}

vtkSmartPointer<vtkPolyData> polyDataFromTriangles(
    const std::vector<GeomTriangle>& triangles) {
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    points->SetNumberOfPoints(static_cast<vtkIdType>(triangles.size() * 3));
    vtkIdType pointId = 0;
    for (const auto& triangle : triangles) {
        const Vector3 vertices[3] = {
            triangle.vertex1, triangle.vertex2, triangle.vertex3};
        vtkIdType ids[3];
        for (int corner = 0; corner < 3; ++corner) {
            const auto& vertex = vertices[corner];
            points->SetPoint(pointId, vertex.x, vertex.y, vertex.z);
            ids[corner] = pointId++;
        }
        cells->InsertNextCell(3, ids);
    }
    auto result = vtkSmartPointer<vtkPolyData>::New();
    result->SetPoints(points);
    result->SetPolys(cells);
    return result;
}

template <typename T>
std::shared_ptr<T> cloneAs(const std::shared_ptr<T>& source) {
    return source
        ? std::dynamic_pointer_cast<T>(source->clone())
        : std::shared_ptr<T>{};
}

bool bindMaterial(
    const std::shared_ptr<ModelPartDB>& part,
    const std::shared_ptr<MaterialDB>& material) {
    if (!part || !material) return false;
    auto* document = DocumentManager::instance();
    if (!document) return false;
    const DBInstanceID partId = part->getDBInstanceID();
    const DBInstanceID materialId = material->getDBInstanceID();
    const DBInstanceID materialOwner = document->getOwner(materialId);
    if (!partId.isValid() || !materialId.isValid() ||
        (materialOwner.isValid() && materialOwner != partId)) {
        return false;
    }

    const auto previous = part->getMaterial();
    if (previous && previous->getDBInstanceID() == materialId) {
        return true;
    }
    if (previous && !document->detachOwnedChild(
            previous->getDBInstanceID())) {
        return false;
    }
    return document->attachOwnedChild(
        partId, materialId, ModelPartDB::kMaterialRelation);
}

}

DBInstanceID ModelGraphUtil::EnsureUniqueResult::mapPart(
    DBInstanceID sourcePartId) const {
    const auto found = std::find_if(
        parts.begin(), parts.end(),
        [sourcePartId](const PartIdMapping& mapping) {
            return mapping.sourcePartId == sourcePartId;
        });
    return found == parts.end() ? DBInstanceID{} : found->targetPartId;
}

ModelGraphUtil::CreateResult ModelGraphUtil::createObject(
    ObjectCreateInfo objectInfo, std::vector<PartCreateInfo> partInfos,
    std::optional<InstanceCreateInfo> instanceInfo) {
    CreateResult result;
    if (partInfos.empty()) {
        result.error = "A model object requires at least one part";
        return result;
    }
    std::set<int> order;
    for (const auto& part : partInfos) {
        if (!part.geometry || part.geometry->GetNumberOfCells() <= 0) {
            result.error = "A model part has no valid mesh";
            return result;
        }
        if (part.defaultFilamentSlot < 1 || !order.insert(part.orderIndex).second) {
            result.error = "Part order must be unique and filament slot positive";
            return result;
        }
    }

    auto transaction = transactionIfNeeded("Create Model Object Graph");
    auto* document = DocumentManager::instance();
    if (!document) {
        result.error = "DocumentManager is not available";
        if (transaction) transaction->rollback();
        return result;
    }

    result.object = trans::TransDB::create<ModelObjectDB>();
    if (!result.object) {
        result.error = "Unable to create ModelObjectDB";
        if (transaction) transaction->rollback();
        return result;
    }
    result.object->setDisplayName(objectInfo.name);
    result.object->setSourceFile(objectInfo.sourceFile);
    result.object->setFileFormat(objectInfo.fileFormat);
    result.object->setPrintableDefault(objectInfo.printableDefault);

    for (auto& info : partInfos) {
        std::string error;
        auto part = addPart(result.object->getDBInstanceID(), std::move(info),
                            &error);
        if (!part) {
            result.error = error;
            document->unregisterDBInstance(result.object->getDBInstanceID());
            result = CreateResult{{}, {}, {}, result.error};
            if (transaction) transaction->rollback();
            return result;
        }
        result.parts.push_back(std::move(part));
    }
    if (instanceInfo) {
        result.instance = createInstance(result.object->getDBInstanceID(),
                                         *instanceInfo, &result.error);
        if (!result.instance) {
            document->unregisterDBInstance(result.object->getDBInstanceID());
            result.object.reset();
            result.parts.clear();
            if (transaction) transaction->rollback();
            return result;
        }
    }
    if (!validateObject(result.object->getDBInstanceID(), &result.error)) {
        document->unregisterDBInstance(result.object->getDBInstanceID());
        result.object.reset();
        result.parts.clear();
        result.instance.reset();
        if (transaction) transaction->rollback();
        return result;
    }
    if (transaction) transaction->commit();
    return result;
}

ModelGraphUtil::CreateResult ModelGraphUtil::createSinglePartObject(
    ObjectCreateInfo object, vtkSmartPointer<vtkPolyData> geometry,
    std::optional<InstanceCreateInfo> instanceInfo, ModelPartRole role,
    int defaultFilamentSlot) {
    Transform identity;
    identity.identity();
    PartCreateInfo part;
    part.name = object.name;
    part.geometry = std::move(geometry);
    part.localTransform = identity;
    part.role = role;
    part.defaultFilamentSlot = defaultFilamentSlot;
    std::vector<PartCreateInfo> parts;
    parts.push_back(std::move(part));
    return createObject(std::move(object), std::move(parts), instanceInfo);
}

ModelGraphUtil::CreateResult ModelGraphUtil::createSinglePartObject(
    ObjectCreateInfo object, const std::vector<GeomTriangle>& triangles,
    std::optional<InstanceCreateInfo> instanceInfo, ModelPartRole role,
    int defaultFilamentSlot) {
    return createSinglePartObject(
        std::move(object), polyDataFromTriangles(triangles), instanceInfo,
        role, defaultFilamentSlot);
}

std::shared_ptr<ModelPartDB> ModelGraphUtil::addPart(
    DBInstanceID objectId, PartCreateInfo info, std::string* error) {
    auto* document = DocumentManager::instance();
    auto object = document ? document->getDB<ModelObjectDB>(objectId) : nullptr;
    if (!object || !info.geometry ||
        info.geometry->GetNumberOfCells() <= 0 ||
        info.defaultFilamentSlot < 1) {
        setError(error, "Invalid object or part payload");
        return {};
    }
    for (const auto& existing : object->parts()) {
        if (existing && existing->getOrderIndex() == info.orderIndex) {
            setError(error, "Part OrderIndex already exists in object");
            return {};
        }
    }
    auto transaction = transactionIfNeeded("Add Model Part");
    auto part = trans::TransDB::create<ModelPartDB>();
    if (!part) {
        setError(error, "Unable to create ModelPartDB");
        if (transaction) transaction->rollback();
        return {};
    }
    part->setDisplayName(info.name);
    part->setOrderIndex(info.orderIndex);
    part->setRole(static_cast<int>(info.role));
    part->setDefaultFilamentSlot(info.defaultFilamentSlot);
    part->setFilamentBindingMode(
        static_cast<int>(info.filamentBindingMode));
    part->setLocalTransform(info.localTransform);
    document->attachOwnedChild(
        objectId, part->getDBInstanceID(), ModelPartDB::kObjectRelation);
    if (document->getOwner(part->getDBInstanceID()) != objectId) {
        document->unregisterDBInstance(part->getDBInstanceID());
        setError(error, "Unable to attach part ownership");
        if (transaction) transaction->rollback();
        return {};
    }

    auto geometry = trans::TransDB::create<ModelGeometryDB>();
    if (!geometry) {
        document->unregisterDBInstance(part->getDBInstanceID());
        setError(error, "Unable to create ModelGeometryDB");
        if (transaction) transaction->rollback();
        return {};
    }
    geometry->setDisplayName(info.name + " geometry");
    std::string geometryError;
    if (!geometry->replacePolyData(info.geometry, {}, &geometryError)) {
        document->unregisterDBInstance(geometry->getDBInstanceID());
        document->unregisterDBInstance(part->getDBInstanceID());
        setError(error, geometryError.empty()
            ? "Unable to publish part geometry" : geometryError);
        if (transaction) transaction->rollback();
        return {};
    }
    document->attachOwnedChild(
        part->getDBInstanceID(), geometry->getDBInstanceID(),
        ModelPartDB::kGeometryRelation);
    if (document->getOwner(geometry->getDBInstanceID()) !=
        part->getDBInstanceID()) {
        document->unregisterDBInstance(geometry->getDBInstanceID());
        document->unregisterDBInstance(part->getDBInstanceID());
        setError(error, "Unable to attach part geometry ownership");
        if (transaction) transaction->rollback();
        return {};
    }

    auto material = trans::TransDB::create<MaterialDB>();
    if (!material) {
        document->unregisterDBInstance(part->getDBInstanceID());
        setError(error, "Unable to create MaterialDB");
        if (transaction) transaction->rollback();
        return {};
    }
    material->setDisplayName(info.name + " material");
    if (!bindMaterial(part, material)) {
        document->unregisterDBInstance(material->getDBInstanceID());
        document->unregisterDBInstance(part->getDBInstanceID());
        setError(error, "Unable to attach Part material relation");
        if (transaction) transaction->rollback();
        return {};
    }
    object->setUseSourceAppearance(false);
    if (transaction) transaction->commit();
    return part;
}

std::shared_ptr<ModelInstanceDB> ModelGraphUtil::createInstance(
    DBInstanceID objectId, InstanceCreateInfo info, std::string* error) {
    auto* document = DocumentManager::instance();
    auto object = document ? document->getDB<ModelObjectDB>(objectId) : nullptr;
    if (!object) {
        setError(error, "ModelObjectDB does not exist");
        return {};
    }
    auto transaction = transactionIfNeeded("Create Model Instance");
    auto instance = trans::TransDB::create<ModelInstanceDB>();
    if (!instance) {
        setError(error, "Unable to create ModelInstanceDB");
        if (transaction) transaction->rollback();
        return {};
    }
    instance->setDisplayName(object->getDisplayName());
    instance->setTransform(info.transform);
    instance->setPrintable(info.printable);
    instance->setArrangeOrder(info.arrangeOrder);
    document->attachOwnedChild(
        objectId, instance->getDBInstanceID(),
        ModelInstanceDB::kObjectRelation);
    instance->setParentPrintBedDBId(info.printBedId);
    instance->setSlicingConfigDBId(info.slicingConfigId);
    if (document->getOwner(instance->getDBInstanceID()) != objectId) {
        document->unregisterDBInstance(instance->getDBInstanceID());
        setError(error, "Unable to attach instance ownership");
        if (transaction) transaction->rollback();
        return {};
    }
    if (transaction) transaction->commit();
    return instance;
}

ModelGraphUtil::EnsureUniqueResult
ModelGraphUtil::ensureUniqueObjectForInstance(DBInstanceID instanceId) {
    EnsureUniqueResult result;
    auto* document = DocumentManager::instance();
    const auto instance = document
        ? document->getDB<ModelInstanceDB>(instanceId)
        : nullptr;
    const auto sourceObject = instance ? instance->object() : nullptr;
    if (!document || !instance || !sourceObject) {
        result.error = "Model instance or object does not exist";
        return result;
    }

    const auto sourceParts = sourceObject->parts();
    if (sourceParts.empty()) {
        result.error = "Model object has no parts";
        return result;
    }
    if (sourceObject->instances().size() <= 1) {
        result.success = true;
        result.objectId = sourceObject->getDBInstanceID();
        result.parts.reserve(sourceParts.size());
        for (const auto& part : sourceParts) {
            if (part) {
                result.parts.push_back(
                    {part->getDBInstanceID(), part->getDBInstanceID()});
            }
        }
        return result;
    }

    auto transaction = transactionIfNeeded("Make Model Instance Unique");
    try {
        auto targetObject = cloneAs(sourceObject);
        if (!targetObject) {
            result.error = "Unable to clone ModelObjectDB";
            if (transaction) transaction->rollback();
            return result;
        }

        result.parts.reserve(sourceParts.size());
        for (const auto& sourcePart : sourceParts) {
            const auto sourceGeometry = sourcePart
                ? sourcePart->geometry()
                : nullptr;
            const auto sourceMaterial = sourcePart
                ? sourcePart->getMaterial()
                : nullptr;
            const auto sourceSurfaceColors = sourcePart
                ? sourcePart->surfaceColors()
                : nullptr;
            auto targetPart = cloneAs(sourcePart);
            auto targetGeometry = cloneAs(sourceGeometry);
            auto targetMaterial = cloneAs(sourceMaterial);
            auto targetSurfaceColors = cloneAs(sourceSurfaceColors);
            if (!sourcePart || !targetPart || !targetGeometry ||
                !targetMaterial) {
                result.error = "Unable to clone complete model part";
                if (transaction) transaction->rollback();
                return result;
            }

            const DBInstanceID targetObjectId =
                targetObject->getDBInstanceID();
            const DBInstanceID targetPartId = targetPart->getDBInstanceID();
            document->attachOwnedChild(
                targetObjectId, targetPartId,
                ModelPartDB::kObjectRelation);
            document->attachOwnedChild(
                targetPartId, targetGeometry->getDBInstanceID(),
                ModelPartDB::kGeometryRelation);
            const bool materialBound = bindMaterial(
                targetPart, targetMaterial);
            if (document->getOwner(targetPartId) != targetObjectId ||
                document->getOwner(targetGeometry->getDBInstanceID()) !=
                    targetPartId ||
                !materialBound) {
                result.error = "Unable to rebuild cloned part ownership";
                if (transaction) transaction->rollback();
                return result;
            }
            if (sourceSurfaceColors) {
                if (!targetSurfaceColors) {
                    result.error = "Unable to clone part surface colors";
                    if (transaction) transaction->rollback();
                    return result;
                }
                document->attachOwnedChild(
                    targetPartId,
                    targetSurfaceColors->getDBInstanceID(),
                    ModelPartDB::kSurfaceColorsRelation);
                if (document->getOwner(
                        targetSurfaceColors->getDBInstanceID()) !=
                    targetPartId) {
                    result.error =
                        "Unable to rebuild cloned surface-color ownership";
                    if (transaction) transaction->rollback();
                    return result;
                }
            }
            result.parts.push_back(
                {sourcePart->getDBInstanceID(), targetPartId});
        }

        document->detachOwnedChild(instanceId);
        document->attachOwnedChild(
            targetObject->getDBInstanceID(), instanceId,
            ModelInstanceDB::kObjectRelation);
        if (document->getOwner(instanceId) !=
            targetObject->getDBInstanceID()) {
            result.error = "Unable to attach instance to cloned object";
            if (transaction) transaction->rollback();
            return result;
        }
        if (!validateObject(sourceObject->getDBInstanceID(), &result.error) ||
            !validateObject(targetObject->getDBInstanceID(), &result.error)) {
            if (transaction) transaction->rollback();
            return result;
        }

        result.success = true;
        result.cloned = true;
        result.objectId = targetObject->getDBInstanceID();
        if (transaction) transaction->commit();
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
    } catch (...) {
        result.error = "Unexpected failure while making model instance unique";
    }
    if (transaction) transaction->rollback();
    return result;
}

bool ModelGraphUtil::replaceInstancePartGeometry(
    DBInstanceID instanceId, DBInstanceID sourcePartId,
    vtkPolyData* geometry, std::string* error) {
    std::vector<PartGeometryReplacement> replacements;
    replacements.push_back({sourcePartId, geometry});
    return replaceInstancePartGeometries(
        instanceId, std::move(replacements), error);
}

bool ModelGraphUtil::replaceInstancePartGeometries(
    DBInstanceID instanceId,
    std::vector<PartGeometryReplacement> replacements,
    std::string* error) {
    if (replacements.empty() ||
        std::any_of(replacements.begin(), replacements.end(),
                    [](const PartGeometryReplacement& replacement) {
                        return !replacement.sourcePartId.isValid() ||
                            !replacement.geometry;
                    })) {
        setError(error, "Invalid instance geometry replacement payload");
        return false;
    }
    auto transaction = transactionIfNeeded("Replace Instance Part Geometry");
    const auto unique = ensureUniqueObjectForInstance(instanceId);
    if (!unique) {
        setError(error, unique.error);
        if (transaction) transaction->rollback();
        return false;
    }
    for (auto& replacement : replacements) {
        const DBInstanceID targetPartId =
            unique.mapPart(replacement.sourcePartId);
        if (!targetPartId.isValid() ||
            !replacePartGeometry(
                targetPartId, std::move(replacement.geometry), error)) {
            if (transaction) transaction->rollback();
            return false;
        }
    }
    if (transaction) transaction->commit();
    return true;
}

bool ModelGraphUtil::replaceInstancePartMaterial(
    DBInstanceID instanceId, DBInstanceID sourcePartId,
    DBInstanceID materialId, std::string* error) {
    auto transaction = transactionIfNeeded("Replace Instance Part Material");
    const auto unique = ensureUniqueObjectForInstance(instanceId);
    const DBInstanceID targetPartId = unique.mapPart(sourcePartId);
    if (!unique || !targetPartId.isValid() ||
        !replacePartMaterial(targetPartId, materialId, error)) {
        if (!unique) setError(error, unique.error);
        if (transaction) transaction->rollback();
        return false;
    }
    if (transaction) transaction->commit();
    return true;
}

bool ModelGraphUtil::replacePartGeometry(
    DBInstanceID partId, vtkSmartPointer<vtkPolyData> geometry,
    std::string* error) {
    auto* document = DocumentManager::instance();
    auto part = document ? document->getDB<ModelPartDB>(partId) : nullptr;
    const auto geometryDB = part ? part->geometry() : nullptr;
    if (!part || !geometryDB || !geometry ||
        geometry->GetNumberOfCells() <= 0) {
        setError(error, "Invalid part or geometry");
        return false;
    }
    auto transaction = transactionIfNeeded("Replace Model Part Mesh");
    if (!geometryDB->replacePolyData(geometry, {}, error)) {
        if (transaction) transaction->rollback();
        return false;
    }
    if (const auto object = document->getDB<ModelObjectDB>(
            document->getOwner(partId))) {
        object->setUseSourceAppearance(false);
    }
    if (transaction) transaction->commit();
    return true;
}

bool ModelGraphUtil::replacePartMaterial(
    DBInstanceID partId, DBInstanceID materialId,
    std::string* error) {
    auto* document = DocumentManager::instance();
    const auto part = document ? document->getDB<ModelPartDB>(partId) : nullptr;
    const auto material = document
        ? document->getDB<MaterialDB>(materialId)
        : nullptr;
    const DBInstanceID materialOwner = material
        ? document->getOwner(materialId) : DBInstanceID{};
    if (!part || !material ||
        (materialOwner.isValid() && materialOwner != partId)) {
        setError(error, "Invalid part or material");
        return false;
    }
    auto transaction = transactionIfNeeded("Replace Model Part Material");
    const auto previous = part->getMaterial();
    if (previous && previous->getDBInstanceID() == materialId) {
        if (transaction) transaction->commit();
        return true;
    }
    if (!bindMaterial(part, material)) {
        if (transaction) transaction->rollback();
        setError(error, "Unable to attach replacement Part material relation");
        return false;
    }
    if (previous &&
        !document->unregisterDBInstance(previous->getDBInstanceID())) {
        if (transaction) transaction->rollback();
        setError(error, "Unable to remove replaced MaterialDB");
        return false;
    }
    if (const auto object = document->getDB<ModelObjectDB>(
            document->getOwner(partId))) {
        object->setUseSourceAppearance(false);
    }
    if (transaction) transaction->commit();
    return true;
}

bool ModelGraphUtil::setPartLocalTransform(
    DBInstanceID partId, const Transform& transform, std::string* error) {
    auto* document = DocumentManager::instance();
    auto part = document ? document->getDB<ModelPartDB>(partId) : nullptr;
    if (!part || !transform.getMatrix().allFinite()) {
        setError(error, "Invalid part or local transform");
        return false;
    }
    auto transaction = transactionIfNeeded("Transform Model Part");
    part->setLocalTransform(transform);
    if (const auto object = document->getDB<ModelObjectDB>(
            document->getOwner(partId))) {
        object->setUseSourceAppearance(false);
    }
    if (transaction) transaction->commit();
    return true;
}

bool ModelGraphUtil::removePart(DBInstanceID id, std::string* error) {
    auto* document = DocumentManager::instance();
    auto part = document ? document->getDB<ModelPartDB>(id) : nullptr;
    if (!part) { setError(error, "ModelPartDB does not exist"); return false; }
    auto owner = document->getDB<ModelObjectDB>(document->getOwner(id));
    if (owner && owner->parts().size() <= 1) {
        setError(error, "Cannot remove the last part from an object");
        return false;
    }
    auto transaction = transactionIfNeeded("Remove Model Part");
    const bool removed = document->unregisterDBInstance(id);
    if (removed && owner) owner->setUseSourceAppearance(false);
    if (transaction) removed ? transaction->commit() : transaction->rollback();
    if (!removed) setError(error, "Unable to remove ModelPartDB");
    return removed;
}

bool ModelGraphUtil::removeInstance(DBInstanceID id, std::string* error) {
    auto* document = DocumentManager::instance();
    const auto instance = document
        ? document->getDB<ModelInstanceDB>(id)
        : nullptr;
    if (!instance) {
        setError(error, "ModelInstanceDB does not exist"); return false;
    }
    const DBInstanceID objectId = document->getOwner(id);
    auto transaction = transactionIfNeeded("Remove Model Instance");
    if (!document->unregisterDBInstance(id)) {
        if (transaction) transaction->rollback();
        setError(error, "Unable to remove ModelInstanceDB");
        return false;
    }
    const auto object = document->getDB<ModelObjectDB>(objectId);
    if (object && object->instances().empty() &&
        !document->unregisterDBInstance(objectId)) {
        if (transaction) transaction->rollback();
        setError(error, "Unable to remove unreferenced ModelObjectDB");
        return false;
    }
    if (transaction) transaction->commit();
    return true;
}

bool ModelGraphUtil::removeObject(DBInstanceID id, std::string* error) {
    auto* document = DocumentManager::instance();
    const auto object = document ? document->getDB<ModelObjectDB>(id) : nullptr;
    if (!document || !object) {
        setError(error, "ModelObjectDB does not exist"); return false;
    }
    auto transaction = transactionIfNeeded("Remove Model Object");
    const bool removed = document->unregisterDBInstance(id);
    if (transaction) removed ? transaction->commit() : transaction->rollback();
    return removed;
}

bool ModelGraphUtil::validateObject(DBInstanceID id, std::string* error) {
    auto* document = DocumentManager::instance();
    auto object = document ? document->getDB<ModelObjectDB>(id) : nullptr;
    if (!object) { setError(error, "ModelObjectDB does not exist"); return false; }
    const auto parts = object->parts();
    if (parts.empty()) { setError(error, "ModelObjectDB has no parts"); return false; }
    std::set<int> order;
    for (const auto& part : parts) {
        if (!part || !part->isValid() ||
            document->getOwner(part->getDBInstanceID()) != id ||
            !order.insert(part->getOrderIndex()).second) {
            setError(error, "ModelObjectDB contains an invalid part");
            return false;
        }
    }
    for (const auto& instance : object->instances()) {
        if (!instance ||
            document->getOwner(instance->getDBInstanceID()) != id) {
            setError(error, "ModelObjectDB contains an invalid instance");
            return false;
        }
    }
    return true;
}
