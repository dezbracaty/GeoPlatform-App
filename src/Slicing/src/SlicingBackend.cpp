#include "SlicingBackend.hpp"
#include "../../AppConfigPaths.hpp"

#include "Foundation/Log.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace GPlatform {

QString describeSlicingProgress(const libslicer::SliceProgress& progress)
{
    const auto translated = [](std::string_view source) {
        const QByteArray text(source.data(), qsizetype(source.size()));
        return QCoreApplication::translate("LibSlicerSettings", text.constData());
    };
    QString description = translated(progress.detail.empty() ? progress.stage : progress.detail);
    if (progress.total_layers > 0) {
        const auto completed = std::min(progress.completed_layers, progress.total_layers);
        description += QLatin1Char('\n') + QCoreApplication::translate("LibSlicerSettings",
            "%1/%2 layers (%3%)")
            .arg(qulonglong(completed)).arg(qulonglong(progress.total_layers))
            .arg(int(100.0 * double(completed) / double(progress.total_layers)));
    }
    return description;
}

class SlicingBackend::Impl final {
public:
    Impl()
        : worker([this]() { run(); })
    {
    }

    ~Impl()
    {
        shutdownRequested.store(true, std::memory_order_release);
        try {
            invoke([this]() {
                library.reset();
            });
        } catch (const std::exception& error) {
            LOG_ERROR("SlicingBackend: worker shutdown failed: {}", error.what());
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stopping = true;
        }
        queueChanged.notify_one();
        if (worker.joinable()) {
            worker.join();
        }
    }

    template <typename Function>
    auto invoke(Function&& function) -> std::invoke_result_t<Function>
    {
        using Result = std::invoke_result_t<Function>;
        if (std::this_thread::get_id() == worker.get_id()) {
            return std::forward<Function>(function)();
        }

        std::packaged_task<Result()> resultTask(std::forward<Function>(function));
        auto result = resultTask.get_future();
        std::packaged_task<void()> queuedTask(
            [task = std::move(resultTask)]() mutable { task(); });
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stopping) {
                throw std::runtime_error("slicing backend is shutting down");
            }
            jobs.push_back(std::move(queuedTask));
        }
        queueChanged.notify_one();
        return result.get();
    }

    std::unique_ptr<libslicer::Library> library;
    std::unique_ptr<QTemporaryDir> isolatedPresets;
    std::atomic_bool shutdownRequested{false};
    std::atomic_uint activeLongJobs{0};
    std::atomic_bool projectCommitActive{false};
    std::mutex configurationAdmissionMutex;

private:
    void run()
    {
        for (;;) {
            std::packaged_task<void()> job;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueChanged.wait(lock, [this]() { return stopping || !jobs.empty(); });
                if (stopping && jobs.empty()) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
        }
    }

    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<std::packaged_task<void()>> jobs;
    bool stopping{false};
    std::thread worker;
};

namespace {

libslicer::SettingsResult unavailableSettingsResult(std::string message)
{
    libslicer::SettingsResult result;
    result.diagnostics.push_back({"initialization", std::move(message)});
    return result;
}

libslicer::SettingsResult busySettingsResult()
{
    libslicer::SettingsResult result;
    result.diagnostics.push_back({"busy", "slicing configuration cannot be changed while a slicing task is running"});
    return result;
}

class LongJobCompletionGuard final {
public:
    explicit LongJobCompletionGuard(std::atomic_uint& activeLongJobs)
        : m_activeLongJobs(activeLongJobs)
    {
    }

    ~LongJobCompletionGuard()
    {
        m_activeLongJobs.fetch_sub(1, std::memory_order_release);
    }

private:
    std::atomic_uint& m_activeLongJobs;
};

} // namespace

SlicingBackend& SlicingBackend::instance()
{
    static SlicingBackend backend;
    return backend;
}

