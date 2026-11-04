#include "CreateGeometryHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    CreateGeometryHandler,
    "geometry.create.sphere",
    "geometry.create.cube",
    "geometry.create.cone",
    "geometry.create.cylinder",
    "parametric.graph.create",
    "parametric.graph.update_parameter",
    "parametric.graph.undo",
    "parametric.graph.redo",
    "parametric.graph.preview",
    "parametric.ir.validate",
    "parametric.graph.history",
    "printbed.update.grid",
    "printbed.update.bounds",
    "geometry.clear")
#include "ActionHandlerRegistry.hpp"
#include "AIDescriptorHelper.hpp"
#include <DocumentManager.hpp>
#include <TransactionManager.hpp>
#include <PrintBedDB.hpp>
#include "SlicingPreviewBridge.hpp"
#include <cmath>
#include <CameraDB.hpp>
#include <CameraNavigationController.hpp>
#include "Foundation/Log.h"
#include "SelectionBridge.hpp"
#include "ModelCreationUtil.hpp"
#include <ModelInstanceDB.hpp>
#include <vtkCellArray.h>
#include <vtkCleanPolyData.h>
#include <vtkConeSource.h>
#include <vtkCubeSource.h>
#include <vtkCylinderSource.h>
#include <vtkPolyData.h>
#include <vtkSphereSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangleFilter.h>
#include <GeometryCommandBridge.hpp>
#include <ParametricCommandNames.hpp>

namespace {

QVariantMap commandResultToMap(const GPlatform::Parametric::CommandResult& result) {
    QVariantMap map;
    map.insert("success", result.success);
    map.insert("status", result.status);
    map.insert("modelId", result.modelId);
    map.insert("prevVersion", result.prevVersion);
    map.insert("newVersion", result.newVersion);
    map.insert("errorCode", result.errorCode);
    map.insert("message", result.message);
    map.insert("delta", result.delta);
    map.insert("createdDbId", result.createdDbId);

    QVariantList diagnostics;
    diagnostics.reserve(result.diagnostics.size());
    for (const auto& item : result.diagnostics) {
        diagnostics.push_back(QVariantMap{
            {"severity", item.severity},
            {"code", item.code},
            {"message", item.message},
        });
    }
    map.insert("diagnostics", diagnostics);
    return map;
}

} // namespace

CreateGeometryHandler::CreateGeometryHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

