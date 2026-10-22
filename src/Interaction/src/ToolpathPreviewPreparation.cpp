#include "ToolpathPreviewPreparation.hpp"
#include "LibSlicerToolpathAdapter.hpp"
#include <QFileInfo>
#include <QDateTime>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <limits>

namespace GPlatform {
namespace {
constexpr auto noIndex = std::numeric_limits<std::size_t>::max();
const auto contour = static_cast<std::uint8_t>(libslicer::ToolpathExtrusionRole::ContinuousFiberContour);
const auto infill = static_cast<std::uint8_t>(libslicer::ToolpathExtrusionRole::ContinuousFiberInfill);

void collectMarker(std::vector<FiberSourceMarker>& markers, std::uint32_t line, QByteArrayView bytes) {
    const auto text = bytes.trimmed();
    if (!text.startsWith(';')) return;
    const auto space = text.indexOf(' ');
    const auto tab = text.indexOf('\t');
    auto end = text.size();
    if (space >= 0) end = std::min(end, space);
    if (tab >= 0) end = std::min(end, tab);
    const auto name = text.first(end);
    using Kind = FiberSourceMarkerKind;
    if (text == ";TYPE:Continuous fiber contour") markers.push_back({line, Kind::Role, contour});
    else if (text == ";TYPE:Continuous fiber infill") markers.push_back({line, Kind::Role, infill});
    else if (text.startsWith(";TYPE:")) markers.push_back({line, Kind::Role, 0});
    else if (name == ";FIBER_BEGIN") {
        std::uint8_t role = 0;
        bool version = false;
        std::istringstream fields(std::string(text.data(), text.size()));
        for (std::string token; fields >> token;) {
            if (token == "v=3") version = true;
            if (token == "purpose=contour") role = contour;
            if (token == "purpose=infill") role = infill;
        }
        markers.push_back({line, Kind::Begin, role, version});
    } else {
        static const std::pair<const char*, Kind> names[] = {
            {";FIBER_CUT", Kind::Cut}, {";FIBER_TAIL_BEGIN", Kind::TailBegin},
            {";FIBER_DEPLETED", Kind::Depleted}, {";FIBER_FINISH_BEGIN", Kind::FinishBegin},
            {";FIBER_FINISH", Kind::Finish}, {";FIBER_END", Kind::End}};
        for (const auto& item : names)
            if (name == item.first) { markers.push_back({line, item.second, 0}); break; }
    }
}

void buildLocalFiberAnnotations(PreparedToolpathPreview& prepared,
    const std::vector<FiberSourceMarker>& markers, const PreviewCancelCheck& cancelled) {
    auto& data = prepared.data;
    if (markers.empty() || std::none_of(data.segments.begin(), data.segments.end(), [](const auto& segment) {
        return segment.fiberPhase != ToolpathFiberPhase::None;
    })) return;
    std::vector<std::size_t> order(data.segments.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        const auto x = data.segments[a].sourceCommandId, y = data.segments[b].sourceCommandId;
        return x == y ? a < b : x < y;
    });
    if (cancelled && cancelled()) { prepared.cancelled = true; return; }
    struct Block {
        std::shared_ptr<ToolpathFiberRoute> route = std::make_shared<ToolpathFiberRoute>();
        std::vector<ToolpathPreviewOption> events;
        bool valid = false, invalid = false;
        std::uint32_t finishBegin = 0, finishEnd = 0;
    };
    std::vector<Block> blocks;
    // Temporary ownership refers to original vector indices, never segment.id.
    std::vector<std::size_t> owners(data.segments.size(), noIndex);
    std::vector<std::uint8_t> phases(data.segments.size(), 0), roles(data.segments.size(), 0);
    std::size_t current = noIndex, anchor = noIndex, cursor = 0;
    std::uint8_t role = 0, phase = 0;
    const auto reject = [&](std::size_t block, const QString& reason) {
        if (block == noIndex) { prepared.warnings << reason; return; }
        if (!blocks[block].invalid) prepared.warnings << QStringLiteral("Fiber block at line %1: %2; annotations skipped")
            .arg(blocks[block].route->beginSourceLine).arg(reason);
        blocks[block].invalid = true;
    };
    using Kind = FiberSourceMarkerKind;
    for (const auto& marker : markers) {
        if (cancelled && cancelled()) { prepared.cancelled = true; return; }
        if (!marker.line || marker.line > data.gcodeSource->lineStarts.size()) {
            reject(current, QStringLiteral("invalid source line"));
            continue;
        }
        while (cursor < order.size() && data.segments[order[cursor]].sourceCommandId < marker.line) {
            const auto index = order[cursor++];
            auto& segment = data.segments[index];
            if ((cursor % 4096) == 0 && cancelled && cancelled()) { prepared.cancelled = true; return; }
            if (current == noIndex || !segment.sourceCommandId) continue;
            anchor = index;
            owners[index] = current;
            roles[index] = role;
            if ((phase == 1 && segment.fiberPhase == ToolpathFiberPhase::Tail &&
                    segment.deposition == ToolpathDepositionKind::ContinuousFiberPassive) ||
                (phase == 2 && segment.fiberPhase == ToolpathFiberPhase::Finish &&
                    segment.deposition == ToolpathDepositionKind::None)) {
                phases[index] = phase;
            }
        }
        if (marker.kind == Kind::Begin) {
            if (current != noIndex) reject(current, QStringLiteral("unterminated block"));
            current = blocks.size(); blocks.emplace_back();
            blocks.back().route->beginSourceLine = marker.line;
            anchor = noIndex; role = marker.displayRole; phase = 0;
            if (!marker.supportedVersion || !role) reject(current, QStringLiteral("unsupported version or purpose"));
            continue;
        }
        if (current == noIndex) continue;
        auto& block = blocks[current];
        auto& route = *block.route;
        switch (marker.kind) {
        case Kind::Role: role = marker.displayRole; break;
        case Kind::Cut: case Kind::Depleted: {
            if (marker.kind == Kind::Cut) route.cutSourceLine = marker.line;
            else { route.depletedSourceLine = marker.line; phase = 0; }
            if (anchor != noIndex) {
                const auto& segment = data.segments[anchor];
                ToolpathPreviewOption event;
                event.positionMm = segment.endMm;
                event.layerId = segment.layerId;
                event.afterSegment = anchor + 1;
                event.kind = marker.kind == Kind::Cut ? ToolpathEventKind::FiberCut : ToolpathEventKind::FiberDepleted;
                event.sourceCommandId = marker.line;
                event.toolId = segment.toolId; event.cpColorId = segment.cpColorId;
                event.fiberDisplayRole = role; event.fiberRoute = block.route;
                block.events.push_back(std::move(event));
            }
            break;
        }
        case Kind::TailBegin: route.tailBeginSourceLine = marker.line; phase = 1; break;
        case Kind::FinishBegin: block.finishBegin = marker.line; phase = 2; break;
        case Kind::Finish: block.finishEnd = marker.line; phase = 0; break;
        case Kind::End:
            route.endSourceLine = marker.line;
            if (route.cutSourceLine && route.depletedSourceLine &&
                !(route.beginSourceLine < route.cutSourceLine && route.cutSourceLine < route.tailBeginSourceLine &&
                  route.tailBeginSourceLine < route.depletedSourceLine && route.depletedSourceLine < route.endSourceLine &&
                  ((!block.finishBegin && !block.finishEnd) ||
                   (route.depletedSourceLine < block.finishBegin && block.finishBegin < block.finishEnd && block.finishEnd < route.endSourceLine)))) {
                reject(current, QStringLiteral("invalid source boundaries"));
            }
            block.valid = !block.invalid && block.events.size() == 2 && route.cutSourceLine && route.depletedSourceLine;
            if (!block.valid) reject(current, QStringLiteral("no trustworthy cut/depleted anchors"));
            current = noIndex; anchor = noIndex; phase = 0; break;
        case Kind::Begin: break;
        }
    }
    if (current != noIndex) reject(current, QStringLiteral("unterminated block"));
    const auto accepted = std::count_if(blocks.begin(), blocks.end(), [](const auto& block) { return block.valid; });
    prepared.annotationStatus = blocks.empty() ? ToolpathAnnotationStatus::NotApplicable :
        accepted == 0 ? ToolpathAnnotationStatus::Unavailable :
        accepted == std::ptrdiff_t(blocks.size()) ? ToolpathAnnotationStatus::Ready : ToolpathAnnotationStatus::Partial;
    data.options.reserve(data.options.size() + std::size_t(accepted) * 2);
    // Merge in original render order; layer boundaries always start a new range.
    for (std::size_t i = 0; i < owners.size(); ++i) {
        if ((i % 4096) == 0 && cancelled && cancelled()) { prepared.cancelled = true; return; }
        if (owners[i] == noIndex || !blocks[owners[i]].valid) continue;
        data.segments[i].fiberDisplayRole = roles[i];
        if (!phases[i]) continue;
        auto& route = *blocks[owners[i]].route;
        auto& ranges = phases[i] == 1 ? route.tailRanges : route.finishRanges;
        if (!ranges.empty() && ranges.back().end == i && data.segments[i-1].layerId == data.segments[i].layerId)
            ranges.back().end = i + 1;
        else ranges.push_back({i, i + 1});
    }
    for (auto& block : blocks)
        if (block.valid) for (auto& event : block.events) data.options.push_back(std::move(event));
}
}

