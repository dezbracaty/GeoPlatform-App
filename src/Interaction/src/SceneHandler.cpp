#include "SceneHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    SceneHandler, "scene.init", "scene.skybox.preset", "scene.skybox.colors")
#include "ActionHandlerRegistry.hpp"
#include "SliceSettingsBridge.hpp"
#include <DocumentManager.hpp>
#include <TransactionManager.hpp>
#include <SkyboxDB.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include <CameraDB.hpp>
#include <ModelGraphUtil.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>
#include <MaterialDB.hpp>
#include <LightDB.hpp>
#include <SystemTypes.hpp>
#include <CameraNavigationController.hpp>
#include <STLLoader.hpp>
#include "Foundation/Log.h"
#include "TaskStateNotifier.hpp"

#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <vtkPolyData.h>

#include <cmath>

// ============================================================================
// 场景默认配置
// ============================================================================

namespace SceneDefaults {

// 默认打印机模型配置
struct PrinterModelConfig {
    QString filePath;
    QString name;
    Vector3 position;
    Vector3 color;
    float zOffset;

    // 获取默认配置
    static PrinterModelConfig getDefault() {
        PrinterModelConfig config;

        // 尝试从多个位置查找模型
        QStringList searchPaths = {
            // 1. macOS Bundle Resources（打包后可用）
            QCoreApplication::applicationDirPath() + "/../Resources/models/blackbelt_platform.STL",
            // 2. Windows 可执行文件同目录
            QCoreApplication::applicationDirPath() + "/resources/models/blackbelt_platform.STL",
            // 3. Linux 相对路径
            QCoreApplication::applicationDirPath() + "/../share/GPlatform/models/blackbelt_platform.STL",
            // 4. 开发时相对路径
            "resources/models/blackbelt_platform.STL",
            // 5. 用户下载目录（开发时可能存在）
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/blackbelt_platform.STL",
        };

        for (const auto& path : searchPaths) {
            if (QFileInfo::exists(path)) {
                config.filePath = path;
                LOG_INFO("PrinterModelConfig: Found printer model at: {}", path.toStdString());
                break;
            }
        }

        if (config.filePath.isEmpty()) {
            LOG_WARN("PrinterModelConfig: No printer model found in any search path");
        }

        config.name = "BlackBelt Platform";
        config.position = Vector3(0, 0, -83);
        config.color = Vector3(0.25f, 0.26f, 0.30f);
        config.zOffset = -5.0f;
        return config;
    }
};

// 默认天空盒配置
struct SkyboxConfig {
    QString preset;
    bool useIBL;

    static SkyboxConfig getDefault() {
        SkyboxConfig config;
        config.preset = "normal";  // 专业蓝灰环境
        config.useIBL = true;
        return config;
    }
};

} // namespace SceneDefaults

