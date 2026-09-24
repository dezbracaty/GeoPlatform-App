#include "SlicingHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    SlicingHandler,
    "model.slice",
    "model.sliceAll",
    "slicing.reslice",
    "support.manual.slice",
    "model.importGCode",
    "slicing.clearAllSliceData",
    "slicing.exit")
#include "TransactionManager.hpp"
#include <DocumentManager.hpp>
#include "CameraDB.hpp"
#include "EnvironmentManager.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include "ActionManager.hpp"
#include "SlicingPreviewBridge.hpp"
#include "SlicingPlaybackHandler.hpp"
#include "SlicingHandlerBridge.hpp"
#include "SliceSettingsBridge.hpp"
#include "SlicingBackend.hpp"
#include "SliceSessionBuilder.hpp"
#include "MeshExporter.hpp"
#include "ToolpathPreviewDB.hpp"
#include "ToolpathPreviewRelations.hpp"
#include "LibSlicerToolpathAdapter.hpp"
#include "ManualSupportDB.hpp"
#include "ManualSupportRelations.hpp"
#include <ModelInstanceDB.hpp>
#include <PrintBedDB.hpp>
#include "Foundation/Log.h"
#include "MenuData.hpp"
#include "AIDescriptorHelper.hpp"
#include "ScopedTransactionContextMetadata.hpp"
#include "NotificationManager.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <chrono>
#include <algorithm>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace {

vtkSmartPointer<vtkPolyData> vtk_poly_data_from_indexed_mesh(
    const IndexedTriangleMesh& mesh) {
    if (!mesh.hasValidTopology()) return {};
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.positions.size()));
    for (std::size_t index = 0; index < mesh.positions.size(); ++index) {
        const Vector3& position = mesh.positions[index];
        points->SetPoint(
            static_cast<vtkIdType>(index), position.x, position.y, position.z);
    }
    auto polys = vtkSmartPointer<vtkCellArray>::New();
    for (std::size_t offset = 0; offset < mesh.indices.size(); offset += 3) {
        const vtkIdType triangle[3]{
            static_cast<vtkIdType>(mesh.indices[offset]),
            static_cast<vtkIdType>(mesh.indices[offset + 1]),
            static_cast<vtkIdType>(mesh.indices[offset + 2])};
        polys->InsertNextCell(3, triangle);
    }
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(polys);
    return polyData;
}

} // namespace

SlicingHandler::SlicingHandler(QObject* parent)
    : StandardActionHandler(parent)
    , m_playbackHandler(std::make_unique<SlicingPlaybackHandler>())
    , m_taskWatcher(std::make_unique<QFutureWatcher<void>>()) {
    auto* bridge = SlicingHandlerBridge::instance();
    connect(bridge, &SlicingHandlerBridge::clearDebugActorsRequested,
            this, &SlicingHandler::clearDebugActors,
            Qt::UniqueConnection);
}

SlicingHandler::~SlicingHandler() {
    if (m_taskWatcher && m_taskWatcher->isRunning()) {
        m_taskWatcher->cancel();
        m_taskWatcher->waitForFinished();
    }
    cleanupUnownedTemporaryPrintOutputs();
}

const QHash<QString, QVariantMap>& SlicingHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = [] {
        QVariantMap descriptor = makeDescriptor(
            "model.slice",
            "Slice Model",
            "Slice one imported model using the current validated printer, process, and filament configuration.",
            "write",
            makeInputSchema({{"modelId", "string", true}}),
            {"slice", "model", "gcode"});
        descriptor.insert("async", true);
        return QHash<QString, QVariantMap>{{"model.slice", descriptor}};
    }();
    return table;
}

