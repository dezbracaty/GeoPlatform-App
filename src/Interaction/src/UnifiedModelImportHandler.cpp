#include "UnifiedModelImportHandler.hpp"
#include "ImportFileFormat.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    UnifiedModelImportHandler, "import_mesh", "import_stl")
#include "ActionHandlerRegistry.hpp"
#include "ModelLoaderUtil.hpp"
#include "SlicingConfigDB.hpp"
#include "GlbDBImporter.hpp"
#include "ModelGraphUtil.hpp"
#include "ModelInstanceDB.hpp"
#include "ModelObjectDB.hpp"
#include "ModelPartDB.hpp"
#include "PrintBedDB.hpp"
#include <DocumentManager.hpp>
#include <CameraNavigationController.hpp>
#include "TransactionManager.hpp"
#include "CameraDB.hpp"
#include "ModelCreationUtil.hpp"
#include "Foundation/Log.h"
#include "AIDescriptorHelper.hpp"
#include "SlicingBackend.hpp"
#include "MaterialDB.hpp"
#include <Geometry.hpp>
#include "ModelSurfaceColorCodec.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "SliceSettingsBridge.hpp"

#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtConcurrent>

#include <vtkCellArray.h>
#include <vtkCleanPolyData.h>
#include <vtkFeatureEdges.h>
#include <vtkMassProperties.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSmartPointer.h>

#include <unordered_map>
#include <numeric>

