#include "UnifiedModelImportHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    UnifiedModelImportHandler, "import_model", "import_stl", "model.library.import")
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
#include "ModelPositionUtil.hpp"
#include "Foundation/Log.h"
#include "AIDescriptorHelper.hpp"
#include "LocalModelLibraryService.h"
#include "SlicingBackend.hpp"
#include "MaterialDB.hpp"
#include <Geometry.hpp>
#include "ModelSurfaceColorCodec.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "SliceSettingsBridge.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QColor>
#include <QUrl>
#include <QVariantList>
#include <QtConcurrent>

#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <unordered_map>
#include <numeric>

namespace {

DBInstanceID activeSlicingConfigId() {
    auto* document = DocumentManager::instance();
    if (!document) return DBInstanceID();
    const auto configs = document->getDBInstancesByType(
        TypeID::SLICING_CONFIG_DB);
    return configs.empty()
        ? DBInstanceID()
        : configs.front()->getDBInstanceID();
}

vtkSmartPointer<vtkPolyData> polyDataFromProjectPart(
    const libslicer::ProjectImportPart& part) {
    if (part.vertices.empty() || part.triangles.empty()) return nullptr;
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(static_cast<vtkIdType>(part.vertices.size()));
    for (std::size_t index = 0; index < part.vertices.size(); ++index) {
        const auto& vertex = part.vertices[index];
        points->SetPoint(static_cast<vtkIdType>(index), vertex.x, vertex.y, vertex.z);
    }
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (const auto& triangle : part.triangles) {
        if (triangle.vertex_a >= part.vertices.size() ||
            triangle.vertex_b >= part.vertices.size() ||
            triangle.vertex_c >= part.vertices.size()) {
            return nullptr;
        }
        const vtkIdType ids[3]{
            static_cast<vtkIdType>(triangle.vertex_a),
            static_cast<vtkIdType>(triangle.vertex_b),
            static_cast<vtkIdType>(triangle.vertex_c)};
        cells->InsertNextCell(3, ids);
    }
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(cells);
    return polyData;
}

struct PreparedSurfaceColors {
    std::string encodedState;
    std::string topologyFingerprint;
};

std::optional<PreparedSurfaceColors> prepareSurfaceColors(
    const libslicer::ProjectImportPart& part) {
    if (part.facet_labels.roots.empty() || part.facet_labels.bitstream.empty()) {
        return std::nullopt;
    }

    ManualSupportOrcaScaffold::TriangleSplittingData state;
    state.trianglesToSplit.reserve(part.facet_labels.roots.size());
    for (const auto& root : part.facet_labels.roots) {
        if (root.triangle_index >= part.triangles.size() ||
            root.bitstream_start_index >= part.facet_labels.bitstream.size()) {
            return std::nullopt;
        }
        state.trianglesToSplit.push_back({
            static_cast<int>(root.triangle_index),
            static_cast<int>(root.bitstream_start_index)});
    }
    state.bitstream.reserve(part.facet_labels.bitstream.size());
    for (const std::uint8_t bit : part.facet_labels.bitstream) {
        state.bitstream.push_back(bit != 0);
    }
    state.update_used_states();

    PreparedSurfaceColors prepared;
    if (!ModelSurfaceColorCodec::encode(state, &prepared.encodedState)) {
        return std::nullopt;
    }

    std::vector<GeomTriangle> triangles;
    triangles.reserve(part.triangles.size());
    for (const auto& triangle : part.triangles) {
        const auto& a = part.vertices[triangle.vertex_a];
        const auto& b = part.vertices[triangle.vertex_b];
        const auto& c = part.vertices[triangle.vertex_c];
        triangles.emplace_back(
            Vector3(a.x, a.y, a.z),
            Vector3(b.x, b.y, b.z),
            Vector3(c.x, c.y, c.z));
    }
    prepared.topologyFingerprint =
        ModelSurfaceColorCodec::topologyFingerprint(triangles);
    return prepared;
}

} // namespace

