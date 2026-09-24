#include "GCodeImportHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(GCodeImportHandler, "load_gcode_file")
#include "ActionHandlerRegistry.hpp"
#include "ToolpathPreviewDB.hpp"
#include "ToolpathPreviewRelations.hpp"
#include "LibSlicerToolpathAdapter.hpp"
#include <DocumentManager.hpp>
#include <PrintBedDB.hpp>
#include "TransactionManager.hpp"
#include "Foundation/Log.h"
#include "MenuData.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include <SlicingPreviewBridge.hpp>
#include "SlicingBackend.hpp"
#include "AIDescriptorHelper.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtConcurrent>
#include <algorithm>

GCodeImportHandler::GCodeImportHandler(QObject* parent)
    : StandardBackgroundHandler(parent),
      m_parseWatcher(std::make_unique<QFutureWatcher<libslicer::GCodePreviewResult>>()),
      m_cancelRequested(std::make_shared<std::atomic_bool>(false)) {
    connect(m_parseWatcher.get(),
            &QFutureWatcher<libslicer::GCodePreviewResult>::finished,
            this,
            &GCodeImportHandler::onParseFinished);
}

GCodeImportHandler::~GCodeImportHandler() {
    if (m_cancelRequested) {
        m_cancelRequested->store(true, std::memory_order_release);
    }
    if (m_parseWatcher && m_parseWatcher->isRunning()) {
        m_parseWatcher->waitForFinished();
    }
}

bool GCodeImportHandler::onInitialize() {
    if (m_parseWatcher && m_parseWatcher->isRunning()) {
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, "A GCode import is already running");
        }
        LOG_ERROR("GCode import requested while a previous parse is still running");
        return false;
    }

    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    m_preview.reset();
    m_createdObjectId.clear();
    m_success = false;
    m_errorMessage.clear();
    m_layerCount = 0;
    m_segmentCount = 0;
    m_pointCount = 0;
    m_totalTime = 0.0f;
    m_totalExtrusion = 0.0f;

    // 获取 confirm 参数（AI 调用需要明确确认）
    bool confirm = getParam("confirm").toBool();
    if (!confirm) {
        if (m_context) {
            m_context->setError(ActionErrorCode::InvalidParams, "confirm parameter is required and must be true for AI calls");
        }
        LOG_WARN("GCode import requires confirm=true for AI calls");
        return false;
    }

    // 获取文件路径参数
    auto filePath = getParam("filePath");
    if (!filePath.isValid() || filePath.toString().isEmpty()) {
        // 也支持 filepath 参数名（兼容小写）
        filePath = getParam("filepath");
    }

    if (!filePath.isValid() || filePath.toString().isEmpty()) {
        if (m_context) {
            m_context->setError(ActionErrorCode::InvalidParams, "No file path provided");
        }
        LOG_ERROR("No file path provided for GCode import");
        return false;
    }

    m_filePath = filePath.toString();
    LOG_INFO("Importing GCode from: {}", m_filePath.toStdString());

    // 检查文件扩展名
    QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix != "gcode" && suffix != "gco" && suffix != "g") {
        if (m_context) {
            m_context->setError(ActionErrorCode::InvalidParams, QString("Unsupported file format: %1 (expected .gcode, .gco, .g)")
                              .arg(suffix));
        }
        LOG_ERROR("Unsupported GCode file format: {}", suffix.toStdString());
        return false;
    }

    // 检查文件是否存在
    QFileInfo fileInfo(m_filePath);
    if (!fileInfo.exists()) {
        if (m_context) {
            m_context->setError(ActionErrorCode::TargetNotFound, QString("File not found: %1").arg(m_filePath));
        }
        LOG_ERROR("GCode file not found: {}", m_filePath.toStdString());
        return false;
    }

    // 获取名称参数
    auto name = getParam("name");
    if (name.isValid() && !name.toString().isEmpty()) {
        m_name = name.toString();
    } else {
        m_name = fileInfo.fileName();
    }

    // 发送开始信号
    emit importProgress(0, QString("Starting import of %1").arg(m_name));

    return true;
}

