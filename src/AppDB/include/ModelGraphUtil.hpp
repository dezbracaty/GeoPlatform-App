#pragma once

#include <ModelPartDB.hpp>
#include <Geometry.hpp>

#include <vtkSmartPointer.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class ModelInstanceDB;
class ModelObjectDB;
class vtkPolyData;

class ModelGraphUtil final {
public:
    struct PartIdMapping {
        DBInstanceID sourcePartId;
        DBInstanceID targetPartId;
    };

    struct EnsureUniqueResult {
        bool success{false};
        bool cloned{false};
        DBInstanceID objectId;
        std::vector<PartIdMapping> parts;
        std::string error;

        DBInstanceID mapPart(DBInstanceID sourcePartId) const;
        explicit operator bool() const noexcept { return success; }
    };

    struct PartGeometryReplacement {
        DBInstanceID sourcePartId;
        vtkSmartPointer<vtkPolyData> geometry;
    };

    struct PartCreateInfo {
        std::string name;
        vtkSmartPointer<vtkPolyData> geometry;
        Transform localTransform;
        ModelPartRole role{ModelPartRole::Model};
        int orderIndex{0};
        int defaultFilamentSlot{1};
        ModelFilamentBindingMode filamentBindingMode{
            ModelFilamentBindingMode::InheritProjectDefault};
    };

    struct ObjectCreateInfo {
        std::string name;
        std::string sourceFile;
        std::string fileFormat;
        bool printableDefault{true};
    };

    struct InstanceCreateInfo {
        Transform transform;
        DBInstanceID printBedId{};
        bool printable{true};
        int arrangeOrder{0};
        DBInstanceID slicingConfigId{};
    };

    struct CreateResult {
        std::shared_ptr<ModelObjectDB> object;
        std::vector<std::shared_ptr<ModelPartDB>> parts;
        std::shared_ptr<ModelInstanceDB> instance;
        std::string error;

        explicit operator bool() const noexcept {
            return object && !parts.empty();
        }
    };

    static CreateResult createObject(
        ObjectCreateInfo object,
        std::vector<PartCreateInfo> parts,
        std::optional<InstanceCreateInfo> instance = std::nullopt);

    static CreateResult createSinglePartObject(
        ObjectCreateInfo object,
        vtkSmartPointer<vtkPolyData> geometry,
        std::optional<InstanceCreateInfo> instance = std::nullopt,
        ModelPartRole role = ModelPartRole::Model,
        int defaultFilamentSlot = 1);
    static CreateResult createSinglePartObject(
        ObjectCreateInfo object,
        const std::vector<GeomTriangle>& triangles,
        std::optional<InstanceCreateInfo> instance = std::nullopt,
        ModelPartRole role = ModelPartRole::Model,
        int defaultFilamentSlot = 1);

    static std::shared_ptr<ModelPartDB> addPart(
        DBInstanceID objectId, PartCreateInfo part,
        std::string* error = nullptr);
    static std::shared_ptr<ModelInstanceDB> createInstance(
        DBInstanceID objectId, InstanceCreateInfo instance,
        std::string* error = nullptr);
    static EnsureUniqueResult ensureUniqueObjectForInstance(
        DBInstanceID instanceId);
    static bool replaceInstancePartGeometry(
        DBInstanceID instanceId, DBInstanceID sourcePartId,
        vtkPolyData* geometry, std::string* error = nullptr);
    static bool replaceInstancePartGeometries(
        DBInstanceID instanceId,
        std::vector<PartGeometryReplacement> replacements,
        std::string* error = nullptr);
    static bool replaceInstancePartMaterial(
        DBInstanceID instanceId, DBInstanceID sourcePartId,
        DBInstanceID materialId, std::string* error = nullptr);
    static bool replacePartGeometry(DBInstanceID partId,
                                    vtkSmartPointer<vtkPolyData> geometry,
                                    std::string* error = nullptr);
    static bool replacePartMaterial(DBInstanceID partId,
                                    DBInstanceID materialId,
                                    std::string* error = nullptr);
    static bool setPartLocalTransform(DBInstanceID partId,
                                      const Transform& transform,
                                      std::string* error = nullptr);
    static bool removePart(DBInstanceID partId, std::string* error = nullptr);
    static bool removeInstance(DBInstanceID instanceId, std::string* error = nullptr);
    static bool removeObject(DBInstanceID objectId, std::string* error = nullptr);
    static bool validateObject(DBInstanceID objectId,
                               std::string* error = nullptr);

    ModelGraphUtil() = delete;
};