UnifiedModelImportHandler::UnifiedModelImportHandler(QObject* parent)
    : StandardBackgroundHandler(parent),
      m_projectImportWatcher(std::make_unique<
          QFutureWatcher<std::shared_ptr<libslicer::ProjectImportResult>>>()),
      m_cancelRequested(std::make_shared<std::atomic_bool>(false)) {
    connect(m_projectImportWatcher.get(),
            &QFutureWatcher<std::shared_ptr<libslicer::ProjectImportResult>>::finished,
            this,
            &UnifiedModelImportHandler::onProjectImportFinished);
}

UnifiedModelImportHandler::~UnifiedModelImportHandler() {
    if (m_cancelRequested) {
        m_cancelRequested->store(true, std::memory_order_release);
    }
    if (m_projectImportWatcher && m_projectImportWatcher->isRunning()) {
        m_projectImportWatcher->waitForFinished();
    }
}

bool UnifiedModelImportHandler::onInitialize() {
    if (m_projectImportWatcher && m_projectImportWatcher->isRunning()) {
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal,
                                "A project import is already running");
        }
        return false;
    }
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    m_filePath.clear();
    m_createdObjectId.clear();
    m_createdObjectIds.clear();
    m_loadResult = {};
    m_glbLoadResult = {};
    m_isGlbImport = false;
    m_projectLoadResult = {};
    m_isProjectImport = false;
    m_position = Vector3(0, 0, 0);
    m_scale = 1.0f;
    m_color = Vector3(0.8f, 0.8f, 0.8f);
    m_name.clear();

    // 获取文件路径参数
    if (getActionCode() == QStringLiteral("model.library.import")) {
        LocalModelRecord libraryModel;
        QString resolveError;
        const QString libraryItemId = getParam("libraryItemId").toString();
        if (!LocalModelLibraryService::instance()->resolve(
                libraryItemId, libraryModel, resolveError)) {
            if (m_context) {
                m_context->setError(ActionErrorCode::TargetNotFound, resolveError);
            }
            LOG_ERROR("Failed to resolve local model library item {}: {}",
                      libraryItemId.toStdString(), resolveError.toStdString());
            return false;
        }
        m_filePath = libraryModel.filePath;
        m_name = libraryModel.name;
    } else {
        auto filePath = getParam("filePath");
        if (!filePath.isValid() || filePath.toString().isEmpty()) {
            if (m_context) {
                m_context->setError(ActionErrorCode::InvalidParams, "No file path provided");
            }
            LOG_ERROR("No file path provided for model import");
            return false;
        }

        const QUrl fileUrl = filePath.toUrl();
        m_filePath = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : filePath.toString();
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

    // 获取位置参数 - 支持两种格式
    // 1. 直接的 x, y, z 参数
    auto x = getParam("x");
    auto y = getParam("y");
    auto z = getParam("z");
    if (x.isValid() && y.isValid() && z.isValid()) {
        m_position = Vector3(x.toFloat(), y.toFloat(), z.toFloat());
    }
    // 2. position 对象参数
    else {
        auto position = getParam("position");
        if (position.isValid() && position.canConvert<QVariantMap>()) {
            auto posMap = position.toMap();
            m_position = Vector3(
                posMap.value("x", 0.0).toFloat(),
                posMap.value("y", 0.0).toFloat(),
                posMap.value("z", 0.0).toFloat()
            );
        }
    }

    // 获取缩放参数
    auto scale = getParam("scale");
    if (scale.isValid()) {
        m_scale = scale.toFloat();
        if (m_scale <= 0) m_scale = 1.0f;
    }

    // 获取颜色参数 - 支持 "#RRGGBB" 或颜色名 (如 "red")
    auto colorStr = getParam("color");
    if (colorStr.isValid() && colorStr.typeId() == QMetaType::QString) {
        QColor qColor(colorStr.toString());
        if (qColor.isValid()) {
            m_color = Vector3(
                qColor.redF(),
                qColor.greenF(),
                qColor.blueF()
            );
        }
    }

    // 获取名称参数
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
        {"name", "string", false},
        {"x", "number", false},
        {"y", "number", false},
        {"z", "number", false},
        {"position", "object", false},
        {"color", "string", false}  // 支持 "#RRGGBB" 或颜色名
    });

    static const QHash<QString, QVariantMap> table = [modelSchema] {
        QVariantMap importModel = makeDescriptor(
            "import_model",
            "Import 3D Model",
            "Import a 3D model file.",
            "write",
            modelSchema,
            {"import", "file", "scene"});
        importModel.insert("async", true);

        QVariantMap importStl = makeDescriptor(
            "import_stl",
            "Import STL",
            "Import an STL file.",
            "write",
            modelSchema,
            {"import", "file", "scene"});
        importStl.insert("async", true);

        QVariantMap libraryImport = makeDescriptor(
            "model.library.import",
            "Import Local Library Model",
            "Import one model selected from the local model library by opaque libraryItemId.",
            "write",
            makeInputSchema({
                {"libraryItemId", "string", true},
                {"printBedId", "number", false},
                {"scale", "number", false}
            }),
            {"model-library", "local", "import"});
        libraryImport.insert("async", true);

        return QHash<QString, QVariantMap>{
            {"import_model", importModel},
            {"import_stl", importStl},
            {"model.library.import", libraryImport}
        };
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

    const QString lowerPath = m_filePath.toLower();
    m_isProjectImport = lowerPath.endsWith(QStringLiteral(".3mf")) &&
        !lowerPath.endsWith(QStringLiteral(".gcode.3mf"));
    if (m_isProjectImport) {
        loadProjectInBackground();
        return;
    }

    // 延迟 300ms 后开始加载，让进度条有时间显示
    QTimer::singleShot(300, this, [this]() {
        loadModelInBackground();
    });
}