namespace {

bool finite(const Vector3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

float suggestedUnitScale(const ModelLoaderUtil::LoadResult& mesh) {
    if (!mesh.success || mesh.triangles.empty()) return 1.0f;
    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    vtkNew<vtkCellArray> cells;
    for (const auto& triangle : mesh.triangles) {
        vtkIdType ids[3];
        const Vector3 vertices[]{triangle.vertex1, triangle.vertex2, triangle.vertex3};
        for (int i = 0; i < 3; ++i) {
            if (!finite(vertices[i])) return 1.0f;
            ids[i] = points->InsertNextPoint(vertices[i].x, vertices[i].y, vertices[i].z);
        }
        cells->InsertNextCell(3, ids);
    }
    vtkNew<vtkPolyData> data;
    data->SetPoints(points);
    data->SetPolys(cells);
    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputData(data);
    // Open/non-manifold surfaces cannot provide a reliable solid volume.
    vtkNew<vtkFeatureEdges> edges;
    edges->SetInputConnection(clean->GetOutputPort());
    edges->BoundaryEdgesOn();
    edges->NonManifoldEdgesOn();
    edges->FeatureEdgesOff();
    edges->ManifoldEdgesOff();
    edges->Update();
    if (edges->GetOutput()->GetNumberOfCells() != 0) return 1.0f;

    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(clean->GetOutputPort());
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->SplittingOff();
    vtkNew<vtkMassProperties> mass;
    mass->SetInputConnection(normals->GetOutputPort());
    mass->Update();
    // Orca stores mesh volume in float; use the same precision at thresholds.
    const float volume = static_cast<float>(mass->GetVolume());
    if (!std::isfinite(volume) || volume <= 0) return 1.0f;
    // Same meter-first thresholds as Orca Model::looks_like_saved_in_meters()
    // and Model::looks_like_imperial_units(). These are hints, never auto-fixes.
    if (volume < 0.008) return 1000.0f;
    if (volume < 8.0) return 25.4f;
    return 1.0f;
}

bool applyImportUnitScale(ModelLoaderUtil::LoadResult& mesh, float factor) {
    if (!std::isfinite(factor) || factor <= 0 ||
        !finite(mesh.boundingBoxMin * factor) || !finite(mesh.boundingBoxMax * factor) ||
        !finite(mesh.center * factor)) return false;
    // Preflight all vertices before mutation, including malformed loader data.
    for (const auto& triangle : mesh.triangles) {
        if (!finite(triangle.vertex1 * factor) || !finite(triangle.vertex2 * factor) ||
            !finite(triangle.vertex3 * factor)) return false;
    }
    for (auto& triangle : mesh.triangles) {
        triangle.vertex1 = triangle.vertex1 * factor;
        triangle.vertex2 = triangle.vertex2 * factor;
        triangle.vertex3 = triangle.vertex3 * factor;
    }
    mesh.boundingBoxMin = mesh.boundingBoxMin * factor;
    mesh.boundingBoxMax = mesh.boundingBoxMax * factor;
    mesh.center = mesh.center * factor;
    return true;
}

} // namespace

UnifiedModelImportHandler::UnifiedModelImportHandler(QObject* parent)
    : StandardBackgroundHandler(parent), m_cancelRequested(std::make_shared<std::atomic_bool>(false)) {}

UnifiedModelImportHandler::~UnifiedModelImportHandler() {
    closeUnitDialog();
    if (m_cancelRequested) m_cancelRequested->store(true, std::memory_order_release);
}

bool UnifiedModelImportHandler::onInitialize() {
    closeUnitDialog();
    m_unitResolved = false;
    m_suggestedUnitScale = 1.0f;
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    m_filePath.clear();
    m_createdObjectId.clear();
    m_createdObjectIds.clear();
    m_loadResult = {};
    m_glbLoadResult = {};
    m_isGlbImport = false;
    m_scale = 1.0f;
    m_name.clear();

    auto filePath = getParam("filePath");
    if (!filePath.isValid() || filePath.toString().isEmpty()) {
        if (m_context) m_context->setError(ActionErrorCode::InvalidParams, "No file path provided");
        LOG_ERROR("No file path provided for model import");
        return false;
    }
    const QUrl fileUrl = filePath.toUrl();
    m_filePath = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : filePath.toString();
    // Apply the product's temporary import policy before any loader or scene mutation.
    const QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (classifyImportFile(m_filePath) != ImportFileFormat::Stl) {
        if (m_context) {
            m_context->setError(ActionErrorCode::InvalidParams,
                               tr("Unsupported file format: %1. Only STL (*.stl) model files are supported.")
                                   .arg(suffix));
        }
        return false;
    }
    LOG_INFO("Importing model from: {}", m_filePath.toStdString());

    // 3MF project files go directly through libslicer. Generic mesh formats
    // continue to use ModelLoaderUtil.
    const QString lowerPath = m_filePath.toLower();
    const bool projectFormat = lowerPath.endsWith(QStringLiteral(".3mf")) &&
        !lowerPath.endsWith(QStringLiteral(".gcode.3mf"));
    m_isGlbImport = lowerPath.endsWith(QStringLiteral(".glb"));
    if (!projectFormat && !m_isGlbImport &&
        !ModelLoaderUtil::isFormatSupported(m_filePath)) {
        if (m_context) {
            m_context->setError(ActionErrorCode::InvalidParams, QString("Unsupported file format: %1")
                              .arg(QFileInfo(m_filePath).suffix()));
        }
        LOG_ERROR("Unsupported file format: {}",
                 QFileInfo(m_filePath).suffix().toStdString());
        return false;
    }

    bool scaleValid = false;
    m_scale = getParam("scale", 1.0f).toFloat(&scaleValid);
    const QString unit = getParam("unit", QStringLiteral("auto")).toString();
    const float unitScale = unit == "m" ? 1000.0f : unit == "in" ? 25.4f : 1.0f;
    if (!scaleValid || !std::isfinite(m_scale) || m_scale <= 0 ||
        (unit != "auto" && unit != "mm" && unit != "m" && unit != "in") ||
        !std::isfinite(m_scale * unitScale)) {
        m_context->setError(ActionErrorCode::InvalidParams,
            tr("Import scale must be finite and positive; unit must be auto, mm, m or in."));
        return false;
    }
    m_scale *= unitScale;
    m_unitResolved = unit != "auto";

    auto name = getParam("name");
    if (name.isValid()) {
        m_name = name.toString();
    } else if (m_name.isEmpty()) {
        m_name = QFileInfo(m_filePath).baseName();
    }

    // 发送开始信号
    emit importProgress(0, QString("Starting import of %1").arg(QFileInfo(m_filePath).fileName()));

    return true;
}

const QHash<QString, QVariantMap>& UnifiedModelImportHandler::aiDescriptorTable() const {
    // 通用模型导入参数
    auto modelSchema = makeInputSchema({
        {"filePath", "string", true},
        {"printBedId", "number", false},
        {"scale", "number", false},
        {"unit", "string", false},
        {"name", "string", false},
        {"x", "number", false},
        {"y", "number", false},
        {"z", "number", false},
        {"position", "object", false},
        {"color", "string", false}  // 支持 "#RRGGBB" 或颜色名
    });

    static const QHash<QString, QVariantMap> table = [modelSchema] {
        QVariantMap importStl = makeDescriptor(
            "import_stl",
            "Import STL",
            "Import an STL file. Optional unit: auto, mm, m or in; scale is an additional multiplier.",
            "write",
            modelSchema,
            {"import", "file", "scene"});
        importStl.insert("async", true);

        auto importMesh = importStl;
        importMesh.insert("actionCode", "import_mesh");
        return QHash<QString, QVariantMap>{{"import_mesh", importMesh}, {"import_stl", importStl}};
    }();
    return table;
}

std::optional<QVariantMap> UnifiedModelImportHandler::getAIDescriptor(const QString& actionCode) const {
    auto it = aiDescriptorTable().find(actionCode);
    if (it != aiDescriptorTable().end()) {
        return std::make_optional(*it);
    }
    return std::nullopt;
}

void UnifiedModelImportHandler::onBackgroundStarted() {
    LOG_INFO("Background import started for: {}", m_filePath.toStdString());

    // 更新进度：开始加载
    updateTaskProgress(0.05f);
    updateTaskDescription(QString("正在加载 %1...").arg(m_name));

    // 延迟 300ms 后开始加载，让进度条有时间显示
    QTimer::singleShot(300, this, [this]() {
        loadModelInBackground();
    });
}

void UnifiedModelImportHandler::loadModelInBackground() {
    // 更新进度：正在读取文件
    updateTaskProgress(0.15f);
    updateTaskDescription(QString("正在读取 %1...").arg(m_name));
    emit importProgress(15, "Loading file...");

    if (m_isGlbImport) {
        m_glbLoadResult = GlbDBImporter{}.importFile(
            m_filePath.toStdString(), m_scale);
        m_loadResult.success = m_glbLoadResult.success;
        m_loadResult.errorMessage = QString::fromStdString(
            m_glbLoadResult.errorMessage);
        m_loadResult.boundingBoxMin = m_glbLoadResult.boundingBoxMin;
        m_loadResult.boundingBoxMax = m_glbLoadResult.boundingBoxMax;
        m_loadResult.center = Vector3{};
        m_loadResult.modelName = QFileInfo(m_filePath).baseName();
        m_loadResult.vertexCount = m_glbLoadResult.vertexCount;
        m_loadResult.faceCount = m_glbLoadResult.faceCount;
    } else {
        // Legacy mesh formats are migrated independently. GLB import retains
        // both its printable triangle projection and its immutable source
        // asset so the formal ModelObject can preserve authored appearance.
        auto result = ModelLoaderUtil::loadModel(m_filePath, m_scale);

        m_loadResult.success = result.success;
        m_loadResult.errorMessage = result.errorMessage;
        m_loadResult.triangles = std::move(result.triangles);
        m_loadResult.boundingBoxMin = result.boundingBoxMin;
        m_loadResult.boundingBoxMax = result.boundingBoxMax;
        m_loadResult.center = result.center;
        m_loadResult.modelName = result.modelName;
        m_loadResult.vertexCount = result.vertexCount;
        m_loadResult.faceCount = result.faceCount;
    }

    if (m_loadResult.success) {
        // 更新进度：模型加载完成（延迟让进度可见）
        QTimer::singleShot(400, this, [this]() {
            updateTaskProgress(0.5f);
            updateTaskDescription(QString("已加载 %1 顶点, %2 面")
                                       .arg(m_loadResult.vertexCount)
                                       .arg(m_loadResult.faceCount));
            emit importProgress(50, QString("Loaded %1 vertices, %2 faces")
                                       .arg(m_loadResult.vertexCount)
                                       .arg(m_loadResult.faceCount));
            LOG_INFO("Model loaded successfully: {} vertices, {} faces",
                     m_loadResult.vertexCount, m_loadResult.faceCount);

            // 再延迟后更新进度
            QTimer::singleShot(400, this, [this]() {
                updateTaskProgress(0.7f);
                updateTaskDescription(QString("正在创建模型..."));
                emit importProgress(70, "Creating model...");

                // 最后延迟后请求返回前台
                QTimer::singleShot(300, this, [this]() {
                    updateTaskProgress(0.85f);
                    updateTaskDescription(QString("正在创建模型对象..."));
                    requestResumeFromBackground();
                });
            });
        });
    } else {
        LOG_ERROR("Failed to load model: {}", m_loadResult.errorMessage.toStdString());
        // 即使失败也要请求返回前台
        QTimer::singleShot(100, this, [this]() {
            requestResumeFromBackground();
        });
    }
}

void UnifiedModelImportHandler::onBackgroundCompleted() {
    LOG_INFO("Model import background task completed");

    if (m_loadResult.success &&
        (m_isGlbImport
             ? !m_glbLoadResult.printableTriangles.empty()
             : !m_loadResult.triangles.empty())) {
        if (!m_isGlbImport && !m_unitResolved) {
            m_suggestedUnitScale = suggestedUnitScale(m_loadResult);
            if (m_suggestedUnitScale > 1.0f) {
                if (!showUnitDialog()) {
                    m_context->setError(ActionErrorCode::ContractValidation, m_loadResult.errorMessage);
                    m_context->setResult(QVariantMap{{"success", false},
                        {"needsUnitConfirmation", true}, {"suggestedScale", m_suggestedUnitScale}});
                    emit importCompleted({}, false, m_loadResult.errorMessage);
                }
                return;
            }
        }
        // 更新进度：正在创建对象
        updateTaskProgress(0.95f);
        updateTaskDescription(QString("正在创建模型对象..."));

        createModelFromResult();

        if (!m_createdObjectId.isEmpty()) {
            // 更新进度：完成
            updateTaskProgress(1.0f);
            updateTaskDescription(QString("导入完成"));
            emit importProgress(100, "Import completed successfully");
            emit importCompleted(m_createdObjectId, true);
            LOG_INFO("Model imported successfully with ID: {}",
                     m_createdObjectId.toStdString());
        } else {
            if (m_context) {
                m_context->setError(ActionErrorCode::Internal, m_loadResult.errorMessage);
            }
            emit importCompleted("", false, m_loadResult.errorMessage);
            LOG_ERROR("Failed to create mesh object from loaded data");
        }
    } else {
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, m_loadResult.errorMessage);
        }
        emit importCompleted("", false, m_loadResult.errorMessage);
        LOG_ERROR("Model import failed: {}", m_loadResult.errorMessage.toStdString());
    }
}