namespace {
Polygon2 printableAreaFromParams(const QVariantMap& params,
                                 float x, float y, float width, float height) {
    Polygon2 area;
    const QVariantList points = params.value("printableArea").toList();
    area.reserve(static_cast<std::size_t>(points.size()));
    for (const QVariant& pointValue : points) {
        const QVariantMap point = pointValue.toMap();
        if (!point.contains("x") || !point.contains("y")) {
            continue;
        }
        area.emplace_back(point.value("x").toFloat(), point.value("y").toFloat());
    }
    if (area.size() < 3) {
        area = {Vector2(x, y), Vector2(x + width, y),
                Vector2(x + width, y + height), Vector2(x, y + height)};
    }
    return area;
}

bool sameFloat(float left, float right) {
    return std::abs(left - right) <= 0.001f;
}

std::shared_ptr<PrintBedTemplateDB> findPrintBedTemplate(
    DocumentManager* document,
    const std::string& modelId,
    const std::string& variantId) {
    if (!document || (modelId.empty() && variantId.empty())) {
        return {};
    }
    for (const auto& object :
         document->getDBInstancesByType(TypeID::PRINT_BED_TEMPLATE_DB)) {
        const auto candidate =
            std::dynamic_pointer_cast<PrintBedTemplateDB>(object);
        if (candidate && candidate->getModelId() == modelId &&
            candidate->getVariantId() == variantId) {
            return candidate;
        }
    }
    return {};
}

void configurePrintBedTemplate(
    const std::shared_ptr<PrintBedTemplateDB>& definition,
    const QVariantMap& params,
    const Vector3& origin,
    float width,
    float height,
    float thickness,
    float printHeight) {
    if (!definition) return;

    definition->setModelId(
        params.value("modelId").toString().toStdString());
    definition->setVariantId(
        params.value("variantId").toString().toStdString());
    definition->setVendor(
        params.value("bedVendor").toString().toStdString());
    definition->setPrinterModel(
        params.value("printerModel").toString().toStdString());
    definition->setWidth(width);
    definition->setHeight(height);
    definition->setThickness(thickness);
    definition->setPrintHeight(printHeight);
    definition->setOrigin(origin);
    definition->setPrintableArea(
        printableAreaFromParams(params, origin.x, origin.y, width, height));
    definition->setBedModelPath(
        params.value("bedModelPath").toString().toStdString());
    definition->setBedTexturePath(
        params.value("bedTexturePath").toString().toStdString());
    definition->setMaxTemperature(
        params.value("maxTemperature", 100.0).toFloat());
    definition->setHeated(params.value("heated", true).toBool());
}
} // namespace

SceneHandler::SceneHandler(QObject* parent)
    : StandardActionHandler(parent) {
    QObject::connect(SliceSettingsBridge::instance(),
                     &SliceSettingsBridge::machineSelectionChanged,
                     this,
                     &SceneHandler::syncPrintBedFromActiveMachine,
                     Qt::QueuedConnection);
}

void SceneHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    QString actionCode = context->getActionCode();
    QVariantMap params = context->getParams();

    // 根据 actionCode 执行不同的场景操作
    if (actionCode == "scene.init") {
        initScene(params);
    } else if (actionCode == "scene.skybox.preset") {
        setSkyboxPreset(params);
    } else if (actionCode == "scene.skybox.colors") {
        setSkyboxColors(params);
    } else {
        LOG_WARN("SceneHandler: Unknown action code: {}", actionCode.toStdString());
    }

    // 场景操作是同步的，立即调用 onExit 清理 Handler 状态
    onExit();
}

// === 场景初始化 ===