void SlicingHandler::onEnter(std::shared_ptr<ActionContext> context) {
    // 调用基类实现
    StandardActionHandler::onEnter(context);

    QString actionCode = getActionCode();
    LOG_INFO("SlicingHandler: onEnter with actionCode={}", actionCode.toStdString());

    // 根据不同的 actionCode 执行不同操作
    if (actionCode == "model.slice" ||
        actionCode == "model.sliceAll" ||
        actionCode == "slicing.reslice" ||
        actionCode == "support.manual.slice") {
        if (m_taskWatcher && m_taskWatcher->isRunning()) {
            const QString message = QStringLiteral("A slicing task is already running");
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, message);
            }
            LOG_WARN("SlicingHandler: {}", message.toStdString());
            return;
        }

        // 切片模式
        m_mode = Mode::Slice;
        m_state = State::Preparing;
        m_sliceAllMode = false;
        m_useManualSupportMeshForSlice = false;
        m_slicePrintBedId = INVALID_DB_ID;
        m_sliceSourceModelId = INVALID_DB_ID;
        m_sliceSourceScope =
            GPlatform::ToolpathPreviewSourceScope::ImportedGCode;

        QString targetError;
        if (actionCode == "slicing.reslice") {
            if (!resolveResliceTargets(m_pendingModelIds, &targetError)) {
                if (m_context) {
                    m_context->setError(
                        ActionErrorCode::TargetRequired, targetError);
                }
                LOG_ERROR("SlicingHandler: {}", targetError.toStdString());
                return;
            }
        } else {
            m_sliceAllMode = (actionCode == "model.sliceAll");
            m_useManualSupportMeshForSlice =
                (actionCode == "support.manual.slice");
            m_pendingModelIds = collectSliceTargets(actionCode);
        }
        m_sliceAllTotalCount = m_pendingModelIds.size();

        if (m_pendingModelIds.isEmpty()) {
            QString message;
            if (m_sliceAllMode) {
                message = QString("No valid model found for sliceAll");
            } else if (!targetError.isEmpty()) {
                message = targetError;
            } else {
                message = QString("No modelId provided");
            }
            if (m_context) {
                m_context->setError(ActionErrorCode::TargetRequired, message);
            }
            LOG_ERROR("SlicingHandler: {}", message.toStdString());
            return;
        }

        // 先切换到 slicing 环境
        EnvironmentSwitchHandler::switchEnvironment("slicing");

        bool started = false;
        QString startError;
        if (m_sliceAllMode) {
            const QStringList plateModelIds = m_pendingModelIds;
            m_pendingModelIds.clear();
            started = startSliceTaskForPlate(plateModelIds, &startError);
        } else {
            while (!m_pendingModelIds.isEmpty() && !started) {
                started = startSliceTaskForModel(m_pendingModelIds.takeFirst(), &startError);
            }
        }

        if (!started) {
            const QString message = startError.isEmpty()
                ? QString("Failed to start slice task")
                : startError;
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, message);
            }
            LOG_ERROR("SlicingHandler: {}", message.toStdString());
            EnvironmentSwitchHandler::switchEnvironment("normal");
        }

    } else if (actionCode == "model.importGCode") {
        // GCode 导入模式：调用 GCodeImportHandler
        m_mode = Mode::Import;
        m_state = State::Ready;  // 导入由 GCodeImportHandler 处理，我们只需要切换环境

        // 获取文件路径
        auto filePath = getParam("filePath");
        if (!filePath.isValid() || filePath.toString().isEmpty()) {
            filePath = getParam("filepath");
        }

        if (!filePath.isValid() || filePath.toString().isEmpty()) {
            if (m_context) {
                m_context->setError(ActionErrorCode::InvalidParams, "No file path provided");
            }
            LOG_ERROR("SlicingHandler: No file path provided for GCode import");
            return;
        }

        m_importFilePath = filePath.toString();
        LOG_INFO("SlicingHandler: Import mode for GCode: {}", m_importFilePath.toStdString());

        // 检查文件
        QFileInfo fileInfo(m_importFilePath);
        if (!fileInfo.exists()) {
            if (m_context) {
                m_context->setError(ActionErrorCode::TargetNotFound, QString("File not found: %1").arg(m_importFilePath));
            }
            LOG_ERROR("SlicingHandler: GCode file not found: {}", m_importFilePath.toStdString());
            return;
        }

        // 先切换到 slicing 环境
        EnvironmentSwitchHandler::switchEnvironment("slicing");

        // 发送开始信号
        emit importStarted(m_importFilePath);

        // 调用 GCodeImportHandler 执行导入
        QVariantMap importParams;
        importParams["filePath"] = m_importFilePath;
        importParams["confirm"] = true;

        bool importTriggered = ActionManager::getInstance()->triggerAction("load_gcode_file", importParams);
        LOG_INFO("SlicingHandler: Triggered load_gcode_file, result: {}", importTriggered ? "success" : "failed");

        if (!importTriggered) {
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, "Failed to trigger GCode import handler");
            }
        }

    } else if (actionCode == "slicing.exit") {
        // 退出 slicing 环境
        LOG_INFO("SlicingHandler: Exit slicing environment");

        // 1. 如果有后台切片任务正在运行，取消并等待完成
        if (m_taskWatcher && m_taskWatcher->isRunning()) {
            LOG_INFO("SlicingHandler: Cancelling running slice task...");
            m_taskWatcher->cancel();
            m_taskWatcher->waitForFinished();
            LOG_INFO("SlicingHandler: Slice task cancelled");

            // 结束 TaskStateNotifier 任务
            if (!m_taskId.isEmpty()) {
                TaskStateNotifier::instance()->endTask(m_taskId);
                m_taskId.clear();
            }
        }

        // 2. 停止切片播放（如果有）
        SlicingPreviewBridge::instance()->stop();

        // 3. 清理切片环境和可视化
        cleanupSlicingEnvironment();

        // 4. 切换回 normal 环境
        EnvironmentSwitchHandler::switchEnvironment("normal");

        // 5. 重置状态
        m_state = State::Inactive;
        m_mode = Mode::Slice;
        m_sliceAllMode = false;
        m_useManualSupportMeshForSlice = false;
        m_sliceAllTotalCount = 0;
        m_pendingModelIds.clear();

        // 标记为非活跃，ActionManager 会自动检测并清理
        setActive(false);
    } else if (actionCode == "slicing.clearAllSliceData") {
        LOG_INFO("SlicingHandler: Clear all slice data");
        clearAllSliceData();
    } else {
        LOG_WARN("SlicingHandler: Unknown actionCode: {}", actionCode.toStdString());
    }
}

QStringList SlicingHandler::collectSliceTargets(const QString& actionCode) const {
    QStringList targets;

    if (actionCode == "model.slice" || actionCode == "support.manual.slice") {
        QString modelId = getParam("modelId").toString();
        if (!modelId.isEmpty()) {
            targets.append(modelId);
        }
        return targets;
    }

    return collectPlateSliceTargets(defaultPrintBedId());
}

