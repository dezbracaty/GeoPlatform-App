#pragma once

#include "RenderObjectDBSync.hpp"
#include "ToolpathPreviewDB.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class vtkPolyData;
class vtkPolyDataMapper;
class vtkGlyph3D;
class vtkAssembly;
class vtkActor;
class vtkProp;
class ToolpathBillboardMapper;

class ToolpathPreviewDBSync : public RenderObjectDBSync {
public:
    explicit ToolpathPreviewDBSync();
    ~ToolpathPreviewDBSync() override;

    void update(
        const DBInstanceID& id,
        ChangeType changeType,
        const std::string& propertyName = "") override;
    void update(
        const DBInstanceID& id,
        ChangeType changeType,
        const trans::Prop& prop) override;

protected:
    void createRenderObjects() override;
    vtkSmartPointer<vtkProp> rootProp() const override;
    bool ownsChildProp(vtkProp* prop) const override;
    void releaseRenderObjects() override;
    void onRenderObjectsCreated() override;

private:
    struct PreviewGeometryInputs {
        std::uint64_t dataGeneration = 0;
        std::string artifactPath;
        std::size_t layerCount = 0;
        std::size_t segmentCount = 0;
        std::size_t colorCount = 0;
        int totalLayers = 0;
        int logicalMotionCount = 0;
        int renderSegmentCount = 0;

        bool operator==(const PreviewGeometryInputs& other) const;
    };

    struct VisibleSegmentLists {
        std::vector<std::uint32_t> forward;
        std::vector<std::uint32_t> reverseLayers;
        std::vector<std::uint32_t> finish;
    };

    using RgbaColor = std::array<unsigned char, 4>;

    PreviewGeometryInputs m_cachedGeometryInputs;
    bool m_hasCachedGeometryInputs = false;

    // --- Main-thread color/visibility bookkeeping ---
    // CPU-side base RGB colors per mode (alpha slot unused, kept at 255). Computed lazily and cached;
    // switching color modes never recomputes an already-cached mode.
    std::map<GPlatform::ToolpathPreviewColorMode, std::vector<RgbaColor>> m_baseCellColorsByMode;
    // Visibility is split into two halves so the preview sliders stay cheap:
    //  - m_filterAlpha: per-segment pass/fail of the FILTER state (travel / hidden role / colour /
    //    tool). Layer-independent; only recomputed when a filter actually changes (occasional click).
    //  - one source span per visible layer, each capped by the shared CurrentStep.
    // Final m_visibleAlpha copies filterAlpha only inside those spans, so dragging either slider
    // avoids recomputing filter predicates.
    // Mode-independent (visibility does not depend on colour mode), so one alpha serves every mode.
    std::vector<unsigned char> m_filterAlpha;
    bool m_hasFilterAlpha = false;
    std::vector<unsigned char> m_visibleAlpha;
    std::uint64_t m_alphaVersion = 0;
    // Shared path-vertex model, faithful to libvgcode: the toolpath is one ordered vertex chain and
    // segment i renders between path vertices [m_segStartVertexId[i], +1]. Continuous extrusions share
    // the joining vertex, so the position/hwa/colour buffers are ~half of a 2-verts-per-segment layout.
    // Built on the main thread during rebuild; read by color packing and visible-order generation.
    std::vector<std::uint32_t> m_segStartVertexId;  // per segment -> its start vertex index (id_a)
    std::vector<std::uint32_t> m_segmentLayerIds;   // per segment, source order is layer-contiguous
    std::size_t m_pathVertexCount = 0;
    std::uint64_t m_geometryUploadGeneration = 0;
    GPlatform::ToolpathPreviewColorMode m_activeColorMode = GPlatform::ToolpathPreviewColorMode::PreviewColor;
    std::weak_ptr<const GPlatform::ToolpathFiberRoute> m_colorSelection;
    bool m_hasGeometry = false;