SlicingBackend::SlicingBackend()
    : m_impl(std::make_unique<Impl>())
{
    std::vector<libslicer::ConfigDiagnostic> diagnostics;
    libslicer::LibraryOptions options;
    const auto overrideDirectory = qEnvironmentVariable("GPLATFORM_SLICER_CONFIG_DIR");
    if (!overrideDirectory.isEmpty()) {
        options.config_directory = overrideDirectory.toStdString();
    } else if (qEnvironmentVariableIntValue("GPLATFORM_JOURNAL_SESSION") != 0 ||
               QCoreApplication::applicationName() != QStringLiteral("GPlatform")) {
        m_impl->isolatedPresets = std::make_unique<QTemporaryDir>();
        if (m_impl->isolatedPresets->isValid())
            options.user_preset_directory = m_impl->isolatedPresets->path().toStdString();
        else
            LOG_WARN("SlicingBackend: could not create an isolated user preset directory");
    } else if (const auto directory = GPlatform::userConfigDirectory(); !directory.isEmpty()) {
        options.config_directory = QDir(directory).filePath(QStringLiteral("slicing")).toStdString();
    }
#ifdef _WIN32
    options.resource_directory =
        (QCoreApplication::applicationDirPath() + "/resources/libslicer").toStdString();
#endif
    m_available = m_impl->invoke([this, &diagnostics, options = std::move(options)] {
        m_impl->library = libslicer::Library::open(options, libslicer::PresetLoadingMode::OnDemand, &diagnostics);
        if (!m_impl->library) {
            return false;
        }
        m_machineModels = m_impl->library->machine_models();
        m_buildPlateOptions = m_impl->library->build_plate_options();
        return true;
    });
    if (!m_available) {
        const std::string message = diagnostics.empty()
            ? std::string("unknown resource error")
            : diagnostics.front().message;
        LOG_ERROR("SlicingBackend: failed to initialize libslicer: {}", message);
    } else {
        for (const auto& diagnostic : diagnostics)
            LOG_WARN("SlicingBackend: {}: {}", diagnostic.key, diagnostic.message);
    }
}

SlicingBackend::~SlicingBackend() = default;

bool SlicingBackend::available() const noexcept
{
    return m_available;
}

bool SlicingBackend::configurationChangesAllowed() const noexcept
{
    return m_available &&
        m_impl->activeLongJobs.load(std::memory_order_acquire) == 0 &&
        !m_impl->projectCommitActive.load(std::memory_order_acquire);
}

const std::vector<libslicer::MachineModelOption>& SlicingBackend::machineModels() const noexcept
{
    return m_machineModels;
}

std::vector<libslicer::MachineModelOption> SlicingBackend::projectMachineModels() const
{
    if (!m_available) return {};
    return m_impl->invoke([this] { return m_impl->library->project_machine_models(); });
}

const std::vector<libslicer::BuildPlateOption>& SlicingBackend::buildPlateOptions() const noexcept
{
    return m_buildPlateOptions;
}

std::vector<libslicer::PresetOption> SlicingBackend::printerPresets(
    const std::string& model, const std::string& variant) const
{
    if (!m_available) return {};
    return m_impl->invoke([this, model, variant] { return m_impl->library->printer_presets(model, variant); });
}

libslicer::PrinterSwitchReview SlicingBackend::reviewPrinterSwitch(
    const libslicer::ConfigSelection& selection, bool afterSave)
{
    if (!configurationChangesAllowed()) {
        libslicer::PrinterSwitchReview result;
        result.diagnostics = busySettingsResult().diagnostics;
        return result;
    }
    return m_impl->invoke([this, selection, afterSave] {
        return m_impl->library->review_printer_switch(selection, afterSave);
    });
}

libslicer::PrinterSwitchSaveResult SlicingBackend::savePrinterSwitchPreset(
    std::uint64_t revision, libslicer::PrinterSwitchPreset type, const std::string& name, bool validateOnly, const std::string& sourcePresetId)
{
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (!configurationChangesAllowed()) {
        libslicer::PrinterSwitchSaveResult result;
        result.diagnostics = busySettingsResult().diagnostics;
        return result;
    }
    return m_impl->invoke([this, revision, type, name, validateOnly, sourcePresetId] {
        return m_impl->library->save_printer_switch_preset(revision, type, name, validateOnly, sourcePresetId);
    });
}