void UnifiedModelImportHandler::loadProjectInBackground() {
    updateTaskProgress(0.1f);
    updateTaskDescription(QStringLiteral("正在解析 3MF 项目..."));
    emit importProgress(10, QStringLiteral("Parsing 3MF project..."));

    const std::string filePath = m_filePath.toStdString();
    const auto cancelRequested = m_cancelRequested;
    const QPointer<UnifiedModelImportHandler> handler(this);
    auto future = QtConcurrent::run([filePath, cancelRequested, handler]() {
        libslicer::SliceCallbacks callbacks;
        callbacks.is_cancelled = [cancelRequested]() {
            return !cancelRequested ||
                cancelRequested->load(std::memory_order_acquire);
        };
        callbacks.progress = [handler, cancelRequested](
            float progress, std::string_view stage) {
            if (!handler || !cancelRequested ||
                cancelRequested->load(std::memory_order_acquire)) return;
            const QString description = QString::fromUtf8(
                stage.data(), static_cast<qsizetype>(stage.size()));
            QMetaObject::invokeMethod(
                handler,
                [handler, cancelRequested, progress, description]() {
                    if (!handler || !cancelRequested ||
                        cancelRequested->load(std::memory_order_acquire) ||
                        handler->getState() != State::Background) return;
                    handler->updateTaskProgress(progress);
                    handler->updateTaskDescription(description);
                },
                Qt::QueuedConnection);
        };
        auto result = std::make_shared<libslicer::ProjectImportResult>();
        try {
            libslicer::ProjectImportRequest request;
            request.path = filePath;
            *result = GPlatform::SlicingBackend::instance().importProject(
                request, callbacks);
        } catch (const std::exception& error) {
            result->diagnostics.push_back(
                {"internal", std::string("Project import failed: ") + error.what(), false});
        } catch (...) {
            result->diagnostics.push_back(
                {"internal", "Project import failed with an unknown error", false});
        }
        return result;
    });
    m_projectImportWatcher->setFuture(future);
}