void SceneHandler::initScene(const QVariantMap& params) {
    LOG_INFO("SceneHandler::initScene - Initializing scene with defaults");

    // 1. 环境必须由同一个 SkyboxDB 驱动。Filament 和 VTK 都从这里获取
    //    可见背景、IBL 资源与强度，避免后端各自使用隐式默认值。
    const bool enableSkybox = params.value("enableSkybox", true).toBool();
    QVariantMap skyboxParams = params.value("skybox").toMap();
    if (!skyboxParams.contains("preset")) {
        skyboxParams.insert("preset", "print-studio");
    }
    const bool skyboxCreated = !enableSkybox || createSkybox(skyboxParams);
    if (!enableSkybox) {
        LOG_INFO("SceneHandler::initScene - Skybox explicitly disabled");
    }

    // 2. 默认创建程序化 PrintBedDB（快速路径）
    QVariantMap printBedParams;
    if (params.contains("printBed")) {
        printBedParams = params.value("printBed").toMap();
    }
    const bool printBedCreated = createPrintBed(printBedParams);

    // 3. 可选创建打印机 STL 模型（默认关闭，避免启动慢）
    bool printerModelCreated = false;
    bool enablePrinterModel = params.value("enablePrinterModel", false).toBool();
    QVariantMap printerParams;
    if (params.contains("printerModel")) {
        printerParams = params.value("printerModel").toMap();
        if (printerParams.contains("enabled")) {
            enablePrinterModel = printerParams.value("enabled").toBool();
        } else {
            // 显式提供 printerModel 参数时，默认视为启用。
            enablePrinterModel = true;
        }
    }

    if (enablePrinterModel) {
        auto printerConfig = SceneDefaults::PrinterModelConfig::getDefault();
        if (printerParams.isEmpty()) {
            if (!printerConfig.filePath.isEmpty()) {
                printerParams["filePath"] = printerConfig.filePath;
                printerParams["name"] = printerConfig.name;
                QVariantMap posMap;
                posMap["x"] = printerConfig.position.x;
                posMap["y"] = printerConfig.position.y;
                posMap["z"] = printerConfig.position.z;
                printerParams["position"] = posMap;
                QVariantMap colorMap;
                colorMap["r"] = printerConfig.color.x;
                colorMap["g"] = printerConfig.color.y;
                colorMap["b"] = printerConfig.color.z;
                printerParams["color"] = colorMap;
                printerParams["zOffset"] = printerConfig.zOffset;
            }
        }

        if (!printerParams.value("filePath").toString().trimmed().isEmpty()) {
            printerModelCreated = createPrinterModel(printerParams);
        } else {
            LOG_WARN("SceneHandler::initScene - enablePrinterModel=true but filePath is empty, skip printer model");
        }
    } else {
        LOG_INFO("SceneHandler::initScene - Printer STL model disabled by default, using PrintBedDB only");
    }

    // 4. 创建默认灯光
    bool lightsCreated = createDefaultLights();

    if (skyboxCreated && printBedCreated && lightsCreated) {
        LOG_INFO(
            "SceneHandler::initScene - Scene initialized successfully (printBed={}, printerModel={}, lights={})",
            printBedCreated,
            printerModelCreated,
            lightsCreated);
    } else {
        LOG_WARN(
            "SceneHandler::initScene - Scene initialization incomplete: skybox={}, printBed={}, printerModel={}, lights={}",
            skyboxCreated,
            printBedCreated,
            printerModelCreated,
            lightsCreated);
    }
}

bool SceneHandler::createSkybox(const QVariantMap& params) {
    // 幂等性检查：如果已存在 SkyboxDB，直接返回成功
    if (hasObjectOfType(static_cast<int>(TypeID::SKYBOX_DB))) {
        LOG_DEBUG("SceneHandler::createSkybox - Skybox already exists, skipping creation");
        return true;
    }

    TransactionGuard guard("Create Skybox");

    auto skyboxDB = trans::TransDB::create<SkyboxDB>();
    if (!skyboxDB) {
        LOG_ERROR("SceneHandler::createSkybox - Failed to create SkyboxDB");
        return false;
    }

    // 应用预设
    QString preset = params.value("preset", "normal").toString();
    if (preset == "print-studio") {
        skyboxDB->applyPrintStudioPreset();
    } else if (preset == "studio") {
        skyboxDB->applyStudioPreset();
    } else if (preset == "minimal") {
        skyboxDB->applyMinimalPreset();
    } else if (preset == "editing") {
        skyboxDB->applyEditingEnvironment();
    } else if (preset == "technical") {
        skyboxDB->applyTechnicalEnvironment();
    } else {
        // 默认使用 Normal 环境（专业蓝灰）
        skyboxDB->applyNormalEnvironment();
    }

    // IBL 设置
    bool useIBL = params.value("useIBL", true).toBool();
    skyboxDB->setUseImageBasedLighting(useIBL);

    // 自定义颜色（如果提供）
    if (params.contains("skyColorTop")) {
        QVariantMap colorMap = params.value("skyColorTop").toMap();
        skyboxDB->setSkyColorTop(Vector3(
            colorMap.value("r", 200).toFloat(),
            colorMap.value("g", 210).toFloat(),
            colorMap.value("b", 220).toFloat()
        ));
    }
    if (params.contains("skyColorBottom")) {
        QVariantMap colorMap = params.value("skyColorBottom").toMap();
        skyboxDB->setSkyColorBottom(Vector3(
            colorMap.value("r", 235).toFloat(),
            colorMap.value("g", 238).toFloat(),
            colorMap.value("b", 242).toFloat()
        ));
    }
    if (params.contains("groundColor")) {
        QVariantMap colorMap = params.value("groundColor").toMap();
        skyboxDB->setGroundColor(Vector3(
            colorMap.value("r", 180).toFloat(),
            colorMap.value("g", 175).toFloat(),
            colorMap.value("b", 170).toFloat()
        ));
    }

    LOG_INFO("SceneHandler::createSkybox - Created SkyboxDB with preset '{}', IBL={}",
             preset.toStdString(), useIBL);
    return true;
}