libslicer::PrinterSwitchSaveResult SlicingBackend::savePreset(std::uint64_t revision,
    libslicer::PrinterSwitchPreset type, libslicer::PresetSaveMode mode, const std::string& name,
    std::optional<std::size_t> filamentSlot, bool validateOnly, bool overwriteConfirmed)
{
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (!configurationChangesAllowed()) {
        libslicer::PrinterSwitchSaveResult result;
        result.diagnostics = busySettingsResult().diagnostics;
        return result;
    }
    return m_impl->invoke([this, revision, type, mode, name, filamentSlot, validateOnly, overwriteConfirmed] {
        return m_impl->library->save_preset(revision, type, mode, name, filamentSlot, validateOnly, overwriteConfirmed);
    });
}

libslicer::PrinterSwitchResult SlicingBackend::preparePrinterSwitch(
    const libslicer::ConfigSelection& selection, std::uint64_t revision,
    const std::vector<libslicer::PrinterSwitchDecision>& decisions,
    const std::vector<std::pair<std::string, std::string>>& projectOverrides)
{
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (!configurationChangesAllowed()) {
        libslicer::PrinterSwitchResult result;
        result.diagnostics = busySettingsResult().diagnostics;
        return result;
    }
    return m_impl->invoke([this, selection, revision, decisions, projectOverrides] {
        return m_impl->library->prepare_printer_switch(selection, revision, decisions, projectOverrides);
    });
}

libslicer::PrinterSwitchResult SlicingBackend::prepareStartupConfiguration(const libslicer::ConfigSelection& fallback)
{
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (!configurationChangesAllowed())
        return {{}, busySettingsResult().diagnostics};
    return m_impl->invoke([this, fallback] {
        return m_impl->library->prepare_startup_configuration(fallback);
    });
}

libslicer::SelectionSaveResult SlicingBackend::saveUserSelection(
    std::uint64_t revision, const libslicer::SelectionSaveOptions& options)
{
    if (!m_available)
        return {false, {{"initialization", "Slicing backend is unavailable"}}};
    return m_impl->invoke([this, revision, options] {
        if (m_impl->library->configuration_revision() != revision)
            return libslicer::SelectionSaveResult{false, {{"revision", "Slicing selection changed before saving"}}};
        return m_impl->library->save_user_selection(m_impl->library->capture_configuration(), options);
    });
}

SlicingConfigActivationResult SlicingBackend::activateConfig(
    const libslicer::ConfigSelection& selection,
    const std::vector<std::pair<std::string, std::string>>& patch)
{
    if (!m_available) {
        SlicingConfigActivationResult output;
        output.diagnostics.push_back({"initialization", "libslicer is not initialized"});
        return output;
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        SlicingConfigActivationResult output;
        output.diagnostics.push_back(
            {"busy", "slicing configuration cannot be changed while a slicing task is running"});
        return output;
    }
    const libslicer::ConfigSelection requested = selection;
    const auto requestedPatch = patch;
    return m_impl->invoke([this, requested, requestedPatch]() {
        return m_impl->library->activate_config(requested, requestedPatch);
    });
}

libslicer::SettingsResult SlicingBackend::applyConfigPatch(
    const std::vector<std::pair<std::string, std::string>>& patch)
{
    if (!m_available) {
        return unavailableSettingsResult("libslicer is not initialized");
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        return busySettingsResult();
    }
    const auto requested = patch;
    return m_impl->invoke([this, requested]() {
        return m_impl->library->apply_active_config_patch(requested);
    });
}

