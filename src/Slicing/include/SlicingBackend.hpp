#pragma once

#include "SliceSession.hpp"
#include <libslicer/Library.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace GPlatform {

struct SlicingConfigActivationResult {
    bool success{false};
    std::uint64_t revision{0};
    libslicer::ResolvedSelection selection;
    std::vector<libslicer::PresetOption> compatibleProcesses;
    std::vector<libslicer::PresetOption> compatibleFilaments;
    std::vector<libslicer::ConfigDiagnostic> diagnostics;
    std::vector<libslicer::SettingItem> settings;
    std::vector<libslicer::FilamentSlotInfo> filamentSlots;

    explicit operator bool() const noexcept { return success; }
};

// Process-level infrastructure gateway for the slicer SDK. QML bridges only
// project its configuration state; job handlers execute through this service.
class SlicingBackend final {
public:
    static SlicingBackend& instance();

    ~SlicingBackend();
    bool available() const noexcept;
    bool configurationChangesAllowed() const noexcept;
    const std::vector<libslicer::MachineModelOption>& machineModels() const noexcept;
    const std::vector<libslicer::BuildPlateOption>& buildPlateOptions() const noexcept;
    SlicingConfigActivationResult activateConfig(
        const libslicer::ConfigSelection& selection,
        const std::vector<std::pair<std::string, std::string>>& patch = {});
    libslicer::SettingsResult applyConfigPatch(
        const std::vector<std::pair<std::string, std::string>>& patch);
    libslicer::SettingsResult setConfigValue(std::string_view key, std::string_view value);
    libslicer::SettingsResult resetConfigValue(std::string_view key);
    std::optional<libslicer::ActiveConfigView> activeConfigView() const;
    libslicer::ConfigActivationResult setFilamentPreset(
        std::size_t slotIndex, std::string_view presetId);
    libslicer::ConfigActivationResult resizeFilamentSlots(std::size_t slotCount);
    libslicer::ConfigActivationResult addFilamentToTool(std::size_t physicalTool);
    libslicer::SettingsResult setFilamentColor(
        std::size_t slotIndex, libslicer::Rgba8 color);
    std::optional<libslicer::ConfigSnapshot> configSnapshot() const;
    std::optional<SlicingProfileSnapshot> activeProfileSnapshot() const;
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