DBInstanceID SlicingHandler::defaultPrintBedId() const {
    auto* document = DocumentManager::instance();
    if (!document) return INVALID_DB_ID;
    const auto beds = document->getDBInstancesByType(TypeID::PRINT_BED_DB);
    DBInstanceID result = INVALID_DB_ID;
    for (const auto& bed : beds) {
        if (!bed) continue;
        const DBInstanceID candidate = bed->getDBInstanceID();
        if (!result.isValid() ||
            candidate.getValue() < result.getValue()) {
            result = candidate;
        }
    }
    return result;
}

QStringList SlicingHandler::collectPlateSliceTargets(
    DBInstanceID printBedId) const {
    QStringList targets;
    auto* document = DocumentManager::instance();
    if (!document || !printBedId.isValid()) return targets;

    const auto models = document->getDBInstancesByType(
        TypeID::MODEL_INSTANCE_DB);
    std::vector<DBInstanceID> sortedIds;
    sortedIds.reserve(models.size());
    for (const auto& object : models) {
        const auto instance =
            std::dynamic_pointer_cast<ModelInstanceDB>(object);
        if (!instance || !instance->isValid() ||
            !instance->getPrintable() ||
            instance->getParentPrintBedDBId() != printBedId) {
            continue;
        }
        sortedIds.push_back(instance->getDBInstanceID());
    }
    std::sort(
        sortedIds.begin(), sortedIds.end(),
        [](DBInstanceID lhs, DBInstanceID rhs) {
            return lhs.getValue() < rhs.getValue();
        });
    for (const DBInstanceID id : sortedIds) {
        targets.push_back(QString::fromStdString(id.toString()));
    }
    return targets;
}

bool SlicingHandler::resolveResliceTargets(
    QStringList& targets, QString* errorMessage) {
    targets.clear();
    const auto preview = SlicingPreviewBridge::instance()
        ? SlicingPreviewBridge::instance()->currentToolpathPreview()
        : nullptr;
    auto* document = DocumentManager::instance();
    if (!preview || !document) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active toolpath preview");
        }
        return false;
    }

    m_sliceSourceScope = preview->getSourceScope();
    if (m_sliceSourceScope ==
        GPlatform::ToolpathPreviewSourceScope::ImportedGCode) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Imported GCode cannot be resliced");
        }
        return false;
    }

    if (m_sliceSourceScope ==
        GPlatform::ToolpathPreviewSourceScope::Plate) {
        m_slicePrintBedId = document->getOwner(
            preview->getDBInstanceID());
        m_sliceAllMode = true;
        targets = collectPlateSliceTargets(m_slicePrintBedId);
        if (targets.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "No printable model remains on the preview print bed");
            }
            return false;
        }
        LOG_INFO(
            "SlicingHandler: resolved reslice scope=Plate bed={} models={}",
            m_slicePrintBedId.getValue(), targets.size());
        return true;
    }

    m_sliceSourceModelId = preview->getSourceModelDBId();
    const auto instance = document->getDB<ModelInstanceDB>(
        m_sliceSourceModelId);
    if (!instance || !instance->isValid() ||
        !instance->getPrintable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The source model for this preview is unavailable");
        }
        return false;
    }
    m_slicePrintBedId = instance->getParentPrintBedDBId();
    m_useManualSupportMeshForSlice =
        SlicingHandlerBridge::instance()
            ->manualSupportHasCommittedMesh(
                m_sliceSourceModelId.getValue());
    targets.push_back(QString::fromStdString(
        m_sliceSourceModelId.toString()));
    LOG_INFO(
        "SlicingHandler: resolved reslice scope=SingleModel model={} bed={} manualSupport={}",
        m_sliceSourceModelId.getValue(),
        m_slicePrintBedId.getValue(),
        m_useManualSupportMeshForSlice);
    return true;
}

bool SlicingHandler::prepareModelForSlice(const QString& modelId, QString* errorMessage) {
    m_modelId = modelId;
    if (m_modelId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "No modelId provided";
        }
        return false;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        if (errorMessage) {
            *errorMessage = "DocumentManager not available";
        }
        return false;
    }

    auto dbObj = docManager->getDBInstance(DBInstanceID(m_modelId));
    m_currentModelInstance =
        std::dynamic_pointer_cast<ModelInstanceDB>(dbObj);
    if (!m_currentModelInstance) {
        if (errorMessage) {
            *errorMessage = QString("Printable model not found: %1").arg(m_modelId);
        }
        return false;
    }

    if (!m_currentModelInstance->isValid() ||
        !m_currentModelInstance->getPrintable()) {
        if (errorMessage) {
            *errorMessage = "Model has no valid mesh data";
        }
        return false;
    }

    m_modelName = QString::fromStdString(dbObj->getDisplayName());
    if (m_modelName.isEmpty()) {
        m_modelName = QString("Model_%1").arg(m_modelId);
    }

    return true;
}

