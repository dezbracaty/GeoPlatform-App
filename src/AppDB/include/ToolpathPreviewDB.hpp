#pragma once

#include "ActorDB.hpp"
#include "ToolpathPreviewTypes.hpp"
#include "GCodeSource.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace GPlatform {

struct ToolpathTemporaryArtifactLease;

class ToolpathPreviewDB : public ActorDB {
public:
    ToolpathPreviewDB();
    ~ToolpathPreviewDB() override;

    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, int, CurrentLayer)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, int, CurrentStep)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, int, ShowLayerRangeStart)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, int, ShowLayerRangeEnd)
    FIELD_VALUE(ToolpathPreviewDB, std::string, SourceModelName)
    FIELD_VALUE(ToolpathPreviewDB, std::string, PreviewArtifactPath)
    FIELD_VALUE(ToolpathPreviewDB, std::string, PrintGCodePath)
    FIELD_VALUE(ToolpathPreviewDB, std::string, PrintGCode3mfPath)
    FIELD_VALUE_SIMPLE(ToolpathPreviewDB, ToolpathPreviewSourceScope, SourceScope)
    FIELD_VALUE_SIMPLE(ToolpathPreviewDB, DBInstanceID, SourceModelDBId)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, ToolpathPreviewColorMode, ColorMode)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, bool, ShowTravel)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, bool, ShowFiberProcess)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, bool, ShowFiberDiagnostics)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, unsigned int, HiddenFiberDiagnosticKinds)
    TRANSIENT_FIELD_VALUE_SIMPLE(ToolpathPreviewDB, bool, ShowSeam)
    TRANSIENT_FIELD_VALUE(ToolpathPreviewDB, std::vector<std::uint16_t>, HiddenPreviewColorIds)
    TRANSIENT_FIELD_VALUE(ToolpathPreviewDB, std::vector<std::uint8_t>, HiddenExtrusionRoles)
    TRANSIENT_FIELD_VALUE(ToolpathPreviewDB, std::vector<std::uint16_t>, HiddenToolIds)

    struct InspectionElement {
        enum class Kind { Segment, Event, Diagnostic, Endpoint };
        Kind kind{Kind::Segment};
        std::size_t index{0}, subIndex{0};
        std::uint32_t sourceLine{0}; // 1-based final G-code source line; zero means none.
    };
    const std::vector<InspectionElement>& inspectionElements() const { return m_inspectionElements; }
    struct InspectionVisibility {
        std::uint64_t generation{0};
        int firstLayer{0}, lastLayer{-1}, step{-1};
        bool visible{false}, travel{false}, fiber{false}, diagnostics{false}, seam{false};
        unsigned int hiddenDiagnosticKinds{0};
        std::vector<std::uint16_t> hiddenColors, hiddenTools;
        std::vector<std::uint8_t> hiddenRoles;
    };
    InspectionVisibility inspectionVisibility() const;
    std::uint32_t sourceLineForPosition(int layerIndex, int step, bool layerStart = false) const;
    bool inspectionElementVisible(std::size_t featureIndex) const;
    bool inspectionElementVisible(std::size_t featureIndex, const InspectionVisibility& visibility) const;
    std::shared_ptr<const GCodeSource> gcodeSource() const { return m_gcodeSource; }
    void setGCodeSource(std::uint64_t generation, std::shared_ptr<const GCodeSource> source);
    qint64 sourceByteSize() const { return m_sourceByteSize; }
    qint64 sourceModified() const { return m_sourceModified; }

    void setPreviewData(ToolpathPreviewData&& data);
    void clearPreviewData();
    void releasePrintOutputArtifacts();

    const std::vector<ToolpathPreviewLayer>& getLayers() const { return m_layers; }
    const std::vector<ToolpathPreviewSegment>& getSegments() const { return m_segments; }
    const std::vector<FiberFillDiagnosticPath>& getFiberFillDiagnostics() const { return m_fiberFillDiagnostics; }
    const std::vector<ToolpathPreviewOption>& getOptions() const { return m_options; }
    const std::vector<ToolpathPreviewTool>& getTools() const { return m_tools; }
    const std::vector<ToolpathPreviewFilament>& getFilaments() const { return m_filaments; }
    const std::vector<ToolpathPreviewColor>& getColors() const { return m_colors; }
    const std::vector<ToolpathPreviewViewType>& getSupportedViewTypes() const { return m_supportedViewTypes; }
    const ToolpathPreviewStats& getStats() const { return m_stats; }
    const std::string& getMetadataJson() const { return m_metadataJson; }
    std::uint64_t getPreviewDataGeneration() const { return m_previewDataGeneration; }

    int getTotalLayers() const { return static_cast<int>(m_layers.size()); }
    int getTotalSteps() const;
    float getTotalTime() const { return m_stats.totalTimeS; }
    float getTotalExtrusion() const { return m_stats.totalExtrusionMm; }
    float getTotalPrintDistance() const { return m_stats.totalPrintDistanceMm; }
    float getTotalTravelDistance() const { return m_stats.totalTravelDistanceMm; }

    bool isPreviewColorIdVisible(std::uint16_t colorId) const;
    bool isExtrusionRoleVisible(std::uint8_t role) const;
    bool isToolIdVisible(std::uint16_t toolId) const;
    bool setPreviewColorIdVisible(std::uint16_t colorId, bool visible);
    bool setExtrusionRoleVisible(std::uint8_t role, bool visible);
    bool setToolIdVisible(std::uint16_t toolId, bool visible);
    bool setPreviewColorIdsVisible(const std::vector<std::uint16_t>& colorIds, bool visible);
    bool setExtrusionRolesVisible(const std::vector<std::uint8_t>& roles, bool visible);
    bool setToolIdsVisible(const std::vector<std::uint16_t>& toolIds, bool visible);
    bool resetLegendVisibility();
    bool hasHiddenLegendItems() const;

    TypeID getTypeID() const override {
        return TypeID::TOOLPATH_PREVIEW_DB;
    }

    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    void initializeSubActorProperties() override;

private:
    void publishInspectionGeometry();
    std::vector<InspectionElement> m_inspectionElements;
    std::shared_ptr<const GCodeSource> m_gcodeSource;
    qint64 m_sourceByteSize{-1}, m_sourceModified{0};
    bool m_suppressPreviewNotifications = false;
    // Monotonic identity for the immutable preview payload. Renderers use this
    // to collapse lifecycle replays of the same PreviewData notification while
    // still rebuilding when a replacement has identical counts and paths.
    std::uint64_t m_previewDataGeneration = 0;
    std::vector<ToolpathPreviewLayer> m_layers;
    std::vector<ToolpathPreviewSegment> m_segments;
    std::vector<FiberFillDiagnosticPath> m_fiberFillDiagnostics;
    std::vector<ToolpathPreviewOption> m_options;
    std::vector<ToolpathPreviewTool> m_tools;
    std::vector<ToolpathPreviewFilament> m_filaments;
    std::vector<ToolpathPreviewColor> m_colors;
    std::vector<ToolpathPreviewViewType> m_supportedViewTypes;
    ToolpathPreviewStats m_stats;
    ToolpathPreviewBounds m_bounds;
    std::string m_metadataJson;
    std::shared_ptr<ToolpathTemporaryArtifactLease> m_printGCodeLease;
    std::shared_ptr<ToolpathTemporaryArtifactLease> m_printGCode3mfLease;
};

} // namespace GPlatform