void UnifiedModelImportHandler::onProjectImportFinished() {
    if (getState() != State::Background) {
        LOG_INFO("3MF project import finished after handler left background; result discarded");
        return;
    }
    const auto result = m_projectImportWatcher->result();
    if (result) {
        m_projectLoadResult = std::move(*result);
    } else {
        m_projectLoadResult = {};
        m_projectLoadResult.diagnostics.push_back(
            {"internal", "Project import returned no result", false});
    }
    requestResumeFromBackground();
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

    if (m_isProjectImport) {
        if (m_projectLoadResult.success &&
            !m_projectLoadResult.objects.empty()) {
            updateTaskProgress(0.95f);
            updateTaskDescription(QStringLiteral("正在创建 3MF 模型对象..."));
            if (!SliceSettingsBridge::instance()->refreshActiveConfig()) {
                m_projectLoadResult.diagnostics.push_back({
                    "configuration",
                    "The 3MF geometry was loaded, but the slicing settings view could not be refreshed",
                    true});
            }
            createModelsFromProject();
        }
        if (!m_createdObjectId.isEmpty()) {
            updateTaskProgress(1.0f);
            updateTaskDescription(QStringLiteral("导入完成"));
            emit importProgress(100, "Import completed successfully");
            emit importCompleted(m_createdObjectId, true);
            const std::size_t partCount = std::accumulate(
                m_projectLoadResult.objects.begin(),
                m_projectLoadResult.objects.end(), std::size_t{0},
                [](std::size_t count, const auto& object) {
                    return count + object.parts.size();
                });
            const std::size_t instanceCount = std::accumulate(
                m_projectLoadResult.objects.begin(),
                m_projectLoadResult.objects.end(), std::size_t{0},
                [](std::size_t count, const auto& object) {
                    return count + object.instances.size();
                });
            LOG_INFO("3MF project imported: objects={}, parts={}, instances={}, filaments={}, created={}",
                     m_projectLoadResult.objects.size(), partCount,
                     instanceCount,
                     m_projectLoadResult.filaments.size(),
                     m_createdObjectIds.size());
            return;
        }

        QStringList errors;
        for (const auto& diagnostic : m_projectLoadResult.diagnostics) {
            if (!diagnostic.warning) {
                errors.push_back(QString::fromStdString(diagnostic.message));
            }
        }
        const QString errorMessage = m_projectLoadResult.cancelled
            ? QStringLiteral("3MF project import cancelled")
            : errors.isEmpty() ? QStringLiteral("Failed to import 3MF project")
                               : errors.join(QStringLiteral("; "));
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, errorMessage);
        }
        emit importCompleted(QString(), false, errorMessage);
        LOG_ERROR("3MF project import failed: {}", errorMessage.toStdString());
        return;
    }

    if (m_loadResult.success &&
        (m_isGlbImport
             ? !m_glbLoadResult.printableTriangles.empty()
             : !m_loadResult.triangles.empty())) {
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
                m_context->setError(ActionErrorCode::Internal, "Failed to create mesh object");
            }
            emit importCompleted("", false, "Failed to create mesh object");
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