bool SlicingHandler::startSliceTaskForModel(const QString& modelId, QString* errorMessage) {
    m_taskStartTime = std::chrono::steady_clock::now();
    m_sliceSessionCapture.reset();
    m_sliceSession.reset();
    QString prepareError;
    if (!prepareModelForSlice(modelId, &prepareError)) {
        LOG_ERROR("SlicingHandler: {}", prepareError.toStdString());
        if (errorMessage) {
            *errorMessage = prepareError;
        }
        return false;
    }

    m_sliceSourceScope =
        GPlatform::ToolpathPreviewSourceScope::SingleModel;
    m_sliceSourceModelId = m_currentModelInstance->getDBInstanceID();
    m_slicePrintBedId = m_currentModelInstance->getParentPrintBedDBId();
    if (!m_slicePrintBedId.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The model is not assigned to a print bed");
        }
        return false;
    }

    m_supportMeshStlPath.clear();
    m_needCleanupSupportMeshStl = false;
    if (m_useManualSupportMeshForSlice) {
        m_supportMeshStlPath = getOrExportSupportMeshSTL();
        if (m_supportMeshStlPath.isEmpty()) {
            const QString message = QString("No committed manual support mesh found for model %1")
                .arg(m_modelName);
            LOG_ERROR("SlicingHandler: {}", message.toStdString());
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }
    }

    LOG_INFO("SlicingHandler: Slice mode for model '{}' (ID: {})",
             m_modelName.toStdString(), m_modelId.toStdString());

    SliceSessionBuilder::Request sessionRequest;
    sessionRequest.modelIds = {DBInstanceID(m_modelId)};
    sessionRequest.supportTargetModelId = DBInstanceID(m_modelId);
    if (!m_supportMeshStlPath.isEmpty()) {
        sessionRequest.supportEnforcerPaths = {m_supportMeshStlPath.toStdString()};
    }
    const auto captureStart = std::chrono::steady_clock::now();
    auto captured = SliceSessionBuilder::capture(sessionRequest);
    const auto captureMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - captureStart).count();
    if (!captured) {
        const QString message = QString::fromStdString(captured.error);
        if (m_needCleanupSupportMeshStl && !m_supportMeshStlPath.isEmpty()) {
            QFile::remove(m_supportMeshStlPath);
            m_supportMeshStlPath.clear();
            m_needCleanupSupportMeshStl = false;
        }
        LOG_ERROR("SlicingHandler: {}", message.toStdString());
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }
    m_sliceSessionCapture = std::move(captured.request);
    LOG_INFO("SlicingHandler: captured immutable slice sources models={} documentMs={}",
             sessionRequest.modelIds.size(), captureMs);

    QString taskDescription = m_useManualSupportMeshForSlice
        ? QString("正在使用手动支撑切片 %1...").arg(m_modelName)
        : QString("正在切片 %1...").arg(m_modelName);
    if (m_sliceAllMode) {
        int processedCount = qMax(1, m_sliceAllTotalCount - m_pendingModelIds.size());
        int totalCount = qMax(processedCount, m_sliceAllTotalCount);
        taskDescription = QString("正在切片 %1 (%2/%3)...")
            .arg(m_modelName)
            .arg(processedCount)
            .arg(totalCount);
    }

    m_taskId = TaskStateNotifier::instance()->beginTask(
        QString("slice_%1_%2").arg(m_modelId).arg(QDateTime::currentMSecsSinceEpoch()),
        taskDescription
    );
    TaskStateNotifier::instance()->updateProgress(m_taskId, 0.0f);

    emit sliceProgress(0, QString("Preparing to slice %1").arg(m_modelName));

    m_state = State::Processing;

    QFuture<void> future = QtConcurrent::run([this]() {
        performSlice();
    });
    m_taskWatcher->setFuture(future);

    connect(m_taskWatcher.get(), &QFutureWatcher<void>::finished,
            this, &SlicingHandler::onTaskCompleted,
            Qt::UniqueConnection);

    return true;
}