const QHash<QString, QVariantMap>& CreateGeometryHandler::aiDescriptorTable() const {
    namespace Names = GPlatform::Parametric::CommandNames;

    auto geometrySchema = makeInputSchema({
        {"x", "number", false},
        {"y", "number", false},
        {"z", "number", false},
        {"position", "object", false},
        {"printBedId", "number", false},
        {"name", "string", false},
        {"color", "object", false}
    });
    const auto shapeSchema = [geometrySchema](const QStringList& dimensions) {
        auto schema = geometrySchema;
        auto properties = schema.value("properties").toMap();
        for (const auto& dimension : dimensions)
            properties.insert(dimension, QVariantMap{{"type", "number"}});
        schema.insert("properties", properties);
        return schema;
    };

    // 通用模型参数（用于 parametric actions）
    auto modelIdSchema = makeInputSchema({
        {"model_id", "string", false},
        {"modelId", "string", false},
        {"instanceId", "string", false},
        {"expected_version", "number", false},
        {"expectedVersion", "number", false}
    }, true);
    modelIdSchema["anyOfRequired"] = QVariantList{
        QVariantList{"model_id"},
        QVariantList{"modelId"},
        QVariantList{"instanceId"}
    };

    // graphCreate / graphPreview 专用 schema
    auto graphSchema = modelIdSchema;
    graphSchema["required"] = QVariantList{"graph", "parameters", "options"};
    graphSchema["properties"] = makeInputSchema({
        {"graph", "object", true},
        {"parameters", "object", true},
        {"options", "object", true},
        {"model_id", "string", false},
        {"modelId", "string", false},
        {"instanceId", "string", false},
        {"displayName", "string", false},
        {"expected_version", "number", false},
        {"expectedVersion", "number", false}
    }, true)["properties"];

    static const QHash<QString, QVariantMap> table = {
        {"geometry.create.sphere", makeDescriptor(
            "geometry.create.sphere", "Create Sphere", "Creates a printable sphere mesh model.",
            "write", shapeSchema({"radius"}), {"geometry", "model"})},
        {"geometry.create.cube", makeDescriptor(
            "geometry.create.cube", "Create Cube", "Creates a printable cube mesh model.",
            "write", shapeSchema({"size"}), {"geometry", "model"})},
        {"geometry.create.cone", makeDescriptor(
            "geometry.create.cone", "Create Cone", "Creates a printable cone mesh model.",
            "write", shapeSchema({"radius", "height"}), {"geometry", "model"})},
        {"geometry.create.cylinder", makeDescriptor(
            "geometry.create.cylinder", "Create Cylinder", "Creates a printable cylinder mesh model.",
            "write", shapeSchema({"radius", "height"}), {"geometry", "model"})},
        {"geometry.clear", makeDescriptor(
            "geometry.clear", "Clear All Geometry", "Clears all geometry objects from the scene.",
            "write", {}, {"geometry", "scene"})},
        {"printbed.update.grid", makeDescriptor(
            "printbed.update.grid", "Update PrintBed Grid", "Update print bed grid visibility.",
            "write", makeInputSchema({{"showGrid", "bool", false}}))},
        {"printbed.update.bounds", makeDescriptor(
            "printbed.update.bounds", "Update PrintBed Bounds", "Update print bed bounds visibility.",
            "write", makeInputSchema({{"showBounds", "bool", false}}))},
        // Parametric actions
        {Names::graphCreate(), makeDescriptor(
            Names::graphCreate(), "Parametric Graph Create",
            "Create a parametric model from graph+parameters+options.",
            "write", graphSchema, {"parametric"})},
        {Names::graphPreview(), makeDescriptor(
            Names::graphPreview(), "Parametric Graph Preview",
            "Dry-run preview of a parametric graph.",
            "write", graphSchema, {"parametric"})},
        {Names::graphUpdateParameter(), makeDescriptor(
            Names::graphUpdateParameter(), "Parametric Update Parameter",
            "Update one model's parameters and rebuild from history.",
            "write", makeInputSchema({
                {"model_id", "string", false},
                {"modelId", "string", false},
                {"instanceId", "string", false},
                {"parameterPatch", "object", false},
                {"parameters", "object", false},
                {"patch", "object", false},
                {"parameterName", "string", false},
                {"value", "object", false},
                {"options", "object", true},
                {"expected_version", "number", false},
                {"expectedVersion", "number", false}
            }, true), {"parametric"})},
        {Names::graphUndo(), makeDescriptor(
            Names::graphUndo(), "Parametric Undo",
            "Undo one parametric history step for one model.",
            "write", modelIdSchema, {"parametric"})},
        {Names::graphRedo(), makeDescriptor(
            Names::graphRedo(), "Parametric Redo",
            "Redo one parametric history step for one model.",
            "write", modelIdSchema, {"parametric"})},
        {Names::graphHistory(), makeDescriptor(
            Names::graphHistory(), "Parametric History",
            "Query parametric history snapshot for one model.",
            "write", modelIdSchema, {"parametric"})},
        {Names::irValidate(), makeDescriptor(
            Names::irValidate(), "Parametric IR Validate",
            "Validate a parametric graph payload against policy and schema.",
            "write", makeInputSchema({
                {"graph", "object", true},
                {"parameters", "object", true},
                {"instanceId", "string", false}
            }, true), {"parametric"})}
    };
    return table;
}

void CreateGeometryHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    if (!context) {
        LOG_WARN("CreateGeometryHandler::onEnter called with null context");
        onExit();
        return;
    }

    QString actionCode = context->getActionCode();
    QVariantMap params = context->getParams();

    LOG_INFO("CreateGeometryHandler::onEnter - actionCode: {}", actionCode.toStdString());

    // 根据actionCode执行不同的创建操作
    if (actionCode == "geometry.create.sphere" || actionCode == "geometry.create.cube" ||
        actionCode == "geometry.create.cone" || actionCode == "geometry.create.cylinder") {
        createPrimitive(actionCode, params);
    } else if (GPlatform::Parametric::CommandNames::isParametricActionType(actionCode)) {
        QVariantMap parametricResult;
        QString parametricError;
        if (!executeParametricCommand(actionCode, params, parametricResult, parametricError)) {
            context->setError(ActionErrorCode::Internal, parametricError.isEmpty() ? "Parametric command failed" : parametricError);
        } else {
            context->setResult(parametricResult);
        }
    } else if (actionCode == "printbed.update.grid") {
        updatePrintBedGrid(params);
    } else if (actionCode == "printbed.update.bounds") {
        updatePrintBedBounds(params);
    } else if (actionCode == "geometry.clear") {
        clearAll();
    } else {
        LOG_WARN("Unknown geometry action: {}", actionCode.toStdString());
        context->setError(ActionErrorCode::InvalidParams, QString("Unknown geometry action: %1").arg(actionCode));
    }

    // 创建操作是同步的，需要立即调用onExit来清理Handler状态
    // 这样避免异步清理导致的并发问题
    onExit();
}

