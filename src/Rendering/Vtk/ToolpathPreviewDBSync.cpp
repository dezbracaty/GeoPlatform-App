#include "ToolpathPreviewDBSync.hpp"

#include "Foundation/Log.h"
#include "ToolpathPreviewUtils.hpp"
#include "VtkDBSyncFactory.hpp"

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkAssembly.h>
#include <vtkGlyph3D.h>
#include <vtkPlatonicSolidSource.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkPointData.h>
#include <vtkDataObject.h>

#include "ToolpathBillboardMapper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace GPlatform;

namespace {

using RgbaColor = std::array<unsigned char, 4>;

bool buildBaseCellColors(const ToolpathPreviewDB& preview,
                         ToolpathPreviewColorMode colorMode,
                         const GPlatform::ToolpathPreviewUtils::PreviewColorMap& previewColorById,
                         const ToolpathPreviewStats& stats,
                         std::vector<RgbaColor>& colors) {
    const auto& segments = preview.getSegments();
    colors.clear();
    colors.reserve(segments.size());

    for (const auto& segment : segments) {
        const auto color = GPlatform::ToolpathPreviewUtils::resolveSegmentColor(
            segment,
            colorMode,
            previewColorById,
            stats);
        if (!color) {
            LOG_ERROR("ToolpathPreviewDBSync: segment missing preview color cpColorId={} toolId={} filamentId={} layerId={}",
                      segment.cpColorId,
                      segment.toolId,
                      segment.filamentId,
                      segment.layerId);
            return false;
        }
        colors.push_back({
            (*color)[0],
            (*color)[1],
            (*color)[2],
            static_cast<unsigned char>(255)
        });
    }
    return true;
}

// Preview-slider properties only change selected layers or their shared Step cap; filter results
// remain reusable.
bool isLayerProperty(std::string_view prop) {
    return prop == "CurrentStep" ||
           prop == "ShowLayerRangeStart" ||
           prop == "ShowLayerRangeEnd";
}

// Filter properties: change which segments pass the travel/role/colour/tool filters, so the
// per-segment filter alpha must be recomputed (O(N), but only on an occasional click).
bool isFilterProperty(std::string_view prop) {
    return prop == "ShowTravel" ||
           prop == "ShowFiberDiagnostics" ||
           prop == "HiddenFiberDiagnosticKinds" ||
           prop == "ShowFiberProcess" ||
           prop == "ShowFiberCutPoints" ||
           prop == "ShowSeam" ||
           prop == "HiddenPreviewColorIds" ||
           prop == "HiddenExtrusionRoles" ||
           prop == "HiddenToolIds";
}

} // namespace

ToolpathPreviewDBSync::ToolpathPreviewDBSync() {
}

ToolpathPreviewDBSync::~ToolpathPreviewDBSync() {
    LOG_DEBUG("ToolpathPreviewDBSync destroyed");
}

