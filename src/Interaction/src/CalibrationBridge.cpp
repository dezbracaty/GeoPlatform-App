#include "CalibrationBridge.hpp"
#include "InteractionRegistration.hpp"
#include "SlicingBackend.hpp"
#include "SliceSettingsBridge.hpp"
#include "SlicingPreviewBridge.hpp"
#include "ToolpathPreviewPreparation.hpp"
#include "ToolpathResultPublication.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include "TaskStateNotifier.hpp"
#include <DocumentManager.hpp>
#include <PrintBedDB.hpp>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <cmath>

REGISTER_INTERACTION_QML_SINGLETON_CUSTOM(
    CalibrationBridge, "CalibrationBridge", &CalibrationBridge::create)

namespace {
using namespace GPlatform;
QString value(const libslicer::ConfigSnapshot& config, const char* key) {
    const auto found = config.value(key);
    return found ? QString::fromStdString(*found) : QString();
}
QString first(const libslicer::ConfigSnapshot& config, const char* key) {
    return value(config, key).section(',', 0, 0).trimmed();
}
QString title(libslicer::CalibMode mode) {
    switch (mode) {
    case libslicer::CalibMode::Calib_PA_Line: return QStringLiteral("压力提前 · PA Line");
    case libslicer::CalibMode::Calib_PA_Pattern: return QStringLiteral("压力提前 · PA Pattern");
    case libslicer::CalibMode::Calib_PA_Tower: return QStringLiteral("压力提前 · PA Tower");
    case libslicer::CalibMode::Calib_Flow_Rate: return QStringLiteral("流量比例");
    case libslicer::CalibMode::Calib_Temp_Tower: return QStringLiteral("温度");
    case libslicer::CalibMode::Calib_Vol_speed_Tower: return QStringLiteral("最大体积流量");
    case libslicer::CalibMode::Calib_VFA_Tower: return QStringLiteral("VFA");
    case libslicer::CalibMode::Calib_Retraction_tower: return QStringLiteral("回抽");
    case libslicer::CalibMode::Calib_Input_shaping_freq: return QStringLiteral("输入整形 · 频率");
    case libslicer::CalibMode::Calib_Input_shaping_damp: return QStringLiteral("输入整形 · 阻尼");
    case libslicer::CalibMode::Calib_Cornering: return QStringLiteral("转角");
    case libslicer::CalibMode::Calib_Tolerance_Model: return QStringLiteral("公差测试");
    default: return QStringLiteral("校准");
    }
}
QString diagnostics(const std::vector<libslicer::SliceDiagnostic>& messages) {
    QStringList result;
    for (const auto& item : messages)
        if (!item.warning) result << QString::fromStdString(item.message);
    return result.join('\n');
}
std::string serialized(const QVariant& value) {
    return value.metaType().id() == QMetaType::Bool ? (value.toBool() ? "1" : "0") : value.toString().toStdString();
}
std::vector<std::pair<std::string, std::string>> parameterValues(const QVariantMap& values) {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto it = values.begin(); it != values.end(); ++it)
        if (it.key() != "mode") result.emplace_back(it.key().toStdString(), serialized(it.value()));
    return result;
}
QVariantMap formValues(const libslicer::CalibrationParameters& form) {
    QVariantMap values{{"mode", int(form.mode)}};
    for (const auto& f : form.fields)
        values.insert(QString::fromStdString(f.key), f.type == libslicer::SettingType::Boolean
            ? QVariant(f.value == "1") : QVariant(QString::fromStdString(f.value)));
    return values;
}
QVariantMap formDiagnostics(const std::vector<libslicer::SliceDiagnostic>& diagnostics) {
    QStringList errors, warnings;
    for (const auto& d : diagnostics) {
        const auto message = QCoreApplication::translate("Calibration", d.message.c_str());
        (d.warning ? warnings : errors) << message;
    }
    return {{"error", errors.join('\n')}, {"warnings", warnings}};
}
QVariantMap formView(const libslicer::CalibrationParameters& form) {
    QVariantList fields;
    for (const auto& f : form.fields) {
        QVariantList choices;
        for (const auto& choice : f.enum_items)
            choices << QVariantMap{{"value", QString::fromStdString(choice.value)}, {"label", QString::fromStdString(choice.label)}};
        fields << QVariantMap{{"key", QString::fromStdString(f.key)}, {"label", QString::fromStdString(f.label)},
            {"unit", QString::fromStdString(f.unit)}, {"enabled", f.enabled && !f.read_only}, {"choices", choices},
            {"visible", f.visible},
            {"type", f.type == libslicer::SettingType::Enum ? "enum" : f.type == libslicer::SettingType::Boolean ? "boolean" : "text"},
            {"minimum", f.minimum ? QVariant(*f.minimum) : QVariant()}, {"maximum", f.maximum ? QVariant(*f.maximum) : QVariant()}};
    }
    auto view = formDiagnostics(form.diagnostics);
    view.insert("values", formValues(form)); view.insert("fields", fields); view.insert("canRefine", form.can_refine);
    return view;
}
DBInstanceID printBed() {
    auto* document = DocumentManager::instance();
    DBInstanceID id{INVALID_DB_ID};
    if (document) for (const auto& bed : document->getDBInstancesByType(TypeID::PRINT_BED_DB))
        if (bed && (!id.isValid() || bed->getDBInstanceID().getValue() < id.getValue())) id = bed->getDBInstanceID();
    return id;
}
QString resultKey(libslicer::CalibMode mode) {
    using M = libslicer::CalibMode;
    switch (mode) {
    case M::Calib_PA_Line: case M::Calib_PA_Pattern: case M::Calib_PA_Tower: return "pressure_advance";
    case M::Calib_Flow_Rate: return "filament_flow_ratio";
    case M::Calib_Temp_Tower: return "nozzle_temperature";
    case M::Calib_Vol_speed_Tower: return "filament_max_volumetric_speed";
    case M::Calib_Retraction_tower: return "filament_retraction_length";
    default: return {};
    }
}
}