bool SlicingHandler::startSliceTaskForPlate(const QStringList& modelIds, QString* errorMessage) {
    m_taskStartTime = std::chrono::steady_clock::now();
    m_sliceSessionCapture.reset();
    m_sliceSession.reset();
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        if (errorMessage)
            *errorMessage = QStringLiteral("DocumentManager not available");
        return false;
    }

    m_plateInstances.clear();
    std::vector<DBInstanceID> validModelIds;
    DBInstanceID printBedId = m_slicePrintBedId;
    for (const QString& modelId : modelIds) {
        auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(
            docManager->getDBInstance(DBInstanceID(modelId)));
        if (!instance || !instance->isValid() || !instance->getPrintable()) {
            LOG_WARN("SlicingHandler: skipping invalid plate model {}", modelId.toStdString());
            continue;
        }
        const DBInstanceID instanceBedId =
            instance->getParentPrintBedDBId();
        if (!instanceBedId.isValid()) {
            LOG_WARN(
                "SlicingHandler: skipping plate model {} without a print bed",
                modelId.toStdString());
            continue;
        }
        if (!printBedId.isValid()) {
            printBedId = instanceBedId;
        }
        if (instanceBedId != printBedId) {
            LOG_WARN(
                "SlicingHandler: skipping model {} from another print bed {}",
                modelId.toStdString(), instanceBedId.getValue());
            continue;
        }
        validModelIds.push_back(DBInstanceID(modelId));
        m_plateInstances.push_back(std::move(instance));
    }
    if (validModelIds.empty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No valid model remains on the plate");
        return false;
    }

    m_sliceSourceScope = GPlatform::ToolpathPreviewSourceScope::Plate;
    m_sliceSourceModelId = INVALID_DB_ID;
    m_slicePrintBedId = printBedId;

    const auto firstModel = docManager->getDBInstance(validModelIds.front());
    m_currentModelInstance =
        std::dynamic_pointer_cast<ModelInstanceDB>(firstModel);
    m_modelId = QString::fromStdString(validModelIds.front().toString());
    QString firstModelName = QString::fromStdString(firstModel->getDisplayName()).trimmed();
    if (firstModelName.isEmpty()) {
        firstModelName = QStringLiteral("Model_%1").arg(m_modelId);
    } else {
        const QString baseName = QFileInfo(firstModelName).completeBaseName().trimmed();
        if (!baseName.isEmpty()) {
            firstModelName = baseName;
        }
    }
    m_modelName = validModelIds.size() == 1
        ? firstModelName
        : QStringLiteral("%1 and %2 more").arg(firstModelName).arg(validModelIds.size() - 1);

    SliceSessionBuilder::Request sessionRequest;
    sessionRequest.modelIds = validModelIds;
    const auto captureStart = std::chrono::steady_clock::now();
    auto captured = SliceSessionBuilder::capture(sessionRequest);
    const auto captureMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - captureStart).count();
    if (!captured) {
        if (errorMessage) *errorMessage = QString::fromStdString(captured.error);
        return false;
    }
    m_sliceSessionCapture = std::move(captured.request);
    LOG_INFO("SlicingHandler: captured immutable plate slice sources models={} documentMs={}",
             sessionRequest.modelIds.size(), captureMs);

    const QString taskDescription = QStringLiteral("正在整板切片 %1 个模型...")
        .arg(validModelIds.size());
    m_taskId = TaskStateNotifier::instance()->beginTask(
        QString("slice_plate_%1").arg(QDateTime::currentMSecsSinceEpoch()),
        taskDescription);
    TaskStateNotifier::instance()->updateProgress(m_taskId, 0.0f);
    emit sliceProgress(0, taskDescription);

    m_state = State::Processing;
    QFuture<void> future = QtConcurrent::run([this]() { performSlice(); });
    m_taskWatcher->setFuture(future);
    connect(m_taskWatcher.get(), &QFutureWatcher<void>::finished,
            this, &SlicingHandler::onTaskCompleted, Qt::UniqueConnection);
    LOG_INFO("SlicingHandler: starting one plate slice with {} models", validModelIds.size());
    return true;
}

void SlicingHandler::onExit() {
    LOG_INFO("SlicingHandler: onExit");

    // 等待后台任务完成
    if (m_taskWatcher && m_taskWatcher->isRunning()) {
        LOG_INFO("SlicingHandler: Waiting for background task to complete...");
        m_taskWatcher->waitForFinished();
    }

    // 清理资源
    m_currentModelInstance.reset();
    m_plateInstances.clear();
    m_sliceSessionCapture.reset();
    m_sliceSession.reset();
    if (m_needCleanupSupportMeshStl && !m_supportMeshStlPath.isEmpty()) {
        QFile::remove(m_supportMeshStlPath);
    }
    m_supportMeshStlPath.clear();
    m_needCleanupSupportMeshStl = false;
    cleanupUnownedTemporaryPrintOutputs();
    m_gcodePath.clear();
    m_gcode3mfPath.clear();
    m_state = State::Inactive;
    m_sliceAllMode = false;
    m_useManualSupportMeshForSlice = false;
    m_sliceAllTotalCount = 0;
    m_pendingModelIds.clear();

    // 调用基类实现
    StandardActionHandler::onExit();
}

