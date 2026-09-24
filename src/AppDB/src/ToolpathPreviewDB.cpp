#include <QFileInfo>
#include <QDateTime>
#include "ToolpathPreviewDB.hpp"
#include "ToolpathPreviewUtils.hpp"

#include "Foundation/Log.h"
#include <TransactionManager.hpp>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>

namespace GPlatform {

struct ToolpathTemporaryArtifactLease {
    explicit ToolpathTemporaryArtifactLease(std::string ownedPath)
        : path(std::move(ownedPath))
    {
    }

    ~ToolpathTemporaryArtifactLease()
    {
        release();
    }

    void release()
    {
        if (released) {
            return;
        }
        released = true;
        namespace fs = std::filesystem;
        std::error_code error;
        if (fs::remove(fs::path(path), error)) {
            LOG_INFO("ToolpathPreviewDB: removed temporary print output: {}", path);
        } else if (error) {
            LOG_WARN("ToolpathPreviewDB: failed to remove temporary print output '{}': {}",
                     path, error.message());
        }
    }

    std::string path;
    bool released{false};
};

namespace {

template <typename T>
std::vector<T> normalizedIds(std::vector<T> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

template <typename T>
bool containsId(const std::vector<T>& values, T id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

template <typename T>
bool setHiddenIdVisible(std::vector<T>& hiddenIds, T id, bool visible) {
    hiddenIds = normalizedIds(std::move(hiddenIds));
    const auto it = std::lower_bound(hiddenIds.begin(), hiddenIds.end(), id);
    const bool hidden = it != hiddenIds.end() && *it == id;
    if (visible) {
        if (!hidden) {
            return false;
        }
        hiddenIds.erase(it);
        return true;
    }
    if (hidden) {
        return false;
    }
    hiddenIds.insert(it, id);
    return true;
}

template <typename T>
bool setHiddenIdsVisible(std::vector<T>& hiddenIds, const std::vector<T>& ids, bool visible) {
    bool changed = false;
    for (const auto id : normalizedIds(ids)) {
        changed = setHiddenIdVisible(hiddenIds, id, visible) || changed;
    }
    return changed;
}

class PreviewNotificationSuppressGuard {
public:
    explicit PreviewNotificationSuppressGuard(bool& target)
        : m_target(target),
          m_previous(target) {
        m_target = true;
    }

    ~PreviewNotificationSuppressGuard() {
        restore();
    }

    bool wasSuppressed() const {
        return m_previous;
    }

    void restore() {
        if (!m_active) {
            return;
        }
        m_target = m_previous;
        m_active = false;
    }

private:
    bool& m_target;
    bool m_previous = false;
    bool m_active = true;
};

} // namespace

ToolpathPreviewDB::ToolpathPreviewDB() {
    LOG_DEBUG("ToolpathPreviewDB created");
}

ToolpathPreviewDB::~ToolpathPreviewDB() {
    LOG_DEBUG("ToolpathPreviewDB destroyed");
}

void ToolpathPreviewDB::initializeSubActorProperties() {
    onCurrentStepChanged = [this](int) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("CurrentStep");
        }
    };
    onShowLayerRangeStartChanged = [this](int) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("ShowLayerRangeStart");
        }
    };
    onShowLayerRangeEndChanged = [this](int) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("ShowLayerRangeEnd");
        }
    };
    onColorModeChanged = [this](ToolpathPreviewColorMode) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("ColorMode");
        }
    };
    onShowTravelChanged = [this](bool) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("ShowTravel");
        }
    };
    onShowFiberProcessChanged = [this](bool) {
        if (!m_suppressPreviewNotifications) notifyGeometryChange("ShowFiberProcess");
    };
    onShowFiberDiagnosticsChanged = [this](bool) {
        if (!m_suppressPreviewNotifications) notifyGeometryChange("ShowFiberDiagnostics");
    };
    onHiddenFiberDiagnosticKindsChanged = [this](unsigned int) {
        if (!m_suppressPreviewNotifications) notifyGeometryChange("HiddenFiberDiagnosticKinds");
    };
    onShowSeamChanged = [this](bool) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("ShowSeam");
        }
    };
    onHiddenPreviewColorIdsChanged = [this](const std::vector<std::uint16_t>&) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("HiddenPreviewColorIds");
        }
    };
    onHiddenExtrusionRolesChanged = [this](const std::vector<std::uint8_t>&) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("HiddenExtrusionRoles");
        }
    };
    onHiddenToolIdsChanged = [this](const std::vector<std::uint16_t>&) {
        if (!m_suppressPreviewNotifications) {
            notifyGeometryChange("HiddenToolIds");
        }
    };

    PreviewNotificationSuppressGuard suppressNotifications(m_suppressPreviewNotifications);
    setCurrentLayer(0);
    setCurrentStep(-1);
    setShowLayerRangeStart(0);
    setShowLayerRangeEnd(0);
    setSourceModelName("");
    setPreviewArtifactPath("");
    setPrintGCodePath("");
    setPrintGCode3mfPath("");
    setSourceScope(ToolpathPreviewSourceScope::ImportedGCode);
    setSourceModelDBId(INVALID_DB_ID);
    setColorMode(ToolpathPreviewColorMode::PreviewColor);
    setShowTravel(false);
    setShowFiberProcess(false);
    setShowFiberDiagnostics(false);
    setHiddenFiberDiagnosticKinds(0);
    setShowSeam(true);
    setHiddenPreviewColorIds({});
    setHiddenExtrusionRoles({});
    setHiddenToolIds({});

    setVisible(true);
    setPickable(false);
    setDragable(false);
}