const QHash<QString, QVariantMap>& GCodeImportHandler::aiDescriptorTable() const {
    // filePath 和 filepath 是互为别名，需要通过 anyOfRequired 校验至少有一个
    auto gcodeSchema = makeInputSchema({
        {"filePath", "string", false},
        {"filepath", "string", false},
        {"name", "string", false},
        {"confirm", "bool", true}
    }, false);
    gcodeSchema["anyOfRequired"] = QVariantList{
        QVariantList{"filePath"},
        QVariantList{"filepath"}
    };

    static const QHash<QString, QVariantMap> table = {
        {"load_gcode_file", makeDescriptor(
            "load_gcode_file",
            "Import GCode",
            "Load a local GCode file from allowed paths.",
            "write",
            gcodeSchema,
            {"import", "gcode", "slicing"}
        )}
    };
    return table;
}

std::optional<QVariantMap> GCodeImportHandler::getAIDescriptor(const QString& actionCode) const {
    auto it = aiDescriptorTable().find(actionCode);
    if (it != aiDescriptorTable().end()) {
        return std::make_optional(*it);
    }
    return std::nullopt;
}

void GCodeImportHandler::onBackgroundStarted() {
    LOG_INFO("Background GCode import started for: {}", m_filePath.toStdString());

    // 更新进度：开始加载
    updateTaskProgress(0.05f);
    updateTaskDescription(QString("正在解析 %1...").arg(m_name));

    // 延迟 300ms 后开始解析，让进度条有时间显示
    QTimer::singleShot(300, this, [this]() {
        if (getState() == State::Background &&
            m_cancelRequested &&
            !m_cancelRequested->load(std::memory_order_acquire)) {
            parseGCodeInBackground();
        }
    });
}

void GCodeImportHandler::parseGCodeInBackground() {
    updateTaskProgress(0.1f);
    updateTaskDescription(QStringLiteral("正在解析 GCode 文件..."));
    emit importProgress(10, QStringLiteral("Parsing GCode file..."));

    const std::string filePath = m_filePath.toStdString();
    const auto cancelRequested = m_cancelRequested;
    const QPointer<GCodeImportHandler> handler(this);

    auto future = QtConcurrent::run([filePath, cancelRequested, handler]() {
        libslicer::GCodePreviewRequest request;
        request.gcode_path = filePath;
        libslicer::SliceCallbacks callbacks;
        callbacks.is_cancelled = [cancelRequested]() {
            return !cancelRequested || cancelRequested->load(std::memory_order_acquire);
        };
        callbacks.progress = [handler, cancelRequested](float progress, std::string_view stage) {
            if (!handler || !cancelRequested || cancelRequested->load(std::memory_order_acquire)) {
                return;
            }
            const QString description = QString::fromUtf8(
                stage.data(), static_cast<qsizetype>(stage.size()));
            QMetaObject::invokeMethod(
                handler,
                [handler, cancelRequested, progress, description]() {
                    if (!handler || !cancelRequested ||
                        cancelRequested->load(std::memory_order_acquire) ||
                        handler->getState() != State::Background) {
                        return;
                    }
                    handler->updateTaskProgress(progress);
                    handler->updateTaskDescription(description);
                },
                Qt::QueuedConnection);
        };
        return GPlatform::SlicingBackend::instance().loadGCodePreview(request, callbacks);
    });
    m_parseWatcher->setFuture(future);
}

void GCodeImportHandler::onParseFinished() {
    if (getState() != State::Background) {
        LOG_INFO("GCode parse finished after handler left background state; result discarded");
        return;
    }

    const auto loaded = m_parseWatcher->result();
    m_success = loaded.success;
    m_preview = loaded.preview;
    if (!m_success || !m_preview) {
        QStringList errors;
        for (const auto& diagnostic : loaded.diagnostics) {
            if (!diagnostic.warning) {
                errors.push_back(QString::fromStdString(diagnostic.message));
            }
        }
        m_errorMessage = loaded.cancelled
            ? QStringLiteral("GCode import cancelled")
            : errors.join(QStringLiteral("; "));
        if (m_errorMessage.isEmpty()) {
            m_errorMessage = QStringLiteral("GCode contains no drawable toolpath");
        }
        LOG_ERROR("GCode import failed: {}", m_errorMessage.toStdString());
        requestResumeFromBackground();
        return;
    }

    const auto& statistics = m_preview->statistics;
    m_layerCount = static_cast<int>(statistics.total_layers);
    m_segmentCount = static_cast<int>(statistics.render_segment_count);
    m_pointCount = m_segmentCount * 2;
    m_totalTime = static_cast<float>(statistics.total_time_seconds);
    m_totalExtrusion = static_cast<float>(statistics.total_extrusion_mm);

    LOG_INFO("GCode parsed by libslicer: {} layers, {} render segments",
             m_layerCount, m_segmentCount);
    updateTaskProgress(0.7f);
    updateTaskDescription(QStringLiteral("正在创建刀路预览..."));
    emit importProgress(70, QStringLiteral("Creating toolpath preview..."));
    createToolpathPreviewDB();
}