struct CalibrationBridge::Session {
    struct Plate {
        std::shared_ptr<GPlatform::SliceOutputRecord> output;
        libslicer::ToolpathPreviewPtr preview;
        QString error;
    };
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString materialName;
    GPlatform::SlicingProfileSnapshot profile;
    libslicer::CalibrationRequest request;
    std::unique_ptr<libslicer::CalibrationTask> task;
    std::vector<Plate> plates;
    std::shared_ptr<std::atomic_bool> valid = std::make_shared<std::atomic_bool>(true);
    std::atomic_bool cancelled{false};
    DBInstanceID bed{INVALID_DB_ID};
    std::uint64_t epoch = 0;
    int target = -1, failed = -1;
    QString error;
    QStringList details;
    bool success = false, canRefine = false;
    GPlatform::PreparedToolpathPreview prepared;
};

CalibrationBridge* CalibrationBridge::instance() {
    static auto* bridge = new CalibrationBridge(QCoreApplication::instance());
    return bridge;
}
CalibrationBridge* CalibrationBridge::create(QQmlEngine*, QJSEngine*) {
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}
CalibrationBridge::CalibrationBridge(QObject* parent) : QObject(parent) {
    m_available = GPlatform::SlicingBackend::instance().available();
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &CalibrationBridge::finishWork);
    auto* settings = SliceSettingsBridge::instance();
    connect(settings, &SliceSettingsBridge::settingsChanged, this, &CalibrationBridge::invalidate);
    connect(settings, &SliceSettingsBridge::machineSelectionChanged, this, &CalibrationBridge::invalidate);
    connect(settings, &SliceSettingsBridge::presetSelectionChanged, this, &CalibrationBridge::invalidate);
    connect(SlicingPreviewBridge::instance(), &SlicingPreviewBridge::printOutputPathChanged, this, [this] { emit changed(); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &CalibrationBridge::shutdown);
}
CalibrationBridge::~CalibrationBridge() { shutdown(); }
bool CalibrationBridge::configurationValid() const { return m_session && m_session->valid->load(); }
bool CalibrationBridge::previewActive() const {
    const auto output = SlicingPreviewBridge::instance()->currentOutput();
    return m_session && output && output->calibrationTaskId == m_session->id;
}
QVariantMap CalibrationBridge::currentContext() const {
    // Never enqueue a synchronous configuration query behind a long SDK job.
    if (TaskStateNotifier::instance()->isBusy() || !GPlatform::SlicingBackend::instance().configurationChangesAllowed()) return {};
    const auto profile = GPlatform::SlicingBackend::instance().activeProfileSnapshot();
    if (!profile) return {};
    auto* settings = SliceSettingsBridge::instance();
    return {{"printer", settings->selectedMachine().value("name")},
            {"material", settings->selectedFilamentPresetName()}, {"slot", 0},
            {"flowRatio", first(profile->config,"filament_flow_ratio")}};
}
QVariantMap CalibrationBridge::parameterForm(int mode, const QVariantMap& values) {
    if (TaskStateNotifier::instance()->isBusy() || !GPlatform::SlicingBackend::instance().configurationChangesAllowed())
        return {{"error", QStringLiteral("任务运行中，请等待完成后编辑参数。")}};
    try {
        const auto profile = GPlatform::SlicingBackend::instance().activeProfileSnapshot();
        if (!profile) throw std::runtime_error("Calibration has no configuration");
        m_form = libslicer::calibration_parameters(static_cast<libslicer::CalibMode>(mode), profile->config, parameterValues(values));
        return formView(*m_form);
    } catch (const std::exception& e) {
        m_form.reset(); return {{"error", QString::fromUtf8(e.what())}};
    }
}
QVariantMap CalibrationBridge::updateParameter(const QString& key, const QVariant& value, bool focusOut) {
    if (!m_form) return {{"error", QStringLiteral("请先载入校准参数。")}};
    try {
        // Pure SDK operation on the captured snapshot; no synchronous query is
        // enqueued behind a long-running slicing job on each text edit.
        m_form = libslicer::update_calibration_parameters(*m_form, key.toStdString(), serialized(value), focusOut);
        return formView(*m_form);
    } catch (const std::exception& e) { return {{"error", QString::fromUtf8(e.what())}}; }
}
QVariantMap CalibrationBridge::validateParameterForm() const {
    if (!m_form) return {{"error", QStringLiteral("请先载入校准参数。")}};
    return formDiagnostics(libslicer::validate_calibration_parameters(*m_form).diagnostics);
}
void CalibrationBridge::openParameters(int mode) {
    if (!m_closing && m_available && !TaskStateNotifier::instance()->isBusy()) emit parametersRequested(mode);
}
bool CalibrationBridge::prepare(const QVariantMap& parameters) {
    if (m_closing || m_running || !m_available || TaskStateNotifier::instance()->isBusy()) return false;
    auto session = std::make_shared<Session>();
    m_error.clear();
    const auto profile = GPlatform::SlicingBackend::instance().activeProfileSnapshot();
    session->bed = printBed();
    if (!profile || !profile->valid() || !session->bed.isValid()) {
        m_error = QStringLiteral("请先选择有效的打印配置并打开准备工作区。"); emit changed(); return false;
    }
    session->profile = *profile;
    try {
        const auto form = libslicer::calibration_parameters(static_cast<libslicer::CalibMode>(parameters.value("mode").toInt()),
            profile->config, parameterValues(parameters));
        auto validation = libslicer::validate_calibration_parameters(form);
        if (!validation) { m_error = formDiagnostics(validation.diagnostics).value("error").toString(); emit changed(); return false; }
        session->request = std::move(validation.request);
        session->canRefine = form.can_refine;
        m_form = form;
    } catch (const std::exception& e) { m_error = QString::fromUtf8(e.what()); emit changed(); return false; }
    session->materialName = SliceSettingsBridge::instance()->selectedFilamentPresetName();
    session->request.config = profile->config;
    invalidate();
    m_session = session;
    m_parameters = formValues(*m_form);
    m_details.clear();
    m_recordPath.clear();
    m_plates.clear(); m_selectedPlate = -1;
    startWork(true);
    return true;
}
void CalibrationBridge::cancel() {
    if (!m_running || !m_session) return;
    if (m_session->cancelled.exchange(true)) return;
    m_state = "cancelling"; m_message = QStringLiteral("正在取消，等待当前操作结束…");
    if (!m_taskId.isEmpty()) TaskStateNotifier::instance()->updateDescription(m_taskId, m_message);
    emit changed();
}
void CalibrationBridge::invalidate() {
    if (!m_session || !m_session->valid->exchange(false)) return;
    if (m_running) cancel();
    else m_state = "stale";
    SlicingPreviewBridge::instance()->notifyCalibrationOutputInvalidated();
    emit changed();
}
bool CalibrationBridge::retry() {
    if (!m_session || m_running || m_closing || !configurationValid() || TaskStateNotifier::instance()->isBusy()) return false;
    startWork(!m_session->task);
    return true;
}
bool CalibrationBridge::selectPlate(int index) {
    if (!m_session || m_running || m_closing || !configurationValid() || TaskStateNotifier::instance()->isBusy() ||
        index < 0 || index >= int(m_session->plates.size()) || !m_session->plates[index].output) return false;
    if (previewActive() && index == m_selectedPlate) return true;
    startWork(false, index);
    return true;
}
void CalibrationBridge::clear() {
    if (m_running) { cancel(); return; }
    invalidate(); m_session.reset(); m_form.reset(); m_parameters.clear(); m_plates.clear();
    m_selectedPlate = -1; m_state = "idle"; m_error.clear(); m_details.clear(); m_recordPath.clear(); emit changed();
}
void CalibrationBridge::shutdown() {
    if (m_closing) return;
    m_closing = true;
    if (m_session) m_session->cancelled.store(true);
    if (m_watcher.isRunning()) { try { m_watcher.waitForFinished(); } catch (...) {} }
    if (!m_taskId.isEmpty()) TaskStateNotifier::instance()->endTask(m_taskId);
    m_taskId.clear(); m_running = false; m_session.reset(); m_form.reset();
}
void CalibrationBridge::startWork(bool prepareTask, int plate) {
    auto session = m_session;
    session->cancelled.store(false); session->success = false; session->error.clear();
    session->failed = -1; session->target = plate; session->prepared = {};
    session->epoch = SlicingPreviewBridge::instance()->beginOutputTask();
    m_running = true; m_error.clear(); m_progress = -1;
    const auto generation = ++m_generation;
    m_state = prepareTask ? "preparing" : plate >= 0 ? "loading" : "slicing";
    m_message = prepareTask ? QStringLiteral("正在准备校准模型与排盘…") : plate >= 0 ? QStringLiteral("正在加载打印盘 %1…").arg(plate+1) : QStringLiteral("正在切片校准任务…");
    m_taskId = TaskStateNotifier::instance()->beginTask("calibration_"+session->id, m_message);
    emit changed();
    const QPointer<CalibrationBridge> self(this);
    auto* publication = SlicingPreviewBridge::instance();
    const auto update = [self, session, generation](const QString& phase, float fraction, const QString& message) {
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, session, generation, phase, fraction, message] {
            if (!self || self->m_closing || self->m_generation != generation || self->m_session != session || !self->m_running || session->cancelled.load()) return;
            self->m_state = phase; self->m_progress = fraction; self->m_message = message;
            TaskStateNotifier::instance()->updateProgress(self->m_taskId, std::max(0.0f, fraction));
            TaskStateNotifier::instance()->updateDescription(self->m_taskId, message);
            emit self->changed();
        }, Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run([session, prepareTask, plate, update, publication] {
        try {
            const auto cancelled = [session, publication] { return session->cancelled.load() || !session->valid->load() || !publication->acceptsPublication(session->epoch); };
            libslicer::SliceCallbacks callbacks;
            callbacks.is_cancelled = cancelled;
            if (prepareTask) {
                session->details.clear();
                auto prepared = GPlatform::SlicingBackend::instance().prepareCalibration(session->request, callbacks);
                for (const auto& d : prepared.diagnostics) session->details << QString::fromStdString(d.message);
                if (!prepared || cancelled()) { session->cancelled.store(prepared.cancelled || cancelled()); session->error = diagnostics(prepared.diagnostics); return; }
                session->task = std::move(prepared.task);
                session->plates.clear(); session->plates.resize(session->task->plates().size());
            }
            if (!session->task || session->plates.empty()) { session->error = QStringLiteral("原版准备逻辑没有返回可用打印盘。"); return; }
            if (plate < 0) {
                const auto count = session->plates.size();
                for (std::size_t i = 0; i < count; ++i) {
                    if (cancelled()) { session->cancelled.store(true); return; }
                    auto& item = session->plates[i];
                    if (item.output) continue; // Retry only incomplete plates, using the same native task.
                    session->failed = int(i);
                    update("slicing", float(i)/float(count), QStringLiteral("正在切片打印盘 %1 / %2…").arg(i+1).arg(count));
                    callbacks.progress = [update, i, count](float p, std::string_view stage) {
                        update("slicing", (float(i)+std::clamp(p,0.0f,1.0f))/float(count),
                            QStringLiteral("打印盘 %1 / %2 · %3").arg(i+1).arg(count).arg(QString::fromUtf8(stage.data(),qsizetype(stage.size()))));
                    };
                    libslicer::CalibrationOutputOptions options;
                    options.generate_preview = true; options.generate_gcode_3mf = true;
                    auto result = GPlatform::SlicingBackend::instance().sliceCalibration(*session->task, i, options, callbacks);
                    auto output = std::make_shared<GPlatform::SliceOutputRecord>();
                    output->setArtifacts(QString::fromStdString(result.output.path),
                        result.output.ownership == libslicer::OutputArtifactOwnership::LibraryTemporary,
                        QString::fromStdString(result.gcode_3mf.path),
                        result.gcode_3mf.ownership == libslicer::OutputArtifactOwnership::LibraryTemporary);
                    if (!result || cancelled()) {
                        session->cancelled.store(result.cancelled || cancelled());
                        item.error = diagnostics(result.diagnostics); session->error = item.error; return;
                    }
                    output->name = title(session->request.mode)+QStringLiteral("_plate_%1").arg(i+1);
                    output->sourceScope = GPlatform::ToolpathPreviewSourceScope::Calibration;
                    output->printBedId = session->bed;
                    output->calibrationTaskId = session->id;
                    output->calibrationPlateIndex = int(i);
                    output->calibrationValidity = session->valid;
                    output->stats.totalTimeS = float(result.summary.estimated_time_seconds);
                    output->stats.totalExtrusionMm = float(result.summary.filament_used_mm);
                    output->stats.totalLayers = int(result.summary.layer_count);
                    for (const auto& d : result.diagnostics) if (d.warning) output->warnings << QString::fromStdString(d.message);
                    if (!output->validate(&item.error)) { session->error = item.error; return; }
                    item.output = std::move(output); item.preview = std::move(result.preview); item.error.clear();
                }
                session->target = 0;
            }
            session->failed = -1;
            auto& selected = session->plates.at(std::size_t(session->target));
            if (!selected.output) { session->error = QStringLiteral("该打印盘尚未切片成功。"); return; }
            update("loading", -1, QStringLiteral("正在加载打印盘 %1 的预览…").arg(session->target+1));
            if (selected.preview) {
                auto source = GPlatform::captureToolpathSource(selected.output->gcode.path, cancelled);
                session->prepared = GPlatform::prepareToolpathPreview(*selected.preview, std::move(source), cancelled);
            } else session->prepared.error = QStringLiteral("校准输出可用，但没有工具路径预览。");
            session->cancelled.store(cancelled() || session->prepared.cancelled);
            session->success = !session->cancelled.load();
        } catch (const std::exception& e) { session->error = QString::fromUtf8(e.what()); }
        catch (...) { session->error = QStringLiteral("校准任务发生未知错误。"); }
    }));
}
void CalibrationBridge::refreshPlates() {
    m_plates.clear();
    if (!m_session) return;
    for (std::size_t i=0; i<m_session->plates.size(); ++i) {
        const auto& p=m_session->plates[i];
        m_plates << QVariantMap{{"index",int(i)},{"name",QStringLiteral("打印盘 %1").arg(i+1)},
            {"ready",bool(p.output)},{"error",p.error}, {"objects",int(m_session->task->plates()[i].objects.size())}};
    }
}
void CalibrationBridge::finishWork() {
    if (m_closing || !m_running || !m_session) return;
    const auto session=m_session;
    try { m_watcher.future().waitForFinished(); } catch (...) { session->success=false; }
    // The SDK worker is now idle. A single synchronous revision check here is
    // safe; no QML getter ever waits behind the calibration job.
    std::optional<GPlatform::SlicingProfileSnapshot> active;
    if (!session->cancelled.load() && session->valid->load() &&
        SlicingPreviewBridge::instance()->acceptsPublication(session->epoch) &&
        GPlatform::SlicingBackend::instance().configurationChangesAllowed()) {
        active=GPlatform::SlicingBackend::instance().activeProfileSnapshot();
        if (!active || active->revision!=session->profile.revision) session->valid->store(false);
    } else session->cancelled.store(true);
    if (!session->valid->load() || !SlicingPreviewBridge::instance()->acceptsPublication(session->epoch)) session->cancelled.store(true);
    bool accepted=false;
    if (session->success && !session->cancelled.load()) {
        try {
            const auto result=GPlatform::publishToolpathResult(session->plates[session->target].output,
                std::move(session->prepared),session->epoch);
            accepted=result.accepted;
            if (!accepted) session->error=result.error;
        } catch (const std::exception& e) { session->error=QString::fromUtf8(e.what()); }
        catch (...) { session->error=QStringLiteral("校准预览发布失败。"); }
    }
    m_running=false;
    if (!m_taskId.isEmpty()) TaskStateNotifier::instance()->endTask(m_taskId);
    m_taskId.clear();
    refreshPlates();
    m_details = session->details.join('\n');
    if (accepted) {
        m_selectedPlate=session->target; m_state="ready"; m_progress=1;
        m_message=QStringLiteral("校准切片完成"); m_error.clear();
        SliceSettingsBridge::instance()->markAsSlicedForRevision(session->profile.revision,active->revision);
        EnvironmentSwitchHandler::switchEnvironment("preview");
    } else {
        m_state=!session->valid->load()?"stale":session->cancelled.load()?"cancelled":"failed";
        m_error=session->error;
        if (m_error.isEmpty() && m_state=="failed") m_error=QStringLiteral("校准失败，未返回详细诊断。");
        m_message=m_state=="cancelled"?QStringLiteral("校准已取消"):m_state=="stale"?QStringLiteral("配置已变更，请重新准备校准。"):QStringLiteral("校准失败");
        if (session->failed>=0 && !m_error.isEmpty()) m_error.prepend(QStringLiteral("打印盘 %1：\n").arg(session->failed+1));
    }
    emit changed(); emit completed(accepted);
}
QVariantMap CalibrationBridge::resultContext() const {
    if (!m_session) return {};
    const auto key=resultKey(m_session->request.mode);
    const auto& config=m_session->profile.config;
    return {{"title",title(m_session->request.mode)},{"mode",int(m_session->request.mode)},
        {"key",key},{"oldValue",key.isEmpty()?QString():first(config,key.toUtf8().constData())},
        {"material",m_session->materialName},
        {"canRefine",m_session->canRefine},
        {"appliesToMaterial",!key.isEmpty()},{"slot",0},
        {"unit",m_session->request.mode==libslicer::CalibMode::Calib_Input_shaping_freq?QStringLiteral("Hz"):
            key=="nozzle_temperature"?QStringLiteral("℃"):key=="filament_max_volumetric_speed"?QStringLiteral("mm³/s"):
            key=="filament_retraction_length"?QStringLiteral("mm"):
            m_session->request.mode==libslicer::CalibMode::Calib_VFA_Tower?QStringLiteral("mm/s"):
            m_session->request.mode==libslicer::CalibMode::Calib_Cornering?
                (first(config,"gcode_flavor")=="marlin2"&&first(config,"machine_max_junction_deviation").toDouble()>0?QStringLiteral("mm"):QStringLiteral("mm/s")):QString()},
        {"inheritedRetraction",first(config,"retraction_length")},
        {"pressureAdvanceEnabled",first(config,"enable_pressure_advance")=="1"}};
}
bool CalibrationBridge::acceptResult(const QVariantMap& observation) {
    if (!m_session || m_running || m_closing || !configurationValid() || TaskStateNotifier::instance()->isBusy() ||
        !previewActive() || m_selectedPlate<0 || m_selectedPlate>=int(m_session->plates.size()) || !m_session->plates[m_selectedPlate].output) return false;
    m_error.clear();
    const auto fail=[this](const QString& error){m_error=error;emit changed();return false;};
    if (!observation.value("observed").toBool()) return fail(QStringLiteral("请先检查试打印实物。"));
    const auto active=GPlatform::SlicingBackend::instance().activeProfileSnapshot();
    if (!active || active->revision!=m_session->profile.revision) { invalidate(); return fail(QStringLiteral("材料或打印配置已经改变，请重新校准。")); }
    const auto key=resultKey(m_session->request.mode);
    const auto input=observation.value("value").toString().trimmed();
    if (input.isEmpty()) return fail(QStringLiteral("请填写观察结果。"));
    if (m_session->request.mode==libslicer::CalibMode::Calib_Input_shaping_freq ||
        m_session->request.mode==libslicer::CalibMode::Calib_Input_shaping_damp) {
        const double maximum=m_session->request.mode==libslicer::CalibMode::Calib_Input_shaping_freq?500:1;
        for(const auto& field:{QStringLiteral("value"),QStringLiteral("valueY")}) {
            bool ok=false;const double n=observation.value(field).toString().toDouble(&ok);
            if(!ok || !std::isfinite(n) || n<0 || n>maximum) return fail(QStringLiteral("请填写有效的 X、Y 轴结果。"));
        }
    }
    std::vector<std::pair<std::string,std::string>> patch;
    if (!key.isEmpty()) {
        bool ok=false; const double number=input.toDouble(&ok);
        if (!ok || !std::isfinite(number) || number<0 ||
            ((key=="filament_flow_ratio" || key=="filament_max_volumetric_speed") && number<=0) ||
            (key=="nozzle_temperature" && (number>500 || number!=std::floor(number))))
            return fail(QStringLiteral("请输入有效的校准结果，温度使用整数。"));
        const auto replaceFirst=[&](const QString& k,const QString& v){
            auto items=value(active->config,k.toUtf8().constData()).split(',');
            if(items.isEmpty())items<<v;else items[0]=v;
            patch.emplace_back(k.toStdString(),items.join(',').toStdString());
        };
        replaceFirst(key,QString::number(number,'g',15));
        if(key=="pressure_advance") {
            if (!observation.value("enablePressureAdvance").toBool() && first(active->config,"enable_pressure_advance")!="1")
                return fail(QStringLiteral("采纳 PA 值前，请确认启用当前材料的压力提前。"));
            replaceFirst("enable_pressure_advance","1");
        }
        if(key=="filament_retraction_length" && !observation.value("overrideRetraction").toBool())
            return fail(QStringLiteral("请确认覆盖当前材料继承的回抽长度。"));
    }
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/calibration";
    if (!QDir().mkpath(directory)) return fail(QStringLiteral("无法创建校准记录目录。"));
    const auto path=directory+"/"+m_session->id+".json";
    QJsonObject record{{"version",1},{"task",m_session->id},{"mode",int(m_session->request.mode)},
        {"plate",m_selectedPlate},{"parameters",QJsonObject::fromVariantMap(m_parameters)},
        {"observation",QJsonObject::fromVariantMap(observation)}, {"materialKey",key},
        {"machine",QString::fromStdString(active->selection.machine_model_id)},
        {"materialPreset",active->selection.filament_preset_ids.empty()?QString():QString::fromStdString(active->selection.filament_preset_ids.front())},
        {"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"applied",false}};
    const auto write=[&](){QSaveFile f(path);f.setDirectWriteFallback(false);if(!f.open(QIODevice::WriteOnly))return false;const auto data=QJsonDocument(record).toJson();return f.write(data)==data.size() && f.commit();};
    if(!write()) return fail(QStringLiteral("校准观察记录保存失败，未修改配置。"));
    if (!patch.empty()) {
        const auto result=GPlatform::SlicingBackend::instance().applyConfigPatch(patch);
        if (!result) {
            QStringList errors;for(const auto& d:result.diagnostics)errors<<QString::fromStdString(d.message);
            return fail(errors.isEmpty()?QStringLiteral("校准值未通过配置校验。"):errors.join('\n'));
        }
        record.insert("applied",true);
        const bool saved=write();
        SliceSettingsBridge::instance()->refreshActiveConfig();
        invalidate();
        if(!saved)m_error=QStringLiteral("当前配置已更新，但记录状态更新失败；观察记录仍保留，预设尚未保存。");
    }
    m_recordPath=path;
    emit changed(); emit resultAccepted(!patch.empty(),path);
    return true;
}