bool SceneHandler::createPrintBed(const QVariantMap& params) {
    // 幂等性检查：如果已存在 PrintBedDB，直接返回成功
    if (hasObjectOfType(static_cast<int>(TypeID::PRINT_BED_DB))) {
        LOG_DEBUG("SceneHandler::createPrintBed - PrintBed already exists, skipping creation");
        return true;
    }

    TransactionGuard guard("Create Print Bed");

    const std::string modelId =
        params.value("modelId").toString().toStdString();
    const std::string variantId =
        params.value("variantId").toString().toStdString();
    auto printBedTemplate = findPrintBedTemplate(
        DocumentManager::instance(), modelId, variantId);
    if (!printBedTemplate) {
        printBedTemplate = trans::TransDB::create<PrintBedTemplateDB>();
    }
    if (!printBedTemplate) {
        LOG_ERROR("SceneHandler::createPrintBed - Failed to create PrintBedTemplateDB");
        return false;
    }

    auto printBed = trans::TransDB::create<PrintBedDB>();
    if (!printBed) {
        LOG_ERROR("SceneHandler::createPrintBed - Failed to create PrintBedDB");
        return false;
    }
    printBed->setTemplateDBId(printBedTemplate->getDBInstanceID());

    // libslicer exposes x/y as the printable area's lower-left origin.
    float x = params.value("x", 0.0).toFloat();
    float y = params.value("y", 0.0).toFloat();
    float z = params.value("z", 0.0).toFloat();
    Vector3 origin(x, y, z);
    // PrintBed geometry is authored in the machine coordinate system. Keep the
    // generic Actor transform at identity or a non-zero printable-area origin
    // would be applied twice by ActorDBSync.
    printBed->setPosition(Vector3(0.0f, 0.0f, 0.0f));

    const QString printerModel = params.value("printerModel").toString();

    // 这些值由 libslicer 对当前机型 preset 解析后传入。200 mm 仅用于库初始化失败时
    // 保持场景仍可操作，不代表任何具体打印机配置。
    float width = params.value("width", 200.0).toFloat();
    float height = params.value("height", 200.0).toFloat();
    float thickness = params.value("thickness", 0.08).toFloat();
    if (width <= 0.0f || height <= 0.0f) {
        LOG_ERROR("SceneHandler::createPrintBed - Invalid printable area {}x{} from machine selection",
                  width, height);
        return false;
    }

    const float printHeight = params.value("printHeight", 200.0).toFloat();
    configurePrintBedTemplate(printBedTemplate, params, origin, width, height,
                              thickness, printHeight);

    // 设置显示属性
    bool showGrid = params.value("showGrid", true).toBool();
    bool showBounds = params.value("showBounds", false).toBool();
    float gridSpacing = params.value("gridSpacing", 10.0).toFloat();
    float opacity = params.value("opacity", 1.0).toFloat();

    printBed->setShowGrid(showGrid);
    printBed->setGridSpacing(gridSpacing);
    printBed->setShowBounds(showBounds);

    // 设置 Material 的 Opacity
    if (auto material = printBed->getMaterial()) {
        material->setOpacity(opacity);
    }

    // 相机自动适应
    if (auto cameraDB = CameraNavigationController::currentCamera()) {
        CameraNavigationController::fitScene(cameraDB);
    }

    LOG_INFO("SceneHandler::createPrintBed - Created PrintBedDB {}x{}x{}mm at ({},{},{}), printer='{}', profileSource='libslicer'",
             width, height, thickness, origin.x, origin.y, origin.z,
             printerModel.toStdString());
    return true;
}

