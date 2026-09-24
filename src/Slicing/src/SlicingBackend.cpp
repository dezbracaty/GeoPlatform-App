#include "SlicingBackend.hpp"

#include "Foundation/Log.h"

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
    std::atomic_bool shutdownRequested{false};
    std::atomic_uint activeLongJobs{0};
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
    m_available = m_impl->invoke([this, &diagnostics]() {
        m_impl->library = libslicer::Library::open({}, &diagnostics);
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
        m_impl->activeLongJobs.load(std::memory_order_acquire) == 0;
}

const std::vector<libslicer::MachineModelOption>& SlicingBackend::machineModels() const noexcept
{
    return m_machineModels;
}

const std::vector<libslicer::BuildPlateOption>& SlicingBackend::buildPlateOptions() const noexcept
{
    return m_buildPlateOptions;
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
    return m_impl->invoke([this, requested, requestedPatch]() mutable {
        SlicingConfigActivationResult output;
        auto activated = m_impl->library->activate_config(requested, requestedPatch);
        output.success = activated.success;
        output.diagnostics = std::move(activated.diagnostics);
        if (!output.success) {
            return output;
        }
        output.revision = activated.view.revision;
        output.selection = std::move(activated.view.selection);
        output.compatibleProcesses = std::move(activated.view.compatible_processes);
        output.compatibleFilaments = std::move(activated.view.compatible_filaments);
        output.settings = std::move(activated.view.settings);
        output.filamentSlots = std::move(activated.view.filament_slots);
        return output;
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

std::optional<libslicer::ActiveConfigView> SlicingBackend::activeConfigView() const
{
    if (!m_available) return std::nullopt;
    return m_impl->invoke([this]() {
        return m_impl->library->active_config();
    });
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

libslicer::SettingsResult SlicingBackend::setFilamentColor(
    std::size_t slotIndex, libslicer::Rgba8 color)
{
    if (!m_available) {
        return unavailableSettingsResult("libslicer is not initialized");
    }
    std::unique_lock<std::mutex> admissionLock(m_impl->configurationAdmissionMutex);
    if (m_impl->activeLongJobs.load(std::memory_order_acquire) != 0) {
        return busySettingsResult();
    }
    return m_impl->invoke([this, slotIndex, color]() {
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

} // namespace GPlatform