void ToolpathPreviewDBSync::createRenderObjects() {
    m_rootAssembly = vtkSmartPointer<ToolpathPreviewAssembly>::New();
    m_rootAssembly->PickableOff();

    // Keep VTK scene semantics in vtkActor; only the high-volume instanced draw lives in a custom
    // mapper. The actor is a real child of the preview assembly, not an empty lifecycle placeholder.
    m_billboardMapper = vtkSmartPointer<ToolpathBillboardMapper>::New();
    m_toolpathActor = vtkSmartPointer<vtkActor>::New();
    m_toolpathActor->SetMapper(m_billboardMapper);
    m_toolpathActor->PickableOff();
    m_rootAssembly->AddPart(m_toolpathActor);

    // Event markers share one glyph pipeline and the path's camera/model transforms.
    m_seamPolyData = vtkSmartPointer<vtkPolyData>::New();
    auto seamSource = vtkSmartPointer<vtkPlatonicSolidSource>::New();
    seamSource->SetSolidTypeToOctahedron();  // circumradius 1
    // Size the diamond by scaling the SOURCE geometry (glyph ScaleFactor is ignored when
    // ScaleModeToDataScalingOff is set, so it must be baked into the source here).
    constexpr double kSeamRadiusMm = 0.5;  // -> ~1 mm diamond (a bit larger than the 0.4 mm extrusion)
    auto seamScale = vtkSmartPointer<vtkTransform>::New();
    seamScale->Scale(kSeamRadiusMm, kSeamRadiusMm, kSeamRadiusMm);
    auto seamScaled = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    seamScaled->SetInputConnection(seamSource->GetOutputPort());
    seamScaled->SetTransform(seamScale);
    m_seamGlyph = vtkSmartPointer<vtkGlyph3D>::New();
    m_seamGlyph->SetInputData(m_seamPolyData);
    m_seamGlyph->SetSourceConnection(seamScaled->GetOutputPort());
    m_seamGlyph->SetScaleModeToDataScalingOff();  // uniform size, ignore point scalars
    auto depletedSource = vtkSmartPointer<vtkPlatonicSolidSource>::New();
    depletedSource->SetSolidTypeToIcosahedron();
    auto depletedScale = vtkSmartPointer<vtkTransform>::New();
    depletedScale->Scale(0.3, 0.3, 0.3);
    auto depletedScaled = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    depletedScaled->SetInputConnection(depletedSource->GetOutputPort());
    depletedScaled->SetTransform(depletedScale);
    m_seamGlyph->SetSourceConnection(1, depletedScaled->GetOutputPort());
    m_seamGlyph->SetIndexModeToScalar();
    m_seamGlyph->SetRange(0, 1);
    m_seamGlyph->SetColorModeToColorByScalar();
    m_seamGlyph->SetInputArrayToProcess(3, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "EventColors");
    m_seamMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_seamMapper->SetInputConnection(m_seamGlyph->GetOutputPort());
    m_seamMapper->ScalarVisibilityOn();
    m_seamMapper->SetScalarModeToUsePointFieldData();
    m_seamMapper->SelectColorArray("EventColors");
    m_seamMapper->SetColorModeToDirectScalars();
    m_seamActor = vtkSmartPointer<vtkActor>::New();
    m_seamActor->SetMapper(m_seamMapper);
    m_seamActor->PickableOff();
    m_seamActor->SetVisibility(0);
    auto* seamProperty = m_seamActor->GetProperty();
    seamProperty->SetColor(kSeamMarkerGrey, kSeamMarkerGrey, kSeamMarkerGrey);  // OrcaSlicer #E6E6E6
    seamProperty->SetInterpolationToFlat();
    seamProperty->SetAmbient(1.0);    // fully emissive -> every visible facet reads at full brightness
    seamProperty->SetDiffuse(0.0);
    seamProperty->SetSpecular(0.0);
    m_rootAssembly->AddPart(m_seamActor);
    m_fiberDiagnosticPolyData = vtkSmartPointer<vtkPolyData>::New();
    m_fiberDiagnosticMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_fiberDiagnosticMapper->SetInputData(m_fiberDiagnosticPolyData);
    m_fiberDiagnosticMapper->ScalarVisibilityOn();
    m_fiberDiagnosticMapper->SetScalarModeToUseCellData();
    m_fiberDiagnosticMapper->SetColorModeToDirectScalars();
    m_fiberDiagnosticActor = vtkSmartPointer<vtkActor>::New();
    m_fiberDiagnosticActor->SetMapper(m_fiberDiagnosticMapper);
    m_fiberDiagnosticActor->PickableOff();
    m_fiberDiagnosticActor->SetVisibility(0);
    m_fiberDiagnosticActor->GetProperty()->SetColor(1.0, 0.5, 0.0);
    m_fiberDiagnosticActor->GetProperty()->SetLineWidth(3.0);
    m_fiberDiagnosticActor->GetProperty()->LightingOff();
    m_rootAssembly->AddPart(m_fiberDiagnosticActor);
}

vtkSmartPointer<vtkProp> ToolpathPreviewDBSync::rootProp() const {
    return m_rootAssembly;
}

bool ToolpathPreviewDBSync::ownsChildProp(vtkProp* prop) const {
    return prop && (prop == m_toolpathActor.GetPointer() || prop == m_seamActor.GetPointer() ||
                    prop == m_fiberDiagnosticActor.GetPointer());
}

void ToolpathPreviewDBSync::releaseRenderObjects() {
    m_rootAssembly = nullptr;
    m_toolpathActor = nullptr;
    m_billboardMapper = nullptr;
    m_seamActor = nullptr;
    m_fiberDiagnosticActor = nullptr;
    m_fiberDiagnosticMapper = nullptr;
    m_fiberDiagnosticPolyData = nullptr;
    m_hasDiagnosticCache = false;
    m_seamMapper = nullptr;
    m_seamGlyph = nullptr;
    m_seamPolyData = nullptr;
    m_baseCellColorsByMode.clear();
    m_filterAlpha.clear();
    m_visibleAlpha.clear();
    m_segStartVertexId.clear();
    m_pathVertexCount = 0;
    m_geometryUploadGeneration = 0;
    m_hasCachedGeometryInputs = false;
    m_hasFilterAlpha = false;
    m_hasGeometry = false;
    m_hasEventCache = false;
}

void ToolpathPreviewDBSync::onRenderObjectsCreated() {
    // A preview DB can predate this renderer (the Preview Dock page owns a separate, lazily started
    // render thread). OBJECT_CREATED only installs the DBSync; it does not replay PreviewData. Seed
    // geometry from the current DB state once the mapper exists so both lifecycle orders converge.
    updateGeometry("PreviewData");
    if (m_renderThread) {
        requestRender();
    }
}

void ToolpathPreviewDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const std::string& propertyName) {
    (void)id;
    if (!isActive()) {
        return;
    }

    switch (changeType) {
    case ChangeType::OBJECT_CREATED:
        updateGeometry(propertyName);
        if (const auto db = documentDB<ToolpathPreviewDB>(m_id)) {
            setVisible(db->getVisible());
        }
        break;
    case ChangeType::PROPERTY_CHANGED:
        if (propertyName == "Visible") {
            if (const auto db = documentDB<ToolpathPreviewDB>(m_id)) {
                setVisible(db->getVisible());
                m_hasEventCache = false;
                if (m_hasGeometry) rebuildEventMarkers(db);
            }
        } else if (propertyName == "Transform") {
            syncTransformFromDB();
        } else if (propertyName.empty() || isGeometryProperty(propertyName)) {
            updateGeometry(propertyName);
        }
        break;
    case ChangeType::OBJECT_DELETED:
    default:
        break;
    }
}

void ToolpathPreviewDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const trans::Prop& prop) {
    update(id, changeType, prop.name());
}

void ToolpathPreviewDBSync::updateGeometry(const std::string& prop) {
    auto db = documentDB<ToolpathPreviewDB>(m_id);
    if (!db) {
        LOG_ERROR("ToolpathPreviewDBSync::updateGeometry - missing DB id={} prop={}",
                  m_id.getValue(),
                  prop);
        return;
    }

    const auto geometryInputs = captureGeometryInputs(*db);
    const bool geometryInputsChanged = !m_hasCachedGeometryInputs ||
                                       !(geometryInputs == m_cachedGeometryInputs);
    const bool previewDataChanged = prop == "PreviewData";

    // PreviewData is the only topology-replacement event. Color/filter/range notifications may overtake
    // it in the render queue; when that happens, defer them and let the pending PreviewData rebuild read
    // the latest state in one pass. Rebuilding here would turn an unrelated buffer update into a full
    // geometry upload and the later PreviewData event would repeat it.
    if (!previewDataChanged && geometryInputsChanged) {
        LOG_DEBUG("ToolpathPreviewDBSync: deferred property={} until PreviewData db={} dataGeneration={}",
                  prop, m_id.getValue(), geometryInputs.dataGeneration);
        return;
    }
    if (previewDataChanged && m_hasCachedGeometryInputs && !geometryInputsChanged) {
        LOG_DEBUG("ToolpathPreviewDBSync: coalesced duplicate PreviewData db={} dataGeneration={}",
                  m_id.getValue(), geometryInputs.dataGeneration);
        return;
    }
    if (previewDataChanged) {
        if (rebuildPreviewGeometry(db, prop)) {
            m_cachedGeometryInputs = geometryInputs;
            m_hasCachedGeometryInputs = true;
        }
        return;
    }

    if (!m_hasGeometry) {
        return;
    }

    // Color mode: cheap active-scalar swap (or a one-time per-mode upload). Never re-streams the
    // whole color array when the target mode is already resident and its alpha is current.
    if (prop == "ColorMode") {
        applyColorMode(db, prop);
        return;
    }

    if (prop == "HighlightFiberTail" || prop == "InspectedElementIndex") {
        if (prop == "InspectedElementIndex" &&
            ToolpathPreviewUtils::inspectedFiberRoute(*db) == m_colorSelection.lock()) return;
        applyColorMode(db, prop);
        return;
    }

    // Filter change (travel / hidden role / colour / tool): recompute the per-segment filter alpha.
    if (isFilterProperty(prop)) {
        m_hasEventCache = false;
        applyVisibilityChange(db, prop, /*refilterFirst=*/true);
        return;
    }

    // Preview sliders: reuse cached filter results and rebuild only the per-layer spans.
    if (isLayerProperty(prop)) {
        applyVisibilityChange(db, prop, /*refilterFirst=*/false);
        return;
    }
}

bool ToolpathPreviewDBSync::isGeometryProperty(std::string_view prop) const {
    return prop == "PreviewData" ||
           prop == "HighlightFiberTail" ||
           prop == "InspectedElementIndex" ||
           prop == "ShowFiberDiagnostics" ||
           prop == "HiddenFiberDiagnosticKinds" ||
           prop == "ShowFiberProcess" ||
           prop == "ShowFiberCutPoints" ||
           prop == "CurrentStep" ||
           prop == "ShowLayerRangeStart" ||
           prop == "ShowLayerRangeEnd" ||
           prop == "ColorMode" ||
           prop == "ShowTravel" ||
           prop == "ShowSeam" ||
           prop == "HiddenPreviewColorIds" ||
           prop == "HiddenExtrusionRoles" ||
           prop == "HiddenToolIds";
}