void UnifiedModelImportHandler::createModelsFromProject() {
    auto targetPrintBed = resolveTargetPrintBed();
    auto* document = DocumentManager::instance();
    const DBInstanceID slicingConfigId = activeSlicingConfigId();
    auto slicingConfig = document
        ? document->getDB<GPlatform::SlicingConfigDB>(slicingConfigId)
        : nullptr;
    if (!targetPrintBed || !document || !slicingConfig) {
        LOG_ERROR("UnifiedModelImportHandler::createModelsFromProject - no usable PrintBedDB");
        return;
    }

    std::unordered_map<std::string, libslicer::ProjectImportColor> filamentColors;
    std::unordered_map<std::string, int> filamentSlots;
    std::vector<GPlatform::SlicingConfigDB::Filament> documentFilaments;
    documentFilaments.reserve(m_projectLoadResult.filaments.size());
    for (std::size_t index = 0;
         index < m_projectLoadResult.filaments.size(); ++index) {
        const auto& filament = m_projectLoadResult.filaments[index];
        filamentColors.emplace(filament.id, filament.color);
        filamentSlots.emplace(filament.id, static_cast<int>(index + 1));
        GPlatform::SlicingConfigDB::Filament value;
        value.index = index;
        value.presetId = filament.preset_id;
        value.presetName = filament.name;
        value.vendor = filament.vendor;
        value.materialType = filament.material_type;
        value.color = QColor::fromRgbF(
            filament.color.red, filament.color.green,
            filament.color.blue, filament.color.alpha);
        documentFilaments.push_back(std::move(value));
    }

    TransactionGuard guard("Import 3MF Project");
    try {
        slicingConfig->setFilaments(documentFilaments);
        for (const auto& sourceObject : m_projectLoadResult.objects) {
        if (sourceObject.parts.empty() || sourceObject.instances.empty()) {
            LOG_ERROR("Invalid imported 3MF object hierarchy: {}", sourceObject.id);
            guard.rollback();
            m_createdObjectIds.clear();
            m_createdObjectId.clear();
            return;
        }

        std::vector<ModelGraphUtil::PartCreateInfo> partInfos;
        std::vector<std::optional<PreparedSurfaceColors>> preparedColors;
        partInfos.reserve(sourceObject.parts.size());
        preparedColors.reserve(sourceObject.parts.size());
        for (std::size_t index = 0; index < sourceObject.parts.size(); ++index) {
            const auto& sourcePart = sourceObject.parts[index];
            auto polyData = polyDataFromProjectPart(sourcePart);
            if (!polyData) {
                LOG_ERROR("Invalid imported 3MF Part: {}", sourcePart.id);
                guard.rollback();
                m_createdObjectIds.clear();
                m_createdObjectId.clear();
                return;
            }
            Transform::Matrix4 matrix;
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    matrix(row, column) = static_cast<float>(
                        sourcePart.local_transform[
                            static_cast<std::size_t>(row * 4 + column)]);
                }
            }
            Transform localTransform;
            localTransform.setMatrix(matrix);
            ModelPartRole role = ModelPartRole::Model;
            switch (sourcePart.role) {
            case libslicer::ProjectImportPartRole::Model:
                role = ModelPartRole::Model;
                break;
            case libslicer::ProjectImportPartRole::SupportEnforcer:
                role = ModelPartRole::SupportEnforcer;
                break;
            case libslicer::ProjectImportPartRole::SupportBlocker:
                role = ModelPartRole::SupportBlocker;
                break;
            }
            const auto slot = filamentSlots.find(sourcePart.filament_id);
            ModelGraphUtil::PartCreateInfo partInfo;
            partInfo.name = sourcePart.name;
            partInfo.geometry = std::move(polyData);
            partInfo.localTransform = localTransform;
            partInfo.role = role;
            partInfo.orderIndex = static_cast<int>(index);
            partInfo.defaultFilamentSlot = slot == filamentSlots.end()
                ? 1 : slot->second;
            partInfo.filamentBindingMode = slot == filamentSlots.end()
                ? ModelFilamentBindingMode::InheritProjectDefault
                : ModelFilamentBindingMode::ExplicitSlot;
            partInfos.push_back(std::move(partInfo));

            if (sourcePart.facet_labels.empty()) {
                preparedColors.push_back(std::nullopt);
            } else {
                auto prepared = prepareSurfaceColors(sourcePart);
                if (!prepared) {
                    LOG_ERROR("Invalid imported 3MF facet-color data: {}",
                              sourcePart.id);
                    guard.rollback();
                    m_createdObjectIds.clear();
                    m_createdObjectId.clear();
                    return;
                }
                preparedColors.push_back(std::move(prepared));
            }
        }

        auto graph = ModelGraphUtil::createObject(
            {sourceObject.name, m_filePath.toStdString(), "3mf", true},
            std::move(partInfos));
        if (!graph || graph.parts.size() != sourceObject.parts.size()) {
            LOG_ERROR("Unable to publish 3MF model object {}: {}",
                      sourceObject.id, graph.error);
            guard.rollback();
            m_createdObjectIds.clear();
            m_createdObjectId.clear();
            return;
        }

        for (std::size_t index = 0; index < graph.parts.size(); ++index) {
            const auto& sourcePart = sourceObject.parts[index];
            const auto& publishedPart = graph.parts[index];
            const auto color = filamentColors.find(sourcePart.filament_id);
            if (color != filamentColors.end()) {
                if (auto material = publishedPart->getMaterial()) {
                    const Vector3 rgb(
                        color->second.red, color->second.green,
                        color->second.blue);
                    material->setColor(rgb);
                    material->setDiffuseColor(rgb);
                }
            }
            if (!preparedColors[index]) continue;
            auto surfaceColors = trans::TransDB::create<ModelSurfaceColorDB>();
            if (!surfaceColors || !DocumentManager::instance()->attachOwnedChild(
                    publishedPart->getDBInstanceID(),
                    surfaceColors->getDBInstanceID(),
                    ModelPartDB::kSurfaceColorsRelation)) {
                LOG_ERROR("Unable to attach imported 3MF facet colors: {}",
                          sourcePart.id);
                guard.rollback();
                m_createdObjectIds.clear();
                m_createdObjectId.clear();
                return;
            }
            surfaceColors->setSurfaceColorData(
                preparedColors[index]->encodedState);
            surfaceColors->setSourceTriangleCount(
                static_cast<int>(sourcePart.triangles.size()));
            surfaceColors->setTopologyFingerprint(
                preparedColors[index]->topologyFingerprint);
            surfaceColors->setRevision(1);
            surfaceColors->setDataVersion(
                static_cast<int>(ModelSurfaceColorCodec::DataVersion));
        }

        for (const auto& sourceInstance : sourceObject.instances) {
            Transform::Matrix4 matrix;
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    matrix(row, column) = static_cast<float>(
                        sourceInstance.transform[
                            static_cast<std::size_t>(row * 4 + column)]);
                }
            }
            Transform transform;
            transform.setMatrix(matrix);
            std::string instanceError;
            auto modelInstance = ModelGraphUtil::createInstance(
                graph.object->getDBInstanceID(),
                {transform, targetPrintBed->getDBInstanceID(),
                 sourceInstance.printable,
                 static_cast<int>(m_createdObjectIds.size()),
                 slicingConfigId},
                &instanceError);
            if (!modelInstance) {
                LOG_ERROR("Unable to publish 3MF model instance: {}",
                          instanceError);
                guard.rollback();
                m_createdObjectIds.clear();
                m_createdObjectId.clear();
                return;
            }
            modelInstance->setDisplayName(sourceInstance.name.empty()
                ? sourceObject.name : sourceInstance.name);
            const QString instanceId = QString::number(
                modelInstance->getDBInstanceID().getValue());
            m_createdObjectIds.push_back(instanceId);
            if (m_createdObjectId.isEmpty()) m_createdObjectId = instanceId;
        }
        }

        if (m_createdObjectIds.isEmpty()) {
            guard.rollback();
            m_createdObjectId.clear();
            return;
        }
        guard.commit();
    } catch (const std::exception& exception) {
        guard.rollback();
        m_createdObjectIds.clear();
        m_createdObjectId.clear();
        LOG_ERROR(
            "Exception while publishing 3MF model graph: {}",
            exception.what());
        return;
    } catch (...) {
        guard.rollback();
        m_createdObjectIds.clear();
        m_createdObjectId.clear();
        LOG_ERROR("Unknown exception while publishing 3MF model graph");
        return;
    }
    if (auto cameraDB = CameraNavigationController::currentCamera()) {
        CameraNavigationController::fitScene(cameraDB);
    }
}