void SceneHandler::syncPrintBedFromActiveMachine() {
    auto* document = DocumentManager::instance();
    if (!document || document->getDBInstancesByType(TypeID::PRINT_BED_DB).empty()) {
        // scene.init owns initial creation; a configuration selection may be
        // resolved before the workspace renderer exists.
        return;
    }
    syncPrintBed(SliceSettingsBridge::instance()->selectedMachine(), true);
}

bool SceneHandler::syncPrintBed(const QVariantMap& params, bool fitCamera) {
    auto* document = DocumentManager::instance();
    if (!document) {
        return false;
    }
    const auto printBeds = document->getDBInstancesByType(TypeID::PRINT_BED_DB);
    if (printBeds.empty()) {
        return createPrintBed(params);
    }
    auto printBed = std::dynamic_pointer_cast<PrintBedDB>(printBeds.front());
    if (!printBed) {
        LOG_ERROR("SceneHandler::syncPrintBed - Existing print bed has an invalid type");
        return false;
    }

    const float x = params.value("x", 0.0).toFloat();
    const float y = params.value("y", 0.0).toFloat();
    const float z = params.value("z", 0.0).toFloat();
    const float width = params.value("width").toFloat();
    const float height = params.value("height").toFloat();
    const float printHeight = params.value("printHeight").toFloat();
    const float thickness = params.value("thickness", printBed->getThickness()).toFloat();
    if (width <= 0.0f || height <= 0.0f || printHeight <= 0.0f) {
        LOG_ERROR("SceneHandler::syncPrintBed - Invalid machine volume {}x{}x{}",
                  width, height, printHeight);
        return false;
    }

    const Vector3 origin(x, y, z);
    const Polygon2 printableArea = printableAreaFromParams(params, x, y, width, height);
    const std::string bedVendor = params.value("bedVendor").toString().toStdString();
    const std::string printerModel = params.value("printerModel").toString().toStdString();
    const std::string bedModelPath = params.value("bedModelPath").toString().toStdString();
    const std::string bedTexturePath = params.value("bedTexturePath").toString().toStdString();
    const std::string modelId = params.value("modelId").toString().toStdString();
    const std::string variantId = params.value("variantId").toString().toStdString();
    const auto currentTemplate = printBed->getTemplateDB();
    const bool identityChanged = !currentTemplate ||
        currentTemplate->getModelId() != modelId ||
        currentTemplate->getVariantId() != variantId;

    const bool geometryChanged = !sameFloat(printBed->getWidth(), width) ||
        !sameFloat(printBed->getHeight(), height) ||
        !sameFloat(printBed->getThickness(), thickness) ||
        !sameFloat(printBed->getPrintHeight(), printHeight) ||
        printBed->getOrigin() != origin || printBed->getPrintableArea() != printableArea;
    const bool visualChanged = printBed->getBedVendor() != bedVendor ||
        printBed->getBedPrinterModel() != printerModel ||
        printBed->getBedModelPath() != bedModelPath ||
        printBed->getBedTexturePath() != bedTexturePath;

    if (!identityChanged && !geometryChanged && !visualChanged) {
        return true;
    }

    // The platform is a projection of the active slicing configuration, not
    // an independently undoable document edit.
    DerivedUpdateGuard derivedUpdate;
    auto printBedTemplate = currentTemplate;
    if (identityChanged) {
        printBedTemplate = findPrintBedTemplate(document, modelId, variantId);
    }
    if (!printBedTemplate) {
        printBedTemplate = trans::TransDB::create<PrintBedTemplateDB>();
        if (!printBedTemplate) {
            LOG_ERROR("SceneHandler::syncPrintBed - Failed to create PrintBedTemplateDB");
            return false;
        }
    }
    configurePrintBedTemplate(printBedTemplate, params, origin, width, height,
                              thickness, printHeight);
    if (printBed->getTemplateDBId() != printBedTemplate->getDBInstanceID()) {
        printBed->setTemplateDBId(printBedTemplate->getDBInstanceID());
    }
    if (geometryChanged && fitCamera) {
        if (auto cameraDB = CameraNavigationController::currentCamera()) {
            CameraNavigationController::fitScene(cameraDB);
        }
    }

    LOG_INFO("SceneHandler::syncPrintBed - Synced machine '{}' platform {}x{}x{} at origin ({},{},{}) resourcesChanged={}",
             printerModel, width, height, printHeight, x, y, z, visualChanged);
    return true;
}

