#include "GCodeImportHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(GCodeImportHandler, "load_gcode_file")
#include "ActionHandlerRegistry.hpp"
#include "ToolpathPreviewDB.hpp"
#include "ToolpathPreviewPreparation.hpp"
#include "ToolpathResultPublication.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"
#include "MenuData.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include <SlicingPreviewBridge.hpp>
#include "SlicingBackend.hpp"
#include "AIDescriptorHelper.hpp"
#include "NotificationManager.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtConcurrent>

GCodeImportHandler::GCodeImportHandler(QObject* parent)
    : StandardBackgroundHandler(parent),
      m_parseWatcher(std::make_unique<QFutureWatcher<GPlatform::PreparedToolpathPreview>>()),
      m_cancelRequested(std::make_shared<std::atomic_bool>(false)) {
    connect(m_parseWatcher.get(),
            &QFutureWatcher<GPlatform::PreparedToolpathPreview>::finished,
            this,
            &GCodeImportHandler::onParseFinished);
}

GCodeImportHandler::~GCodeImportHandler() {
    if (m_cancelRequested) {
        m_cancelRequested->store(true, std::memory_order_release);
    }
    if (m_parseWatcher && m_parseWatcher->isRunning()) {
        try { m_parseWatcher->waitForFinished(); } catch (...) { /* Discard cancelled worker errors during destruction. */ }
    }
}