void UnifiedModelImportHandler::createModelFromResult() {
    TransactionGuard guard("Import Model");
    try {
        auto targetPrintBed = resolveTargetPrintBed();
        if (!targetPrintBed) {
            LOG_ERROR("UnifiedModelImportHandler::createModelFromResult - no usable PrintBedDB for model import");
            m_createdObjectId.clear();
            guard.rollback();
            return;
        }

        // 先计算位置（在对象创建之前）
        Vector3 modelSize = m_loadResult.boundingBoxMax - m_loadResult.boundingBoxMin;
        const Vector3 importPosition = calculateImportPosition(modelSize, targetPrintBed);
        LOG_INFO("Calculated import position: ({}, {}, {})",
                 importPosition.x, importPosition.y, importPosition.z);

        auto triangles = m_isGlbImport
            ? std::move(m_glbLoadResult.printableTriangles)
            : std::move(m_loadResult.triangles);
        if (triangles.empty()) {
            LOG_ERROR("Failed to create imported model part geometry");
            guard.rollback();
            return;
        }
        Transform instanceTransform;
        instanceTransform.identity();
        auto graph = ModelGraphUtil::createSinglePartObject(
            {m_name.toStdString(), m_filePath.toStdString(),
             ModelLoaderUtil::getFormatType(m_filePath).toStdString(), true},
            triangles,
            ModelGraphUtil::InstanceCreateInfo{
                instanceTransform, targetPrintBed->getDBInstanceID(), true, 0,
                activeSlicingConfigId()});
        if (!graph || !graph.instance || graph.parts.empty()) {
            LOG_ERROR("Failed to create imported model graph: {}", graph.error);
            guard.rollback();
            return;
        }
        if (m_isGlbImport &&
            !graph.object->publishGlbSourceAsset(m_glbLoadResult.asset)) {
            LOG_ERROR("Failed to retain imported GLB source appearance");
            guard.rollback();
            return;
        }
        if (auto material = graph.parts.front()->getMaterial()) {
            material->setColor(m_color);
            material->setDiffuseColor(m_color);
        }
        // Generic mesh formats carry appearance only. They deliberately do
        // not acquire a filament binding until the user assigns one.
        graph.parts.front()->setFilamentBindingMode(
            static_cast<int>(
                ModelFilamentBindingMode::InheritProjectDefault));
        applyTransformations(graph.instance, importPosition);

        m_createdObjectId = QString::number(
            graph.instance->getDBInstanceID().getValue());
        m_createdObjectIds = {m_createdObjectId};

        guard.commit();
        // 自动调整相机以适应新模型
        if (auto cameraDB = CameraNavigationController::currentCamera()) {
            CameraNavigationController::fitScene(cameraDB);
        }
        LOG_INFO("Created model graph with {} triangles, bounds: ({},{},{}) to ({},{},{})",
                 m_loadResult.faceCount,
                 m_loadResult.boundingBoxMin.x, m_loadResult.boundingBoxMin.y, m_loadResult.boundingBoxMin.z,
                 m_loadResult.boundingBoxMax.x, m_loadResult.boundingBoxMax.y, m_loadResult.boundingBoxMax.z);

    } catch (const std::exception& e) {
        guard.rollback();
        LOG_ERROR("Exception while creating model graph: {}", e.what());
        m_createdObjectId.clear();
        m_createdObjectIds.clear();
    } catch (...) {
        guard.rollback();
        LOG_ERROR("Unknown exception while creating model graph");
        m_createdObjectId.clear();
        m_createdObjectIds.clear();
    }
}