bool SceneHandler::createPrinterModel(const QVariantMap& params) {
    // 获取文件路径
    QString filePath = params.value("filePath").toString();
    if (filePath.isEmpty()) {
        LOG_WARN("SceneHandler::createPrinterModel - No filePath provided, skipping printer model creation");
        return false;
    }

    // 检查文件是否存在
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        LOG_ERROR("SceneHandler::createPrinterModel - File not found: {}", filePath.toStdString());
        return false;
    }

    // 提取参数（在主线程中提取，避免异步访问 QVariantMap）
    QString modelName = params.value("name", fileInfo.baseName()).toString();
    Vector3 position = extractVector3(params, "position", Vector3(0, 0, 0));
    Vector3 color = extractVector3(params, "color", Vector3(0.25f, 0.26f, 0.30f));
    float zOffset = params.value("zOffset", -5.0f).toFloat();

    LOG_INFO("SceneHandler::createPrinterModel - Starting async load from: {}", filePath.toStdString());

    // 注册任务到 TaskStateNotifier
    QString taskId = TaskStateNotifier::instance()->beginTask(
        "printer_model_load",
        QString("正在加载打印机模型..."));

    // 使用 QtConcurrent 在后台线程加载 STL
    (void)QtConcurrent::run([filePath, modelName, position, color, zOffset, taskId]() {
        LOG_INFO("SceneHandler::createPrinterModel - Loading STL in background thread");

        // 进度更新辅助函数（跨线程安全）
        auto updateProgress = [](const QString& tid, float progress) {
            QMetaObject::invokeMethod(qApp, [tid, progress]() {
                TaskStateNotifier::instance()->updateProgress(tid, progress);
            });
        };

        // 10% - 开始加载
        updateProgress(taskId, 0.1f);

        // 后台线程：加载 STL 数据
        STLLoader loader;
        auto polyData = loader.loadSTLSyncPolyData(filePath);

        if (!polyData || polyData->GetNumberOfCells() <= 0) {
            LOG_ERROR("SceneHandler::createPrinterModel - Failed to load model: {}", filePath.toStdString());
            // 返回主线程清理任务
            QMetaObject::invokeMethod(qApp, [taskId]() {
                TaskStateNotifier::instance()->endTask(taskId);
            });
            return;
        }

        // 40% - STL 文件读取完成
        updateProgress(taskId, 0.4f);

        LOG_INFO("SceneHandler::createPrinterModel - Model loaded: {} vertices, {} triangles",
                 polyData->GetNumberOfPoints(), polyData->GetNumberOfCells());

        // Return to the main thread to publish the non-printable printer model.
        QMetaObject::invokeMethod(
            qApp,
            [polyData = std::move(polyData), modelName, filePath, position, color, zOffset, taskId]() mutable {
            // 50% - begin publishing the model graph
            TaskStateNotifier::instance()->updateProgress(taskId, 0.5f);

            TransactionGuard guard("Create Printer Model");
            ModelGraphUtil::InstanceCreateInfo instanceInfo;
            instanceInfo.printable = false;
            auto graph = ModelGraphUtil::createSinglePartObject(
                {modelName.toStdString(), filePath.toStdString(), "stl", false},
                polyData, instanceInfo);
            if (!graph || !graph.instance || graph.parts.empty()) {
                LOG_ERROR("SceneHandler::createPrinterModel - Failed to create model graph: {}",
                          graph.error);
                TaskStateNotifier::instance()->endTask(taskId);
                return;
            }
            const auto& model = graph.instance;
            const auto& part = graph.parts.front();

            // 60% - 网格数据设置完成
            TaskStateNotifier::instance()->updateProgress(taskId, 0.6f);

            // 设置位置
            Vector3 finalPosition = position;
            Vector3 boundingBoxMin = model->localBounds().min;
            float minZ = boundingBoxMin.z;
            if (minZ < 0) {
                finalPosition.z += -minZ;
            }
            finalPosition.z += zOffset;

            Transform transform = model->getTransform();
            transform.setPosition(finalPosition);
            model->setTransform(transform);

            // 70% - 基本属性设置完成
            TaskStateNotifier::instance()->updateProgress(taskId, 0.7f);

            // 设置为不可选择
            model->setPickable(false);
            model->setDragable(false);

            // 设置材质
            auto material = part->getMaterial();
            if (material) {
                material->setDiffuseColor(color);
                material->setColor(color);
                material->setRenderingMode(static_cast<int>(MaterialDB::RenderingMode::PBR));
                material->setMetallic(0.85f);
                material->setRoughness(0.35f);
                material->setDiffuse(0.8f);
            }

            // 80% - 材质设置完成
            TaskStateNotifier::instance()->updateProgress(taskId, 0.8f);

            LOG_INFO("SceneHandler::createPrinterModel - Created printer model '{}' at ({:.2f},{:.2f},{:.2f})",
                     modelName.toStdString(), finalPosition.x, finalPosition.y, finalPosition.z);

            // 相机看向原点 (0, 0, 0)
            if (auto cameraDB = CameraNavigationController::currentCamera()) {
                // 设置轨道中心为原点（相机围绕原点旋转）
                cameraDB->setOrbitCenter(Vector3(0, 0, 0));
                // 设置目标点为原点
                cameraDB->setTarget(Vector3(0, 0, 0));
                // 智能适应场景
                CameraNavigationController::fitScene(cameraDB);
                LOG_INFO("SceneHandler::createPrinterModel - Camera focused on origin (0, 0, 0)");
            }

            // 95% - 相机调整完成
            TaskStateNotifier::instance()->updateProgress(taskId, 0.95f);

            // 结束任务
            TaskStateNotifier::instance()->endTask(taskId);
        });
    });

    // 异步加载已启动，返回 true
    return true;
}