bool ToolpathPreviewDBSync::PreviewGeometryInputs::operator==(const PreviewGeometryInputs& other) const {
    return dataGeneration == other.dataGeneration &&
           artifactPath == other.artifactPath &&
           layerCount == other.layerCount &&
           segmentCount == other.segmentCount &&
           colorCount == other.colorCount &&
           totalLayers == other.totalLayers &&
           logicalMotionCount == other.logicalMotionCount &&
           renderSegmentCount == other.renderSegmentCount;
}

ToolpathPreviewDBSync::PreviewGeometryInputs ToolpathPreviewDBSync::captureGeometryInputs(
    const ToolpathPreviewDB& db) const {
    const auto& stats = db.getStats();
    return {
        db.getPreviewDataGeneration(),
        db.getPreviewArtifactPath(),
        db.getLayers().size(),
        db.getSegments().size(),
        db.getColors().size(),
        db.getTotalLayers(),
        stats.logicalMotionCount,
        stats.renderSegmentCount
    };
}

const std::vector<ToolpathPreviewDBSync::RgbaColor>* ToolpathPreviewDBSync::ensureBaseColors(
    const ToolpathPreviewDB& db,
    ToolpathPreviewColorMode mode) {
    auto it = m_baseCellColorsByMode.find(mode);
    if (it != m_baseCellColorsByMode.end() && it->second.size() == db.getSegments().size()) {
        return &it->second;
    }
    const auto previewColorById = GPlatform::ToolpathPreviewUtils::buildPreviewColorMap(db.getColors());
    std::vector<RgbaColor> baseColors;
    if (!buildBaseCellColors(db, mode, previewColorById, db.getStats(), baseColors)) {
        return nullptr;
    }
    it = m_baseCellColorsByMode.insert_or_assign(mode, std::move(baseColors)).first;
    return &it->second;
}

void ToolpathPreviewDBSync::recomputeFilterAlpha(const ToolpathPreviewDB& db) {
    m_filterAlpha = GPlatform::ToolpathPreviewUtils::collectFilterPassAlpha(db);
    m_hasFilterAlpha = true;
}

bool ToolpathPreviewDBSync::combineVisibleAlpha(const ToolpathPreviewDB& db) {
    const std::size_t n = m_filterAlpha.size();
    std::vector<unsigned char> next(n, 0);
    if (n > 0) {
        const auto spans = GPlatform::ToolpathPreviewUtils::computeVisibleSegmentSpans(db);
        for (const auto& span : spans) {
            const std::size_t begin = std::min<std::size_t>(span.begin, n);
            const std::size_t end = std::min<std::size_t>(span.end, n);
            if (begin < end) {
                std::memcpy(next.data() + begin, m_filterAlpha.data() + begin, end - begin);
            }
        }
    }
    if (next == m_visibleAlpha) {
        return false;
    }
    m_visibleAlpha = std::move(next);
    ++m_alphaVersion;
    return true;
}