void UnifiedModelImportHandler::applyTransformations(
    const std::shared_ptr<ModelInstanceDB>& instance,
    const Vector3& importPosition) {
    if (!instance) return;

    Vector3 finalPosition = importPosition;
    float minZ = m_loadResult.boundingBoxMin.z;
    if (minZ < 0) {
        finalPosition.z += -minZ;  // 平移量等于最低点 z 坐标的绝对值
        LOG_INFO("Adjusted model Z position: moved up by {} units to place on platform", -minZ);
    }

    // 记录最终位置用于日志
    LOG_INFO("Using import position: model placed at ({}, {}, {})",
             finalPosition.x, finalPosition.y, finalPosition.z);

    // 使用 Transform 系统设置位置和缩放
    Transform transform = instance->getTransform();
    transform.setPosition(finalPosition);
    transform.setScale(Vector3(1.0f, 1.0f, 1.0f));
    instance->setTransform(transform);
}

void UnifiedModelImportHandler::onCleanup() {
    // 清理资源
    if (m_cancelRequested) {
        m_cancelRequested->store(true, std::memory_order_release);
    }
    m_loadResult.triangles.clear();
    m_projectLoadResult = {};
    m_createdObjectId.clear();
    m_createdObjectIds.clear();
    StandardBackgroundHandler::onCleanup();
}

QVariant UnifiedModelImportHandler::getCompletionResult() const {
    QVariantMap result;
    result["success"] = !m_createdObjectId.isEmpty() &&
        (m_isProjectImport ? m_projectLoadResult.success : m_loadResult.success);
    result["objectId"] = m_createdObjectId;
    result["modelId"] = m_createdObjectId;
    QVariantList objectIds;
    for (const QString& id : m_createdObjectIds) objectIds.push_back(id);
    result["objectIds"] = objectIds;
    result["errorMessage"] = m_isProjectImport
        ? QString() : m_loadResult.errorMessage;
    if (m_isProjectImport) {
        qulonglong vertexCount = 0;
        qulonglong faceCount = 0;
        for (const auto& object : m_projectLoadResult.objects) {
            for (const auto& part : object.parts) {
                vertexCount += static_cast<qulonglong>(part.vertices.size());
                faceCount += static_cast<qulonglong>(part.triangles.size());
            }
        }
        result["vertexCount"] = vertexCount;
        result["faceCount"] = faceCount;
        result["filamentCount"] = static_cast<qulonglong>(
            m_projectLoadResult.filaments.size());
    } else {
        result["vertexCount"] = static_cast<qulonglong>(m_loadResult.vertexCount);
        result["faceCount"] = static_cast<qulonglong>(m_loadResult.faceCount);
    }
    return result;
}