bool SceneHandler::createDefaultLights() {
    // 幂等性检查：如果已存在灯光，跳过创建
    if (hasObjectOfType(static_cast<int>(TypeID::LIGHT_DB))) {
        LOG_DEBUG("SceneHandler::createDefaultLights - Lights already exist, skipping creation");
        return true;
    }

    TransactionGuard guard("Create Default Lights");

    // OrcaSlicer gouraud_light.vs:
    // LIGHT_TOP_DIR=(-0.457, 0.457, 0.762), diffuse=0.48, specular=0.075
    auto keyLight = trans::TransDB::create<LightDB>();
    if (!keyLight) {
        LOG_ERROR("SceneHandler::createDefaultLights - Failed to create key light");
        return false;
    }
    keyLight->setName("Orca Top Light");
    keyLight->setLightTypeEnum(LightType::SCENE_LIGHT);
    keyLight->setColor(Vector3(1.0f, 1.0f, 1.0f));
    keyLight->setIntensity(0.78f);
    keyLight->setPosition(Vector3(-120.0f, 120.0f, 200.0f));
    keyLight->setFocalPoint(Vector3(0.0f, 0.0f, 0.0f)); // 指向原点
    keyLight->setEnabled(true);

    // LIGHT_FRONT_DIR=(0.699, 0.140, 0.699), diffuse=0.18
    auto fillLight = trans::TransDB::create<LightDB>();
    if (!fillLight) {
        LOG_ERROR("SceneHandler::createDefaultLights - Failed to create fill light");
        return false;
    }
    fillLight->setName("Orca Front Light");
    fillLight->setLightTypeEnum(LightType::SCENE_LIGHT);
    fillLight->setColor(Vector3(1.0f, 1.0f, 1.0f));
    fillLight->setIntensity(0.36f);
    fillLight->setPosition(Vector3(160.0f, 32.0f, 160.0f));
    fillLight->setFocalPoint(Vector3(0.0f, 0.0f, 0.0f));
    fillLight->setEnabled(true);

    LOG_INFO("SceneHandler::createDefaultLights - Created Orca-style top/front lights");
    return true;
}

