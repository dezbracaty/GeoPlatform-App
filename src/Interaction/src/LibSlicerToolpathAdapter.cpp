#include "LibSlicerToolpathAdapter.hpp"

#include <libslicer/Toolpath.hpp>

namespace GPlatform {
namespace {

Vector3 point(const libslicer::ToolpathPoint& value)
{
    return Vector3(value.x, value.y, value.z);
}

Vector4 color(const libslicer::ToolpathColorValue& value)
{
    return Vector4(value.red, value.green, value.blue, value.alpha);
}

ToolpathMotionKind motionKind(libslicer::ToolpathMotionKind value)
{
    switch (value) {
    case libslicer::ToolpathMotionKind::Travel: return ToolpathMotionKind::Travel;
    case libslicer::ToolpathMotionKind::Extrusion: return ToolpathMotionKind::Extrusion;
    case libslicer::ToolpathMotionKind::Wipe: return ToolpathMotionKind::Wipe;
    }
    return ToolpathMotionKind::Travel;
}

ToolpathEventKind eventKind(libslicer::ToolpathEventKind value)
{
    switch (value) {
    case libslicer::ToolpathEventKind::Seam: return ToolpathEventKind::Seam;
    case libslicer::ToolpathEventKind::ToolChange: return ToolpathEventKind::ToolChange;
    case libslicer::ToolpathEventKind::ColorChange: return ToolpathEventKind::ColorChange;
    case libslicer::ToolpathEventKind::Pause: return ToolpathEventKind::Pause;
    case libslicer::ToolpathEventKind::CustomGCode: return ToolpathEventKind::CustomGCode;
    }
    return ToolpathEventKind::CustomGCode;
}

ToolpathPreviewViewType viewType(libslicer::ToolpathViewType value)
{
    switch (value) {
    case libslicer::ToolpathViewType::Summary: return ToolpathPreviewViewType::Summary;
    case libslicer::ToolpathViewType::FeatureType: return ToolpathPreviewViewType::FeatureType;
    case libslicer::ToolpathViewType::Filament: return ToolpathPreviewViewType::Filament;
    case libslicer::ToolpathViewType::Speed: return ToolpathPreviewViewType::Speed;
    case libslicer::ToolpathViewType::ActualSpeed: return ToolpathPreviewViewType::ActualSpeed;
    case libslicer::ToolpathViewType::Acceleration: return ToolpathPreviewViewType::Acceleration;
    case libslicer::ToolpathViewType::Jerk: return ToolpathPreviewViewType::Jerk;
    case libslicer::ToolpathViewType::LayerHeight: return ToolpathPreviewViewType::LayerHeight;
    case libslicer::ToolpathViewType::LineWidth: return ToolpathPreviewViewType::LineWidth;
    case libslicer::ToolpathViewType::VolumetricFlow: return ToolpathPreviewViewType::VolumetricFlow;
    case libslicer::ToolpathViewType::ActualVolumetricFlow: return ToolpathPreviewViewType::ActualVolumetricFlow;
    case libslicer::ToolpathViewType::LayerTime: return ToolpathPreviewViewType::LayerTime;
    case libslicer::ToolpathViewType::LayerTimeLogarithmic: return ToolpathPreviewViewType::LayerTimeLogarithmic;
    case libslicer::ToolpathViewType::FanSpeed: return ToolpathPreviewViewType::FanSpeed;
    case libslicer::ToolpathViewType::Temperature: return ToolpathPreviewViewType::Temperature;
    case libslicer::ToolpathViewType::PressureAdvance: return ToolpathPreviewViewType::PressureAdvance;
    }
    return ToolpathPreviewViewType::Summary;
}

ToolpathPreviewOptionKind optionKind(libslicer::ToolpathOptionKind value)
{
    switch (value) {
    case libslicer::ToolpathOptionKind::Travel: return ToolpathPreviewOptionKind::Travel;
    case libslicer::ToolpathOptionKind::Wipe: return ToolpathPreviewOptionKind::Wipe;
    case libslicer::ToolpathOptionKind::Seam: return ToolpathPreviewOptionKind::Seam;
    }
    return ToolpathPreviewOptionKind::Travel;
}

} // namespace

ToolpathPreviewData adaptLibSlicerToolpath(const libslicer::ToolpathPreview& preview)
{
    ToolpathPreviewData result;
    result.printGCodePath = preview.source_path;
    result.metadataJson = preview.metadata_json;
    for (const auto& source : preview.fiber_fill_diagnostics) {
        FiberFillDiagnosticPath path;
        path.reason = source.reason;
        path.contour = source.contour;
        path.layerId = source.layer_index;
        path.sourceLengthMm = source.source_length_mm;
        path.objectIndex = source.object_index;
        path.instanceIndex = source.instance_index;
        switch (source.kind) {
        case libslicer::FiberDiagnosticKind::RejectedPath: path.kind = FiberDiagnosticKind::RejectedPath; break;
        case libslicer::FiberDiagnosticKind::OriginalContourRegion: path.kind = FiberDiagnosticKind::OriginalContourRegion; break;
        case libslicer::FiberDiagnosticKind::ContourCandidate: path.kind = FiberDiagnosticKind::ContourCandidate; break;
        case libslicer::FiberDiagnosticKind::RoundedContourCandidate: path.kind = FiberDiagnosticKind::RoundedContourCandidate; break;
        case libslicer::FiberDiagnosticKind::MissingContourRegion: path.kind = FiberDiagnosticKind::MissingContourRegion; break;
        }
        path.policyGroupId = source.policy_group_id;
        path.componentId = source.component_id;
        for (const auto& boundary : source.boundaries) {
            std::vector<Vector3> loop;
            for (const auto& value : boundary) loop.push_back(point(value));
            path.boundaries.push_back(std::move(loop));
        }
        for (const auto& value : source.triangles) path.triangles.push_back(point(value));
        for (const auto& value : source.points) path.points.push_back(point(value));
        result.fiberFillDiagnostics.push_back(std::move(path));
    }
    result.layers.reserve(preview.layers.size());
    result.segments.reserve(preview.segments.size());
    result.options.reserve(preview.events.size());
    result.tools.reserve(preview.tools.size());
    result.filaments.reserve(preview.filaments.size());
    result.colors.reserve(preview.colors.size());
    result.supportedViewTypes.reserve(preview.supported_view_types.size());

    for (const auto input : preview.supported_view_types) {
        result.supportedViewTypes.push_back(viewType(input));
    }

    for (const auto& input : preview.tools) {
        ToolpathPreviewTool output;
        output.id = input.id;
        output.primaryFilamentId = input.primary_filament_id;
        output.offsetMm = point(input.offset_mm);
        output.nozzleDiameterMm = input.nozzle_diameter_mm;
        result.tools.push_back(output);
    }
    for (const auto& input : preview.filaments) {
        ToolpathPreviewFilament output;
        output.id = input.id;
        output.toolId = input.tool_id;
        output.colorRgba = color(input.color);
        output.diameterMm = input.diameter_mm;
        output.density = input.density_g_cm3;
        output.cost = input.cost_per_kg;
        result.filaments.push_back(output);
    }
    for (const auto& input : preview.colors) {
        ToolpathPreviewColor output;
        output.id = input.id;
        output.filamentId = input.filament_id;
        output.source = static_cast<std::uint8_t>(input.source);
        output.colorRgba = color(input.color);
        output.name = input.name;
        result.colors.push_back(std::move(output));
    }
    for (const auto& input : preview.layers) {
        ToolpathPreviewLayer output;
        output.id = input.index;
        output.segmentBegin = static_cast<std::uint32_t>(input.segment_begin);
        output.segmentCount = static_cast<std::uint32_t>(input.segment_count);
        output.printZMm = input.print_z_mm;
        output.heightMm = input.height_mm;
        output.durationS = input.duration_seconds;
        result.layers.push_back(output);
    }
    for (const auto& input : preview.segments) {
        ToolpathPreviewSegment output;
        output.startMm = point(input.start_mm);
        output.endMm = point(input.end_mm);
        output.id = input.id;
        output.runId = input.run_id;
        output.sourceCommandId = input.source_command_id;
        output.layerId = input.layer_index;
        output.objectId = input.object_id;
        output.instanceId = input.instance_id;
        output.toolId = input.tool_id;
        output.filamentId = input.filament_id;
        output.cpColorId = input.color_id;
        output.motion = motionKind(input.motion);
        switch (input.deposition) {
        case libslicer::ToolpathDepositionKind::None: output.deposition = ToolpathDepositionKind::None; break;
        case libslicer::ToolpathDepositionKind::Thermoplastic: output.deposition = ToolpathDepositionKind::Thermoplastic; break;
        case libslicer::ToolpathDepositionKind::ContinuousFiberPowered: output.deposition = ToolpathDepositionKind::ContinuousFiberPowered; break;
        case libslicer::ToolpathDepositionKind::ContinuousFiberPassive: output.deposition = ToolpathDepositionKind::ContinuousFiberPassive; break;
        }
        switch (input.fiber_phase) {
        case libslicer::ToolpathFiberPhase::None: output.fiberPhase = ToolpathFiberPhase::None; break;
        case libslicer::ToolpathFiberPhase::Approach: output.fiberPhase = ToolpathFiberPhase::Approach; break;
        case libslicer::ToolpathFiberPhase::Prefeed: output.fiberPhase = ToolpathFiberPhase::Prefeed; break;
        case libslicer::ToolpathFiberPhase::Ready: output.fiberPhase = ToolpathFiberPhase::Ready; break;
        case libslicer::ToolpathFiberPhase::Landing: output.fiberPhase = ToolpathFiberPhase::Landing; break;
        case libslicer::ToolpathFiberPhase::Powered: output.fiberPhase = ToolpathFiberPhase::Powered; break;
        case libslicer::ToolpathFiberPhase::Cut: output.fiberPhase = ToolpathFiberPhase::Cut; break;
        case libslicer::ToolpathFiberPhase::Tail: output.fiberPhase = ToolpathFiberPhase::Tail; break;
        case libslicer::ToolpathFiberPhase::Depleted: output.fiberPhase = ToolpathFiberPhase::Depleted; break;
        case libslicer::ToolpathFiberPhase::Finish: output.fiberPhase = ToolpathFiberPhase::Finish; break;
        case libslicer::ToolpathFiberPhase::Complete: output.fiberPhase = ToolpathFiberPhase::Complete; break;
        }
        output.fiberOccurrence = input.fiber_occurrence;
        output.fiberFeedDeltaMm = input.fiber_feed_delta_mm;
        output.depositedPathLengthMm = input.deposited_path_length_mm;
        output.extrusionRole = static_cast<std::uint8_t>(input.extrusion_role);
        output.extrusionDeltaMm = input.extrusion_delta_mm;
        output.widthMm = input.width_mm;
        output.heightMm = input.height_mm;
        output.feedrateMmS = input.nominal_speed_mm_s;
        output.actualFeedrateMmS = input.actual_speed_mm_s;
        output.mm3PerMm = input.mm3_per_mm;
        output.printZMm = input.print_z_mm;
        output.durationS = input.duration_seconds;
        output.layerDurationS = input.layer_duration_seconds;
        output.fanSpeedPercent = input.fan_speed_percent;
        output.temperatureC = input.temperature_c;
        output.pressureAdvance = input.pressure_advance;
        output.accelerationMmS2 = input.acceleration_mm_s2;
        output.jerkMmS = input.jerk_mm_s;
        result.segments.push_back(output);
    }
    for (const auto& input : preview.events) {
        ToolpathPreviewOption output;
        output.sourceCommandId = input.source_command_id;
        output.positionMm = point(input.position_mm);
        output.layerId = input.layer_index;
        output.afterSegment = input.after_segment;
        output.kind = eventKind(input.kind);
        result.options.push_back(output);
    }

    const auto& stats = preview.statistics;
    result.stats.totalLayers = static_cast<int>(stats.total_layers);
    result.stats.logicalMotionCount = static_cast<int>(stats.logical_motion_count);
    result.stats.renderSegmentCount = static_cast<int>(stats.render_segment_count);
    result.stats.totalTimeS = static_cast<float>(stats.total_time_seconds);
    result.stats.totalExtrusionMm = static_cast<float>(stats.total_extrusion_mm);
    result.stats.totalPrintDistanceMm = static_cast<float>(stats.total_print_distance_mm);
    result.stats.totalTravelDistanceMm = static_cast<float>(stats.total_travel_distance_mm);
    result.stats.totalFilamentLengthMm = stats.total_filament_length_mm;
    result.stats.totalFilamentWeightG = stats.total_filament_weight_g;
    result.stats.totalFilamentCost = stats.total_filament_cost;
    result.stats.totalFilamentChanges = stats.total_filament_changes;
    result.stats.totalToolChanges = stats.total_tool_changes;
    result.stats.minSpeedMmS = stats.min_speed_mm_s;
    result.stats.maxSpeedMmS = stats.max_speed_mm_s;
    result.stats.minActualSpeedMmS = stats.min_actual_speed_mm_s;
    result.stats.maxActualSpeedMmS = stats.max_actual_speed_mm_s;
    result.stats.minLayerHeightMm = stats.min_layer_height_mm;
    result.stats.maxLayerHeightMm = stats.max_layer_height_mm;
    result.stats.minWidthMm = stats.min_width_mm;
    result.stats.maxWidthMm = stats.max_width_mm;
    result.stats.minVolumetricFlow = stats.min_volumetric_flow_mm3_s;
    result.stats.maxVolumetricFlow = stats.max_volumetric_flow_mm3_s;
    result.stats.minActualVolumetricFlow = stats.min_actual_volumetric_flow_mm3_s;
    result.stats.maxActualVolumetricFlow = stats.max_actual_volumetric_flow_mm3_s;
    result.stats.minLayerTimeS = stats.min_layer_time_seconds;
    result.stats.maxLayerTimeS = stats.max_layer_time_seconds;
    result.stats.minFanSpeedPercent = stats.min_fan_speed_percent;
    result.stats.maxFanSpeedPercent = stats.max_fan_speed_percent;
    result.stats.minTemperatureC = stats.min_temperature_c;
    result.stats.maxTemperatureC = stats.max_temperature_c;
    result.stats.minPressureAdvance = stats.min_pressure_advance;
    result.stats.maxPressureAdvance = stats.max_pressure_advance;
    result.stats.minAccelerationMmS2 = stats.min_acceleration_mm_s2;
    result.stats.maxAccelerationMmS2 = stats.max_acceleration_mm_s2;
    result.stats.minJerkMmS = stats.min_jerk_mm_s;
    result.stats.maxJerkMmS = stats.max_jerk_mm_s;
    result.stats.features.reserve(stats.features.size());
    for (const auto& input : stats.features) {
        ToolpathPreviewFeatureStats output;
        output.extrusionRole = static_cast<std::uint8_t>(input.role);
        output.renderSegmentCount = input.render_segment_count;
        output.pathCount = input.path_count;
        output.lengthMm = input.length_mm;
        output.extrusionVolumeMm3 = input.extrusion_volume_mm3;
        output.durationS = input.duration_seconds;
        output.filamentLengthM = input.filament_length_m;
        output.filamentWeightG = input.filament_weight_g;
        result.stats.features.push_back(output);
    }
    result.stats.options.reserve(stats.options.size());
    for (const auto& input : stats.options) {
        ToolpathPreviewOptionStats output;
        output.kind = optionKind(input.kind);
        output.occurrenceCount = input.occurrence_count;
        output.durationS = input.duration_seconds;
        output.distanceMm = input.distance_mm;
        result.stats.options.push_back(output);
    }
    result.stats.filamentUsage.reserve(stats.filament_usage.size());
    for (const auto& input : stats.filament_usage) {
        ToolpathPreviewFilamentUsage output;
        output.filamentId = input.filament_id;
        output.modelVolumeMm3 = input.model_volume_mm3;
        output.supportVolumeMm3 = input.support_volume_mm3;
        output.flushedVolumeMm3 = input.flushed_volume_mm3;
        output.towerVolumeMm3 = input.tower_volume_mm3;
        output.totalVolumeMm3 = input.total_volume_mm3;
        result.stats.filamentUsage.push_back(output);
    }
    result.bounds.valid = preview.bounds.valid;
    if (result.bounds.valid) {
        result.bounds.min = point(preview.bounds.minimum);
        result.bounds.max = point(preview.bounds.maximum);
    }
    return result;
}

} // namespace GPlatform