void ToolpathPreviewDB::setPreviewData(ToolpathPreviewData&& data) {
    PreviewNotificationSuppressGuard suppressNotifications(m_suppressPreviewNotifications);

    // Replacing preview data ends this DB object's ownership of the previous
    // generated artifact immediately. Transaction snapshots may retain a
    // shared lease, so merely resetting the pointer would defer deletion.
    releasePrintOutputArtifacts();
    if (data.printGCodeOwnership == ToolpathArtifactOwnership::GeneratedTemporary &&
        !data.printGCodePath.empty()) {
        m_printGCodeLease = std::make_shared<ToolpathTemporaryArtifactLease>(data.printGCodePath);
    }
    if (data.printGCode3mfOwnership == ToolpathArtifactOwnership::GeneratedTemporary &&
        !data.printGCode3mfPath.empty()) {
        m_printGCode3mfLease = std::make_shared<ToolpathTemporaryArtifactLease>(data.printGCode3mfPath);
    }

    m_layers = std::move(data.layers);
    m_segments = std::move(data.segments);
    m_options = std::move(data.options);
    m_tools = std::move(data.tools);
    m_filaments = std::move(data.filaments);
    m_colors = std::move(data.colors);
    m_supportedViewTypes = std::move(data.supportedViewTypes);
    m_stats = data.stats;
    m_bounds = data.bounds;
    m_metadataJson = std::move(data.metadataJson);
    m_fiberFillDiagnostics = std::move(data.fiberFillDiagnostics);
    setShowFiberDiagnostics(false);
    setHiddenFiberDiagnosticKinds(0);
    ++m_previewDataGeneration;

    m_gcodeSource.reset();
    const QFileInfo sourceInfo(QString::fromStdString(data.printGCodePath));
    m_sourceByteSize = sourceInfo.exists() ? sourceInfo.size() : -1;
    m_sourceModified = sourceInfo.lastModified().toMSecsSinceEpoch();
    publishInspectionGeometry();
    setPreviewArtifactPath(data.artifactPath);
    setPrintGCodePath(data.printGCodePath);
    setPrintGCode3mfPath(data.printGCode3mfPath);
    // CurrentLayer names the upper visible layer for UI metadata. CurrentStep is
    // independent and caps every layer in the selected range by the same count.
    setCurrentLayer(std::max(0, getTotalLayers() - 1));
    setCurrentStep(-1);
    setShowLayerRangeStart(0);
    setShowLayerRangeEnd(std::max(0, getTotalLayers() - 1));
    if (!getHiddenPreviewColorIds().empty()) {
        setHiddenPreviewColorIds({});
    }
    if (!getHiddenExtrusionRoles().empty()) {
        setHiddenExtrusionRoles({});
    }
    if (!getHiddenToolIds().empty()) {
        setHiddenToolIds({});
    }
    if (getShowTravel()) {
        setShowTravel(false);
    }

    suppressNotifications.restore();
    if (!suppressNotifications.wasSuppressed()) {
        notifyGeometryChange("PreviewData");
    }
    LOG_INFO("ToolpathPreviewDB::setPreviewData registered preview data id={} artifact={} layers={} logicalMotions={} renderSegments={} tools={} filaments={} colors={}",
             getDBInstanceID().getValue(),
             getPreviewArtifactPath(),
             m_stats.totalLayers,
             m_stats.logicalMotionCount,
             m_stats.renderSegmentCount,
             m_tools.size(),
             m_filaments.size(),
             m_colors.size());
}