bool ToolpathPreviewDBSync::rebuildPreviewGeometry(
    std::shared_ptr<const ToolpathPreviewDB> db,
    const std::string& reason) {
    if (!db || !m_renderThread) {
        return false;
    }

    // Fresh geometry invalidates every cached color.
    m_baseCellColorsByMode.clear();
    m_hasGeometry = false;

    const auto activeMode = db->getColorMode();
    if (!ensureBaseColors(*db, activeMode)) {
        LOG_ERROR("ToolpathPreviewDBSync: aborted geometry rebuild due to invalid color data reason={}", reason);
        return false;
    }
    recomputeFilterAlpha(*db);
    combineVisibleAlpha(*db);
    const auto& segments = db->getSegments();
    const std::size_t n = segments.size();

    // Build one vertex chain per explicit run. runId is the topology contract supplied by the adapter;
    // render code must never infer continuity from coordinates because a travel may end exactly where
    // an extrusion starts. Travel/wipe paths intentionally use a thin display-only width and are never
    // assigned extrusion bead dimensions.
    std::vector<float> positions;
    std::vector<float> hwa;
    positions.reserve((n + 1) * 4);
    hwa.reserve((n + 1) * 4);
    m_segStartVertexId.assign(n, 0);
    m_segmentLayerIds.resize(n);
    const auto pushVertex = [&](float x, float y, float z, float h, float w, float angle) {
        positions.push_back(x); positions.push_back(y); positions.push_back(z); positions.push_back(0.0f);
        hwa.push_back(h); hwa.push_back(w); hwa.push_back(angle); hwa.push_back(0.0f);
    };
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = segments[i];
        const bool travel = ToolpathPreviewUtils::isTravelSegment(s);
        const float h = travel ? 0.04f : (s.heightMm > 0.0f ? s.heightMm : 0.2f);
        const float w = travel ? 0.04f : (s.widthMm > 0.0f ? s.widthMm : 0.42f);
        const bool shared = i > 0 && segments[i - 1].runId == s.runId;
        if (!shared) {
            pushVertex(s.startMm.x, s.startMm.y, s.startMm.z - 0.5f * h, h, w, 0.0f);
        }
        const std::uint32_t idA = static_cast<std::uint32_t>(positions.size() / 4 - 1);
        float jointAngle = 0.0f;
        if (i + 1 < n && segments[i + 1].runId == s.runId) {
            const auto& next = segments[i + 1];
            const float ax = s.endMm.x - s.startMm.x;
            const float ay = s.endMm.y - s.startMm.y;
            const float bx = next.endMm.x - next.startMm.x;
            const float by = next.endMm.y - next.startMm.y;
            const float lengthProduct = std::hypot(ax, ay) * std::hypot(bx, by);
            if (lengthProduct > 1e-8f) {
                jointAngle = std::atan2(ax * by - ay * bx, ax * bx + ay * by);
            }
        }
        pushVertex(s.endMm.x, s.endMm.y, s.endMm.z - 0.5f * h, h, w, jointAngle);
        m_segStartVertexId[i] = idA;
        m_segmentLayerIds[i] = s.layerId;
    }
    m_pathVertexCount = positions.size() / 4;
    auto packedColors = packVertexColors(activeMode, *db);
    auto visibleLists = buildVisibleSegmentLists(*db);

    m_activeColorMode = activeMode;
    m_hasGeometry = true;
    m_geometryUploadGeneration = db->getPreviewDataGeneration();

    const auto token = generation();
    const auto dataGeneration = m_geometryUploadGeneration;
    auto self = shared_from_this();
    dispatchRenderCommand([self,
                                     token,
                                     positions = std::move(positions),
                                     hwa = std::move(hwa),
                                     packedColors = std::move(packedColors),
                                     visibleLists = std::move(visibleLists),
                                     dataGeneration,
                                     reason](auto*, auto*) mutable {
        auto sync = std::static_pointer_cast<ToolpathPreviewDBSync>(self);
        if (!sync->isGenerationActive(token) || !sync->m_billboardMapper ||
            dataGeneration != sync->m_geometryUploadGeneration) {
            LOG_DEBUG("ToolpathPreviewDBSync: stale or missing billboard mapper during rebuild reason={} dataGeneration={}",
                      reason, dataGeneration);
            return;
        }
        const std::size_t verts = positions.size() / 4;
        const std::size_t visible = visibleLists.forward.size();
        sync->m_billboardMapper->setGeometry(std::move(positions), std::move(hwa));
        sync->m_billboardMapper->setColors(std::move(packedColors));
        sync->m_billboardMapper->setVisibleSegments(
            std::move(visibleLists.forward),
            std::move(visibleLists.reverseLayers),
            std::move(visibleLists.finish));
        LOG_INFO("TPBILLBOARD rebuild db={} dataGeneration={} reason={} verts={} visibleSegments={}",
                 sync->m_id.getValue(), dataGeneration, reason, verts, visible);
    });
    m_hasEventCache = false;  // new preview data -> force an event rebuild regardless of the window key
    rebuildEventMarkers(db);
    return true;
}

void ToolpathPreviewDBSync::rebuildEventMarkers(std::shared_ptr<const ToolpathPreviewDB> db) {
    rebuildFiberDiagnostics(db);
    if (!db || !m_renderThread) return;
    const auto visibility = db->inspectionVisibility();
    const bool show = visibility.visible && (visibility.seam || visibility.fiberCuts);
    const auto& layers = db->getLayers();
    const auto minLayer = layers.empty() ? 0u : layers[std::size_t(visibility.firstLayer)].id;
    const auto maxLayer = layers.empty() ? 0u : layers[std::size_t(visibility.lastLayer)].id;
    if (m_hasEventCache && m_lastEventShown == show && m_lastEventMinLayerId == minLayer &&
        m_lastEventMaxLayerId == maxLayer && m_lastEventSharedStep == visibility.step) return;
    m_hasEventCache = true;
    m_lastEventShown = show;
    m_lastEventMinLayerId = minLayer;
    m_lastEventMaxLayerId = maxLayer;
    m_lastEventSharedStep = visibility.step;

    std::vector<float> coords;
    std::vector<unsigned char> colors, kinds;
    std::map<const ToolpathFiberRoute*, Vector3> visibleCuts;
    if (show) for (const auto& event : db->getOptions()) {
        if (!ToolpathPreviewUtils::isEventVisible(*db, event, visibility)) continue;
        const bool cut = event.kind == ToolpathEventKind::FiberCut;
        const bool depleted = event.kind == ToolpathEventKind::FiberDepleted;
        if (depleted && event.fiberRoute) {
            const auto previous = visibleCuts.find(event.fiberRoute.get());
            if (previous != visibleCuts.end() && previous->second.x == event.positionMm.x &&
                previous->second.y == event.positionMm.y && previous->second.z == event.positionMm.z)
                continue; // Two source events remain independently inspectable; one glyph at a shared point.
        }
        if (cut && event.fiberRoute) visibleCuts.emplace(event.fiberRoute.get(), event.positionMm);
        coords.insert(coords.end(), {event.positionMm.x, event.positionMm.y, event.positionMm.z});
        const std::array<unsigned char, 3> color = cut ? std::array<unsigned char, 3>{255, 140, 40} :
            depleted ? std::array<unsigned char, 3>{60, 200, 255} : std::array<unsigned char, 3>{230, 230, 230};
        colors.insert(colors.end(), color.begin(), color.end());
        kinds.push_back(depleted ? 1 : 0);
    }
    const auto token = generation();
    auto self = shared_from_this();
    dispatchRenderCommand([self, token, coords = std::move(coords), colors = std::move(colors),
                                     kinds = std::move(kinds)](auto*, auto*) {
        auto sync = std::static_pointer_cast<ToolpathPreviewDBSync>(self);
        if (!sync->isGenerationActive(token) || !sync->m_seamActor || !sync->m_seamPolyData) return;
        auto points = vtkSmartPointer<vtkPoints>::New();
        auto rgb = vtkSmartPointer<vtkUnsignedCharArray>::New();
        rgb->SetName("EventColors"); rgb->SetNumberOfComponents(3);
        auto indices = vtkSmartPointer<vtkUnsignedCharArray>::New();
        indices->SetName("EventKinds"); indices->SetNumberOfComponents(1);
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            points->InsertNextPoint(coords[3*i], coords[3*i+1], coords[3*i+2]);
            rgb->InsertNextTypedTuple(colors.data() + 3*i);
            indices->InsertNextValue(kinds[i]);
        }
        sync->m_seamPolyData->SetPoints(points);
        sync->m_seamPolyData->GetPointData()->SetScalars(indices);
        sync->m_seamPolyData->GetPointData()->AddArray(rgb);
        sync->m_seamPolyData->Modified();
        sync->m_seamActor->SetVisibility(!kinds.empty());
    });
}

