#pragma once

#include "SystemTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace GPlatform {

enum class ToolpathMotionKind : std::uint8_t {
    Travel = 0,
    Extrusion,
    Wipe
};

enum class ToolpathEventKind : std::uint8_t {
    Seam = 0,
    ToolChange,
    ColorChange,
    Pause,
    CustomGCode
};

enum class ToolpathPreviewColorMode : int {
    PreviewColor = 0,
    FeatureType,
    Speed,
    ActualSpeed,
    Acceleration,
    Jerk,
    LayerHeight,
    LineWidth,
    VolumetricFlow,
    ActualVolumetricFlow,
    LayerTime,
    LayerTimeLogarithmic,
    FanSpeed,
    Temperature,
    PressureAdvance,
    Tool
};

enum class ToolpathPreviewViewType : std::uint8_t {
    Summary = 0,
    FeatureType,
    Filament,
    Speed,
    ActualSpeed,
    Acceleration,
    Jerk,
    LayerHeight,
    LineWidth,
    VolumetricFlow,
    ActualVolumetricFlow,
    LayerTime,
    LayerTimeLogarithmic,
    FanSpeed,
    Temperature,
    PressureAdvance
};

enum class ToolpathPreviewOptionKind : std::uint8_t {
    Travel = 0,
    Wipe,
    Seam
};

enum class ToolpathArtifactOwnership : std::uint8_t {
    External = 0,
    GeneratedTemporary
};

/** Identifies how a toolpath preview can be regenerated. */
enum class ToolpathPreviewSourceScope : std::uint8_t {
    Plate = 0,
    SingleModel,
    ImportedGCode
};

struct ToolpathPreviewTool {
    std::uint16_t id = 0xffffu;
    std::uint16_t primaryFilamentId = 0xffffu;
    Vector3 offsetMm;
    float nozzleDiameterMm = 0.0f;
    std::uint32_t flags = 0;
};

struct ToolpathPreviewFilament {
    std::uint16_t id = 0xffffu;
    std::uint16_t toolId = 0xffffu;
    Vector4 colorRgba;
    float diameterMm = 0.0f;
    float density = 0.0f;
    float cost = 0.0f;
    std::uint32_t flags = 0;
};

struct ToolpathPreviewColor {
    std::uint16_t id = 0xffffu;
    std::uint16_t filamentId = 0xffffu;
    std::uint8_t source = 0;
    Vector4 colorRgba;
    std::string name;
    std::uint32_t flags = 0;
};

struct ToolpathPreviewLayer {
    std::uint32_t id = 0;
    std::uint32_t segmentBegin = 0;
    std::uint32_t segmentCount = 0;
    float printZMm = 0.0f;
    float heightMm = 0.0f;
    float durationS = 0.0f;
};

enum class ToolpathDepositionKind : std::uint8_t { None, Thermoplastic, ContinuousFiberPowered, ContinuousFiberPassive };
enum class ToolpathFiberPhase : std::uint8_t { None, Approach, Prefeed, Ready, Landing, Powered, Cut, Tail, Depleted, Finish, Complete };

struct ToolpathPreviewSegment {
    Vector3 startMm;
    Vector3 endMm;
    std::uint64_t id = 0;
    std::uint64_t runId = 0;
    std::uint32_t sourceCommandId = 0;
    std::uint32_t layerId = 0;
    std::uint32_t objectId = 0xffffffffu;
    std::uint32_t instanceId = 0xffffffffu;
    std::uint16_t toolId = 0xffffu;
    std::uint16_t filamentId = 0xffffu;
    std::uint16_t cpColorId = 0xffffu;
    ToolpathMotionKind motion = ToolpathMotionKind::Travel;
    ToolpathDepositionKind deposition = ToolpathDepositionKind::None;
    ToolpathFiberPhase fiberPhase = ToolpathFiberPhase::None;
    std::uint64_t fiberOccurrence = 0;
    float fiberFeedDeltaMm = 0.0f;
    float depositedPathLengthMm = 0.0f;
    std::uint8_t extrusionRole = 0;
    float extrusionDeltaMm = 0.0f;
    float widthMm = 0.0f;
    float heightMm = 0.0f;
    float feedrateMmS = 0.0f;
    float actualFeedrateMmS = 0.0f;
    float mm3PerMm = 0.0f;
    float printZMm = 0.0f;
    float durationS = 0.0f;
    float layerDurationS = 0.0f;
    float fanSpeedPercent = 0.0f;
    float temperatureC = 0.0f;
    float pressureAdvance = 0.0f;
    float accelerationMmS2 = 0.0f;
    float jerkMmS = 0.0f;
};

struct ToolpathPreviewFeatureStats {
    std::uint8_t extrusionRole = 0;
    std::size_t renderSegmentCount = 0;
    std::size_t pathCount = 0;
    double lengthMm = 0.0;
    double extrusionVolumeMm3 = 0.0;
    double durationS = 0.0;
    double filamentLengthM = 0.0;
    double filamentWeightG = 0.0;
};

struct ToolpathPreviewOptionStats {
    ToolpathPreviewOptionKind kind = ToolpathPreviewOptionKind::Travel;
    std::size_t occurrenceCount = 0;
    double durationS = 0.0;
    double distanceMm = 0.0;
};

