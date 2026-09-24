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
#include <SphereDB.hpp>
#include <CubeDB.hpp>
#include <ConeDB.hpp>
#include <CylinderDB.hpp>
#include <PrintBedDB.hpp>
#include "SlicingPreviewBridge.hpp"
#include <limits>
#include <cmath>
#include <CameraDB.hpp>
#include <SystemTypes.hpp>
#include <CameraNavigationController.hpp>
#include "Foundation/Log.h"
#include "ModelSettingsBridge.hpp"
#include <ModelPositionUtil.hpp>
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

    // 通用几何体参数
    auto geometrySchema = makeInputSchema({
        {"x", "number", false},
        {"y", "number", false},
        {"z", "number", false},
        {"color", "object", false}
    }, true);  // allowUnknownParams = true

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
            "geometry.create.sphere", "Create Sphere", "Creates a sphere geometry object.",
            "write", geometrySchema, {"geometry", "parametric"})},
        {"geometry.create.cube", makeDescriptor(
            "geometry.create.cube", "Create Cube", "Creates a cube geometry object.",
            "write", geometrySchema, {"geometry", "parametric"})},
        {"geometry.create.cone", makeDescriptor(
            "geometry.create.cone", "Create Cone", "Creates a cone geometry object.",
            "write", geometrySchema, {"geometry", "parametric"})},
        {"geometry.create.cylinder", makeDescriptor(
            "geometry.create.cylinder", "Create Cylinder", "Creates a cylinder geometry object.",
            "write", geometrySchema, {"geometry", "parametric"})},
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
    if (actionCode == "geometry.create.sphere") {
        createSphere(params);
    } else if (actionCode == "geometry.create.cube") {
        createCube(params);
    } else if (actionCode == "geometry.create.cone") {
        createCone(params);
    } else if (actionCode == "geometry.create.cylinder") {
        createCylinder(params);
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

void CreateGeometryHandler::createSphere(const QVariantMap& params) {
    // 使用TransactionGuard确保可撤销
    TransactionGuard guard("Create Sphere");

    // 创建SphereDB实例
    auto sphere = trans::TransDB::create<SphereDB>();
    if (!sphere) {
        LOG_WARN("Failed to create SphereDB");
        return;
    }

    // 设置半径
    double radius = params.value("radius", 0.6).toDouble();
    sphere->setRadius(radius);

    // 智能位置计算
    Vector3 basePosition = extractPosition(params);
    if (basePosition.x == 0.0f && basePosition.y == 0.0f && basePosition.z == 0.0f) {
        // 用户没有指定位置，使用智能摆放
        Vector3 sphereSize(radius * 2, radius * 2, radius * 2);
        basePosition = findAvailablePosition(sphereSize);

        // 检查智能摆放是否成功
        if (std::isnan(basePosition.x)) {
            LOG_ERROR("Sphere创建失败: 智能摆放无法找到合适位置，终止创建操作");
            return;
        }
    }

    // 应用贴底逻辑
    if (ModelSettingsBridge::instance()->snapToGround()) {
        Vector3 adjustedPosition = ModelPositionUtil::adjustPositionToBed(basePosition, sphere);
        sphere->setPosition(adjustedPosition);
    } else {
        sphere->setPosition(basePosition);
    }

    // 设置材质颜色和属性
    setMaterialProperties(sphere, params, "sphere");

    // 创建完成后智能适应相机
    smartFitCamera();
}

void CreateGeometryHandler::createCube(const QVariantMap& params) {
    TransactionGuard guard("Create Cube");

    auto cube = trans::TransDB::create<CubeDB>();
    if (!cube) {
        LOG_WARN("Failed to create CubeDB");
        return;
    }

    float size = params.value("size", 1.0).toFloat();
    Vector3 sizeVec(size, size, size); // CubeDB expects Vector3 for size
    cube->setSize(sizeVec);

    // 智能位置计算
    Vector3 basePosition = extractPosition(params);
    if (basePosition.x == 0.0f && basePosition.y == 0.0f && basePosition.z == 0.0f) {
        // 用户没有指定位置，使用智能摆放
        basePosition = findAvailablePosition(sizeVec);

        // 检查智能摆放是否成功
        if (std::isnan(basePosition.x)) {
            LOG_ERROR("Cube创建失败: 智能摆放无法找到合适位置，终止创建操作");
            return;
        }
    }

    // 应用贴底逻辑
    if (ModelSettingsBridge::instance()->snapToGround()) {
        Vector3 adjustedPosition = ModelPositionUtil::adjustPositionToBed(basePosition, cube);
        cube->setPosition(adjustedPosition);
    } else {
        cube->setPosition(basePosition);
    }

    // 设置材质颜色和属性
    setMaterialProperties(cube, params, "cube");

    // 创建完成后智能适应相机
    smartFitCamera();
}

void CreateGeometryHandler::createCone(const QVariantMap& params) {
    TransactionGuard guard("Create Cone");

    auto cone = trans::TransDB::create<ConeDB>();
    if (!cone) {
        LOG_WARN("Failed to create ConeDB");
        return;
    }

    double height = params.value("height", 1.2).toDouble();
    double radius = params.value("radius", 0.5).toDouble();
    cone->setHeight(height);
    cone->setRadius(radius);

    // 智能位置计算
    Vector3 basePosition = extractPosition(params);
    if (basePosition.x == 0.0f && basePosition.y == 0.0f && basePosition.z == 0.0f) {
        // 用户没有指定位置，使用智能摆放
        Vector3 coneSize(radius * 2, radius * 2, height);
        basePosition = findAvailablePosition(coneSize);

        // 检查智能摆放是否成功
        if (std::isnan(basePosition.x)) {
            LOG_ERROR("Cone创建失败: 智能摆放无法找到合适位置，终止创建操作");
            return;
        }
    }
    if (ModelSettingsBridge::instance()->snapToGround()) {
        Vector3 adjustedPosition = ModelPositionUtil::adjustPositionToBed(basePosition, cone);
        cone->setPosition(adjustedPosition);
    } else {
        cone->setPosition(basePosition);
    }

    // 设置材质颜色和属性
    setMaterialProperties(cone, params, "cone");

    // 创建完成后智能适应相机
    smartFitCamera();
}

void CreateGeometryHandler::createCylinder(const QVariantMap& params) {
    TransactionGuard guard("Create Cylinder");

    auto cylinder = trans::TransDB::create<CylinderDB>();
    if (!cylinder) {
        LOG_WARN("Failed to create CylinderDB");
        return;
    }

    double height = params.value("height", 1.0).toDouble();
    double radius = params.value("radius", 0.4).toDouble();
    cylinder->setHeight(height);
    cylinder->setRadius(radius);

    // 智能位置计算
    Vector3 basePosition = extractPosition(params);
    if (basePosition.x == 0.0f && basePosition.y == 0.0f && basePosition.z == 0.0f) {
        // 用户没有指定位置，使用智能摆放
        Vector3 cylinderSize(radius * 2, radius * 2, height);
        basePosition = findAvailablePosition(cylinderSize);

        // 检查智能摆放是否成功
        if (std::isnan(basePosition.x)) {
            LOG_ERROR("Cylinder创建失败: 智能摆放无法找到合适位置，终止创建操作");
            return;
        }
    }
    if (ModelSettingsBridge::instance()->snapToGround()) {
        Vector3 adjustedPosition = ModelPositionUtil::adjustPositionToBed(basePosition, cylinder);
        cylinder->setPosition(adjustedPosition);
    } else {
        cylinder->setPosition(basePosition);
    }

    // 设置材质颜色和属性
    setMaterialProperties(cylinder, params, "cylinder");

    // 创建完成后智能适应相机
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
}

Vector3 CreateGeometryHandler::extractPosition(const QVariantMap& params) {
    float x = params.value("x", 0.0).toFloat();
    float y = params.value("y", 0.0).toFloat();
    float z = params.value("z", 0.0).toFloat();

    return Vector3(x, y, z);
}


void CreateGeometryHandler::setMaterialProperties(std::shared_ptr<ActorDB> actor, const QVariantMap& params, const QString& geometryType) {
    auto material = actor->getMaterial();
    if (!material) {
        LOG_WARN("No material found for actor");
        return;
    }

    // 从参数中获取颜色，如果没有提供则使用基于位置的随机颜色
    Vector3 color;
    if (params.contains("color")) {
        // 如果提供了颜色参数
        QVariantMap colorMap = params.value("color").toMap();
        color.x = colorMap.value("r", 0.5).toFloat();
        color.y = colorMap.value("g", 0.7).toFloat();
        color.z = colorMap.value("b", 0.9).toFloat();
    } else {
        // 使用基于位置的随机颜色生成算法（与原始代码完全一致）
        Vector3 position = extractPosition(params);
        int hash = static_cast<int>(position.x * 73856093) ^
                   static_cast<int>(position.y * 19349663) ^
                   static_cast<int>(position.z * 83492791);

        float r = ((hash & 0xFF) / 255.0f) * 0.7f + 0.3f; // 范围 0.3-1.0
        float g = (((hash >> 8) & 0xFF) / 255.0f) * 0.7f + 0.3f;
        float b = (((hash >> 16) & 0xFF) / 255.0f) * 0.7f + 0.3f;

        color = Vector3(r, g, b);
    }

    // 设置材质属性 - 根据几何体类型使用不同的参数
    material->setColor(color);

    if (geometryType == "sphere") {
        material->setSpecular(0.5f);
        material->setSpecularPower(30.0f);
    } else if (geometryType == "cube") {
        material->setSpecular(0.4f);
        material->setSpecularPower(20.0f);
    } else if (geometryType == "cone") {
        material->setSpecular(0.6f);
        material->setSpecularPower(40.0f);
    } else if (geometryType == "cylinder") {
        material->setSpecular(0.3f);
        material->setSpecularPower(10.0f);
    } else if (geometryType == "printbed") {
        // Print bed uses a more subdued appearance
        material->setSpecular(0.2f);
        material->setSpecularPower(5.0f);
        // Set default light blue color for print bed if no color specified
        if (!params.contains("color")) {
            material->setColor(Vector3(0.3f, 0.5f, 0.8f)); // Light blue
        }
    }
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

Vector3 CreateGeometryHandler::findAvailablePosition(const Vector3& objectSize, float margin) {
    // 获取DocumentManager实例
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("DocumentManager不可用，使用默认位置");
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    // 收集所有几何体对象的位置和尺寸信息
    std::vector<ModelPositionUtil::ExistingObject> existingObjects;

    // 获取所有几何体类型的对象
    std::vector<TypeID> geometryTypes = {
        TypeID::CUBE_DB,
        TypeID::SPHERE_DB,
        TypeID::CYLINDER_DB,
        TypeID::CONE_DB,
        TypeID::MODEL_INSTANCE_DB
    };

    for (auto typeId : geometryTypes) {
        auto objects = docManager->getDBInstancesByType(typeId);
        for (const auto& objPtr : objects) {
            auto actor = std::dynamic_pointer_cast<ActorDB>(objPtr);
            if (!actor) continue;

            try {
                // 获取对象当前位置
                Vector3 position = actor->getPosition();

                // 获取对象包围盒来确定尺寸
                const auto bounds = actor->localBounds();
                Vector3 size = bounds.max - bounds.min;

                // 添加到现有对象列表
                existingObjects.push_back({position, size});

            } catch (const std::exception& e) {
                LOG_WARN("获取{}对象信息失败: {}", static_cast<int>(typeId), e.what());
            }
        }
    }

    // 调用ModelPositionUtil的智能摆放算法
    Vector3 availablePosition = ModelPositionUtil::findAvailablePosition(objectSize, existingObjects, margin);

    // 检查是否找到了有效位置
    if (std::isnan(availablePosition.x) || std::isnan(availablePosition.y) || std::isnan(availablePosition.z)) {
        LOG_ERROR("智能摆放失败: 无法找到合适的摆放位置，创建操作终止");
        // 返回一个不可能的位置来标识失败
        return Vector3(std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::quiet_NaN());
    }

    return availablePosition;
}