// === 天空盒操作 ===

void SceneHandler::setSkyboxPreset(const QVariantMap& params) {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("SceneHandler::setSkyboxPreset - DocumentManager not available");
        return;
    }

    auto skyboxes = docManager->getDBInstancesByType(TypeID::SKYBOX_DB);
    if (skyboxes.empty()) {
        createSkybox(params);
        return;
    }

    auto skyboxDB = std::dynamic_pointer_cast<SkyboxDB>(skyboxes.front());
    if (!skyboxDB) {
        LOG_ERROR("SceneHandler::setSkyboxPreset - Invalid SkyboxDB");
        return;
    }

    QString preset = params.value("preset", "normal").toString();

    TransactionGuard guard("Set Skybox Preset");

    if (preset == "print-studio") {
        skyboxDB->applyPrintStudioPreset();
    } else if (preset == "studio") {
        skyboxDB->applyStudioPreset();
    } else if (preset == "minimal") {
        skyboxDB->applyMinimalPreset();
    } else if (preset == "editing") {
        skyboxDB->applyEditingEnvironment();
    } else if (preset == "technical") {
        skyboxDB->applyTechnicalEnvironment();
    } else {
        // 默认使用 Normal 环境（专业蓝灰）
        skyboxDB->applyNormalEnvironment();
    }

    LOG_INFO("SceneHandler::setSkyboxPreset - Applied preset '{}'", preset.toStdString());
}

void SceneHandler::setSkyboxColors(const QVariantMap& params) {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("SceneHandler::setSkyboxColors - DocumentManager not available");
        return;
    }

    auto skyboxes = docManager->getDBInstancesByType(TypeID::SKYBOX_DB);
    if (skyboxes.empty()) {
        LOG_WARN("SceneHandler::setSkyboxColors - No SkyboxDB found");
        return;
    }

    auto skyboxDB = std::dynamic_pointer_cast<SkyboxDB>(skyboxes.front());
    if (!skyboxDB) {
        LOG_ERROR("SceneHandler::setSkyboxColors - Invalid SkyboxDB");
        return;
    }

    TransactionGuard guard("Set Skybox Colors");

    if (params.contains("skyColorTop")) {
        Vector3 color = extractVector3(params, "skyColorTop", Vector3(200, 210, 220));
        skyboxDB->setSkyColorTop(color);
    }
    if (params.contains("skyColorBottom")) {
        Vector3 color = extractVector3(params, "skyColorBottom", Vector3(235, 238, 242));
        skyboxDB->setSkyColorBottom(color);
    }
    if (params.contains("groundColor")) {
        Vector3 color = extractVector3(params, "groundColor", Vector3(180, 175, 170));
        skyboxDB->setGroundColor(color);
    }

    LOG_INFO("SceneHandler::setSkyboxColors - Colors updated");
}

// === 辅助方法 ===

bool SceneHandler::hasObjectOfType(int typeId) const {
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        return false;
    }

    auto objects = docManager->getDBInstancesByType(static_cast<TypeID>(typeId));
    return !objects.empty();
}

Vector3 SceneHandler::extractVector3(const QVariantMap& params, const QString& key, const Vector3& defaultValue) {
    if (!params.contains(key)) {
        return defaultValue;
    }

    QVariant value = params.value(key);

    // 支持 QVariantMap 格式
    if (value.canConvert<QVariantMap>()) {
        QVariantMap map = value.toMap();

        // 支持 x/y/z 格式（用于 position 等）
        if (map.contains("x") || map.contains("y") || map.contains("z")) {
            return Vector3(
                map.value("x", defaultValue.x).toFloat(),
                map.value("y", defaultValue.y).toFloat(),
                map.value("z", defaultValue.z).toFloat()
            );
        }

        // 支持 r/g/b 格式（用于 color 等）
        return Vector3(
            map.value("r", defaultValue.x).toFloat(),
            map.value("g", defaultValue.y).toFloat(),
            map.value("b", defaultValue.z).toFloat()
        );
    }

    return defaultValue;
}