void SlicingHandler::performSlice() {
    LOG_INFO("[SLICING] performSlice started for: {}", m_modelName.toStdString());
    m_toolpathPreview.reset();
    cleanupUnownedTemporaryPrintOutputs();

    auto cleanupTemporaryInputs = [this]() {
        if (m_needCleanupSupportMeshStl && !m_supportMeshStlPath.isEmpty()) {
            QFile::remove(m_supportMeshStlPath);
            LOG_DEBUG("SlicingHandler: Cleaned up temporary support STL: {}",
                      m_supportMeshStlPath.toStdString());
            m_needCleanupSupportMeshStl = false;
        }
    };

    // 辅助函数：在主线程中更新进度
    auto updateProgress = [this](float progress, const QString& description = QString()) {
        QMetaObject::invokeMethod(qApp, [this, progress, description]() {
            if (!m_taskId.isEmpty()) {
                TaskStateNotifier::instance()->updateProgress(m_taskId, progress);
                if (!description.isEmpty()) {
                    TaskStateNotifier::instance()->updateDescription(m_taskId, description);
                }
            }
        });
    };

    updateProgress(0.1f, QString("正在生成切片几何快照..."));
    auto captured = m_sliceSessionCapture;
    if (!captured) {
        m_success = false;
        m_errorMessage = "Slice source snapshot is unavailable";
        cleanupTemporaryInputs();
        LOG_ERROR("SlicingHandler: {}", m_errorMessage.toStdString());
        return;
    }
    const auto snapshotStart = std::chrono::steady_clock::now();
    auto built = SliceSessionBuilder::build(*captured, [this]() {
        return m_taskWatcher && m_taskWatcher->isCanceled();
    });
    if (!built) {
        m_success = false;
        m_errorMessage = QString::fromStdString(built.error);
        cleanupTemporaryInputs();
        LOG_ERROR("SlicingHandler: {}", m_errorMessage.toStdString());
        return;
    }
    m_sliceSession = std::move(built.session);
    // The materialized session now owns all slicer arrays and configuration.
    // Release the source lease before the long slice phase.
    m_sliceSessionCapture.reset();
    captured.reset();
    const auto snapshotMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - snapshotStart).count();
    const auto session = m_sliceSession;
    LOG_INFO("SlicingHandler: materialized immutable session={} profileRevision={} models={} in workerMs={}",
             session->id, session->profileRevision, session->sources.size(), snapshotMs);

    updateProgress(0.2f, QString("正在切片 %1...").arg(m_modelName));
    libslicer::SliceCallbacks callbacks;
    callbacks.progress = [&updateProgress](float progress, std::string_view stage) {
        // libslicer reports progress relative to its own slicing phase. Map it
        // into the whole task range so preparation (0-30%), slicing (30-90%)
        // and preview creation (90-100%) remain monotonic in the shared UI.
        const float taskProgress = 0.3f + std::clamp(progress, 0.0f, 1.0f) * 0.6f;
        updateProgress(taskProgress,
                       QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size())));
    };
    callbacks.is_cancelled = [this]() {
        return m_taskWatcher && m_taskWatcher->isCanceled();
    };

    updateProgress(0.3f, QString("执行 libslicer 切片..."));
    const auto result = GPlatform::SlicingBackend::instance().slice(*session, callbacks);
    m_success = result.success;
    m_gcodePath = QString::fromStdString(result.output.path);
    m_gcodeIsLibraryTemporary = result.success &&
        result.output.ownership == libslicer::OutputArtifactOwnership::LibraryTemporary;
    m_gcode3mfPath = QString::fromStdString(result.gcode_3mf.path);
    m_gcode3mfIsLibraryTemporary = result.success &&
        result.gcode_3mf.ownership == libslicer::OutputArtifactOwnership::LibraryTemporary;
    m_totalTime = result.summary.estimated_time_seconds;
    m_filamentUsed = result.summary.filament_used_mm / 1000.0;
    m_layerCount = static_cast<int>(result.summary.layer_count);
    m_toolpathPreview = result.preview;
    if (!result.belt_support_debug.path.empty()) {
        LOG_INFO("SlicingHandler: Belt support audit saved to {}",
                 result.belt_support_debug.path);
    }

    if (m_toolpathPreview) {
        for (const auto& feature : m_toolpathPreview->statistics.features) {
            if (feature.role == libslicer::ToolpathExtrusionRole::SparseInfill) {
                LOG_INFO("TOOLPATH sparseInfill paths={} renderSegments={} lengthMm={:.3f} volumeMm3={:.3f}",
                         feature.path_count,
                         feature.render_segment_count,
                         feature.length_mm,
                         feature.extrusion_volume_mm3);
                break;
            }
        }
    }

    m_sliceWarnings.clear();
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.warning) {
            LOG_WARN("Slicing warning [{}]: {}", diagnostic.code, diagnostic.message);
            m_sliceWarnings.push_back(QString::fromStdString(diagnostic.message));
        } else if (diagnostic.code == "fiber_infill_summary") {
            LOG_INFO("{}", diagnostic.message);
        }
    }
    if (!m_success) {
        QStringList messages;
        for (const auto& diagnostic : result.diagnostics) {
            if (!diagnostic.warning) {
                messages.push_back(QString::fromStdString(diagnostic.message));
            }
        }
        m_errorMessage = result.cancelled
            ? QStringLiteral("Slicing cancelled")
            : messages.join(QStringLiteral("; "));
        if (m_errorMessage.isEmpty()) {
            m_errorMessage = QStringLiteral("libslicer failed without a diagnostic");
        }
    } else {
        m_errorMessage.clear();
    }

    cleanupTemporaryInputs();

    if (m_success) {
        updateProgress(0.9f, QString("创建可视化..."));
        LOG_INFO("SlicingHandler: Slicing completed successfully");
        LOG_INFO("  GCode: {}", m_gcodePath.toStdString());
        LOG_INFO("  GCode 3MF: {}", m_gcode3mfPath.toStdString());
        LOG_INFO("  Time: {:.1f} min, Filament: {:.2f} m, Layers: {}",
                 m_totalTime / 60.0, m_filamentUsed, m_layerCount);
    } else {
        LOG_ERROR("SlicingHandler: Slicing failed: {}", m_errorMessage.toStdString());
    }
}