    // Renderer owns one VTK-native root. The root groups the primary toolpath actor and event
    // actor, so visibility, transforms, removal and graphics-resource release share one lifecycle.
    vtkSmartPointer<vtkAssembly> m_rootAssembly;
    vtkSmartPointer<vtkActor> m_toolpathActor;
    vtkSmartPointer<ToolpathBillboardMapper> m_billboardMapper;

    // Shared non-motion event glyphs: diamonds for seams/cuts and smaller spheres for depletion.
    // Visibility uses the same layer, shared Step and process filters as inspection.
    vtkSmartPointer<vtkPolyData> m_seamPolyData;   // one vtkPoints entry per visible event (glyph input)
    vtkSmartPointer<vtkGlyph3D> m_seamGlyph;
    vtkSmartPointer<vtkPolyDataMapper> m_seamMapper;
    vtkSmartPointer<vtkActor> m_seamActor;
    vtkSmartPointer<vtkPolyData> m_fiberDiagnosticPolyData;
    vtkSmartPointer<vtkPolyDataMapper> m_fiberDiagnosticMapper;
    vtkSmartPointer<vtkActor> m_fiberDiagnosticActor;
    std::uint64_t m_diagnosticDataGeneration = 0;
    int m_diagnosticStart = -1, m_diagnosticEnd = -1;
    bool m_diagnosticShown = false;
    unsigned int m_diagnosticHiddenKinds = 0;
    bool m_hasDiagnosticCache = false;
    void rebuildFiberDiagnostics(std::shared_ptr<const GPlatform::ToolpathPreviewDB> db);
    // Cache the event inputs so slider ticks skip the O(N) option rescan and upload when the shown
    // state, visible layer range and shared Step are unchanged. Invalidated on a full rebuild.
    bool m_hasEventCache = false;
    bool m_lastEventShown = false;
    std::uint32_t m_lastEventMinLayerId = 0;
    std::uint32_t m_lastEventMaxLayerId = 0;
    int m_lastEventSharedStep = -2;

    PreviewGeometryInputs captureGeometryInputs(const GPlatform::ToolpathPreviewDB& db) const;
    bool isGeometryProperty(std::string_view prop) const;
    void updateGeometry(const std::string& prop);
    // Ensure CPU base colors for `mode` are cached; returns pointer to them or nullptr on failure.
    const std::vector<RgbaColor>* ensureBaseColors(const GPlatform::ToolpathPreviewDB& db,
                                                   GPlatform::ToolpathPreviewColorMode mode);
    // Recompute the layer-independent filter alpha (O(N), only when a filter changes).
    void recomputeFilterAlpha(const GPlatform::ToolpathPreviewDB& db);
    // Rebuild m_visibleAlpha from cached m_filterAlpha and the per-layer shared-Step spans. Returns
    // true only when the visible set actually changed.
    bool combineVisibleAlpha(const GPlatform::ToolpathPreviewDB& db);
    // Billboard feeds: per-path-vertex packed RGB (2 per segment) for `mode`, and the list of visible
    // segments' start-vertex ids (id_a = 2*segment). Visibility is carried by the list, not the colour.
    std::vector<float> packVertexColors(GPlatform::ToolpathPreviewColorMode mode,
                                      const GPlatform::ToolpathPreviewDB& db);
    VisibleSegmentLists buildVisibleSegmentLists(const GPlatform::ToolpathPreviewDB& db) const;

    bool rebuildPreviewGeometry(std::shared_ptr<const GPlatform::ToolpathPreviewDB> db,
                                const std::string& reason);
    // Rebuild the shared seam/cut/depletion point cloud using the common event visibility rules.
    void rebuildEventMarkers(std::shared_ptr<const GPlatform::ToolpathPreviewDB> db);
    void applyColorMode(std::shared_ptr<const GPlatform::ToolpathPreviewDB> db,
                        const std::string& reason);
    // refilterFirst=true when a filter property changed (recompute filter alpha first); false for a
    // pure preview-slider change (reuse cached filter alpha, only rebuild layer spans).
    void applyVisibilityChange(std::shared_ptr<const GPlatform::ToolpathPreviewDB> db,
                               const std::string& reason,
                               bool refilterFirst);

};