void CreateGeometryHandler::createPrimitive(const QString& actionCode, const QVariantMap& params) {
    const QString shape = actionCode.section('.', -1);
    const auto positive = [&params](const char* key, float fallback) {
        bool ok = false;
        const float value = params.value(key, fallback).toFloat(&ok);
        return ok && std::isfinite(value) && value > 0 ? value : 0.0f;
    };
    vtkSmartPointer<vtkPolyData> body;
    QString name;
    if (shape == "cube") {
        const float size = positive("size", 20.0f);
        if (size > 0) {
            auto source = vtkSmartPointer<vtkCubeSource>::New();
            source->SetXLength(size);
            source->SetYLength(size);
            source->SetZLength(size);
            source->Update();
            body = source->GetOutput();
        }
        name = "Cube";
    } else if (shape == "sphere") {
        const float radius = positive("radius", 10.0f);
        if (radius > 0) {
            auto source = vtkSmartPointer<vtkSphereSource>::New();
            source->SetRadius(radius);
            source->SetThetaResolution(64);
            source->SetPhiResolution(32);
            source->Update();
            body = source->GetOutput();
        }
        name = "Sphere";
    } else if (shape == "cone") {
        const float radius = positive("radius", 10.0f);
        const float height = positive("height", 20.0f);
        if (radius > 0 && height > 0) {
            auto source = vtkSmartPointer<vtkConeSource>::New();
            source->SetRadius(radius);
            source->SetHeight(height);
            source->SetResolution(64);
            source->SetDirection(0, 0, 1);
            source->CappingOn();
            source->Update();
            body = source->GetOutput();
        }
        name = "Cone";
    } else if (shape == "cylinder") {
        const float radius = positive("radius", 10.0f);
        const float height = positive("height", 20.0f);
        if (radius > 0 && height > 0) {
            auto source = vtkSmartPointer<vtkCylinderSource>::New();
            source->SetRadius(radius);
            source->SetHeight(height);
            source->SetResolution(64);
            source->CappingOn();
            auto transform = vtkSmartPointer<vtkTransform>::New();
            transform->RotateX(90);
            auto filter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
            filter->SetInputConnection(source->GetOutputPort());
            filter->SetTransform(transform);
            filter->Update();
            body = filter->GetOutput();
        }
        name = "Cylinder";
    }
    if (!body) {
        m_context->setError(ActionErrorCode::InvalidParams,
                           "Model dimensions must be finite positive numbers");
        return;
    }

    // Weld primitive seams and cap vertices, then publish only triangles just
    // like the file loader. No temporary files or standalone primitive DBs.
    auto clean = vtkSmartPointer<vtkCleanPolyData>::New();
    clean->SetInputData(body);
    auto triangulate = vtkSmartPointer<vtkTriangleFilter>::New();
    triangulate->SetInputConnection(clean->GetOutputPort());
    triangulate->PassLinesOff();
    triangulate->PassVertsOff();
    triangulate->Update();
    auto* mesh = triangulate->GetOutput();
    ModelLoaderUtil::LoadResult result;
    result.modelName = params.value("name", name).toString();
    result.vertexCount = static_cast<std::size_t>(mesh->GetNumberOfPoints());
    result.faceCount = static_cast<std::size_t>(mesh->GetNumberOfPolys());
    double bounds[6];
    mesh->GetBounds(bounds);
    result.boundingBoxMin = Vector3(bounds[0], bounds[2], bounds[4]);
    result.boundingBoxMax = Vector3(bounds[1], bounds[3], bounds[5]);
    result.triangles.reserve(result.faceCount);
    vtkIdType count;
    const vtkIdType* ids;
    auto* cells = mesh->GetPolys();
    for (cells->InitTraversal(); cells->GetNextCell(count, ids);) {
        double points[3][3];
        for (int corner = 0; corner < 3; ++corner)
            mesh->GetPoint(ids[corner], points[corner]);
        result.triangles.emplace_back(
            Vector3(points[0][0], points[0][1], points[0][2]),
            Vector3(points[1][0], points[1][1], points[1][2]),
            Vector3(points[2][0], points[2][1], points[2][2]));
    }
    result.success = !result.triangles.empty();
    const auto graph = ModelCreationUtil::createModel(
        result, {result.modelName.toStdString(), {}, "generated", true}, params);
    if (!graph || !graph.instance) {
        m_context->setError(ActionErrorCode::Internal, QString::fromStdString(graph.error));
        return;
    }
    const QString id = QString::number(graph.instance->getDBInstanceID().getValue());
    m_context->setResult(QVariantMap{
        {"success", true}, {"objectId", id}, {"modelId", id},
        {"objectIds", QVariantList{id}},
        {"vertexCount", static_cast<qulonglong>(result.vertexCount)},
        {"faceCount", static_cast<qulonglong>(result.faceCount)}});
    smartFitCamera();
}