void GCodeImportHandler::createToolpathPreviewDB() {
    if (!m_preview) {
        m_success = false;
        m_errorMessage = QStringLiteral("libslicer returned no toolpath preview");
        requestResumeFromBackground();
        return;
    }

    TransactionGuard guard("Load GCode Toolpath Preview");
    auto preview = trans::TransDB::create<GPlatform::ToolpathPreviewDB>();
    if (!preview) {
        m_success = false;
        m_errorMessage = QStringLiteral("Failed to create ToolpathPreviewDB");
        requestResumeFromBackground();
        return;
    }

    auto data = GPlatform::adaptLibSlicerToolpath(*m_preview);
    data.printGCodePath = m_filePath.toStdString();
    preview->setSourceModelName(m_name.toStdString());
    preview->setSourceScope(
        GPlatform::ToolpathPreviewSourceScope::ImportedGCode);
    preview->setSourceModelDBId(INVALID_DB_ID);
    preview->setPreviewData(std::move(data));
    preview->setVisible(true);

    if (auto* manager = DocumentManager::instance()) {
        manager->registerDBInstance(preview, TypeID::TOOLPATH_PREVIEW_DB);
        const auto beds = manager->getDBInstancesByType(
            TypeID::PRINT_BED_DB);
        const auto bed = std::min_element(
            beds.begin(), beds.end(),
            [](const auto& lhs, const auto& rhs) {
                if (!lhs) return false;
                if (!rhs) return true;
                return lhs->getDBInstanceID().getValue() <
                    rhs->getDBInstanceID().getValue();
            });
        if (bed == beds.end() || !*bed ||
            !manager->attachOwnedChild(
                (*bed)->getDBInstanceID(),
                preview->getDBInstanceID(),
                ToolpathPreviewRelations::PrintBedPreviewsRelation)) {
            m_success = false;
            m_errorMessage = QStringLiteral(
                "Failed to attach imported GCode preview to print bed");
            guard.rollback();
            requestResumeFromBackground();
            return;
        }
    } else {
        m_success = false;
        m_errorMessage = QStringLiteral("DocumentManager is unavailable");
        requestResumeFromBackground();
        return;
    }

    m_createdObjectId = QString::number(preview->getDBInstanceID().getValue());
    if (auto* bridge = SlicingPreviewBridge::instance()) {
        const int previewId = static_cast<int>(preview->getDBInstanceID().getValue());
        bridge->completePreviewLoad(QString(), m_filePath, previewId, m_name, preview);
    }

    updateTaskProgress(0.95f);
    updateTaskDescription(QStringLiteral("正在完成..."));
    QTimer::singleShot(0, this, [this]() { requestResumeFromBackground(); });
}

void GCodeImportHandler::onBackgroundCompleted() {
    LOG_INFO("GCode import background task completed");

    if (m_success) {
        // 更新进度：完成
        updateTaskProgress(1.0f);
        updateTaskDescription(QString("加载完成"));
        emit importProgress(100, "Import completed successfully");
        emit importCompleted(m_createdObjectId, true);

        LOG_INFO("GCode imported successfully with ID: {}", m_createdObjectId.toStdString());
        // 注意：环境切换由 SlicingHandler 处理，此处不再切换
    } else {
        emit importCompleted("", false, m_errorMessage);
        LOG_ERROR("GCode import failed: {}", m_errorMessage.toStdString());

        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, m_errorMessage);
        }
    }
}

void GCodeImportHandler::onCleanup() {
    if (m_cancelRequested) {
        m_cancelRequested->store(true, std::memory_order_release);
    }
    m_createdObjectId.clear();
    m_preview.reset();
    StandardBackgroundHandler::onCleanup();
}

QVariant GCodeImportHandler::getCompletionResult() const {
    QVariantMap result;
    result["success"] = m_success;
    result["objectId"] = m_createdObjectId;
    result["errorMessage"] = m_errorMessage;
    result["layerCount"] = m_layerCount;
    result["segmentCount"] = m_segmentCount;
    result["pointCount"] = m_pointCount;
    result["totalTime"] = m_totalTime;
    result["totalExtrusion"] = m_totalExtrusion;
    result["fileName"] = m_name;
    return result;
}