void UnifiedModelImportHandler::createModelFromResult() {
    if (m_isGlbImport)
        m_loadResult.triangles = std::move(m_glbLoadResult.printableTriangles);
    const auto graph = ModelCreationUtil::createModel(
        m_loadResult,
        {m_name.toStdString(), m_filePath.toStdString(),
         ModelLoaderUtil::getFormatType(m_filePath).toStdString(), true},
        getParams(), m_isGlbImport ? m_glbLoadResult.asset : nullptr);
    if (!graph || !graph.instance) {
        m_loadResult.errorMessage = QString::fromStdString(graph.error);
        if (m_context)
            m_context->setError(ActionErrorCode::Internal, m_loadResult.errorMessage);
        LOG_ERROR("Failed to create imported model: {}", graph.error);
        return;
    }
    m_createdObjectId = QString::number(graph.instance->getDBInstanceID().getValue());
    m_createdObjectIds = {m_createdObjectId};
    if (auto camera = CameraNavigationController::currentCamera())
        CameraNavigationController::fitScene(camera);
}

bool UnifiedModelImportHandler::showUnitDialog() {
    // Use the existing window's engine/theme; the handler owns only this dialog.
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        m_loadResult.errorMessage = tr("Model units need confirmation. Import again with unit set to mm, m or in.");
        return false;
    }
    QQuickWindow* window = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());
    if (!window || !window->isVisible() || !qmlEngine(window)) {
        window = nullptr;
        for (auto* candidate : QGuiApplication::allWindows()) {
            auto* quick = qobject_cast<QQuickWindow*>(candidate);
            if (quick && quick->isVisible() && qmlEngine(quick)) { window = quick; break; }
        }
    }
    auto bed = ModelCreationUtil::resolvePrintBed(getParam("printBedId"));
    if (!window || !bed) {
        m_loadResult.errorMessage = tr("Model units need confirmation. Import again with unit set to mm, m or in.");
        return false;
    }
    m_unitPrintBedId = bed->getDBInstanceID();
    const auto size = m_loadResult.boundingBoxMax - m_loadResult.boundingBoxMin;
    const auto dimensions = [this](const Vector3& value) {
        const QLocale locale;
        return tr("%1 × %2 × %3 mm").arg(locale.toString(value.x, 'g', 6),
            locale.toString(value.y, 'g', 6), locale.toString(value.z, 'g', 6));
    };
    QQmlComponent component(qmlEngine(window),
        QUrl(QStringLiteral("qrc:/qt/qml/GPlatform/qml/dialogs/ImportUnitDialog.qml")));
    auto* dialog = component.createWithInitialProperties({
        {"hostItem", QVariant::fromValue(window->contentItem())},
        {"fileName", QFileInfo(m_filePath).fileName()},
        {"currentSize", dimensions(size)},
        {"convertedSize", dimensions(size * m_suggestedUnitScale)},
        {"suspectedUnit", m_suggestedUnitScale == 1000.0f ? tr("meters") : tr("inches")},
        {"conversionFactor", m_suggestedUnitScale}});
    if (!dialog) {
        LOG_WARN("Cannot create import unit dialog: {}", component.errorString().toStdString());
        m_loadResult.errorMessage = tr("Cannot open the model unit confirmation dialog.");
        return false;
    }
    dialog->setParent(this);
    QQmlEngine::setObjectOwnership(dialog, QQmlEngine::CppOwnership);
    m_unitDialog = dialog;
    connect(dialog, SIGNAL(decision(int)), this, SLOT(resolveUnitChoice(int)));
    connect(window, &QObject::destroyed, dialog, [this] { resolveUnitChoice(0); });
    QMetaObject::invokeMethod(dialog, "open");
    return true;
}