struct ToolpathPreviewFilamentUsage {
    std::uint16_t filamentId = 0xffffu;
    double modelVolumeMm3 = 0.0;
    double supportVolumeMm3 = 0.0;
    double flushedVolumeMm3 = 0.0;
    double towerVolumeMm3 = 0.0;
    double totalVolumeMm3 = 0.0;
};

struct ToolpathPreviewStats {
    int totalLayers = 0;
    int logicalMotionCount = 0;
    int renderSegmentCount = 0;
    float totalTimeS = 0.0f;
    float totalExtrusionMm = 0.0f;
    float totalPrintDistanceMm = 0.0f;
    float totalTravelDistanceMm = 0.0f;
    double totalFilamentLengthMm = 0.0;
    double totalFilamentWeightG = 0.0;
    double totalFilamentCost = 0.0;
    std::size_t totalFilamentChanges = 0;
    std::size_t totalToolChanges = 0;
    float minSpeedMmS = 0.0f;
    float maxSpeedMmS = 0.0f;
    float minActualSpeedMmS = 0.0f;
    float maxActualSpeedMmS = 0.0f;
    float minLayerHeightMm = 0.0f;
    float maxLayerHeightMm = 0.0f;
    float minWidthMm = 0.0f;
    float maxWidthMm = 0.0f;
    float minVolumetricFlow = 0.0f;
    float maxVolumetricFlow = 0.0f;
    float minActualVolumetricFlow = 0.0f;
    float maxActualVolumetricFlow = 0.0f;
    float minLayerTimeS = 0.0f;
    float maxLayerTimeS = 0.0f;
    float minFanSpeedPercent = 0.0f;
    float maxFanSpeedPercent = 0.0f;
    float minTemperatureC = 0.0f;
    float maxTemperatureC = 0.0f;
    float minPressureAdvance = 0.0f;
    float maxPressureAdvance = 0.0f;
    float minAccelerationMmS2 = 0.0f;
    float maxAccelerationMmS2 = 0.0f;
    float minJerkMmS = 0.0f;
    float maxJerkMmS = 0.0f;
    std::vector<ToolpathPreviewFeatureStats> features;
    std::vector<ToolpathPreviewOptionStats> options;
    std::vector<ToolpathPreviewFilamentUsage> filamentUsage;
};

struct ToolpathPreviewBounds {
    Vector3 min;
    Vector3 max;
    bool valid = false;
};

enum class ToolpathPreviewLegendGroupKind : int {
    Filament = 0,
    FeatureType = 1,
    Tool = 2,
    MoveType = 3
};

struct ToolpathPreviewLegendItem {
    std::string id;
    std::string label;
    Vector4 colorRgba;
    ToolpathPreviewLegendGroupKind group = ToolpathPreviewLegendGroupKind::Filament;
    std::uint32_t rawId = 0;
    int segmentCount = 0;
    bool visible = true;
};

// Point-like event rendered independently from path segments, e.g. a Z seam.
struct ToolpathPreviewOption {
    Vector3 positionMm;
    std::uint32_t layerId = 0;
    // Global segment boundary after which this event becomes visible.
    std::uint64_t afterSegment = 0;
    ToolpathEventKind kind = ToolpathEventKind::CustomGCode;
    std::uint32_t sourceCommandId = 0;
};

inline constexpr float kSeamMarkerGrey = 0.902f;

enum class FiberDiagnosticKind : std::uint8_t { RejectedPath, OriginalContourRegion, MissingContourRegion, ContourCandidate, RoundedContourCandidate };

struct FiberFillDiagnosticPath {
    std::vector<Vector3> points;
    std::string reason;
    bool contour{false};
    std::uint32_t layerId{0};
    double sourceLengthMm{0.0};
    std::size_t objectIndex{0};
    std::size_t instanceIndex{0};
    FiberDiagnosticKind kind{FiberDiagnosticKind::RejectedPath};
    // Each region record has one outer loop followed by its holes; loops are closed.
    std::vector<std::vector<Vector3>> boundaries;
    // Independent triangle vertices for missing-region fill, with holes excluded.
    std::vector<Vector3> triangles;
    std::size_t policyGroupId{0};
    std::size_t componentId{0};
};

struct ToolpathPreviewData {
    std::string artifactPath;
    std::string printGCodePath;
    ToolpathArtifactOwnership printGCodeOwnership{ToolpathArtifactOwnership::External};
    std::string printGCode3mfPath;
    ToolpathArtifactOwnership printGCode3mfOwnership{ToolpathArtifactOwnership::External};
    std::string metadataJson;
    std::vector<FiberFillDiagnosticPath> fiberFillDiagnostics;
    std::vector<ToolpathPreviewLayer> layers;
    std::vector<ToolpathPreviewSegment> segments;
    std::vector<ToolpathPreviewOption> options;
    std::vector<ToolpathPreviewTool> tools;
    std::vector<ToolpathPreviewFilament> filaments;
    std::vector<ToolpathPreviewColor> colors;
    std::vector<ToolpathPreviewViewType> supportedViewTypes;
    ToolpathPreviewStats stats;
    ToolpathPreviewBounds bounds;
};

} // namespace GPlatform