void SlicingHandler::onTaskCompleted() {
    const bool trackedInvocation = m_context &&
        !m_context->getInvocationId().trimmed().isEmpty();
    const ScopedTransactionContextMetadata transactionScope(
        getActionCode(),
        m_context ? m_context->getInvocationMetadata() : QVariantMap{},
        trackedInvocation);

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - m_taskStartTime).count();
    LOG_INFO("[SLICING TIMING] Backend ready task={} elapsed={}ms", m_taskId.toStdString(), duration);

    if (m_state == State::Processing) {
        m_state = State::Ready;
    }

    // 只处理切片模式（导入由 GCodeImportHandler 处理）
    if (m_mode == Mode::Slice) {
        if (m_success) {
            // 在主线程中创建可视化
            createToolpathPreview();
        }

        // Preview creation is part of slicing success. It may fail even after
        // libslicer produced a valid GCode file.
        if (m_success) {
            SliceSettingsBridge::instance()->markCurrentAsSliced();
            EnvironmentSwitchHandler::switchEnvironment("preview");

            if (!m_taskId.isEmpty()) {
                TaskStateNotifier::instance()->updateProgress(m_taskId, 1.0f);
                TaskStateNotifier::instance()->updateDescription(m_taskId, "切片完成");
            }

            const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_taskStartTime).count();
            LOG_INFO("[SLICING TIMING] Preview ready task={} total={}ms previewAndSwitch={}ms",
                     m_taskId.toStdString(), totalMs, totalMs - duration);
            emit sliceProgress(100, "Slicing completed");
            if (!m_sliceWarnings.isEmpty())
                NotificationManager::instance()->showWarning(
                    tr("Slicing warnings"), m_sliceWarnings.join(QStringLiteral("\n")));
            emit sliceCompleted(m_gcodePath, true);
        } else {
            EnvironmentSwitchHandler::switchEnvironment("normal");
            emit sliceCompleted("", false, m_errorMessage);
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, m_errorMessage);
            }
        }

        QVariantMap completionResult;
        completionResult.insert("modelId", m_modelId);
        completionResult.insert("gcodePath", m_success ? m_gcodePath : QString());
        completionResult.insert("estimatedTimeSeconds", m_totalTime);
        completionResult.insert("filamentUsedMeters", m_filamentUsed);
        completionResult.insert("layerCount", m_layerCount);
        completeInvocation(m_context, completionResult);

        if (!m_taskId.isEmpty()) {
            TaskStateNotifier::instance()->endTask(m_taskId);
            m_taskId.clear();
        }
        if (!m_success) {
            NotificationManager::instance()->showSliceConfigurationError(
                QString(), m_errorMessage, QString());
        }

        m_sliceAllMode = false;
        m_useManualSupportMeshForSlice = false;
        m_sliceAllTotalCount = 0;
        m_pendingModelIds.clear();
    }

    // 不自动退出 - 保持 Handler 活跃
    LOG_INFO("SlicingHandler: Task completed in environment '{}'",
             m_success ? "preview" : "normal");
}

void SlicingHandler::cleanupSlicingEnvironment() {
    LOG_INFO("SlicingHandler: Cleaning up slicing environment");

    // 清理 GCode 和切片可视化
    auto* bridge = SlicingPreviewBridge::instance();
    if (bridge) {
        bridge->clearPreview();
        LOG_INFO("SlicingHandler: Cleared toolpath previews");
    }
}

void SlicingHandler::cleanupUnownedTemporaryPrintOutputs() {
    const auto removeTemporary = [](QString& path, bool& isTemporary, const char* label) {
        if (!isTemporary || path.isEmpty()) {
            return;
        }
        if (QFile::remove(path)) {
            LOG_INFO("SlicingHandler: removed unowned temporary {}: {}", label, path.toStdString());
        } else if (QFileInfo::exists(path)) {
            LOG_WARN("SlicingHandler: failed to remove unowned temporary {}: {}", label, path.toStdString());
        }
        isTemporary = false;
    };
    removeTemporary(m_gcodePath, m_gcodeIsLibraryTemporary, "GCode");
    removeTemporary(m_gcode3mfPath, m_gcode3mfIsLibraryTemporary, "GCode 3MF");
}

void SlicingHandler::clearAllSliceData() {
    LOG_INFO("SlicingHandler: Clearing all slicing visualization DB data");

    if (m_taskWatcher && m_taskWatcher->isRunning()) {
        LOG_INFO("SlicingHandler: Cancelling running slice task before clearing...");
        m_taskWatcher->cancel();
        m_taskWatcher->waitForFinished();

        if (!m_taskId.isEmpty()) {
            TaskStateNotifier::instance()->endTask(m_taskId);
            m_taskId.clear();
        }
    }

    SlicingPreviewBridge::instance()->stop();

    auto* bridge = SlicingPreviewBridge::instance();
    if (bridge) {
        bridge->clearPreview();
        LOG_INFO("SlicingHandler: Cleared all toolpath previews and DB registrations");
    }

    m_state = State::Ready;
}

QString SlicingHandler::getOrExportSupportMeshSTL() {
    m_needCleanupSupportMeshStl = false;

    auto* document = DocumentManager::instance();
    const auto support = ManualSupportRelations::findForModel(
        DBInstanceID(m_modelId));
    const auto geometry = support ? support->getGeneratedGeometry() : nullptr;
    const auto supportPolyData = geometry && support->hasGeneratedGeometry()
        ? vtk_poly_data_from_indexed_mesh(*geometry)
        : vtkSmartPointer<vtkPolyData>{};
    if (!supportPolyData ||
        supportPolyData->GetNumberOfPoints() <= 0 ||
        supportPolyData->GetNumberOfPolys() <= 0) {
        return QString();
    }

    QString tempDir = QDir::tempPath();
    QString tempStl = QString("%1/slice_support_%2.stl")
        .arg(tempDir)
        .arg(QDateTime::currentMSecsSinceEpoch());

    if (GPlatform::MeshExporter::exportToSTL(supportPolyData, tempStl.toStdString(), true)) {
        m_needCleanupSupportMeshStl = true;
        LOG_INFO("SlicingHandler: Exported temporary support STL: {}", tempStl.toStdString());
        return tempStl;
    }

    LOG_ERROR("SlicingHandler: Failed to export support STL");
    return QString();
}

