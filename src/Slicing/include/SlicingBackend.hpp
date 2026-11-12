#pragma once

#include "SliceSession.hpp"
#include <libslicer/Library.hpp>
#include <libslicer/Calibration.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace GPlatform {

using SlicingConfigActivationResult = libslicer::ConfigActivationResult;

struct SlicingConfigPatchResult {
    libslicer::SettingsResult settings;
    std::uint64_t appliedRevision{0};
    explicit operator bool() const noexcept { return bool(settings); }
};

// Process-level infrastructure gateway for the slicer SDK. QML bridges only
// project its configuration state; job handlers execute through this service.
class SlicingBackend final {
public:
    class ProjectCommitLease final {
    public:
        ProjectCommitLease();
        ProjectCommitLease(ProjectCommitLease&&) noexcept;
        ProjectCommitLease& operator=(ProjectCommitLease&&) noexcept;
        ~ProjectCommitLease();
        explicit operator bool() const noexcept;
        libslicer::ConfigurationInstallResult install(const libslicer::ConfigurationState&);
        void release() noexcept;
    private:
        class State;
        std::unique_ptr<State> m_state;
        friend class SlicingBackend;
    };
    static SlicingBackend& instance();

    ~SlicingBackend();
    bool available() const noexcept;
    bool configurationChangesAllowed() const noexcept;
    const std::vector<libslicer::MachineModelOption>& machineModels() const noexcept;
    std::vector<libslicer::MachineModelOption> projectMachineModels() const;
    const std::vector<libslicer::BuildPlateOption>& buildPlateOptions() const noexcept;
    std::vector<libslicer::PresetOption> printerPresets(const std::string& model, const std::string& variant) const;
    libslicer::PrinterSwitchReview reviewPrinterSwitch(const libslicer::ConfigSelection&, bool afterSave = false);
    libslicer::PrinterSwitchSaveResult savePrinterSwitchPreset(
        std::uint64_t revision, libslicer::PrinterSwitchPreset, const std::string& name, bool validateOnly = false);
    libslicer::PrinterSwitchResult preparePrinterSwitch(const libslicer::ConfigSelection&,
        std::uint64_t revision, const std::vector<libslicer::PrinterSwitchDecision>&,
        const std::vector<std::pair<std::string, std::string>>& remembered);
    SlicingConfigActivationResult activateConfig(
        const libslicer::ConfigSelection& selection,
        const std::vector<std::pair<std::string, std::string>>& patch = {});
    libslicer::SettingsResult applyConfigPatch(
        const std::vector<std::pair<std::string, std::string>>& patch);
    SlicingConfigPatchResult applyConfigPatchIfRevision(std::uint64_t expectedRevision,
        const std::vector<std::pair<std::string, std::string>>& patch);
    libslicer::SettingsResult setConfigValue(std::string_view key, std::string_view value);
    libslicer::SettingsResult resetConfigValue(std::string_view key);
    std::optional<libslicer::ActiveConfigView> activeConfigView() const;
    std::uint64_t configurationRevision() const;
    libslicer::ConfigActivationResult setFilamentPreset(
        std::size_t slotIndex, std::string_view presetId);
    libslicer::ConfigActivationResult resizeFilamentSlots(std::size_t slotCount);
    libslicer::ConfigActivationResult addFilamentToTool(std::size_t physicalTool);
    libslicer::SettingsResult setFilamentColor(
        std::size_t slotIndex, libslicer::Rgba8 color);
    std::optional<libslicer::ConfigSnapshot> configSnapshot() const;
    std::optional<SlicingProfileSnapshot> activeProfileSnapshot() const;
    std::vector<libslicer::CalibMode> calibrationModes() const;
    libslicer::CalibrationPrepareResult prepareCalibration(
        const libslicer::CalibrationRequest& request,
        const libslicer::SliceCallbacks& callbacks = {}) const;
    libslicer::SliceResult sliceCalibration(
        const libslicer::CalibrationTask& task, std::size_t plateIndex,
        const libslicer::CalibrationOutputOptions& output = {},
        const libslicer::SliceCallbacks& callbacks = {}) const;
    libslicer::SliceResult slice(const SliceSession& session,
                                 const libslicer::SliceCallbacks& callbacks = {}) const;
    libslicer::SliceResult slice(const libslicer::SliceRequest& request,
                                 const libslicer::SliceCallbacks& callbacks = {}) const;
    libslicer::GCodePreviewResult loadGCodePreview(
        const libslicer::GCodePreviewRequest& request,
        const libslicer::SliceCallbacks& callbacks = {}) const;
    libslicer::ProjectImportResult importProject(
        const libslicer::ProjectImportRequest& request,
        const libslicer::SliceCallbacks& callbacks = {}) const;
    libslicer::ProjectReadResult readProject(const libslicer::ProjectReadRequest&,
        const libslicer::SliceCallbacks& = {},
        const libslicer::ProjectDecisionCallback& = {}) const;
    std::optional<libslicer::ProjectMachine> activeProjectMachine() const;
    ProjectCommitLease beginProjectCommit(std::uint64_t expectedRevision);

    SlicingBackend(const SlicingBackend&) = delete;
    SlicingBackend& operator=(const SlicingBackend&) = delete;

private:
    SlicingBackend();

    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_available{false};
    std::vector<libslicer::MachineModelOption> m_machineModels;
    std::vector<libslicer::BuildPlateOption> m_buildPlateOptions;
};

} // namespace GPlatform