bool GCodeImportHandler::onInitialize() {
    if (m_parsePending || (m_parseWatcher && m_parseWatcher->isRunning())) {
        if (m_context) {
            m_context->setError(ActionErrorCode::Internal, "A GCode import is already running");
        }
        LOG_ERROR("GCode import requested while a previous parse is still running");
        return false;
    }

    auto* bridge = SlicingPreviewBridge::instance();
    m_publicationEpoch = bridge->publicationEpoch();
    if (!bridge->acceptsPublication(m_publicationEpoch)) return false;
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    m_prepared = {};
    m_pendingOutput.reset();
    m_acceptedOutput.reset();
    m_completionEpoch = 0;
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

    // Pin a managed input before the deferred start can release the old result.
    m_publicationEpoch = bridge->beginOutputTask();
    m_pendingOutput = std::make_shared<GPlatform::SliceOutputRecord>();
    m_pendingOutput->setArtifacts(m_filePath, false, {}, false, bridge->currentOutput());
    m_pendingOutput->name = m_name;
    m_pendingOutput->sourceScope = GPlatform::ToolpathPreviewSourceScope::ImportedGCode;

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
    const auto epoch = m_publicationEpoch;
    QTimer::singleShot(300, this, [this, epoch]() {
        if (getState() == State::Background &&
            m_cancelRequested &&
            !m_cancelRequested->load(std::memory_order_acquire)) {
            if (SlicingPreviewBridge::instance()->acceptsPublication(epoch)) parseGCodeInBackground();
            else {
                m_pendingOutput.reset();
                m_success = false; m_errorMessage = QStringLiteral("G-code import cancelled or superseded");
                requestResumeFromBackground();
            }
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

    auto* bridge = SlicingPreviewBridge::instance();
    const auto epoch = m_publicationEpoch;
    const auto inputLease = m_pendingOutput;
    auto future = QtConcurrent::run([filePath, cancelRequested, handler, epoch, bridge, inputLease]() {
        (void)inputLease; // Pins an existing temporary input throughout parsing.
        libslicer::SliceCallbacks callbacks;
        callbacks.is_cancelled = [cancelRequested, epoch, bridge] {
            return !cancelRequested || cancelRequested->load(std::memory_order_acquire) || !bridge->acceptsPublication(epoch);
        };
        callbacks.progress = [handler, cancelRequested, epoch, bridge](float progress, std::string_view stage) {
            if (!handler || cancelRequested->load() || !bridge->acceptsPublication(epoch)) return;
            const QString description = QString::fromUtf8(stage.data(), qsizetype(stage.size()));
            QMetaObject::invokeMethod(handler, [handler, cancelRequested, epoch, bridge, progress, description] {
                if (!handler || cancelRequested->load() || !bridge->acceptsPublication(epoch) ||
                    handler->getState() != State::Background) return;
                handler->updateTaskProgress(progress); handler->updateTaskDescription(description);
            }, Qt::QueuedConnection);
        };
        return GPlatform::prepareImportedToolpath(QString::fromStdString(filePath),
            [&](const std::string& source) {
                libslicer::GCodePreviewRequest request; request.gcode_path = source;
                return GPlatform::SlicingBackend::instance().loadGCodePreview(request, callbacks);
            }, callbacks.is_cancelled);
    });
    m_parsePending = true;
    m_parseWatcher->setFuture(future);
}

void GCodeImportHandler::onParseFinished() {
    m_parsePending = false;
    try {
        m_prepared = m_parseWatcher->future().takeResult();
    } catch (const std::exception& error) {
        m_prepared = {};
        m_prepared.error = QString::fromUtf8(error.what());
    } catch (...) {
        m_prepared = {};
        m_prepared.error = QStringLiteral("Unexpected G-code preparation failure");
    }
    if (getState() != State::Background || !m_cancelRequested || m_cancelRequested->load(std::memory_order_acquire) ||
        !SlicingPreviewBridge::instance()->acceptsPublication(m_publicationEpoch)) {
        LOG_INFO("GCode parse finished after handler left background state; result discarded");
        m_prepared = {};
        m_pendingOutput.reset();
        m_success = false; m_errorMessage = QStringLiteral("G-code import cancelled");
        if (getState() == State::Background) requestResumeFromBackground();
        return;
    }

    m_success = m_prepared.geometryAvailable && !m_prepared.cancelled && m_prepared.error.isEmpty();
    if (!m_success) {
        m_errorMessage = m_prepared.cancelled ? QStringLiteral("GCode import cancelled") : m_prepared.error;
        LOG_ERROR("GCode import failed: {}", m_errorMessage.toStdString());
        requestResumeFromBackground();
        return;
    }
    for (const auto& warning : m_prepared.warnings) LOG_WARN("{}", warning.toStdString());
    const auto& statistics = m_prepared.data.stats;
    m_layerCount = statistics.totalLayers;
    m_segmentCount = statistics.renderSegmentCount;
    m_pointCount = m_segmentCount * 2;
    m_totalTime = statistics.totalTimeS;
    m_totalExtrusion = statistics.totalExtrusionMm;

    LOG_INFO("GCode parsed by libslicer: {} layers, {} render segments",
             m_layerCount, m_segmentCount);
    updateTaskProgress(0.7f);
    updateTaskDescription(QStringLiteral("正在创建刀路预览..."));
    emit importProgress(70, QStringLiteral("Creating toolpath preview..."));
    try { createToolpathPreviewDB(); }
    catch (const std::exception& error) {
        m_success = false; m_errorMessage = QString::fromUtf8(error.what());
        m_pendingOutput.reset(); requestResumeFromBackground();
    } catch (...) {
        m_success = false; m_errorMessage = QStringLiteral("Unexpected toolpath publication failure");
        m_pendingOutput.reset(); requestResumeFromBackground();
    }
}

void GCodeImportHandler::createToolpathPreviewDB() {
    if (!m_success || !m_pendingOutput) { requestResumeFromBackground(); return; }
    auto* document = DocumentManager::instance();
    const auto beds = document->getDBInstancesByType(TypeID::PRINT_BED_DB);
    for (const auto& bed : beds) if (bed) { m_pendingOutput->printBedId = bed->getDBInstanceID(); break; }
    m_pendingOutput->stats = m_prepared.data.stats;
    auto published = GPlatform::publishToolpathResult(m_pendingOutput, std::move(m_prepared), m_publicationEpoch);
    m_success = published.accepted;
    if (!m_success) { m_errorMessage = published.error; m_pendingOutput.reset(); requestResumeFromBackground(); return; }
    m_createdObjectId = QString::fromStdString(published.preview->getDBInstanceID().toString());
    m_acceptedOutput = SlicingPreviewBridge::instance()->currentOutput();
    m_completionEpoch = SlicingPreviewBridge::instance()->publicationEpoch();
    m_pendingOutput.reset();
    updateTaskProgress(0.95f); updateTaskDescription(QStringLiteral("正在完成..."));
    QTimer::singleShot(0, this, [this]() { requestResumeFromBackground(); });
}

void GCodeImportHandler::onBackgroundCompleted() {
    LOG_INFO("GCode import background task completed");

    auto* bridge = SlicingPreviewBridge::instance();
    const auto accepted = m_acceptedOutput.lock();
    if (m_success && (!bridge->acceptsPublication(m_completionEpoch) || !accepted || bridge->currentOutput() != accepted)) {
        m_success = false;
        m_errorMessage = QStringLiteral("G-code import cancelled or superseded");
    }

    if (m_success) {
        // 更新进度：完成
        updateTaskProgress(1.0f);
        updateTaskDescription(QString("加载完成"));
        emit importProgress(100, "Import completed successfully");
        if (!accepted->warnings.isEmpty())
            NotificationManager::instance()->showWarning(tr("G-code import warnings"), accepted->warnings.join(QStringLiteral("\n")));
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
    m_prepared = {};
    m_pendingOutput.reset();
    m_acceptedOutput.reset();
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