bool CreateGeometryHandler::executeParametricCommand(const QString& commandType,
                                                     const QVariantMap& params,
                                                     QVariantMap& outResult,
                                                     QString& outError) {
    outResult.clear();
    outError.clear();

    const auto result = GPlatform::Parametric::GeometryCommandBridge::instance().executeAction(
        commandType, params);

    if (!result.success) {
        LOG_ERROR("[Parametric] {} failed: code={}, message={}",
                  commandType.toStdString(),
                  result.errorCode.toStdString(),
                  result.message.toStdString());
        outError = QString("%1 failed: [%2] %3")
                       .arg(commandType, result.errorCode, result.message);
        outResult = commandResultToMap(result);
        return false;
    }

    LOG_INFO("[Parametric] {} completed: modelId={}, version={} dbId={}",
             commandType.toStdString(),
             result.modelId.toStdString(),
             result.newVersion,
             result.createdDbId.toStdString());

    outResult = commandResultToMap(result);

    if (commandType == GPlatform::Parametric::CommandNames::graphCreate() ||
        commandType == GPlatform::Parametric::CommandNames::graphUpdateParameter() ||
        commandType == GPlatform::Parametric::CommandNames::graphUndo() ||
        commandType == GPlatform::Parametric::CommandNames::graphRedo()) {
        smartFitCamera();
    }
    return true;
}

// 注意：createPrintBed 已移至 SceneHandler::createPrintBed (scene.init action)

void CreateGeometryHandler::updatePrintBedGrid(const QVariantMap& params) {
    // LOG_INFO("Updating PrintBed grid visibility");

    TransactionGuard guard("Update PrintBed Grid");

    // Get DocumentManager instance
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("DocumentManager instance not available");
        return;
    }

    // Find all PrintBed objects by type
    auto printBedObjects = docManager->getDBInstancesByType(TypeID::PRINT_BED_DB);
    for (const auto& obj : printBedObjects) {
        auto printBed = std::dynamic_pointer_cast<PrintBedDB>(obj);
        if (printBed) {
            bool showGrid = params.value("showGrid", true).toBool();
            printBed->setShowGrid(showGrid);
            // LOG_INFO("Updated PrintBed {} grid visibility: {}",
            //          printBed->getDBInstanceID().toString(), showGrid ? "ON" : "OFF");
            // setShowGrid() 通过 markGeometryChanged() 自动触发 VTK 几何体更新
        }
    }
}

void CreateGeometryHandler::updatePrintBedBounds(const QVariantMap& params) {
    // LOG_INFO("Updating PrintBed bounds visibility");

    TransactionGuard guard("Update PrintBed Bounds");

    // Get DocumentManager instance
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("DocumentManager instance not available");
        return;
    }

    // Find all PrintBed objects by type
    auto printBedObjects = docManager->getDBInstancesByType(TypeID::PRINT_BED_DB);
    for (const auto& obj : printBedObjects) {
        auto printBed = std::dynamic_pointer_cast<PrintBedDB>(obj);
        if (printBed) {
            bool showBounds = params.value("showBounds", true).toBool();
            printBed->setShowBounds(showBounds);
            // LOG_INFO("Updated PrintBed {} bounds visibility: {}",
            //          printBed->getDBInstanceID().toString(), showBounds ? "ON" : "OFF");
            // setShowBounds() 通过 markGeometryChanged() 自动触发 VTK 几何体更新
        }
    }
}

void CreateGeometryHandler::clearAll() {
    TransactionGuard guard("Clear All");

    // 获取DocumentManager实例
    auto* docManager = DocumentManager::instance();

    // 清空所有几何体
    docManager->clearAllGeometry();
    // The bridge observes only its primary selection; an unordered multi-object
    // deletion can otherwise leave another deleted ID selected.
    SelectionBridge::instance()->clearSelection();
}

void CreateGeometryHandler::smartFitCamera() {
    // 通过DocumentManager获取相机实例并执行智能fit
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("CreateGeometryHandler::smartFitCamera - DocumentManager not available for camera fit");
        return;
    }

    auto camera = CameraNavigationController::currentCamera();
    if (!camera) {
        LOG_WARN("CreateGeometryHandler::smartFitCamera - No camera found for fit operation");
        return;
    }

    CameraNavigationController::fitScene(camera);
}