void ToolpathPreviewDBSync::rebuildFiberDiagnostics(std::shared_ptr<const ToolpathPreviewDB> db) {
    if (!db || !m_renderThread) return;
    if (m_hasDiagnosticCache && m_diagnosticDataGeneration == db->getPreviewDataGeneration() &&
        m_diagnosticStart == db->getShowLayerRangeStart() && m_diagnosticEnd == db->getShowLayerRangeEnd() &&
        m_diagnosticShown == db->getShowFiberDiagnostics() &&
        m_diagnosticHiddenKinds == db->getHiddenFiberDiagnosticKinds()) return;
    m_hasDiagnosticCache = true;
    m_diagnosticDataGeneration = db->getPreviewDataGeneration();
    m_diagnosticStart = db->getShowLayerRangeStart();
    m_diagnosticEnd = db->getShowLayerRangeEnd();
    m_diagnosticShown = db->getShowFiberDiagnostics();
    m_diagnosticHiddenKinds = db->getHiddenFiberDiagnosticKinds();
    std::vector<std::array<double, 3>> coords, triangles;
    std::vector<RgbaColor> lineColors;
    const auto& layers = db->getLayers();
    if (db->getShowFiberDiagnostics() && !layers.empty()) {
        const int start = std::clamp(db->getShowLayerRangeStart(), 0, int(layers.size()) - 1);
        const int end = std::clamp(std::max(start, db->getShowLayerRangeEnd()), 0, int(layers.size()) - 1);
        for (const auto& path : db->getFiberFillDiagnostics()) {
            if (path.layerId < layers[start].id || path.layerId > layers[end].id ||
                (db->getHiddenFiberDiagnosticKinds() & (1u << unsigned(path.kind)))) continue;
            const auto color = path.kind == FiberDiagnosticKind::ContourCandidate
                ? RgbaColor{180,128,255,255} : path.kind == FiberDiagnosticKind::RoundedContourCandidate
                ? RgbaColor{96,223,128,255} : path.kind == FiberDiagnosticKind::OriginalContourRegion
                ? RgbaColor{0,200,230,255} : path.kind == FiberDiagnosticKind::MissingContourRegion
                    ? RgbaColor{255,48,48,255} : RgbaColor{255,128,0,255};
            for (const auto& loop : path.boundaries)
                for (size_t i = 1; i < loop.size(); ++i) {
                    coords.push_back({loop[i-1].x, loop[i-1].y, loop[i-1].z+0.03});
                    coords.push_back({loop[i].x, loop[i].y, loop[i].z+0.03});
                    lineColors.push_back(color);
                }
            for (const auto& point : path.triangles)
                triangles.push_back({point.x, point.y, point.z+0.02});
            // Dash phase follows arc length, independent of tessellation. This
            // overlay has no execution step and is never filtered by travel/tool.
            double arc = 0.0;
            for (size_t i = 1; i < path.points.size(); ++i) {
                const auto& a = path.points[i - 1];
                const auto& b = path.points[i];
                const double dx = b.x-a.x, dy = b.y-a.y, dz = b.z-a.z;
                const double length = std::sqrt(dx*dx + dy*dy + dz*dz);
                for (double s = 0.0; s < length - 1e-8;) {
                    const double phase = std::fmod(arc+s, 1.5);
                    const bool draw = phase < 1.0;
                    const double step = std::min(length-s, (draw ? 1.0 : 1.5) - phase);
                    if (step < 1e-8) { s += 1e-8; continue; }
                    if (draw) {
                        // Small display-only lift avoids z fighting with resin.
                        coords.push_back({a.x+dx*s/length, a.y+dy*s/length, a.z+dz*s/length+0.03});
                        coords.push_back({a.x+dx*(s+step)/length, a.y+dy*(s+step)/length, a.z+dz*(s+step)/length+0.03});
                        lineColors.push_back(color);
                    }
                    s += step;
                }
                arc += length;
            }
        }
    }
    const auto token = generation();
    auto self = shared_from_this();
    dispatchRenderCommand([self, token, coords = std::move(coords),
        triangles = std::move(triangles), lineColors = std::move(lineColors)](auto*, auto*) {
        auto sync = std::static_pointer_cast<ToolpathPreviewDBSync>(self);
        if (!sync->isGenerationActive(token) || !sync->m_fiberDiagnosticActor) return;
        auto points = vtkSmartPointer<vtkPoints>::New();
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        for (const auto& p : coords) points->InsertNextPoint(p.data());
        for (vtkIdType i = 0; i + 1 < points->GetNumberOfPoints(); i += 2) {
            const vtkIdType ids[] = {i, i+1};
            lines->InsertNextCell(2, ids);
        }
        auto polys = vtkSmartPointer<vtkCellArray>::New();
        auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
        colors->SetNumberOfComponents(4);
        colors->SetName("FiberDiagnosticColors");
        for (const auto& color : lineColors) colors->InsertNextTypedTuple(color.data());
        const vtkIdType firstTriangle = points->GetNumberOfPoints();
        for (const auto& point : triangles) points->InsertNextPoint(point.data());
        const RgbaColor fill{255,48,48,64};
        for (vtkIdType i = firstTriangle; i + 2 < points->GetNumberOfPoints(); i += 3) {
            const vtkIdType ids[]{i,i+1,i+2};
            polys->InsertNextCell(3,ids);
            colors->InsertNextTypedTuple(fill.data());
        }
        sync->m_fiberDiagnosticPolyData->SetPoints(points);
        sync->m_fiberDiagnosticPolyData->SetLines(lines);
        sync->m_fiberDiagnosticPolyData->SetPolys(polys);
        sync->m_fiberDiagnosticPolyData->GetCellData()->SetScalars(colors);
        sync->m_fiberDiagnosticPolyData->Modified();
        sync->m_fiberDiagnosticActor->SetVisibility(!coords.empty() || !triangles.empty());
    });
}