void UnifiedModelImportHandler::closeUnitDialog() {
    if (auto* dialog = m_unitDialog.data()) {
        m_unitDialog.clear();
        disconnect(dialog, nullptr, this, nullptr);
        QMetaObject::invokeMethod(dialog, "close");
        dialog->deleteLater();
    }
}

void UnifiedModelImportHandler::resolveUnitChoice(int choice) {
    if (!m_unitDialog || getState() != State::Active || !m_context) return;
    closeUnitDialog();
    m_unitResolved = true;
    const bool tracked = !m_context->getInvocationId().trimmed().isEmpty();
    const ScopedTransactionContextMetadata scope(getActionCode(), m_context->getInvocationMetadata(), tracked);
    if (choice != 1 && choice != 2) {
        m_cancelRequested->store(true, std::memory_order_release);
        m_loadResult.errorMessage = tr("Model import cancelled.");
        m_context->setError(ActionErrorCode::ContractValidation, m_loadResult.errorMessage);
        auto result = getCompletionResult().toMap();
        result.insert("cancelled", true);
        m_context->setResult(result);
        emit importCompleted({}, false, m_loadResult.errorMessage);
    } else if (!DocumentManager::instance()->getDB<PrintBedDB>(m_unitPrintBedId)) {
        m_context->setError(ActionErrorCode::TargetNotFound, tr("The target print bed is no longer available."));
        emit importCompleted({}, false, m_context->getError());
    } else if (choice == 2 && !applyImportUnitScale(m_loadResult, m_suggestedUnitScale)) {
        m_context->setError(ActionErrorCode::InvalidParams, tr("Converted model coordinates are not finite."));
        emit importCompleted({}, false, m_context->getError());
    } else {
        // Retain the intended bed while the confirmation is displayed.
        m_context->setParam("printBedId", QVariant::fromValue(m_unitPrintBedId.getValue()));
        onBackgroundCompleted();
        m_context->setResult(getCompletionResult());
    }
    completeInvocation(m_context);
    m_loadResult.triangles.clear();
    setActive(false);
}

void UnifiedModelImportHandler::onCleanup() {
    closeUnitDialog();
    // 清理资源
    if (m_cancelRequested) {
        m_cancelRequested->store(true, std::memory_order_release);
    }
    m_loadResult.triangles.clear();
    m_createdObjectId.clear();
    m_createdObjectIds.clear();
    StandardBackgroundHandler::onCleanup();
}

QVariant UnifiedModelImportHandler::getCompletionResult() const {
    QVariantList ids;
    for (const auto& id : m_createdObjectIds) ids.push_back(id);
    return QVariantMap{{"success", !m_createdObjectId.isEmpty() && m_loadResult.success},
        {"objectId", m_createdObjectId}, {"modelId", m_createdObjectId}, {"objectIds", ids},
        {"errorMessage", m_loadResult.errorMessage},
        {"vertexCount", QVariant::fromValue<qulonglong>(m_loadResult.vertexCount)},
        {"faceCount", QVariant::fromValue<qulonglong>(m_loadResult.faceCount)}};
}