CapturedToolpathSource captureToolpathSource(const QString& path, const PreviewCancelCheck& cancelled,
    const GCodeCaptureIO& io) {
    CapturedToolpathSource captured;
    const QFileInfo info(path);
    captured.source = GCodeSource::capture(path, info.exists() ? info.size() : -1,
        info.lastModified().toMSecsSinceEpoch(), [&](auto line, auto bytes) {
            collectMarker(captured.markers, line, bytes);
        }, cancelled, io);
    if (!captured.source->available()) captured.markers.clear();
    return captured;
}

PreparedToolpathPreview prepareToolpathPreview(const libslicer::ToolpathPreview& preview,
    CapturedToolpathSource captured, const PreviewCancelCheck& cancelled) {
    PreparedToolpathPreview result;
    if (cancelled && cancelled()) { result.cancelled = true; return result; }
    try {
        result.data = adaptLibSlicerToolpath(preview);
        result.geometryAvailable = true;
    } catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& error) {
        result.error = QString::fromUtf8(error.what());
        return result;
    }
    if (!captured.source || !captured.source->available()) {
        result.data.gcodeSource = captured.source;
        result.sourceError = captured.source ? captured.source->error : QStringLiteral("Missing G-code snapshot");
        result.annotationStatus = ToolpathAnnotationStatus::Unavailable;
        result.warnings << result.sourceError;
        result.cancelled = (cancelled && cancelled()) ||
            (captured.source && captured.source->failure == GCodeCaptureFailure::Cancelled);
    } else {
        result.data.gcodeSource = std::move(captured.source);
        const auto originalOptions = result.data.options.size();
        try { buildLocalFiberAnnotations(result, captured.markers, cancelled); }
        catch (const std::bad_alloc&) { throw; }
        catch (const std::exception& error) {
            result.data.options.resize(originalOptions);
            for (auto& segment : result.data.segments) segment.fiberDisplayRole = 0;
            result.annotationStatus = ToolpathAnnotationStatus::Unavailable;
            result.warnings << QStringLiteral("Fiber annotations unavailable: %1").arg(QString::fromUtf8(error.what()));
        }
    }
    if (cancelled && cancelled()) result.cancelled = true;
    if (result.cancelled) { result.data = {}; result.geometryAvailable = false; }
    return result;
}