void ToolpathPreviewDBSync::applyColorMode(
    std::shared_ptr<const ToolpathPreviewDB> db,
    const std::string& reason) {
    if (!db || !m_renderThread) {
        return;
    }
    const auto targetMode = db->getColorMode();
    if (!ensureBaseColors(*db, targetMode)) {
        LOG_ERROR("ToolpathPreviewDBSync: aborted color mode switch due to invalid color data reason={}", reason);
        return;
    }

    m_activeColorMode = targetMode;
    // Colour mode switch = refill the per-vertex colour buffer (O(vertices)); geometry untouched.
    auto packedColors = packVertexColors(targetMode, *db);
    const auto token = generation();
    auto self = shared_from_this();
    dispatchRenderCommand([self, token, packedColors = std::move(packedColors)](auto*, auto*) {
        auto sync = std::static_pointer_cast<ToolpathPreviewDBSync>(self);
        if (sync->isGenerationActive(token) && sync->m_billboardMapper) {
            sync->m_billboardMapper->setColors(std::move(packedColors));
        }
    });
    m_hasEventCache = false;
    rebuildEventMarkers(db);
    // dispatchCommand only queues the GPU colour upload. Request a frame explicitly so a mode
    // switch becomes visible even when no unrelated scene mutation happens afterwards.
    requestRender();
}

