#include "ModelCreationUtil.hpp"

#include <DocumentManager.hpp>
#include <MaterialDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPositionUtil.hpp>
#include <PrintBedDB.hpp>
#include <TransactionManager.hpp>
#include <QColor>

#include <cmath>
#include <exception>

namespace {
Vector3 calculatePosition(
    const Vector3& modelSize, const std::shared_ptr<PrintBedDB>& bed) {
    const Vector3 center = bed->getCenter();
    Vector3 position(center.x, center.y, bed->getOrigin().z);
    if (modelSize.x > bed->getWidth() || modelSize.y > bed->getHeight())
        return position;

    std::vector<ModelPositionUtil::ExistingObject> existing;
    for (const auto& object : DocumentManager::instance()->getDBInstancesByType(
             TypeID::MODEL_INSTANCE_DB)) {
        const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(object);
        if (!instance || instance->getParentPrintBedDBId() != bed->getDBInstanceID())
            continue;
        const auto bounds = instance->worldBounds();
        if (!bounds.valid) continue;
        const Vector3 size = bounds.max - bounds.min;
        if (size.x <= 0.01f || size.y <= 0.01f || size.z <= 0.01f) continue;
        existing.push_back({(bounds.min + bounds.max) * 0.5f, size});
    }
    position = ModelPositionUtil::findAvailablePosition(
        modelSize, existing,
        ModelPositionUtil::PlacementArea{bed->getWidth(), bed->getHeight(), center},
        10.0f);
    position.z = bed->getOrigin().z;
    return position;
}

bool finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

std::shared_ptr<PrintBedDB> ModelCreationUtil::resolvePrintBed(const QVariant& requestedId) {
    auto* document = DocumentManager::instance();
    if (!document) return {};
    const auto usable = [](const std::shared_ptr<PrintBedDB>& bed) {
        return bed && bed->getTemplateDBId().isValid() &&
            bed->getWidth() > 0.0f && bed->getHeight() > 0.0f;
    };
    if (requestedId.isValid()) {
        auto bed = document->getDB<PrintBedDB>(DBInstanceID(requestedId.toULongLong()));
        if (usable(bed)) return bed;
    }
    for (const auto& object : document->getDBInstancesByType(TypeID::PRINT_BED_DB)) {
        auto bed = std::dynamic_pointer_cast<PrintBedDB>(object);
        if (usable(bed)) return bed;
    }
    return {};
}

DBInstanceID ModelCreationUtil::activeSlicingConfigId() {
    auto* document = DocumentManager::instance();
    if (!document) return DBInstanceID();
    const auto configs = document->getDBInstancesByType(TypeID::SLICING_CONFIG_DB);
    return configs.empty() ? DBInstanceID{} : configs.front()->getDBInstanceID();
}

ModelGraphUtil::CreateResult ModelCreationUtil::createModel(
    const ModelLoaderUtil::LoadResult& mesh,
    ModelGraphUtil::ObjectCreateInfo object,
    const QVariantMap& params,
    std::shared_ptr<const GlbAssetData> sourceAsset) {
    const auto failure = [](const std::string& message) {
        ModelGraphUtil::CreateResult result;
        result.error = message;
        return result;
    };
    if (!mesh.success || mesh.triangles.empty())
        return failure("No usable mesh was produced");
    const auto bed = resolvePrintBed(params.value("printBedId"));
    if (!bed) return failure("No usable print bed is available");

    const bool explicitPosition = params.contains("x") || params.contains("y") ||
        params.contains("z") || params.contains("position");
    Vector3 position;
    if (explicitPosition) {
        if (params.contains("position") && !params.value("position").canConvert<QVariantMap>())
            return failure("Model position must be an object containing coordinates");
        const auto values = params.contains("position")
            ? params.value("position").toMap() : params;
        float* coordinates[] = {&position.x, &position.y, &position.z};
        const char* keys[] = {"x", "y", "z"};
        for (int index = 0; index < 3; ++index) {
            bool ok = false;
            *coordinates[index] = values.value(keys[index], 0.0).toFloat(&ok);
            if (!ok) return failure("Model position must contain numeric coordinates");
        }
    } else {
        position = calculatePosition(mesh.boundingBoxMax - mesh.boundingBoxMin, bed);
        position.z -= mesh.boundingBoxMin.z;
    }
    if (!finite(position)) return failure("Unable to place the model on the print bed");

    Vector3 color(0.8f, 0.8f, 0.8f);
    if (params.contains("color")) {
        const auto value = params.value("color");
        if (value.typeId() == QMetaType::QString) {
            const QColor selected(value.toString());
            if (!selected.isValid()) return failure("Invalid model color");
            color = Vector3(selected.redF(), selected.greenF(), selected.blueF());
        } else if (value.canConvert<QVariantMap>()) {
            const auto components = value.toMap();
            float* channels[] = {&color.x, &color.y, &color.z};
            const char* keys[] = {"r", "g", "b"};
            for (int index = 0; index < 3; ++index) {
                bool ok = false;
                *channels[index] = components.value(keys[index], 0.8).toFloat(&ok);
                if (!ok) return failure("Model color must contain numeric components");
            }
        } else {
            return failure("Invalid model color");
        }
        if (!finite(color) || color.x < 0 || color.x > 1 ||
            color.y < 0 || color.y > 1 || color.z < 0 || color.z > 1)
            return failure("Model color components must be between zero and one");
    }

    TransactionGuard transaction("Create Model");
    try {
        Transform transform;
        transform.identity();
        transform.setPosition(position);
        auto graph = ModelGraphUtil::createSinglePartObject(
            std::move(object), mesh.triangles,
            ModelGraphUtil::InstanceCreateInfo{
                transform, bed->getDBInstanceID(), true, 0, activeSlicingConfigId()});
        if (!graph || !graph.instance || graph.parts.empty()) {
            transaction.rollback();
            return failure(graph.error.empty() ? "Failed to create model graph" : graph.error);
        }
        if (sourceAsset && !graph.object->publishGlbSourceAsset(std::move(sourceAsset))) {
            transaction.rollback();
            return failure("Failed to retain the GLB source appearance");
        }
        if (auto material = graph.parts.front()->getMaterial()) {
            material->setColor(color);
            material->setDiffuseColor(color);
        }
        graph.parts.front()->setFilamentBindingMode(
            static_cast<int>(ModelFilamentBindingMode::InheritProjectDefault));
        transaction.commit();
        return graph;
    } catch (const std::exception& error) {
        transaction.rollback();
        return failure(error.what());
    } catch (...) {
        transaction.rollback();
        return failure("Failed to create model graph");
    }
}