SlicingConfigPatchResult SlicingBackend::applyConfigPatchIfRevision(std::uint64_t expectedRevision,
    const std::vector<std::pair<std::string, std::string>>& patch)
{
    if (!m_available) return {unavailableSettingsResult("libslicer is not initialized"), 0};
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) return {busySettingsResult(), 0};
    const auto requested = patch;
    return m_impl->invoke([this, expectedRevision, requested]() {
        const auto active = m_impl->library->active_config();
        if (!active || active->revision != expectedRevision) {
            SlicingConfigPatchResult rejected;
            rejected.settings.diagnostics.push_back({"revision", "Calibration configuration has changed"});
            return rejected;
        }
        SlicingConfigPatchResult result;
        result.settings = m_impl->library->apply_active_config_patch(requested);
        if (result.settings) result.appliedRevision = m_impl->library->active_config()->revision;
        return result;
    });
}

libslicer::SettingsResult SlicingBackend::applyFilamentPatch(std::size_t slotIndex,
    std::uint64_t expectedRevision, const std::vector<std::pair<std::string, std::string>>& patch)
{
    if (!m_available) return unavailableSettingsResult("libslicer is not initialized");
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) return busySettingsResult();
    return m_impl->invoke([this, slotIndex, expectedRevision, patch]() {
        return m_impl->library->apply_active_filament_patch(slotIndex, expectedRevision, patch);
    });
}

libslicer::SettingsResult SlicingBackend::setConfigValue(
    std::string_view key, std::string_view value)
{
    if (!m_available) {
        return unavailableSettingsResult("libslicer is not initialized");
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        return busySettingsResult();
    }
    const std::string requestedKey(key);
    const std::string requestedValue(value);
    return m_impl->invoke([this, requestedKey, requestedValue]() {
        return m_impl->library->set_active_config_value(requestedKey, requestedValue);
    });
}

libslicer::SettingsResult SlicingBackend::resetConfigValue(std::string_view key)
{
    if (!m_available) {
        return unavailableSettingsResult("libslicer is not initialized");
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        return busySettingsResult();
    }
    const std::string requestedKey(key);
    return m_impl->invoke([this, requestedKey]() {
        return m_impl->library->reset_active_config_value(requestedKey);
    });
}

SlicingConfigActivationResult SlicingBackend::restorePreset(
    libslicer::SettingGroup group, std::optional<std::size_t> filamentSlot)
{
    if (!m_available) {
        SlicingConfigActivationResult result;
        result.diagnostics = unavailableSettingsResult("libslicer is not initialized").diagnostics;
        return result;
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        SlicingConfigActivationResult result;
        result.diagnostics = busySettingsResult().diagnostics;
        return result;
    }
    return m_impl->invoke([this, group, filamentSlot] {
        return m_impl->library->restore_active_preset(group, filamentSlot);
    });
}

std::optional<libslicer::ActiveConfigView> SlicingBackend::activeConfigView() const
{
    if (!m_available) return std::nullopt;
    return m_impl->invoke([this]() {
        return m_impl->library->active_config();
    });
}

std::uint64_t SlicingBackend::configurationRevision() const
{
    if (!m_available) return 0;
    return m_impl->invoke([this] { return m_impl->library->configuration_revision(); });
}

libslicer::ConfigActivationResult SlicingBackend::setFilamentPreset(
    std::size_t slotIndex, std::string_view presetId)
{
    if (!m_available) {
        libslicer::ConfigActivationResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized"});
        return result;
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        libslicer::ConfigActivationResult result;
        result.diagnostics.push_back(
            {"busy", "slicing configuration cannot be changed while a slicing task is running"});
        return result;
    }
    const std::string requestedPreset(presetId);
    return m_impl->invoke([this, slotIndex, requestedPreset]() {
        return m_impl->library->set_active_filament_preset(slotIndex, requestedPreset);
    });
}

libslicer::ConfigActivationResult SlicingBackend::resizeFilamentSlots(
    std::size_t slotCount)
{
    if (!m_available) {
        libslicer::ConfigActivationResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized"});
        return result;
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        libslicer::ConfigActivationResult result;
        result.diagnostics.push_back(
            {"busy", "slicing configuration cannot be changed while a slicing task is running"});
        return result;
    }
    return m_impl->invoke([this, slotCount]() {
        return m_impl->library->resize_active_filament_slots(slotCount);
    });
}