PreparedToolpathPreview prepareImportedToolpath(const QString& path,
    const std::function<libslicer::GCodePreviewResult(const std::string&)>& load,
    const PreviewCancelCheck& cancelled, const GCodeCaptureIO& io) {
    PreparedToolpathPreview failed;
    const QFileInfo before(path);
    const auto beforeSize = before.size();
    const auto beforeModified = before.lastModified();
    auto captured = captureToolpathSource(path, cancelled, io);
    if ((cancelled && cancelled()) || captured.source->failure == GCodeCaptureFailure::Cancelled) {
        failed.cancelled = true; return failed;
    }
    const bool snapshot = captured.source->available();
    if (!snapshot && !captured.source->permitsOriginalFallback()) {
        failed.error = captured.source->error; return failed;
    }
    const auto unchanged = [&] {
        const QFileInfo now(path);
        return now.isFile() && now.size() == beforeSize && now.lastModified() == beforeModified;
    };
    if (!snapshot && !unchanged()) {
        failed.error = QStringLiteral("G-code changed while reading; reload the preview."); return failed;
    }
    const auto loaded = load(snapshot ? captured.source->file->fileName().toStdString() : path.toStdString());
    if (loaded.cancelled || (cancelled && cancelled())) { failed.cancelled = true; return failed; }
    if (!snapshot && !unchanged()) {
        failed.error = QStringLiteral("G-code changed while reading; reload the preview."); return failed;
    }
    if (!loaded.success || !loaded.preview) {
        for (const auto& diagnostic : loaded.diagnostics)
            if (!diagnostic.warning) failed.error += QString::fromStdString(diagnostic.message) + QStringLiteral("; ");
        if (failed.error.isEmpty()) failed.error = QStringLiteral("G-code contains no drawable toolpath");
        return failed;
    }
    auto result = prepareToolpathPreview(*loaded.preview, std::move(captured), cancelled);
    result.data.printGCodePath = path.toStdString();
    return result;
}
}