void ToolpathPreviewDB::clearPreviewData() {
    PreviewNotificationSuppressGuard suppressNotifications(m_suppressPreviewNotifications);

    // Clearing a preview is an ownership boundary, not just a data reset.
    releasePrintOutputArtifacts();
    publishSnapGeometry({});
    m_inspectionElements.clear();
    m_gcodeSource.reset();
    m_sourceByteSize = -1;
    m_layers.clear();
    m_segments.clear();
    m_options.clear();
    m_tools.clear();
    m_filaments.clear();
    m_colors.clear();
    m_supportedViewTypes.clear();
    m_stats = {};
    m_bounds = {};
    m_metadataJson.clear();
    m_fiberFillDiagnostics.clear();
    setShowFiberDiagnostics(false);
    setHiddenFiberDiagnosticKinds(0);
    ++m_previewDataGeneration;
    setPreviewArtifactPath("");
    setPrintGCodePath("");
    setPrintGCode3mfPath("");
    setCurrentLayer(0);
    setCurrentStep(-1);
    setShowLayerRangeStart(0);
    setShowLayerRangeEnd(0);
    if (!getHiddenPreviewColorIds().empty()) {
        setHiddenPreviewColorIds({});
    }
    if (!getHiddenExtrusionRoles().empty()) {
        setHiddenExtrusionRoles({});
    }
    if (!getHiddenToolIds().empty()) {
        setHiddenToolIds({});
    }
    if (getShowTravel()) {
        setShowTravel(false);
    }
    suppressNotifications.restore();
    if (!suppressNotifications.wasSuppressed()) {
        notifyGeometryChange("PreviewData");
    }
}

void ToolpathPreviewDB::releasePrintOutputArtifacts() {
    if (m_printGCodeLease) {
        m_printGCodeLease->release();
    }
    m_printGCodeLease.reset();
    if (m_printGCode3mfLease) {
        m_printGCode3mfLease->release();
    }
    m_printGCode3mfLease.reset();
}