libslicer::ConfigActivationResult SlicingBackend::addFilamentToTool(std::size_t physicalTool)
{
    if (!m_available) {
        libslicer::ConfigActivationResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized"});
        return result;
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        libslicer::ConfigActivationResult result;
        result.diagnostics.push_back({"busy", "slicing configuration cannot be changed while a slicing task is running"});
        return result;
    }
    return m_impl->invoke([this, physicalTool]() {
        return m_impl->library->add_active_filament(physicalTool);
    });
}

libslicer::ConfigurationState SlicingBackend::prepareFilamentRemoval(
    std::size_t slotIndex, std::uint64_t expectedRevision, std::vector<libslicer::ConfigDiagnostic>& diagnostics)
{
    if (!configurationChangesAllowed()) {
        diagnostics = {{"filament", "The slicing configuration is unavailable or busy"}};
        return {};
    }
    return m_impl->invoke([this, slotIndex, expectedRevision, &diagnostics]() {
        return m_impl->library->prepare_filament_removal(slotIndex, expectedRevision, diagnostics);
    });
}

libslicer::SettingsResult SlicingBackend::setFilamentColor(
    std::size_t slotIndex, libslicer::Rgba8 color, std::optional<std::uint64_t> expectedRevision)
{
    if (!m_available) {
        return unavailableSettingsResult("libslicer is not initialized");
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        return busySettingsResult();
    }
    return m_impl->invoke([this, slotIndex, color, expectedRevision]() {
        if (expectedRevision && *expectedRevision != m_impl->library->configuration_revision())
            return unavailableSettingsResult("The configuration changed. Reopen the color picker.");
        return m_impl->library->set_active_filament_color(slotIndex, color);
    });
}

std::optional<libslicer::ConfigSnapshot> SlicingBackend::configSnapshot() const
{
    if (!m_available) {
        return std::nullopt;
    }
    return m_impl->invoke([this]() {
        return m_impl->library->active_config_snapshot();
    });
}

std::optional<SlicingProfileSnapshot> SlicingBackend::activeProfileSnapshot() const
{
    if (!m_available) {
        return std::nullopt;
    }
    return m_impl->invoke([this]() -> std::optional<SlicingProfileSnapshot> {
        const auto view = m_impl->library->active_config();
        const auto config = m_impl->library->active_config_snapshot();
        if (!view || !config) {
            return std::nullopt;
        }
        SlicingProfileSnapshot snapshot;
        snapshot.revision = view->revision;
        snapshot.selection = view->selection;
        snapshot.config = *config;
        return snapshot;
    });
}

std::vector<libslicer::CalibMode> SlicingBackend::calibrationModes() const
{
    if (!m_available) return {};
    return m_impl->invoke([this]() { return m_impl->library->calibration_modes(); });
}