void ToolpathPreviewDBSync::applyVisibilityChange(
    std::shared_ptr<const ToolpathPreviewDB> db,
    const std::string& reason,
    bool refilterFirst) {
    if (!db || !m_renderThread) {
        return;
    }
    if (refilterFirst || !m_hasFilterAlpha) {
        recomputeFilterAlpha(*db);
    }
    if (ToolpathPreviewUtils::inspectedFiberRoute(*db) != m_colorSelection.lock())
        applyColorMode(db, reason);
    // Event visibility can change without changing the visible segment set.
    rebuildEventMarkers(db);
    if (!combineVisibleAlpha(*db)) {
        // Event markers may still have queued a visibility update, which needs a frame.
        requestRender();
        return;
    }
    // Visibility / layer change = refill the visible-segment list (O(visible)); geometry & colours
    // untouched. This is the cheap path the layer slider rides on.
    auto visibleLists = buildVisibleSegmentLists(*db);
    const auto token = generation();
    auto self = shared_from_this();
    dispatchRenderCommand([self, token, visibleLists = std::move(visibleLists)](auto*, auto*) mutable {
        auto sync = std::static_pointer_cast<ToolpathPreviewDBSync>(self);
        if (sync->isGenerationActive(token) && sync->m_billboardMapper) {
            sync->m_billboardMapper->setVisibleSegments(
                std::move(visibleLists.forward),
                std::move(visibleLists.reverseLayers),
                std::move(visibleLists.finish));
        }
    });
    // The command queue is consumed by a coalesced render request. Layer/Step scrubbing must not rely
    // on an unrelated scene mutation to make its newly uploaded visible-segment list appear.
    requestRender();
}

std::vector<float> ToolpathPreviewDBSync::packVertexColors(ToolpathPreviewColorMode mode,
                                                                        const ToolpathPreviewDB& db) {
    const auto it = m_baseCellColorsByMode.find(mode);
    if (it == m_baseCellColorsByMode.end() || m_pathVertexCount == 0) {
        return {};
    }
    const auto& base = it->second;  // one RGB per segment
    const auto selected = ToolpathPreviewUtils::inspectedFiberRoute(db);
    m_colorSelection = selected;
    const auto& segments = db.getSegments();
    std::vector<float> packed(m_pathVertexCount, 0.0f);
    const std::size_t count = std::min(base.size(), m_segStartVertexId.size());
    for (std::size_t i = 0; i < count; ++i) {
        auto c = base[i];
        if (i < segments.size()) {
            if (db.getHighlightFiberTail() && ToolpathPreviewUtils::isFiberTail(segments[i]))
                c = {180, 80, 230, 255};
            else if (ToolpathPreviewUtils::isFiberFinish(segments[i]))
                c = {70, 155, 255, 255};
            if (selected && (ToolpathPreviewUtils::containsSegment(selected->tailRanges, i) ||
                             ToolpathPreviewUtils::containsSegment(selected->finishRanges, i)))
                c = {255, 210, 60, 255};
        }
        // Pack RGB as the float the shader's decode_color() expects: (r<<16)|(g<<8)|b.
        const float value = static_cast<float>((static_cast<int>(c[0]) << 16) |
                                               (static_cast<int>(c[1]) << 8) |
                                               static_cast<int>(c[2]));
        const std::uint32_t idA = m_segStartVertexId[i];
        // Both endpoints get the segment colour; a shared junction ends up with the outgoing
        // segment's colour (matches libvgcode's per-vertex colouring).
        if (idA < m_pathVertexCount) packed[idA] = value;
        if (idA + 1 < m_pathVertexCount) packed[idA + 1] = value;
    }
    return packed;
}

ToolpathPreviewDBSync::VisibleSegmentLists ToolpathPreviewDBSync::buildVisibleSegmentLists(const ToolpathPreviewDB& db) const {
    const auto& segments = db.getSegments();
    VisibleSegmentLists lists;
    const std::size_t count = std::min(
        {m_visibleAlpha.size(), m_segStartVertexId.size(), m_segmentLayerIds.size()});
    lists.forward.reserve(count);
    lists.reverseLayers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (m_visibleAlpha[i] != 0) {
            if (i < segments.size() && ToolpathPreviewUtils::isFiberFinish(segments[i]))
                lists.finish.push_back(m_segStartVertexId[i]);
            else
                lists.forward.push_back(m_segStartVertexId[i]);
        }
    }

    // Source segments are contiguous by layer. Visit layer groups from high to low while preserving
    // the source order inside each layer; this improves early depth rejection without changing the
    // winner at same-layer joins.
    std::size_t groupEnd = count;
    while (groupEnd > 0) {
        const std::uint32_t layerId = m_segmentLayerIds[groupEnd - 1];
        std::size_t groupBegin = groupEnd - 1;
        while (groupBegin > 0 && m_segmentLayerIds[groupBegin - 1] == layerId) {
            --groupBegin;
        }
        for (std::size_t i = groupBegin; i < groupEnd; ++i) {
            if (m_visibleAlpha[i] != 0 &&
                !(i < segments.size() && ToolpathPreviewUtils::isFiberFinish(segments[i]))) {
                lists.reverseLayers.push_back(m_segStartVertexId[i]);
            }
        }
        groupEnd = groupBegin;
    }
    return lists;
}

REGISTER_VTK_DBSYNC(TypeID::TOOLPATH_PREVIEW_DB, ToolpathPreviewDBSync,
                        GPlatform::Rendering::RendererRoleSets::Preview)