void SlicingHandler::createToolpathPreview() {
    auto vizStartTime = std::chrono::steady_clock::now();

    if (!m_success) {
        return;
    }

    if (!m_toolpathPreview) {
        m_success = false;
        m_errorMessage = "libslicer returned no toolpath preview";
        LOG_ERROR("SlicingHandler: {}", m_errorMessage.toStdString());
        cleanupUnownedTemporaryPrintOutputs();
        return;
    }

    TransactionGuard guard("Create Toolpath Preview");

    auto preview = trans::TransDB::create<GPlatform::ToolpathPreviewDB>();
    if (!preview) {
        m_success = false;
        m_errorMessage = "Failed to create ToolpathPreviewDB";
        LOG_ERROR("SlicingHandler: {}", m_errorMessage.toStdString());
        cleanupUnownedTemporaryPrintOutputs();
        return;
    }

    auto data = GPlatform::adaptLibSlicerToolpath(*m_toolpathPreview);
    data.printGCodePath = m_gcodePath.toStdString();
    data.printGCodeOwnership = m_gcodeIsLibraryTemporary
        ? GPlatform::ToolpathArtifactOwnership::GeneratedTemporary
        : GPlatform::ToolpathArtifactOwnership::External;
    data.printGCode3mfPath = m_gcode3mfPath.toStdString();
    data.printGCode3mfOwnership = m_gcode3mfIsLibraryTemporary
        ? GPlatform::ToolpathArtifactOwnership::GeneratedTemporary
        : GPlatform::ToolpathArtifactOwnership::External;
    const DBInstanceID sourceModelId(m_modelId);
    preview->setSourceModelName(m_modelName.toStdString());
    preview->setSourceScope(m_sliceSourceScope);
    preview->setSourceModelDBId(
        m_sliceSourceScope ==
                GPlatform::ToolpathPreviewSourceScope::SingleModel
            ? m_sliceSourceModelId
            : INVALID_DB_ID);
    preview->setPreviewData(std::move(data));
    if (m_toolpathPreview->metadata_json.find(
            "gplatform.belt_support_algorithm_audit.v1") != std::string::npos) {
        preview->setShowLayerRangeStart(0);
        preview->setShowLayerRangeEnd(0);
        preview->setCurrentLayer(0);
    }
    m_gcodeIsLibraryTemporary = false;
    m_gcode3mfIsLibraryTemporary = false;
    preview->setVisible(true);

    // Slice inputs are required to be immutable world-space snapshots.
    Transform identityTransform;
    identityTransform.identity();
    preview->setTransform(identityTransform);

    if (auto* manager = DocumentManager::instance()) {
        manager->registerDBInstance(preview, TypeID::TOOLPATH_PREVIEW_DB);
        if (!m_slicePrintBedId.isValid() ||
            !manager->attachOwnedChild(
                m_slicePrintBedId, preview->getDBInstanceID(),
                ToolpathPreviewRelations::PrintBedPreviewsRelation)) {
            m_success = false;
            m_errorMessage =
                "Failed to attach ToolpathPreviewDB to print bed";
            LOG_ERROR("SlicingHandler: {}", m_errorMessage.toStdString());
            guard.rollback();
            cleanupUnownedTemporaryPrintOutputs();
            return;
        }
    }

    // 注册到 SlicingPreviewBridge
    auto* bridge = SlicingPreviewBridge::instance();
    if (bridge) {
        bridge->completePreviewLoad(
            QString(),
            m_gcodePath,
            static_cast<int>(sourceModelId.getValue()),
            m_modelName,
            preview
        );

    }

    auto vizEndTime = std::chrono::steady_clock::now();
    if (!preview->getFiberFillDiagnostics().empty()) {
        const QString token = QStringLiteral("%1:%2")
            .arg(preview->getDBInstanceID().getValue()).arg(preview->getPreviewDataGeneration());
        emit NotificationManager::instance()->fiberFillDebugAvailable(
            token, static_cast<int>(preview->getFiberFillDiagnostics().size()));
    } else if (m_sliceSession && m_sliceSession->request.config.value("fiber_fill_debug") == "1") {
        NotificationManager::instance()->showInfo(tr("铺纤填充调试"),
            tr("没有可显示的铺纤诊断。未生成区域仅比较已有候选轮廓与最终纤维覆盖，不包含从未生成候选的部分。"));
    }
    auto vizDuration = std::chrono::duration_cast<std::chrono::milliseconds>(vizEndTime - vizStartTime).count();
    LOG_INFO("[SLICING TIMING] 创建 ToolpathPreviewDB 完成 (总耗时: {}ms)", vizDuration);
    LOG_INFO("SlicingHandler: Created ToolpathPreviewDB with {} layers and {} drawable moves",
             preview->getTotalLayers(), preview->getStats().renderSegmentCount);
    LOG_INFO(
        "SlicingHandler: preview source scope={} bed={} model={}",
        static_cast<int>(preview->getSourceScope()),
        m_slicePrintBedId.getValue(),
        preview->getSourceModelDBId().getValue());
}

void SlicingHandler::setDebugMode(bool enabled) {
    LOG_INFO("SlicingHandler: Debug mode set to {}", enabled);
}

void SlicingHandler::clearDebugActors() {
    LOG_DEBUG("SlicingHandler: Cleared debug actors");
}