int ToolpathPreviewDB::getTotalSteps() const {
    if (m_layers.empty()) {
        return 0;
    }
    const int maxLayer = static_cast<int>(m_layers.size()) - 1;
    const int start = std::clamp(getShowLayerRangeStart(), 0, maxLayer);
    const int end = std::clamp(std::max(start, getShowLayerRangeEnd()), start, maxLayer);
    std::uint32_t maximum = 0;
    for (int layer = start; layer <= end; ++layer) {
        maximum = std::max(maximum, m_layers[static_cast<std::size_t>(layer)].segmentCount);
    }
    return static_cast<int>(std::min<std::uint32_t>(
        maximum, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

bool ToolpathPreviewDB::isPreviewColorIdVisible(std::uint16_t colorId) const {
    return !containsId(getHiddenPreviewColorIds(), colorId);
}

bool ToolpathPreviewDB::isExtrusionRoleVisible(std::uint8_t role) const {
    return !containsId(getHiddenExtrusionRoles(), role);
}

bool ToolpathPreviewDB::isToolIdVisible(std::uint16_t toolId) const {
    return !containsId(getHiddenToolIds(), toolId);
}

bool ToolpathPreviewDB::setPreviewColorIdVisible(std::uint16_t colorId, bool visible) {
    auto hiddenIds = getHiddenPreviewColorIds();
    if (!setHiddenIdVisible(hiddenIds, colorId, visible)) {
        return false;
    }
    setHiddenPreviewColorIds(hiddenIds);
    return true;
}

bool ToolpathPreviewDB::setExtrusionRoleVisible(std::uint8_t role, bool visible) {
    auto hiddenRoles = getHiddenExtrusionRoles();
    if (!setHiddenIdVisible(hiddenRoles, role, visible)) {
        return false;
    }
    setHiddenExtrusionRoles(hiddenRoles);
    return true;
}

bool ToolpathPreviewDB::setToolIdVisible(std::uint16_t toolId, bool visible) {
    auto hiddenIds = getHiddenToolIds();
    if (!setHiddenIdVisible(hiddenIds, toolId, visible)) {
        return false;
    }
    setHiddenToolIds(hiddenIds);
    return true;
}

bool ToolpathPreviewDB::setPreviewColorIdsVisible(const std::vector<std::uint16_t>& colorIds, bool visible) {
    auto hiddenIds = getHiddenPreviewColorIds();
    if (!setHiddenIdsVisible(hiddenIds, colorIds, visible)) {
        return false;
    }
    setHiddenPreviewColorIds(hiddenIds);
    return true;
}

bool ToolpathPreviewDB::setExtrusionRolesVisible(const std::vector<std::uint8_t>& roles, bool visible) {
    auto hiddenRoles = getHiddenExtrusionRoles();
    if (!setHiddenIdsVisible(hiddenRoles, roles, visible)) {
        return false;
    }
    setHiddenExtrusionRoles(hiddenRoles);
    return true;
}

bool ToolpathPreviewDB::setToolIdsVisible(const std::vector<std::uint16_t>& toolIds, bool visible) {
    auto hiddenIds = getHiddenToolIds();
    if (!setHiddenIdsVisible(hiddenIds, toolIds, visible)) {
        return false;
    }
    setHiddenToolIds(hiddenIds);
    return true;
}

bool ToolpathPreviewDB::resetLegendVisibility() {
    bool changed = false;
    if (!getHiddenPreviewColorIds().empty()) {
        setHiddenPreviewColorIds({});
        changed = true;
    }
    if (!getHiddenExtrusionRoles().empty()) {
        setHiddenExtrusionRoles({});
        changed = true;
    }
    if (!getHiddenToolIds().empty()) {
        setHiddenToolIds({});
        changed = true;
    }
    if (!getShowTravel()) {
        setShowTravel(true);
        changed = true;
    }
    if (!getShowSeam()) {
        setShowSeam(true);
        changed = true;
    }
    return changed;
}

bool ToolpathPreviewDB::hasHiddenLegendItems() const {
    return !getHiddenPreviewColorIds().empty() ||
           !getHiddenExtrusionRoles().empty() ||
           !getHiddenToolIds().empty() ||
           !getShowTravel() ||
           !getShowSeam();
}

ActorDB::BoundingBox ToolpathPreviewDB::localBounds() const {
    BoundingBox box;
    if (!m_bounds.valid) {
        return box;
    }
    box.min = m_bounds.min;
    box.max = m_bounds.max;
    box.valid = true;
    return box;
}

std::shared_ptr<AutoRegisterDB> ToolpathPreviewDB::clone() const {
    auto cloned = trans::TransDB::create<ToolpathPreviewDB>();
    if (!cloned) {
        return nullptr;
    }

    PreviewNotificationSuppressGuard suppressNotifications(cloned->m_suppressPreviewNotifications);

    ToolpathPreviewData data;
    data.artifactPath = getPreviewArtifactPath();
    data.printGCodePath = getPrintGCodePath();
    data.printGCodeOwnership = ToolpathArtifactOwnership::External;
    data.printGCode3mfPath = getPrintGCode3mfPath();
    data.printGCode3mfOwnership = ToolpathArtifactOwnership::External;
    data.metadataJson = m_metadataJson;
    data.fiberFillDiagnostics = m_fiberFillDiagnostics;
    data.layers = m_layers;
    data.segments = m_segments;
    data.options = m_options;
    data.tools = m_tools;
    data.filaments = m_filaments;
    data.colors = m_colors;
    data.supportedViewTypes = m_supportedViewTypes;
    data.stats = m_stats;
    data.bounds = m_bounds;
    cloned->setPreviewData(std::move(data));
    cloned->m_gcodeSource = m_gcodeSource;
    cloned->m_sourceByteSize = m_sourceByteSize;
    cloned->m_sourceModified = m_sourceModified;
    cloned->m_printGCodeLease = m_printGCodeLease;
    cloned->m_printGCode3mfLease = m_printGCode3mfLease;
    cloned->copyTransformFrom(*this);
    cloned->setVisible(isVisible());
    cloned->setSourceModelName(getSourceModelName());
    cloned->setSourceScope(getSourceScope());
    cloned->setSourceModelDBId(getSourceModelDBId());
    cloned->setColorMode(getColorMode());
    cloned->setShowTravel(getShowTravel());
    cloned->setShowFiberProcess(getShowFiberProcess());
    cloned->setShowFiberDiagnostics(getShowFiberDiagnostics());
    cloned->setHiddenFiberDiagnosticKinds(getHiddenFiberDiagnosticKinds());
    cloned->setShowSeam(getShowSeam());
    cloned->setHiddenPreviewColorIds(getHiddenPreviewColorIds());
    cloned->setHiddenExtrusionRoles(getHiddenExtrusionRoles());
    cloned->setHiddenToolIds(getHiddenToolIds());
    return cloned;
}


void ToolpathPreviewDB::publishInspectionGeometry() {
    SnapGeometry geometry;
    m_inspectionElements.clear();
    geometry.features.reserve(m_segments.size() + m_options.size());
    const auto add = [&](SnapFeature feature, InspectionElement element) {
        feature.id = geometry.features.size() + 1;
        geometry.features.push_back(feature);
        m_inspectionElements.push_back(element);
    };
    // One command is one logical path, even when the parser inserts speed
    // transitions or arc subdivisions. Keep members individually filterable.
    const auto connected = [](Vector3 a, Vector3 b) {
        return std::abs(a.x-b.x) < 1e-5f && std::abs(a.y-b.y) < 1e-5f && std::abs(a.z-b.z) < 1e-5f;
    };
    for (std::size_t begin = 0; begin < m_segments.size();) {
        std::size_t end = begin + 1;
        const auto& first = m_segments[begin];
        while (end < m_segments.size() && first.sourceCommandId != 0 &&
               m_segments[end].sourceCommandId == first.sourceCommandId &&
               m_segments[end].layerId == first.layerId &&
               connected(m_segments[end-1].endMm, m_segments[end].startMm))
            ++end;
        const auto groupBegin = std::uint32_t(geometry.features.size());
        const auto groupCount = std::uint32_t(end - begin + 2);
        for (auto i = begin; i < end; ++i) {
            const auto& segment = m_segments[i];
            SnapFeature feature{0, SnapFeatureKind::Segment, segment.startMm, segment.endMm};
            feature.radiusMm = std::max(0.0f, segment.widthMm)*.5f;
            feature.groupBegin = groupBegin;
            feature.groupCount = groupCount;
            add(feature, {InspectionElement::Kind::Segment, i, 0, segment.sourceCommandId});
        }
        for (bool last : {false, true}) {
            const auto index = last ? end - 1 : begin;
            SnapFeature endpoint{0, SnapFeatureKind::Point,
                                 last ? m_segments[index].endMm : first.startMm};
            endpoint.groupBegin = groupBegin;
            endpoint.groupCount = groupCount;
            add(endpoint, {InspectionElement::Kind::Endpoint, index, last ? 1u : 0u, first.sourceCommandId});
        }
        begin = end;
    }
    for (std::size_t i=0;i<m_options.size();++i) {
        const auto& event=m_options[i];
        SnapFeature f{0,SnapFeatureKind::Point,event.positionMm}; f.radiusMm=.5f;
        add(f,{InspectionElement::Kind::Event,i,0,event.sourceCommandId});
    }
    for (std::size_t i=0;i<m_fiberFillDiagnostics.size();++i) {
        const auto& path=m_fiberFillDiagnostics[i];
        const auto begin = std::uint32_t(geometry.features.size());
        std::size_t count = path.points.empty() ? 0 : path.points.size()-1;
        for (const auto& loop : path.boundaries) if (!loop.empty()) count += loop.size()-1;
        std::size_t edge = 0;
        const auto add_edges = [&](const std::vector<Vector3>& points) {
            for (std::size_t j=1;j<points.size();++j) {
                SnapFeature feature{0,SnapFeatureKind::Segment,points[j-1],points[j]};
                feature.groupBegin = begin;
                feature.groupCount = std::uint32_t(count);
                add(feature, {InspectionElement::Kind::Diagnostic,i,edge++,0});
            }
        };
        add_edges(path.points);
        for (const auto& loop : path.boundaries) add_edges(loop);
    }
    if (!publishSnapGeometry(std::move(geometry))) {
        m_inspectionElements.clear();
        publishSnapGeometry({});
    }
}

void ToolpathPreviewDB::setGCodeSource(std::uint64_t generation,
                                     std::shared_ptr<const GCodeSource> source) {
    if (generation==m_previewDataGeneration) m_gcodeSource=std::move(source);
}

std::uint32_t ToolpathPreviewDB::sourceLineForPosition(int layerIndex, int step, bool layerStart) const {
    if (layerIndex < 0 || layerIndex >= int(m_layers.size())) return 0;
    const auto& layer = m_layers[std::size_t(layerIndex)];
    const auto begin = std::size_t(layer.segmentBegin);
    const auto end = std::min(m_segments.size(), begin + std::size_t(layer.segmentCount));
    if (begin >= end) return 0;
    if (layerStart) {
        for (auto i = begin; i < end; ++i)
            if (m_segments[i].sourceCommandId) return m_segments[i].sourceCommandId;
        return 0;
    }
    if (step >= 0) return m_segments[begin + std::min(std::size_t(step), end - begin - 1)].sourceCommandId;
    auto index = end - 1;
    for (;;) {
        if (m_segments[index].sourceCommandId) return m_segments[index].sourceCommandId;
        if (index == begin) return 0;
        --index;
    }
}

ToolpathPreviewDB::InspectionVisibility ToolpathPreviewDB::inspectionVisibility() const {
    InspectionVisibility result;
    result.generation = m_previewDataGeneration;
    result.visible = isVisible() && !m_layers.empty();
    if (!result.visible) return result;
    result.firstLayer = std::clamp(getShowLayerRangeStart(),0,int(m_layers.size())-1);
    result.lastLayer = std::clamp(std::max(result.firstLayer,getShowLayerRangeEnd()),result.firstLayer,int(m_layers.size())-1);
    result.step = getCurrentStep();
    result.travel = getShowTravel();
    result.fiber = getShowFiberProcess();
    result.diagnostics = getShowFiberDiagnostics();
    result.hiddenDiagnosticKinds = getHiddenFiberDiagnosticKinds();
    result.seam = getShowSeam() && getColorMode()==ToolpathPreviewColorMode::FeatureType;
    result.hiddenColors = getHiddenPreviewColorIds();
    result.hiddenTools = getHiddenToolIds();
    result.hiddenRoles = getHiddenExtrusionRoles();
    return result;
}

bool ToolpathPreviewDB::inspectionElementVisible(std::size_t featureIndex) const {
    return inspectionElementVisible(featureIndex,inspectionVisibility());
}

bool ToolpathPreviewDB::inspectionElementVisible(std::size_t featureIndex,
                                                const InspectionVisibility& visibility) const {
    if (!visibility.visible || visibility.generation != m_previewDataGeneration || featureIndex>=m_inspectionElements.size()) return false;
    const auto& item=m_inspectionElements[featureIndex];
    const auto layerId=(item.kind==InspectionElement::Kind::Segment || item.kind==InspectionElement::Kind::Endpoint) ?m_segments[item.index].layerId
        :item.kind==InspectionElement::Kind::Event ?m_options[item.index].layerId :m_fiberFillDiagnostics[item.index].layerId;
    const auto layer=std::lower_bound(m_layers.begin(),m_layers.end(),layerId,
        [](const auto& value,auto id){return value.id<id;});
    if (layer==m_layers.end() || layer->id!=layerId) return false;
    const int layerIndex=int(layer-m_layers.begin());
    if (layerIndex<visibility.firstLayer || layerIndex>visibility.lastLayer) return false;
    if (item.kind==InspectionElement::Kind::Diagnostic)
        return visibility.diagnostics && !(visibility.hiddenDiagnosticKinds &
            (1u << unsigned(m_fiberFillDiagnostics[item.index].kind)));
    const std::uint64_t visibleEnd=std::uint64_t(layer->segmentBegin)+
        (visibility.step<0 ? layer->segmentCount : std::min<std::uint64_t>(std::uint64_t(visibility.step)+1,layer->segmentCount));
    if (item.kind==InspectionElement::Kind::Event) {
        const auto& event=m_options[item.index];
        return event.kind==ToolpathEventKind::Seam && visibility.seam && event.afterSegment<=visibleEnd;
    }
    if (item.index<layer->segmentBegin || item.index>=visibleEnd) return false;
    const auto& segment=m_segments[item.index];
    const bool travel=ToolpathPreviewUtils::isTravelSegment(segment);
    const bool finish=segment.fiberPhase==ToolpathFiberPhase::Finish;
    const auto hidden=[](const auto& list,auto id){return std::find(list.begin(),list.end(),id)!=list.end();};
    return !(finish && !visibility.fiber) && !(travel && !finish && !visibility.travel) &&
        (travel || !hidden(visibility.hiddenRoles,segment.extrusionRole)) &&
        (segment.cpColorId==0xffffu || !hidden(visibility.hiddenColors,segment.cpColorId)) &&
        (segment.toolId==0xffffu || !hidden(visibility.hiddenTools,segment.toolId));
}

} // namespace GPlatform