libslicer::CalibrationPrepareResult SlicingBackend::prepareCalibration(
    const libslicer::CalibrationRequest& request,
    const libslicer::SliceCallbacks& callbacks) const
{
    if (!m_available) {
        libslicer::CalibrationPrepareResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized", false});
        return result;
    }
    libslicer::SliceCallbacks serializedCallbacks = callbacks;
    const auto callerCancelled = callbacks.is_cancelled;
    serializedCallbacks.is_cancelled = [impl = m_impl.get(), callerCancelled]() {
        return impl->shutdownRequested.load(std::memory_order_acquire) ||
            (callerCancelled && callerCancelled());
    };
    {
        std::lock_guard<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completionGuard(m_impl->activeLongJobs);
    auto requested = request;
    return m_impl->invoke([this, requested, serializedCallbacks]() mutable {
        if (!requested.config.valid()) {
            const auto active = m_impl->library->active_config_snapshot();
            if (active) requested.config = *active;
        }
        return m_impl->library->prepare_calibration(requested, serializedCallbacks);
    });
}

libslicer::SliceResult SlicingBackend::sliceCalibration(
    const libslicer::CalibrationTask& task, std::size_t plateIndex,
    const libslicer::CalibrationOutputOptions& output,
    const libslicer::SliceCallbacks& callbacks) const
{
    if (!m_available) {
        libslicer::SliceResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized", false});
        return result;
    }
    libslicer::SliceCallbacks serializedCallbacks = callbacks;
    const auto callerCancelled = callbacks.is_cancelled;
    serializedCallbacks.is_cancelled = [impl = m_impl.get(), callerCancelled]() {
        return impl->shutdownRequested.load(std::memory_order_acquire) ||
            (callerCancelled && callerCancelled());
    };
    {
        std::lock_guard<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completionGuard(m_impl->activeLongJobs);
    return m_impl->invoke([this, &task, plateIndex, output, serializedCallbacks]() {
        return m_impl->library->slice_calibration(task, plateIndex, output, serializedCallbacks);
    });
}

libslicer::SliceResult SlicingBackend::slice(
    const SliceSession& session,
    const libslicer::SliceCallbacks& callbacks) const
{
    if (!session.valid()) {
        libslicer::SliceResult result;
        result.diagnostics.push_back({"session", "Slice session is incomplete", false});
        return result;
    }
    return slice(session.request, callbacks);
}

libslicer::SliceResult SlicingBackend::slice(
    const libslicer::SliceRequest& request,
    const libslicer::SliceCallbacks& callbacks) const
{
    if (!m_available) {
        libslicer::SliceResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized", false});
        return result;
    }
    const libslicer::SliceRequest requested = request;
    libslicer::SliceCallbacks serializedCallbacks = callbacks;
    const auto callerCancelled = callbacks.is_cancelled;
    serializedCallbacks.is_cancelled = [impl = m_impl.get(), callerCancelled]() {
        return impl->shutdownRequested.load(std::memory_order_acquire) ||
            (callerCancelled && callerCancelled());
    };
    {
        std::lock_guard<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completionGuard(m_impl->activeLongJobs);
    return m_impl->invoke([this, requested, serializedCallbacks]() {
        return m_impl->library->slice(requested, serializedCallbacks);
    });
}

libslicer::GCodePreviewResult SlicingBackend::loadGCodePreview(
    const libslicer::GCodePreviewRequest& request,
    const libslicer::SliceCallbacks& callbacks) const
{
    if (!m_available) {
        libslicer::GCodePreviewResult result;
        result.diagnostics.push_back({"initialization", "libslicer is not initialized", false});
        return result;
    }
    const libslicer::GCodePreviewRequest requested = request;
    libslicer::SliceCallbacks serializedCallbacks = callbacks;
    const auto callerCancelled = callbacks.is_cancelled;
    serializedCallbacks.is_cancelled = [impl = m_impl.get(), callerCancelled]() {
        return impl->shutdownRequested.load(std::memory_order_acquire) ||
            (callerCancelled && callerCancelled());
    };
    {
        std::lock_guard<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completionGuard(m_impl->activeLongJobs);
    return m_impl->invoke([this, requested, serializedCallbacks]() {
        return m_impl->library->load_gcode_preview(requested, serializedCallbacks);
    });
}

libslicer::ProjectImportResult SlicingBackend::importProject(
    const libslicer::ProjectImportRequest& request,
    const libslicer::SliceCallbacks& callbacks) const
{
    if (!m_available) {
        libslicer::ProjectImportResult result;
        result.diagnostics.push_back(
            {"initialization", "libslicer is not initialized", false});
        return result;
    }
    const libslicer::ProjectImportRequest requested = request;
    libslicer::SliceCallbacks serializedCallbacks = callbacks;
    const auto callerCancelled = callbacks.is_cancelled;
    serializedCallbacks.is_cancelled = [impl = m_impl.get(), callerCancelled]() {
        return impl->shutdownRequested.load(std::memory_order_acquire) ||
            (callerCancelled && callerCancelled());
    };
    {
        std::lock_guard<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completionGuard(m_impl->activeLongJobs);
    return m_impl->invoke([this, requested, serializedCallbacks]() {
        return m_impl->library->import_project(requested, serializedCallbacks);
    });
}

class SlicingBackend::ProjectCommitLease::State final {
public:
    State(SlicingBackend::Impl& owner, std::unique_lock<std::mutex> acquired, std::uint64_t revision)
        : backend(owner), lock(std::move(acquired)), revision(revision) {
        backend.projectCommitActive.store(true, std::memory_order_release);
    }
    ~State() { backend.projectCommitActive.store(false, std::memory_order_release); }
    SlicingBackend::Impl& backend;
    std::unique_lock<std::mutex> lock;
    std::uint64_t revision;
};

SlicingBackend::ProjectCommitLease::ProjectCommitLease() = default;
SlicingBackend::ProjectCommitLease::ProjectCommitLease(ProjectCommitLease&&) noexcept = default;
SlicingBackend::ProjectCommitLease& SlicingBackend::ProjectCommitLease::operator=(ProjectCommitLease&&) noexcept = default;
SlicingBackend::ProjectCommitLease::~ProjectCommitLease() = default;
SlicingBackend::ProjectCommitLease::operator bool() const noexcept { return bool(m_state); }
void SlicingBackend::ProjectCommitLease::release() noexcept { m_state.reset(); }

libslicer::ConfigurationInstallResult SlicingBackend::ProjectCommitLease::install(
    const libslicer::ConfigurationState& configuration) {
    if (!m_state) return {};
    auto result = m_state->backend.invoke([this, &configuration] {
        return m_state->backend.library->install_configuration(configuration, m_state->revision);
    });
    if (result) m_state->revision = result.revision;
    return result;
}

SlicingBackend::ProjectCommitLease SlicingBackend::beginProjectCommit(std::uint64_t expectedRevision) {
    ProjectCommitLease lease;
    if (!m_available || m_impl->shutdownRequested.load(std::memory_order_acquire)) return lease;
    std::unique_lock<std::mutex> lock(m_impl->configurationAdmissionMutex, std::try_to_lock);
    if (!lock.owns_lock() || m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) return lease;
    const auto revision = m_impl->invoke([this] { return m_impl->library->configuration_revision(); });
    if (revision != expectedRevision) return lease;
    lease.m_state = std::make_unique<ProjectCommitLease::State>(*m_impl, std::move(lock), expectedRevision);
    return lease;
}

libslicer::ProjectReadResult SlicingBackend::readProject(const libslicer::ProjectReadRequest& request,
    const libslicer::SliceCallbacks& callbacks, const libslicer::ProjectDecisionCallback& decisions) const {
    if (!m_available) return {};
    auto wrapped = callbacks;
    wrapped.is_cancelled = [impl = m_impl.get(), cancelled = callbacks.is_cancelled] {
        return impl->shutdownRequested.load(std::memory_order_acquire) || (cancelled && cancelled());
    };
    {
        std::lock_guard<std::mutex> lock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completion(m_impl->activeLongJobs);
    return m_impl->invoke([this, request, wrapped, decisions] {
        return m_impl->library->read_project(request, wrapped, decisions);
    });
}

libslicer::ProjectWriteResult SlicingBackend::writeProject(const libslicer::ProjectWriteRequest& request,
    const libslicer::SliceCallbacks& callbacks) const {
    if (!m_available) return {};
    auto wrapped = callbacks;
    wrapped.is_cancelled = [impl = m_impl.get(), cancelled = callbacks.is_cancelled] {
        return impl->shutdownRequested.load(std::memory_order_acquire) || (cancelled && cancelled());
    };
    {
        std::lock_guard<std::mutex> lock(m_impl->configurationAdmissionMutex);
        m_impl->activeLongJobs.fetch_add(1, std::memory_order_release);
    }
    LongJobCompletionGuard completion(m_impl->activeLongJobs);
    return m_impl->invoke([this, request, wrapped] { return m_impl->library->write_project(request, wrapped); });
}

std::optional<libslicer::ProjectMachine> SlicingBackend::activeProjectMachine() const {
    if (!m_available) return std::nullopt;
    return m_impl->invoke([this] { return m_impl->library->active_project_machine(); });
}

} // namespace GPlatform