std::shared_ptr<PrintBedDB> UnifiedModelImportHandler::resolveTargetPrintBed() const {
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return nullptr;
    }

    auto isUsablePrintBed = [](const std::shared_ptr<PrintBedDB>& bed) {
        return bed && bed->getTemplateDBId().isValid() &&
            bed->getWidth() > 0.0f && bed->getHeight() > 0.0f;
    };

    const auto printBedIdParam = getParam("printBedId");
    if (printBedIdParam.isValid()) {
        const DBInstanceID printBedId(printBedIdParam.toULongLong());
        if (printBedId.isValid()) {
            auto bed = std::dynamic_pointer_cast<PrintBedDB>(
                docManager->getDBInstance(printBedId));
            if (isUsablePrintBed(bed)) {
                return bed;
            }
            LOG_WARN("UnifiedModelImportHandler::resolveTargetPrintBed - printBedId {} is not a usable PrintBedDB",
                     printBedId.getValue());
        }
    }

    for (const auto& object : docManager->getDBInstancesByType(TypeID::PRINT_BED_DB)) {
        auto bed = std::dynamic_pointer_cast<PrintBedDB>(object);
        if (isUsablePrintBed(bed)) return bed;
    }
    return {};
}

Vector3 UnifiedModelImportHandler::calculateImportPosition(
    const Vector3& modelSize,
    const std::shared_ptr<PrintBedDB>& targetPrintBed) const {
    if (!targetPrintBed) {
        LOG_WARN("UnifiedModelImportHandler::calculateImportPosition - no target print bed, using origin");
        return Vector3(0, 0, 0);
    }

    const DBInstanceID targetPrintBedId = targetPrintBed->getDBInstanceID();
    const Vector3 plateCenter = targetPrintBed->getCenter();
    const float plateWidth = targetPrintBed->getWidth();
    const float plateHeight = targetPrintBed->getHeight();
    const float safetyMargin = 10.0f;

    if (modelSize.x > plateWidth || modelSize.y > plateHeight) {
        LOG_WARN("UnifiedModelImportHandler::calculateImportPosition - model size {}x{} exceeds print bed {} size {}x{}, centering on bed",
                 modelSize.x, modelSize.y, targetPrintBedId.getValue(), plateWidth, plateHeight);
        return Vector3(
            plateCenter.x, plateCenter.y, targetPrintBed->getOrigin().z);
    }

    std::vector<ModelPositionUtil::ExistingObject> existingObjects;
    const auto modelInstances = DocumentManager::instance()->getDBInstancesByType(
        TypeID::MODEL_INSTANCE_DB);
    existingObjects.reserve(modelInstances.size());
    for (const auto& object : modelInstances) {
        const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(object);
        if (!instance ||
            instance->getParentPrintBedDBId() != targetPrintBedId) continue;

        const auto bounds = instance->worldBounds();
        if (!bounds.valid) continue;
        const Vector3 size = bounds.max - bounds.min;
        if (size.x <= 0.01f || size.y <= 0.01f || size.z <= 0.01f) continue;
        existingObjects.push_back({(bounds.min + bounds.max) * 0.5f, size});
    }

    const ModelPositionUtil::PlacementArea placementArea{
        plateWidth,
        plateHeight,
        plateCenter
    };
    Vector3 result = ModelPositionUtil::findAvailablePosition(
        modelSize, existingObjects, placementArea, safetyMargin);

    // PrintBed::Center.z is the solid bed body's center. Printable models sit
    // on the build-volume base at Origin.z.
    result.z = targetPrintBed->getOrigin().z;
    LOG_DEBUG("智能摆放结果: ({:.1f}, {:.1f}, {:.1f})", result.x, result.y, result.z);
    return result;
}
