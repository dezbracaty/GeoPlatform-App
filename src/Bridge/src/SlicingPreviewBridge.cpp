#include "SlicingPreviewBridge.hpp"
#include "BridgeRegistration.hpp"
#include "SlicingPreviewViewDescriptors.hpp"

#include "DocumentManager.hpp"
#include "ModelInstanceDB.hpp"
#include "ToolpathPreviewDB.hpp"
#include "ToolpathPreviewUtils.hpp"
#include "Foundation/Log.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

SlicingPreviewBridge::SlicingPreviewBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    m_gcodeLines = new GCodeLineModel(this);
    connect(this, &SlicingPreviewBridge::currentToolpathPreviewChanged, this, [this] {
        m_gcodeLines->setPreview(currentToolpathPreview());
        navigateGCode(true);
    });
    LOG_INFO("SlicingPreviewBridge created");
}

SlicingPreviewBridge::~SlicingPreviewBridge() = default;

SlicingPreviewBridge* SlicingPreviewBridge::instance() {
    static SlicingPreviewBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new SlicingPreviewBridge();
    }
    return s_instance;
}

SlicingPreviewBridge* SlicingPreviewBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

std::shared_ptr<GPlatform::ToolpathPreviewDB> SlicingPreviewBridge::getCurrentPreview() const {
    return m_activePreview;
}

QString SlicingPreviewBridge::resolveCurrentPrintOutputPath() const {
    if (auto preview = getCurrentPreview()) {
        const QString path = QString::fromStdString(preview->getPrintGCodePath());
        if (!path.isEmpty()) {
            return path;
        }
    }
    return m_printOutputPath;
}

QString SlicingPreviewBridge::resolveCurrentPrintOutputPath(const QString& formatId) const {
    if (formatId == QStringLiteral("gcode-3mf")) {
        if (auto preview = getCurrentPreview()) {
            return QString::fromStdString(preview->getPrintGCode3mfPath());
        }
        return {};
    }
    if (formatId == QStringLiteral("gcode")) {
        return resolveCurrentPrintOutputPath();
    }
    return {};
}

QString SlicingPreviewBridge::previewArtifactPath() const {
    if (auto preview = getCurrentPreview()) {
        const QString path = QString::fromStdString(preview->getPreviewArtifactPath());
        if (!path.isEmpty()) {
            return path;
        }
    }
    return m_previewArtifactPath;
}

QString SlicingPreviewBridge::printOutputPath() const {
    return resolveCurrentPrintOutputPath();
}

QString SlicingPreviewBridge::printPackagePath() const {
    return resolveCurrentPrintOutputPath(QStringLiteral("gcode-3mf"));
}

bool SlicingPreviewBridge::printOutputReady() const {
    return !printOutputPath().isEmpty() || !printPackagePath().isEmpty();
}

bool SlicingPreviewBridge::printPackageExportAvailable() const {
    return canExportPrintOutput(QStringLiteral("gcode-3mf"));
}

bool SlicingPreviewBridge::resliceAvailable() const {
    return canResliceCurrentModel();
}

bool SlicingPreviewBridge::isValidPreviewPanelViewType(const QString& viewType) const {
    return GPlatform::slicingPreviewViewDescriptorForId(viewType) != nullptr;
}

std::optional<GPlatform::ToolpathPreviewColorMode>
SlicingPreviewBridge::previewPanelColorModeForViewType(const QString& viewType) {
    return GPlatform::slicingPreviewColorModeForViewType(viewType);
}

static QString colorToHex(const Vector4& color) {
    const auto clampChannel = [](float value) {
        return std::clamp(static_cast<int>(std::round(value * 255.0f)), 0, 255);
    };
    return QColor(clampChannel(color.x), clampChannel(color.y), clampChannel(color.z)).name(QColor::HexRgb);
}

static QString compactDuration(double seconds) {
    const qlonglong totalSeconds = std::max<qlonglong>(0, std::llround(seconds));
    const qlonglong hours = totalSeconds / 3600;
    const qlonglong minutes = (totalSeconds % 3600) / 60;
    const qlonglong remainingSeconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QStringLiteral("%1m %2s").arg(minutes).arg(remainingSeconds);
    }
    return QStringLiteral("%1s").arg(remainingSeconds);
}

static QString percentOf(double value, double total) {
    return QStringLiteral("%1%").arg(total > 0.0 ? 100.0 * value / total : 0.0, 0, 'f', 1);
}

static QString filamentUsageText(double volumeMm3,
                                 const GPlatform::ToolpathPreviewFilament& filament) {
    constexpr double pi = 3.14159265358979323846;
    const double areaMm2 = pi * filament.diameterMm * filament.diameterMm / 4.0;
    const double lengthM = areaMm2 > 0.0 ? volumeMm3 / areaMm2 / 1000.0 : 0.0;
    const double weightG = volumeMm3 * filament.density / 1000.0;
    return QStringLiteral("%1 m\n%2 g").arg(lengthM, 0, 'f', 2).arg(weightG, 0, 'f', 2);
}

static QVariantMap summaryRow(const QString& label, const QString& value) {
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return row;
}

struct RangeDescription {
    QString title;
    float minimum = 0.0f;
    float maximum = 0.0f;
    int decimals = 0;
    bool logarithmic = false;
    bool showTravelOption = false;
};

static std::optional<RangeDescription> rangeDescription(
    const QString& viewType,
    const GPlatform::ToolpathPreviewStats& stats) {
    if (viewType == QStringLiteral("speed"))
        return RangeDescription{QStringLiteral("Speed (mm/s)"), stats.minSpeedMmS, stats.maxSpeedMmS, 0, false, true};
    if (viewType == QStringLiteral("actualSpeed"))
        return RangeDescription{QStringLiteral("Actual Speed (mm/s)"), stats.minActualSpeedMmS, stats.maxActualSpeedMmS, 0, false, true};
    if (viewType == QStringLiteral("acceleration"))
        return RangeDescription{QStringLiteral("Acceleration (mm/s²)"), stats.minAccelerationMmS2, stats.maxAccelerationMmS2, 0, false, true};
    if (viewType == QStringLiteral("jerk"))
        return RangeDescription{QStringLiteral("Jerk (mm/s)"), stats.minJerkMmS, stats.maxJerkMmS, 1, false, true};
    if (viewType == QStringLiteral("layerHeight"))
        return RangeDescription{QStringLiteral("Layer height (mm)"), stats.minLayerHeightMm, stats.maxLayerHeightMm, 2, false, false};
    if (viewType == QStringLiteral("lineWidth"))
        return RangeDescription{QStringLiteral("Line width (mm)"), stats.minWidthMm, stats.maxWidthMm, 2, false, false};
    if (viewType == QStringLiteral("volumetricFlow"))
        return RangeDescription{QStringLiteral("Volumetric flow rate (mm³/s)"), stats.minVolumetricFlow, stats.maxVolumetricFlow, 2, false, false};
    if (viewType == QStringLiteral("actualVolumetricFlow"))
        return RangeDescription{QStringLiteral("Actual volumetric flow rate (mm³/s)"), stats.minActualVolumetricFlow, stats.maxActualVolumetricFlow, 2, false, false};
    if (viewType == QStringLiteral("layerTime"))
        return RangeDescription{QStringLiteral("Layer Time"), stats.minLayerTimeS, stats.maxLayerTimeS, 1, false, false};
    if (viewType == QStringLiteral("layerTimeLog"))
        return RangeDescription{QStringLiteral("Layer Time (log)"), stats.minLayerTimeS, stats.maxLayerTimeS, 1, true, false};
    if (viewType == QStringLiteral("fanSpeed"))
        return RangeDescription{QStringLiteral("Fan speed (%)"), stats.minFanSpeedPercent, stats.maxFanSpeedPercent, 0, false, false};
    if (viewType == QStringLiteral("temperature"))
        return RangeDescription{QStringLiteral("Temperature (°C)"), stats.minTemperatureC, stats.maxTemperatureC, 0, false, false};
    if (viewType == QStringLiteral("pressureAdvance"))
        return RangeDescription{QStringLiteral("Pressure Advance"), stats.minPressureAdvance, stats.maxPressureAdvance, 3, false, false};
    return std::nullopt;
}

static QVariantMap legendItemToVariant(const GPlatform::ToolpathPreviewLegendItem& item) {
    QVariantMap map;
    map.insert(QStringLiteral("id"), QString::fromStdString(item.id));
    map.insert(QStringLiteral("label"), QString::fromStdString(item.label));
    map.insert(QStringLiteral("color"), colorToHex(item.colorRgba));
    map.insert(QStringLiteral("group"), QString::fromUtf8(GPlatform::ToolpathPreviewUtils::legendGroupName(item.group)));
    map.insert(QStringLiteral("rawId"), static_cast<int>(item.rawId));
    map.insert(QStringLiteral("count"), item.segmentCount);
    map.insert(QStringLiteral("visible"), item.visible);
    return map;
}

static QString formatPreviewRange(float value, const QString& unit) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return QStringLiteral("0 %1").arg(unit);
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value >= 100.0f ? 0 : 1).arg(unit);
}

static std::vector<GPlatform::ToolpathPreviewLegendItem> buildLegendItemsForViewType(
    const GPlatform::ToolpathPreviewDB& preview,
    const QString& viewType) {
    std::vector<GPlatform::ToolpathPreviewLegendItem> items;
    if (viewType == QStringLiteral("filament")) {
        return GPlatform::ToolpathPreviewUtils::buildLegendItems(
            preview,
            GPlatform::ToolpathPreviewLegendGroupKind::Filament);
    }
    if (viewType == QStringLiteral("lineType")) {
        items = GPlatform::ToolpathPreviewUtils::buildLegendItems(
            preview,
            GPlatform::ToolpathPreviewLegendGroupKind::FeatureType);
        auto moveItems = GPlatform::ToolpathPreviewUtils::buildLegendItems(
            preview,
            GPlatform::ToolpathPreviewLegendGroupKind::MoveType);
        items.insert(items.end(), moveItems.begin(), moveItems.end());
        // Z-seam markers (OrcaSlicer "options"): a non-drawable move, so it is not a role — add it as
        // its own legend row (colour #E6E6E6) when the model has any.
        std::size_t seamCount = 0;
        for (const auto& opt : preview.getOptions()) {
            if (opt.kind == GPlatform::ToolpathEventKind::Seam) {
                ++seamCount;
            }
        }
        if (seamCount > 0) {
            GPlatform::ToolpathPreviewLegendItem seam;
            seam.id = "move.seam";
            seam.label = "Seam";
            seam.colorRgba = Vector4(GPlatform::kSeamMarkerGrey, GPlatform::kSeamMarkerGrey,
                                     GPlatform::kSeamMarkerGrey, 1.0f);
            seam.group = GPlatform::ToolpathPreviewLegendGroupKind::MoveType;
            seam.rawId = static_cast<std::uint32_t>(GPlatform::ToolpathEventKind::Seam);
            seam.segmentCount = static_cast<int>(seamCount);
            seam.visible = preview.getShowSeam();
            items.push_back(seam);
        }
        return items;
    }
    if (viewType == QStringLiteral("tool")) {
        return GPlatform::ToolpathPreviewUtils::buildLegendItems(
            preview,
            GPlatform::ToolpathPreviewLegendGroupKind::Tool);
    }
    return items;
}

bool SlicingPreviewBridge::applyPreviewPanelRenderStateForViewType(
    const QString& viewType,
    bool restoreFeatureVisibility,
    bool logWhenUnavailable) {
    const auto colorMode = previewPanelColorModeForViewType(viewType);
    if (!colorMode) {
        if (logWhenUnavailable) {
            LOG_ERROR("SlicingPreviewBridge: viewType={} has no render color mapping",
                      viewType.toStdString());
        }
        return false;
    }
    auto preview = getCurrentPreview();
    if (!preview) {
        if (logWhenUnavailable) {
            LOG_DEBUG("SlicingPreviewBridge: no current preview while selecting viewType={}",
                      viewType.toStdString());
        }
        return false;
    }

    bool colorChanged = false;
    if (preview->getColorMode() != *colorMode) {
        preview->setColorMode(*colorMode);
        colorChanged = true;
    }

    bool visibilityChanged = false;
    if (restoreFeatureVisibility &&
        *colorMode == GPlatform::ToolpathPreviewColorMode::FeatureType) {
        std::vector<std::uint8_t> roles;
        roles.reserve(preview->getStats().features.size());
        for (const auto& feature : preview->getStats().features) {
            roles.push_back(feature.extrusionRole);
        }
        std::sort(roles.begin(), roles.end());
        roles.erase(std::unique(roles.begin(), roles.end()), roles.end());
        visibilityChanged = preview->setExtrusionRolesVisible(roles, true);
    }

    if (colorChanged) {
        emit colorModeChanged();
    }
    if (visibilityChanged) {
        emit pathVisibilityChanged();
        emit previewLegendItemsChanged();
    }
    return true;
}

void SlicingPreviewBridge::setPreviewInspectorOpen(bool open) {
    if (m_previewInspectorOpen == open) return;
    m_previewInspectorOpen = open;
    emit previewInspectorOpenChanged();
}
void SlicingPreviewBridge::setPreviewInspectorTab(const QString& tab) {
    if (tab != QStringLiteral("types") && tab != QStringLiteral("gcode")) return;
    setPreviewPanelFolded(false);
    if (m_previewInspectorTab == tab) return;
    m_previewInspectorTab = tab;
    emit previewInspectorTabChanged();
}
void SlicingPreviewBridge::togglePreviewInspector() {
    setPreviewInspectorOpen(!m_previewInspectorOpen);
}
void SlicingPreviewBridge::navigateGCode(bool layerStart) {
    const auto preview = getCurrentPreview();
    if (!preview) return;
    const auto layer = preview->getCurrentLayer();
    const auto step = layerStart ? 0 : preview->getCurrentStep();
    const auto line = preview->sourceLineForPosition(layer, step, layerStart);
    const auto label = !line ? tr("Layer %1 · No G-code source for this position").arg(layer+1)
        : layerStart ? tr("Layer %1 · First command · G-code line %2").arg(layer+1).arg(line)
        : step < 0 ? tr("Layer %1 · Last command · G-code line %2").arg(layer+1).arg(line)
        : tr("Layer %1 · Step %2 · G-code line %3").arg(layer+1).arg(step+1).arg(line);
    m_gcodeLines->navigate(line, label);
}

void SlicingPreviewBridge::setPreviewInspectModifierPressed(bool pressed) {
    pressed = pressed && m_previewPanelVisible;
    if (m_previewInspectModifierPressed == pressed) return;
    m_previewInspectModifierPressed = pressed;
    emit previewInspectModifierPressedChanged();
}

void SlicingPreviewBridge::setPreviewCursorSnapEnabled(bool enabled) {
    if (m_previewCursorSnapEnabled == enabled) return;
    m_previewCursorSnapEnabled = enabled;
    emit previewCursorSnapEnabledChanged();
}

void SlicingPreviewBridge::setPreviewPanelVisible(bool visible) {
    if (m_previewPanelVisible == visible) {
        return;
    }
    m_previewPanelVisible = visible;
    // Each preview session starts with the normal pointer. Key releases within
    // a session keep the user's cursor preference.
    setPreviewInspectModifierPressed(false);
    setPreviewCursorSnapEnabled(false);
    emit previewPanelVisibleChanged();
}

void SlicingPreviewBridge::setPreviewPanelFolded(bool folded) {
    if (m_previewPanelFolded == folded) {
        return;
    }
    m_previewPanelFolded = folded;
    emit previewPanelFoldedChanged();
}

bool SlicingPreviewBridge::setPreviewPanelViewType(const QString& viewType) {
    if (!isValidPreviewPanelViewType(viewType)) {
        LOG_ERROR("SlicingPreviewBridge::setPreviewPanelViewType invalid viewType={}",
                  viewType.toStdString());
        return false;
    }
    const bool viewChanged = m_previewPanelViewType != viewType;
    // Apply the renderer state first. The QML selection notification is emitted only after the
    // corresponding DB state has been reconciled, so UI and renderer cannot report different modes.
    applyPreviewPanelRenderStateForViewType(viewType, viewChanged, true);
    if (!viewChanged) {
        return true;
    }
    m_previewPanelViewType = viewType;
    emit previewPanelViewTypeChanged();
    return true;
}

void SlicingPreviewBridge::setPreviewDetailsVisible(bool visible) {
    if (m_previewDetailsVisible == visible) {
        return;
    }
    m_previewDetailsVisible = visible;
    emit previewDetailsVisibleChanged();
}

QVariantList SlicingPreviewBridge::previewPanelViewTypes() const {
    QVariantList result;
    const auto append = [&result](const GPlatform::SlicingPreviewViewDescriptor& descriptor) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::fromLatin1(descriptor.id));
        item.insert(QStringLiteral("label"), QString::fromLatin1(descriptor.label));
        item.insert(QStringLiteral("layoutKind"), QString::fromLatin1(descriptor.layoutKind));
        result.append(item);
    };
    const auto preview = getCurrentPreview();
    if (!preview) {
        return result;
    }
    for (const auto type : preview->getSupportedViewTypes()) {
        if (const auto* descriptor = GPlatform::slicingPreviewViewDescriptorForType(type)) {
            append(*descriptor);
        }
    }
    return result;
}

QVariantMap SlicingPreviewBridge::previewPanelData(const QString& viewType) const {
    QVariantMap result;
    const auto* descriptor = GPlatform::slicingPreviewViewDescriptorForId(viewType);
    result.insert(QStringLiteral("viewType"), viewType);
    result.insert(QStringLiteral("layoutKind"),
                  descriptor ? QString::fromLatin1(descriptor->layoutKind) : QString());
    result.insert(QStringLiteral("preferredWidth"), descriptor ? descriptor->preferredWidth : 330);
    if (!descriptor) {
        return result;
    }

    const auto preview = getCurrentPreview();
    if (!preview) {
        return result;
    }
    const auto& stats = preview->getStats();

    if (viewType == QStringLiteral("summary")) {
        QVariantList rows;
        rows.append(summaryRow(QStringLiteral("Total"),
                               QStringLiteral("%1 m / %2 g")
                                   .arg(stats.totalFilamentLengthMm / 1000.0, 0, 'f', 2)
                                   .arg(stats.totalFilamentWeightG, 0, 'f', 2)));
        rows.append(summaryRow(QStringLiteral("Cost"),
                               QString::number(stats.totalFilamentCost, 'f', 2)));
        rows.append(summaryRow(QStringLiteral("Total time"),
                               compactDuration(stats.totalTimeS)));
        result.insert(QStringLiteral("summaryRows"), rows);
        return result;
    }

    if (viewType == QStringLiteral("lineType")) {
        QVariantList rows;
        const auto legendItems = GPlatform::ToolpathPreviewUtils::buildLegendItems(
            *preview, GPlatform::ToolpathPreviewLegendGroupKind::FeatureType);
        for (const auto& feature : stats.features) {
            const auto legend = std::find_if(legendItems.begin(), legendItems.end(),
                                             [&feature](const auto& item) {
                                                 return item.rawId == feature.extrusionRole;
                                             });
            if (legend == legendItems.end()) {
                continue;
            }
            QVariantMap row;
            row.insert(QStringLiteral("id"), QString::fromStdString(legend->id));
            row.insert(QStringLiteral("color"), colorToHex(legend->colorRgba));
            row.insert(QStringLiteral("label"), QString::fromStdString(legend->label));
            row.insert(QStringLiteral("time"), compactDuration(feature.durationS));
            row.insert(QStringLiteral("percent"), percentOf(feature.durationS, stats.totalTimeS));
            row.insert(QStringLiteral("usagePrimary"),
                       QStringLiteral("%1 m").arg(feature.filamentLengthM, 0, 'f', 2));
            row.insert(QStringLiteral("usageSecondary"),
                       QStringLiteral("%1 g").arg(feature.filamentWeightG, 0, 'f', 2));
            row.insert(QStringLiteral("visible"), legend->visible);
            row.insert(QStringLiteral("displayEnabled"), true);
            rows.append(row);
        }

        for (const auto kind : {GPlatform::ToolpathPreviewOptionKind::Travel,
                                GPlatform::ToolpathPreviewOptionKind::Wipe,
                                GPlatform::ToolpathPreviewOptionKind::Seam}) {
            const auto option = std::find_if(stats.options.begin(), stats.options.end(),
                                             [kind](const auto& value) { return value.kind == kind; });
            if (option == stats.options.end() ||
                (option->occurrenceCount == 0 && option->durationS <= 0.0 && option->distanceMm <= 0.0)) {
                continue;
            }
            const bool seam = kind == GPlatform::ToolpathPreviewOptionKind::Seam;
            const bool wipe = kind == GPlatform::ToolpathPreviewOptionKind::Wipe;
            QVariantMap row;
            row.insert(QStringLiteral("id"), seam ? QStringLiteral("move.seam")
                                                   : wipe ? QStringLiteral("move.wipe")
                                                          : QStringLiteral("move.travel"));
            row.insert(QStringLiteral("color"), seam
                ? QStringLiteral("#e6e6e6")
                : colorToHex(GPlatform::ToolpathPreviewUtils::featureTypeColorRgba(
                      0, wipe ? GPlatform::ToolpathMotionKind::Wipe
                              : GPlatform::ToolpathMotionKind::Travel)));
            row.insert(QStringLiteral("label"), seam ? QStringLiteral("Seam")
                                                       : wipe ? QStringLiteral("Wipe")
                                                              : QStringLiteral("Travel"));
            row.insert(QStringLiteral("time"), compactDuration(option->durationS));
            row.insert(QStringLiteral("percent"), percentOf(option->durationS, stats.totalTimeS));
            row.insert(QStringLiteral("usagePrimary"),
                       QStringLiteral("%1 m").arg(option->distanceMm / 1000.0, 0, 'f', 2));
            row.insert(QStringLiteral("usageSecondary"),
                       QStringLiteral("%1 occurrences").arg(option->occurrenceCount));
            row.insert(QStringLiteral("visible"), seam ? preview->getShowSeam()
                                                        : wipe ? true : preview->getShowTravel());
            row.insert(QStringLiteral("displayEnabled"), !wipe);
            rows.append(row);
        }
        if (std::any_of(preview->getSegments().begin(), preview->getSegments().end(), [](const auto& segment) {
                return segment.fiberPhase == GPlatform::ToolpathFiberPhase::Finish;
            })) {
            QVariantMap row;
            row.insert(QStringLiteral("id"), QStringLiteral("move.fiberProcess"));
            row.insert(QStringLiteral("label"), tr("Fiber process motion"));
            row.insert(QStringLiteral("color"), QStringLiteral("#808080"));
            row.insert(QStringLiteral("visible"), preview->getShowFiberProcess());
            row.insert(QStringLiteral("displayEnabled"), true);
            rows.append(row);
        }
        using DiagnosticKind = GPlatform::FiberDiagnosticKind;
        const auto diagnostic_row = [&](DiagnosticKind kind, const QString& id,
                                        const QString& label, const QString& color, const QString& description) {
            const auto count = std::count_if(preview->getFiberFillDiagnostics().begin(),
                preview->getFiberFillDiagnostics().end(), [kind](const auto& value) { return value.kind == kind; });
            if (!count) return;
            QVariantMap row;
            row.insert(QStringLiteral("id"), id);
            row.insert(QStringLiteral("label"), label);
            row.insert(QStringLiteral("color"), color);
            row.insert(QStringLiteral("visible"), preview->getShowFiberDiagnostics() &&
                       !(preview->getHiddenFiberDiagnosticKinds() & (1u << unsigned(kind))));
            row.insert(QStringLiteral("displayEnabled"), true);
            row.insert(QStringLiteral("usageSecondary"), description.arg(count));
            rows.append(row);
        };
        diagnostic_row(DiagnosticKind::RejectedPath, QStringLiteral("diagnostic.fiberRejected"),
                       tr("被拒绝的纤维路径（调试）"), QStringLiteral("#ff8000"), tr("%1 条候选路径"));
        diagnostic_row(DiagnosticKind::OriginalContourRegion, QStringLiteral("diagnostic.fiberOriginalRegion"),
                       tr("原始轮廓区域（调试）"), QStringLiteral("#00c8e6"), tr("%1 个区域（包含孔洞）"));
        diagnostic_row(DiagnosticKind::ContourCandidate, QStringLiteral("diagnostic.fiberCandidate"),
                       tr("原始轮廓候选（调试）"), QStringLiteral("#b480ff"), tr("%1 条路径"));
        diagnostic_row(DiagnosticKind::RoundedContourCandidate, QStringLiteral("diagnostic.fiberRoundedCandidate"),
                       tr("圆角后轮廓候选（调试）"), QStringLiteral("#60df80"), tr("%1 条路径"));
        diagnostic_row(DiagnosticKind::MissingContourRegion, QStringLiteral("diagnostic.fiberMissingRegion"),
                       tr("候选轮廓未生成区域（调试）"), QStringLiteral("#ff3030"), tr("%1 个未铺设区域（按原因记录）"));
        result.insert(QStringLiteral("featureRows"), rows);
        return result;
    }

    if (viewType == QStringLiteral("filament")) {
        const auto positive = [](double value) { return value > 0.0; };
        const bool hasModel = std::any_of(stats.filamentUsage.begin(), stats.filamentUsage.end(),
                                         [&](const auto& usage) { return positive(usage.modelVolumeMm3); });
        const bool hasSupport = std::any_of(stats.filamentUsage.begin(), stats.filamentUsage.end(),
                                           [&](const auto& usage) { return positive(usage.supportVolumeMm3); });
        const bool hasFlushed = std::any_of(stats.filamentUsage.begin(), stats.filamentUsage.end(),
                                           [&](const auto& usage) { return positive(usage.flushedVolumeMm3); });
        const bool hasTower = std::any_of(stats.filamentUsage.begin(), stats.filamentUsage.end(),
                                         [&](const auto& usage) { return positive(usage.towerVolumeMm3); });
        const bool hasTotal = hasSupport || hasFlushed || hasTower;
        QVariantList columns;
        const auto appendColumn = [&columns](const char* id, const char* label, bool present) {
            if (!present) return;
            QVariantMap column;
            column.insert(QStringLiteral("id"), QString::fromLatin1(id));
            column.insert(QStringLiteral("label"), QString::fromLatin1(label));
            columns.append(column);
        };
        appendColumn("model", "Model", hasModel);
        appendColumn("support", "Support", hasSupport);
        appendColumn("flushed", "Flushed", hasFlushed);
        appendColumn("tower", "Tower", hasTower);
        appendColumn("total", "Total", hasTotal);

        QVariantList rows;
        auto usages = stats.filamentUsage;
        std::sort(usages.begin(), usages.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.filamentId < rhs.filamentId;
        });
        for (const auto& usage : usages) {
            const auto filament = std::find_if(preview->getFilaments().begin(), preview->getFilaments().end(),
                                               [&usage](const auto& item) { return item.id == usage.filamentId; });
            if (filament == preview->getFilaments().end()) {
                continue;
            }
            const auto previewColor = std::find_if(preview->getColors().begin(), preview->getColors().end(),
                                                   [&usage](const auto& item) {
                                                       return item.filamentId == usage.filamentId;
                                                   });
            const bool displayEnabled = previewColor != preview->getColors().end();
            QVariantMap row;
            row.insert(QStringLiteral("id"), displayEnabled
                ? QStringLiteral("filament.color.%1").arg(previewColor->id)
                : QStringLiteral("filament.%1").arg(usage.filamentId));
            row.insert(QStringLiteral("color"), colorToHex(displayEnabled
                ? previewColor->colorRgba : filament->colorRgba));
            row.insert(QStringLiteral("label"), displayEnabled && !previewColor->name.empty()
                ? QString::fromStdString(previewColor->name)
                : QStringLiteral("Filament %1").arg(usage.filamentId));
            row.insert(QStringLiteral("model"), filamentUsageText(usage.modelVolumeMm3, *filament));
            row.insert(QStringLiteral("support"), filamentUsageText(usage.supportVolumeMm3, *filament));
            row.insert(QStringLiteral("flushed"), filamentUsageText(usage.flushedVolumeMm3, *filament));
            row.insert(QStringLiteral("tower"), filamentUsageText(usage.towerVolumeMm3, *filament));
            row.insert(QStringLiteral("total"), filamentUsageText(usage.totalVolumeMm3, *filament));
            row.insert(QStringLiteral("visible"), displayEnabled
                ? preview->isPreviewColorIdVisible(previewColor->id) : true);
            row.insert(QStringLiteral("displayEnabled"), displayEnabled);
            rows.append(row);
        }

        QVariantList footerRows;
        footerRows.append(summaryRow(QStringLiteral("Filament changes"),
                                     QString::number(stats.totalFilamentChanges)));
        footerRows.append(summaryRow(QStringLiteral("Tool changes"),
                                     QString::number(stats.totalToolChanges)));
        footerRows.append(summaryRow(QStringLiteral("Cost"),
                                     QString::number(stats.totalFilamentCost, 'f', 2)));
        result.insert(QStringLiteral("filamentRows"), rows);
        result.insert(QStringLiteral("usageColumns"), columns);
        result.insert(QStringLiteral("footerRows"), footerRows);
        const int preferredWidth = 420 + 84 * (static_cast<int>(columns.size()) - 1);
        result.insert(QStringLiteral("preferredWidth"), std::clamp(preferredWidth, 420, 640));
        return result;
    }

    if (const auto range = rangeDescription(viewType, stats)) {
        QVariantList rows;
        for (int index = 10; index >= 0; --index) {
            const double normalized = static_cast<double>(index) / 10.0;
            double value = range->minimum + normalized * (range->maximum - range->minimum);
            if (range->logarithmic) {
                const double logMinimum = std::log1p(std::max(range->minimum, 0.0f));
                const double logMaximum = std::log1p(std::max(range->maximum, 0.0f));
                value = std::expm1(logMinimum + normalized * (logMaximum - logMinimum));
            }
            QVariantMap row;
            row.insert(QStringLiteral("color"), colorToHex(
                GPlatform::ToolpathPreviewUtils::rangeColorRgba(static_cast<float>(normalized))));
            row.insert(QStringLiteral("label"), QString::number(value, 'f', range->decimals));
            rows.append(row);
        }
        QVariantList optionRows;
        if (range->showTravelOption) {
            QVariantMap option;
            option.insert(QStringLiteral("id"), QStringLiteral("move.travel"));
            option.insert(QStringLiteral("color"), colorToHex(
                GPlatform::ToolpathPreviewUtils::featureTypeColorRgba(
                    0, GPlatform::ToolpathMotionKind::Travel)));
            option.insert(QStringLiteral("label"), QStringLiteral("Travel"));
            option.insert(QStringLiteral("visible"), preview->getShowTravel());
            option.insert(QStringLiteral("displayEnabled"), true);
            optionRows.append(option);
        }
        result.insert(QStringLiteral("rangeTitle"), range->title);
        result.insert(QStringLiteral("rangeRows"), rows);
        result.insert(QStringLiteral("optionRows"), optionRows);
    }
    return result;
}

bool SlicingPreviewBridge::setPreviewPanelRowVisible(const QString& viewType,
                                                     const QString& rowId,
                                                     bool visible) {
    const auto preview = getCurrentPreview();
    if (!preview || !GPlatform::slicingPreviewViewDescriptorForId(viewType)) {
        return false;
    }

    bool changed = false;
    if (rowId == QStringLiteral("diagnostic.fiberRejected") ||
        rowId == QStringLiteral("diagnostic.fiberOriginalRegion") ||
        rowId == QStringLiteral("diagnostic.fiberMissingRegion") ||
        rowId == QStringLiteral("diagnostic.fiberCandidate") ||
        rowId == QStringLiteral("diagnostic.fiberRoundedCandidate")) {
        const auto kind = rowId == QStringLiteral("diagnostic.fiberCandidate")
            ? GPlatform::FiberDiagnosticKind::ContourCandidate
            : rowId == QStringLiteral("diagnostic.fiberRoundedCandidate")
                ? GPlatform::FiberDiagnosticKind::RoundedContourCandidate
            : rowId == QStringLiteral("diagnostic.fiberOriginalRegion")
            ? GPlatform::FiberDiagnosticKind::OriginalContourRegion
            : rowId == QStringLiteral("diagnostic.fiberMissingRegion")
                ? GPlatform::FiberDiagnosticKind::MissingContourRegion : GPlatform::FiberDiagnosticKind::RejectedPath;
        const unsigned int bit = 1u << unsigned(kind);
        const auto previous = preview->getHiddenFiberDiagnosticKinds();
        const unsigned int allKinds = (1u << (unsigned(GPlatform::FiberDiagnosticKind::RoundedContourCandidate) + 1u)) - 1u;
        const auto hidden = visible
            ? (preview->getShowFiberDiagnostics() ? previous : allKinds) & ~bit
            : previous | bit;
        const bool shown = (visible || preview->getShowFiberDiagnostics()) &&
            std::any_of(preview->getFiberFillDiagnostics().begin(), preview->getFiberFillDiagnostics().end(),
                        [hidden](const auto& value) { return !(hidden & (1u << unsigned(value.kind))); });
        changed = hidden != previous || shown != preview->getShowFiberDiagnostics();
        preview->setHiddenFiberDiagnosticKinds(hidden);
        preview->setShowFiberDiagnostics(shown);
    } else if (rowId == QStringLiteral("move.fiberProcess")) {
        if (preview->getShowFiberProcess() != visible) {
            preview->setShowFiberProcess(visible);
            changed = true;
        }
    } else if (rowId == QStringLiteral("move.travel")) {
        if (preview->getShowTravel() != visible) {
            preview->setShowTravel(visible);
            changed = true;
        }
    } else if (rowId == QStringLiteral("move.seam")) {
        if (preview->getShowSeam() != visible) {
            preview->setShowSeam(visible);
            changed = true;
        }
    } else if (rowId.startsWith(QStringLiteral("feature.role."))) {
        bool ok = false;
        const auto role = rowId.mid(13).toUInt(&ok);
        if (!ok || role > 0xffu) return false;
        changed = preview->setExtrusionRoleVisible(static_cast<std::uint8_t>(role), visible);
    } else if (rowId.startsWith(QStringLiteral("filament.color."))) {
        bool ok = false;
        const auto colorId = rowId.mid(15).toUInt(&ok);
        if (!ok || colorId > 0xffffu) return false;
        changed = preview->setPreviewColorIdVisible(static_cast<std::uint16_t>(colorId), visible);
    } else {
        return false;
    }

    if (changed) {
        emit pathVisibilityChanged();
        emit previewLegendItemsChanged();
    }
    return true;
}

QVariantList SlicingPreviewBridge::previewLegendItems(const QString& viewType) const {
    QVariantList result;
    if (viewType != QStringLiteral("filament") &&
        viewType != QStringLiteral("lineType") &&
        viewType != QStringLiteral("tool")) {
        LOG_ERROR("SlicingPreviewBridge::previewLegendItems unsupported viewType={}", viewType.toStdString());
        return result;
    }

    const auto preview = getCurrentPreview();
    if (!preview) {
        return result;
    }

    const auto items = buildLegendItemsForViewType(*preview, viewType);
    for (const auto& item : items) {
        result.append(legendItemToVariant(item));
    }
    return result;
}

QVariantMap SlicingPreviewBridge::previewGradientRange(const QString& viewType) const {
    QVariantMap result;
    const auto preview = getCurrentPreview();
    if (!preview) {
        return result;
    }
    const auto& stats = preview->getStats();
    if (viewType == QStringLiteral("speed")) {
        result.insert(QStringLiteral("leftLabel"), formatPreviewRange(stats.minSpeedMmS, QStringLiteral("mm/s")));
        result.insert(QStringLiteral("rightLabel"), formatPreviewRange(stats.maxSpeedMmS, QStringLiteral("mm/s")));
        result.insert(QStringLiteral("min"), stats.minSpeedMmS);
        result.insert(QStringLiteral("max"), stats.maxSpeedMmS);
        result.insert(QStringLiteral("unit"), QStringLiteral("mm/s"));
        return result;
    }
    if (viewType == QStringLiteral("layerHeight")) {
        result.insert(QStringLiteral("leftLabel"), formatPreviewRange(stats.minLayerHeightMm, QStringLiteral("mm")));
        result.insert(QStringLiteral("rightLabel"), formatPreviewRange(stats.maxLayerHeightMm, QStringLiteral("mm")));
        result.insert(QStringLiteral("min"), stats.minLayerHeightMm);
        result.insert(QStringLiteral("max"), stats.maxLayerHeightMm);
        result.insert(QStringLiteral("unit"), QStringLiteral("mm"));
        return result;
    }
    if (viewType == QStringLiteral("lineWidth")) {
        result.insert(QStringLiteral("min"), stats.minWidthMm);
        result.insert(QStringLiteral("max"), stats.maxWidthMm);
        result.insert(QStringLiteral("unit"), QStringLiteral("mm"));
        return result;
    }
    if (viewType == QStringLiteral("flow")) {
        result.insert(QStringLiteral("min"), stats.minVolumetricFlow);
        result.insert(QStringLiteral("max"), stats.maxVolumetricFlow);
        result.insert(QStringLiteral("unit"), QStringLiteral("mm³/s"));
        return result;
    }
    LOG_ERROR("SlicingPreviewBridge::previewGradientRange unsupported viewType={}", viewType.toStdString());
    return result;
}

bool SlicingPreviewBridge::setPreviewLegendItemVisible(const QString& viewType,
                                                       const QString& itemId,
                                                       bool visible) {
    if (viewType != QStringLiteral("filament") &&
        viewType != QStringLiteral("lineType") &&
        viewType != QStringLiteral("tool")) {
        LOG_ERROR("SlicingPreviewBridge::setPreviewLegendItemVisible unsupported viewType={} itemId={}",
                  viewType.toStdString(),
                  itemId.toStdString());
        return false;
    }

    const auto preview = getCurrentPreview();
    if (!preview) {
        LOG_ERROR("SlicingPreviewBridge::setPreviewLegendItemVisible no current preview viewType={} itemId={}",
                  viewType.toStdString(),
                  itemId.toStdString());
        return false;
    }

    const auto items = buildLegendItemsForViewType(*preview, viewType);
    const auto itemIt = std::find_if(items.begin(), items.end(), [&itemId](const auto& item) {
        return item.id == itemId.toStdString();
    });
    if (itemIt == items.end()) {
        LOG_ERROR("SlicingPreviewBridge::setPreviewLegendItemVisible unknown item viewType={} itemId={}",
                  viewType.toStdString(),
                  itemId.toStdString());
        return false;
    }
    if (itemIt->visible == visible) {
        return true;
    }

    bool changed = false;
    switch (itemIt->group) {
    case GPlatform::ToolpathPreviewLegendGroupKind::Filament:
        changed = preview->setPreviewColorIdVisible(static_cast<std::uint16_t>(itemIt->rawId), visible);
        break;
    case GPlatform::ToolpathPreviewLegendGroupKind::FeatureType:
        changed = preview->setExtrusionRoleVisible(static_cast<std::uint8_t>(itemIt->rawId), visible);
        break;
    case GPlatform::ToolpathPreviewLegendGroupKind::Tool:
        changed = preview->setToolIdVisible(static_cast<std::uint16_t>(itemIt->rawId), visible);
        break;
    case GPlatform::ToolpathPreviewLegendGroupKind::MoveType:
        if (itemIt->id == "move.seam") {
            if (preview->getShowSeam() != visible) {
                preview->setShowSeam(visible);
                changed = true;
            }
        } else if (preview->getShowTravel() != visible) {  // Travel
            preview->setShowTravel(visible);
            changed = true;
        }
        break;
    }

    if (changed) {
        emit pathVisibilityChanged();
        emit previewLegendItemsChanged();
    }
    return true;
}

bool SlicingPreviewBridge::setPreviewLegendGroupVisible(const QString& viewType, bool visible) {
    if (viewType != QStringLiteral("filament") &&
        viewType != QStringLiteral("lineType") &&
        viewType != QStringLiteral("tool")) {
        LOG_ERROR("SlicingPreviewBridge::setPreviewLegendGroupVisible unsupported viewType={}",
                  viewType.toStdString());
        return false;
    }

    const auto preview = getCurrentPreview();
    if (!preview) {
        LOG_ERROR("SlicingPreviewBridge::setPreviewLegendGroupVisible no current preview viewType={}",
                  viewType.toStdString());
        return false;
    }

    const auto items = buildLegendItemsForViewType(*preview, viewType);
    std::vector<std::uint16_t> colorIds;
    std::vector<std::uint8_t> roles;
    std::vector<std::uint16_t> toolIds;
    bool hasTravelItem = false;
    bool hasSeamItem = false;
    for (const auto& item : items) {
        switch (item.group) {
        case GPlatform::ToolpathPreviewLegendGroupKind::Filament:
            colorIds.push_back(static_cast<std::uint16_t>(item.rawId));
            break;
        case GPlatform::ToolpathPreviewLegendGroupKind::FeatureType:
            roles.push_back(static_cast<std::uint8_t>(item.rawId));
            break;
        case GPlatform::ToolpathPreviewLegendGroupKind::Tool:
            toolIds.push_back(static_cast<std::uint16_t>(item.rawId));
            break;
        case GPlatform::ToolpathPreviewLegendGroupKind::MoveType:
            if (item.id == "move.seam") {
                hasSeamItem = true;
            } else {
                hasTravelItem = true;
            }
            break;
        }
    }

    bool changed = false;
    if (!colorIds.empty()) {
        changed = preview->setPreviewColorIdsVisible(colorIds, visible) || changed;
    }
    if (!roles.empty()) {
        changed = preview->setExtrusionRolesVisible(roles, visible) || changed;
    }
    if (!toolIds.empty()) {
        changed = preview->setToolIdsVisible(toolIds, visible) || changed;
    }
    if (hasTravelItem && preview->getShowTravel() != visible) {
        preview->setShowTravel(visible);
        changed = true;
    }
    if (hasSeamItem && preview->getShowSeam() != visible) {
        preview->setShowSeam(visible);
        changed = true;
    }

    if (changed) {
        emit pathVisibilityChanged();
        emit previewLegendItemsChanged();
    }
    return true;
}

bool SlicingPreviewBridge::resetPreviewLegendVisibility() {
    const auto preview = getCurrentPreview();
    if (!preview) {
        LOG_ERROR("SlicingPreviewBridge::resetPreviewLegendVisibility no current preview");
        return false;
    }

    const bool changed = preview->resetLegendVisibility();
    if (changed) {
        emit pathVisibilityChanged();
        emit previewLegendItemsChanged();
    }
    return true;
}

void SlicingPreviewBridge::togglePreviewPanelVisible() { setPreviewPanelVisible(!m_previewPanelVisible); }
void SlicingPreviewBridge::togglePreviewPanelFolded() { setPreviewPanelFolded(!m_previewPanelFolded); }
void SlicingPreviewBridge::togglePreviewDetailsVisible() { setPreviewDetailsVisible(!m_previewDetailsVisible); }

int SlicingPreviewBridge::currentLayer() const {
    if (auto preview = getCurrentPreview()) {
        return preview->getCurrentLayer();
    }
    return 0;
}

int SlicingPreviewBridge::currentStep() const {
    if (auto preview = getCurrentPreview()) {
        const int step = preview->getCurrentStep();
        const int totalSteps = preview->getTotalSteps();
        return step < 0 || totalSteps <= 0 ? -1 : std::min(step, totalSteps - 1);
    }
    return -1;
}

void SlicingPreviewBridge::setCurrentStep(int step) {
    if (auto preview = getCurrentPreview()) {
        const int totalSteps = preview->getTotalSteps();
        const int nextStep =
            step < 0 || totalSteps <= 0 ? -1 : std::min(step, totalSteps - 1);
        if (preview->getCurrentStep() != nextStep) {
            preview->setCurrentStep(nextStep);
            emit currentStepChanged();
        }
        navigateGCode(false);
    }
}

int SlicingPreviewBridge::layerRangeStart() const {
    if (auto preview = getCurrentPreview()) {
        return preview->getShowLayerRangeStart();
    }
    return 0;
}

int SlicingPreviewBridge::layerRangeEnd() const {
    if (auto preview = getCurrentPreview()) {
        return preview->getShowLayerRangeEnd();
    }
    return 0;
}

int SlicingPreviewBridge::totalLayers() const {
    if (auto preview = getCurrentPreview()) {
        return preview->getTotalLayers();
    }
    return 0;
}

int SlicingPreviewBridge::totalSteps() const {
    if (auto preview = getCurrentPreview()) {
        return preview->getTotalSteps();
    }
    return 0;
}

void SlicingPreviewBridge::refreshAlgorithmAuditCache(
    const std::shared_ptr<GPlatform::ToolpathPreviewDB>& preview) const {
    const auto cachedPreview = m_cachedAuditPreview.lock();
    const std::uint64_t generation = preview
        ? preview->getPreviewDataGeneration() : 0;
    if (cachedPreview == preview &&
        m_cachedAuditPreviewGeneration == generation) {
        return;
    }

    m_cachedAuditPreview = preview;
    m_cachedAuditPreviewGeneration = generation;
    m_cachedAuditStages.clear();
    if (!preview) {
        return;
    }

    const QByteArray metadata = QByteArray::fromStdString(preview->getMetadataJson());
    if (metadata.isEmpty()) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(metadata, &error);
    const QJsonObject root = document.object();
    if (error.error != QJsonParseError::NoError ||
        root.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("gplatform.belt_support_algorithm_audit.v1")) {
        return;
    }

    const QJsonArray stages = root.value(QStringLiteral("stages")).toArray();
    m_cachedAuditStages.reserve(stages.size());
    for (const QJsonValue& value : stages) {
        const QJsonObject stage = value.toObject();
        const QJsonObject metricObject =
            stage.value(QStringLiteral("metrics")).toObject();
        QStringList metricKeys = metricObject.keys();
        metricKeys.sort();
        QStringList metricText;
        for (const QString& key : metricKeys) {
            metricText.push_back(QStringLiteral("%1: %2").arg(
                key,
                QString::number(metricObject.value(key).toDouble(), 'f', 2)));
        }
        m_cachedAuditStages.append(QVariantMap{
            {QStringLiteral("algorithmAudit"), true},
            {QStringLiteral("stageId"), stage.value(QStringLiteral("id")).toString()},
            {QStringLiteral("stageName"), stage.value(QStringLiteral("name")).toString()},
            {QStringLiteral("purpose"), stage.value(QStringLiteral("purpose")).toString()},
            {QStringLiteral("expected"), stage.value(QStringLiteral("expected")).toString()},
            {QStringLiteral("elapsedMs"), stage.value(QStringLiteral("elapsed_ms")).toDouble()},
            {QStringLiteral("lineCount"), stage.value(QStringLiteral("line_count")).toInteger()},
            {QStringLiteral("recordCount"), stage.value(QStringLiteral("record_count")).toInteger()},
            {QStringLiteral("metrics"), metricObject.toVariantMap()},
            {QStringLiteral("metricsText"), metricText.join(QStringLiteral("  ·  "))},
            {QStringLiteral("auditOutputPath"), root.value(QStringLiteral("output_path")).toString()},
            {QStringLiteral("finalSupportGenerated"), root.value(QStringLiteral("final_support_generated")).toBool()}
        });
    }
}

QVariantMap SlicingPreviewBridge::layerInfo(int layer) const {
    const auto preview = getCurrentPreview();
    if (!preview) {
        return {};
    }
    const auto& layers = preview->getLayers();
    if (layer < 0 || layer >= static_cast<int>(layers.size())) {
        return {};
    }
    const auto& item = layers[static_cast<std::size_t>(layer)];
    QVariantMap result{
        {QStringLiteral("index"), layer},
        {QStringLiteral("displayNumber"), layer + 1},
        {QStringLiteral("printZMm"), item.printZMm},
        {QStringLiteral("layerHeightMm"), item.heightMm},
        {QStringLiteral("durationS"), item.durationS},
        {QStringLiteral("stepCount"), static_cast<qulonglong>(item.segmentCount)}
    };
    refreshAlgorithmAuditCache(preview);
    if (layer < m_cachedAuditStages.size()) {
        const QVariantMap audit = m_cachedAuditStages.at(layer).toMap();
        for (auto it = audit.cbegin(); it != audit.cend(); ++it) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

void SlicingPreviewBridge::play() { emit playbackPlayRequested(); }
void SlicingPreviewBridge::pause() { emit playbackPauseRequested(); }
void SlicingPreviewBridge::stop() { emit playbackStopRequested(); }

void SlicingPreviewBridge::setLayerRange(int start, int end) {
    if (auto preview = getCurrentPreview()) {
        if (preview->getTotalLayers() <= 0) {
            return;
        }
        const int maxLayer = std::max(0, preview->getTotalLayers() - 1);
        const int validStart = std::clamp(start, 0, maxLayer);
        const int validEnd = std::clamp(end, validStart, maxLayer);
        const bool rangeChanged =
            preview->getShowLayerRangeStart() != validStart ||
            preview->getShowLayerRangeEnd() != validEnd;
        if (!rangeChanged) {
            return;
        }

        const int previousLayer = preview->getCurrentLayer();
        const int previousStep = preview->getCurrentStep();
        if (preview->getShowLayerRangeStart() != validStart) {
            preview->setShowLayerRangeStart(validStart);
        }
        if (preview->getShowLayerRangeEnd() != validEnd) {
            preview->setShowLayerRangeEnd(validEnd);
        }
        // A new vertical range starts fully visible. Horizontal scrubbing then
        // applies one shared Step cap to every layer in the range.
        if (previousLayer != validEnd) {
            preview->setCurrentLayer(validEnd);
        }
        if (previousStep >= 0) {
            preview->setCurrentStep(-1);
        }
        emit layerRangeChanged();
        emit totalStepsChanged();
        if (previousLayer != validEnd) {
            emit currentLayerChanged();
        }
        if (previousStep >= 0) {
            emit currentStepChanged();
        }
        navigateGCode(true);
    }
}

void SlicingPreviewBridge::setIsPlaying(bool playing) {
    playing ? emit playbackPlayRequested() : emit playbackPauseRequested();
}

void SlicingPreviewBridge::setPlaybackSpeed(float speed) { emit playbackSpeedChangeRequested(speed); }
void SlicingPreviewBridge::setIsLooping(bool looping) { emit playbackLoopingChangeRequested(looping); }

void SlicingPreviewBridge::updatePlaybackPlaying(bool playing) {
    if (m_isPlaying == playing) {
        return;
    }
    m_isPlaying = playing;
    emit isPlayingChanged();
}

void SlicingPreviewBridge::updatePlaybackSpeed(float speed) {
    if (qFuzzyCompare(m_playbackSpeed, speed)) {
        return;
    }
    m_playbackSpeed = speed;
    emit playbackSpeedChanged();
}

void SlicingPreviewBridge::updatePlaybackLooping(bool looping) {
    if (m_isLooping == looping) {
        return;
    }
    m_isLooping = looping;
    emit isLoopingChanged();
}

void SlicingPreviewBridge::addToolpathPreview(int modelId,
                                              const QString& modelName,
                                              std::shared_ptr<GPlatform::ToolpathPreviewDB> preview) {
    if (!preview) {
        LOG_ERROR("SlicingPreviewBridge::addToolpathPreview null preview modelId={}", modelId);
        return;
    }

    if (m_activePreview && m_activePreview != preview) {
        m_activePreview->releasePrintOutputArtifacts();
        if (auto* docManager = DocumentManager::instance()) {
            m_activePreview->removeMaterial();
            docManager->unregisterDBInstance(
                m_activePreview->getDBInstanceID());
        }
    }

    m_activePreview = std::move(preview);
    m_activePreviewName = modelName;

    if (m_activePreview->getSourceScope() ==
        GPlatform::ToolpathPreviewSourceScope::SingleModel) {
        const DBInstanceID sourceModelId =
            m_activePreview->getSourceModelDBId();
        modelId = sourceModelId.isValid() &&
                sourceModelId.getValue() <=
                    static_cast<std::uint64_t>(
                        std::numeric_limits<int>::max())
            ? static_cast<int>(sourceModelId.getValue())
            : -1;
    }

    const bool modelChanged = m_currentModelId != modelId;
    m_currentModelId = modelId;
    if (modelChanged) {
        emit currentModelChanged();
    }
    emit currentToolpathPreviewChanged();
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit slicedModelListChanged();
    emit currentLayerChanged();
    emit currentStepChanged();
    emit totalLayersChanged();
    emit totalStepsChanged();
    emit statisticsChanged();
    emit colorModeChanged();
    emit pathVisibilityChanged();
    emit previewLegendItemsChanged();
}

void SlicingPreviewBridge::removeToolpathPreview(int modelId) {
    if (!m_activePreview || modelId != m_currentModelId) {
        return;
    }
    m_activePreview->releasePrintOutputArtifacts();
    if (auto* docManager = DocumentManager::instance()) {
        m_activePreview->removeMaterial();
        docManager->unregisterDBInstance(
            m_activePreview->getDBInstanceID());
    }
    m_activePreview.reset();
    m_activePreviewName.clear();
    m_currentModelId = -1;
    emit currentModelChanged();
    emit currentToolpathPreviewChanged();
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit totalLayersChanged();
    emit totalStepsChanged();
    emit previewLegendItemsChanged();
    emit slicedModelListChanged();
}

void SlicingPreviewBridge::clearAllToolpathPreviewsInternal(int preservedModelId) {
    if (m_activePreview && preservedModelId != m_currentModelId) {
        m_activePreview->releasePrintOutputArtifacts();
        if (auto* docManager = DocumentManager::instance()) {
            m_activePreview->removeMaterial();
            docManager->unregisterDBInstance(
                m_activePreview->getDBInstanceID());
        }
        m_activePreview.reset();
        m_activePreviewName.clear();
        m_currentModelId = -1;
    }

    emit currentModelChanged();
    emit currentToolpathPreviewChanged();
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit slicedModelListChanged();
    emit totalLayersChanged();
    emit totalStepsChanged();
    emit statisticsChanged();
    emit previewLegendItemsChanged();
}

void SlicingPreviewBridge::clearAllToolpathPreviews() {
    clearAllToolpathPreviewsInternal(-1);
}

void SlicingPreviewBridge::clearAllDBReferences() {
    clearAllToolpathPreviews();
}

bool SlicingPreviewBridge::showFiberFillDiagnostics(const QString& previewToken) {
    const auto preview = getCurrentPreview();
    if (!preview || preview->getFiberFillDiagnostics().empty()) return false;
    const QString token = QStringLiteral("%1:%2")
        .arg(preview->getDBInstanceID().getValue()).arg(preview->getPreviewDataGeneration());
    if (previewToken != token) return false; // Never activate a stale result.
    setPreviewPanelViewType(QStringLiteral("lineType"));
    setPreviewPanelVisible(true);
    setPreviewPanelFolded(false);
    preview->setHiddenFiberDiagnosticKinds(0);
    preview->setShowFiberDiagnostics(true);
    emit pathVisibilityChanged();
    emit previewLegendItemsChanged();
    // Focus the first affected layer, even if the previous slider range excluded it.
    const auto& diagnostics = preview->getFiberFillDiagnostics();
    const auto missing = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& value) {
        return value.kind == GPlatform::FiberDiagnosticKind::MissingContourRegion;
    });
    const auto layerId = (missing == diagnostics.end() ? diagnostics.front() : *missing).layerId;
    for (size_t i = 0; i < preview->getLayers().size(); ++i)
        if (preview->getLayers()[i].id == layerId) { setLayerRange(int(i), int(i)); break; }
    return true;
}

void SlicingPreviewBridge::setCurrentModelId(int modelId) {
    if (modelId == m_currentModelId) {
        return;
    }
    if (!m_activePreview) {
        LOG_ERROR("SlicingPreviewBridge::setCurrentModelId unknown modelId={}", modelId);
        return;
    }
    m_currentModelId = modelId;
    applyPreviewPanelRenderStateForViewType(m_previewPanelViewType, false, false);
    emit currentModelChanged();
    emit currentToolpathPreviewChanged();
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit currentLayerChanged();
    emit currentStepChanged();
    emit totalLayersChanged();
    emit totalStepsChanged();
    emit statisticsChanged();
    emit previewLegendItemsChanged();
}

std::shared_ptr<GPlatform::ToolpathPreviewDB> SlicingPreviewBridge::currentToolpathPreview() const {
    return getCurrentPreview();
}

QVariant SlicingPreviewBridge::slicedModelList() const {
    QVariantList result;
    if (m_activePreview) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), m_currentModelId);
        item.insert(
            QStringLiteral("name"),
            m_activePreviewName.isEmpty()
                ? QStringLiteral("Toolpath %1").arg(m_currentModelId)
                : m_activePreviewName);
        result.append(item);
    }
    return result;
}

void SlicingPreviewBridge::beginPreviewLoad(const QString& artifactPath) {
    m_previewArtifactPath = artifactPath;
    emit previewArtifactPathChanged();
    if (!m_isLoading) {
        m_isLoading = true;
        emit isLoadingChanged();
    }
}

void SlicingPreviewBridge::completePreviewLoad(const QString& artifactPath,
                                               const QString& printOutputPath,
                                               int modelId,
                                               const QString& modelName,
                                               std::shared_ptr<GPlatform::ToolpathPreviewDB> preview) {
    if (!preview) {
        failPreviewLoad(artifactPath, tr("Failed to create toolpath preview object"));
        return;
    }

    addToolpathPreview(modelId, modelName, preview);
    applyPreviewPanelRenderStateForViewType(m_previewPanelViewType, false, false);

    setPreviewPanelVisible(true);

    m_previewArtifactPath = artifactPath;
    m_printOutputPath = printOutputPath;
    m_isLoading = false;
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit isLoadingChanged();
    // The layer/range/timeline/statistics properties now read from a freshly
    // loaded preview. Notify the always-resident controls to discard their
    // stale empty-preview values.
    emit totalLayersChanged();
    emit totalStepsChanged();
    emit currentLayerChanged();
    emit currentStepChanged();
    emit layerRangeChanged();
    emit statisticsChanged();
    emit previewLoadFinished(true, artifactPath);

    LOG_INFO("SlicingPreviewBridge::completePreviewLoad artifact={} printOutput={} layers={} drawable={}",
             artifactPath.toStdString(),
             printOutputPath.toStdString(),
             preview->getTotalLayers(),
             preview->getStats().renderSegmentCount);
}

void SlicingPreviewBridge::failPreviewLoad(const QString& artifactPath, const QString& errorMessage) {
    if (m_isLoading) {
        m_isLoading = false;
        emit isLoadingChanged();
    }
    emit previewLoadError(errorMessage);
    emit previewLoadFinished(false, artifactPath);
}

void SlicingPreviewBridge::cancelPreviewLoad() {
    if (m_isLoading) {
        m_isLoading = false;
        emit isLoadingChanged();
    }
}

void SlicingPreviewBridge::clearPreview() {
    // Clearing a preview is destructive in the current workspace architecture. Hide the
    // presentation state first so QML cannot keep stale preview controls visible while DB/render
    // teardown is being processed. The future Preview workspace will replace this compatibility
    // behavior with separate preview.exit and preview.clear lifecycles.
    setPreviewPanelVisible(false);
    cancelPreviewLoad();
    clearAllToolpathPreviewsInternal(-1);
    m_previewArtifactPath.clear();
    m_printOutputPath.clear();
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit previewLegendItemsChanged();
}

void SlicingPreviewBridge::clearPreviewPreservingModel(int modelId) {
    cancelPreviewLoad();
    clearAllToolpathPreviewsInternal(modelId);
    m_previewArtifactPath.clear();
    m_printOutputPath.clear();
    emit previewArtifactPathChanged();
    emit printOutputPathChanged();
    emit previewLegendItemsChanged();
}

bool SlicingPreviewBridge::canExportPrintOutput(const QString& formatId) const {
    const QString sourcePath = resolveCurrentPrintOutputPath(formatId);
    return !sourcePath.isEmpty() && QFileInfo::exists(sourcePath);
}

bool SlicingPreviewBridge::exportCurrentPrintOutputAs(const QString& formatId, const QString& savePath) {
    const QString sourcePath = resolveCurrentPrintOutputPath(formatId);
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
        emit previewLoadError(tr("The selected print output is unavailable: %1").arg(formatId));
        return false;
    }

    QString target = savePath.trimmed();
    if (target.isEmpty()) {
        return false;
    }
    if (QFileInfo(target).suffix().isEmpty()) {
        target += formatId == QStringLiteral("gcode-3mf")
            ? QStringLiteral(".gcode.3mf")
            : QStringLiteral(".gcode");
    }
    if (QFileInfo(sourcePath).absoluteFilePath() == QFileInfo(target).absoluteFilePath()) {
        return true;
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        emit previewLoadError(tr("Unable to read source print output: %1").arg(source.errorString()));
        return false;
    }

    QSaveFile destination(target);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) {
        emit previewLoadError(tr("Unable to create export file: %1").arg(destination.errorString()));
        return false;
    }

    constexpr qint64 chunkSize = 1024 * 1024;
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(chunkSize);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            destination.cancelWriting();
            emit previewLoadError(tr("Unable to read source print output: %1").arg(source.errorString()));
            return false;
        }
        if (destination.write(chunk) != chunk.size()) {
            const QString error = destination.errorString();
            destination.cancelWriting();
            emit previewLoadError(tr("Export failed: %1").arg(error));
            return false;
        }
    }
    if (!destination.commit()) {
        emit previewLoadError(tr("Unable to replace export file: %1").arg(destination.errorString()));
        return false;
    }
    return true;
}

QString SlicingPreviewBridge::defaultPrintOutputFileName(const QString& formatId) const {
    QString baseName = QStringLiteral("slicing_result");
    if (const auto preview = getCurrentPreview(); preview && !preview->getSourceModelName().empty()) {
        baseName = QFileInfo(QString::fromStdString(preview->getSourceModelName())).completeBaseName();
        baseName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")),
                         QStringLiteral("_"));
        if (baseName.trimmed().isEmpty()) {
            baseName = QStringLiteral("slicing_result");
        }
    }
    return baseName + (formatId == QStringLiteral("gcode-3mf")
        ? QStringLiteral(".gcode.3mf")
        : QStringLiteral(".gcode"));
}

QString SlicingPreviewBridge::defaultExportPrintOutputFileUrl(const QString& formatId) const {
    const QString suggestedName = defaultPrintOutputFileName(formatId);
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/GPlatform/Gcode";
    QDir().mkpath(defaultDir);
    return QUrl::fromLocalFile(QDir(defaultDir).absoluteFilePath(suggestedName)).toString();
}

QStringList SlicingPreviewBridge::printOutputNameFilters(const QString& formatId) const {
    if (formatId == QStringLiteral("gcode-3mf")) {
        return {tr("Sliced G-code 3MF (*.gcode.3mf)"), tr("All files (*)")};
    }
    if (formatId == QStringLiteral("gcode")) {
        return {tr("G-code (*.gcode)"), tr("All files (*)")};
    }
    return {tr("All files (*)")};
}

bool SlicingPreviewBridge::canResliceCurrentModel() const {
    const auto preview = getCurrentPreview();
    auto* document = DocumentManager::instance();
    if (!preview || !document ||
        preview->getSourceScope() ==
            GPlatform::ToolpathPreviewSourceScope::ImportedGCode) {
        return false;
    }

    if (preview->getSourceScope() ==
        GPlatform::ToolpathPreviewSourceScope::SingleModel) {
        const auto instance = document->getDB<ModelInstanceDB>(
            preview->getSourceModelDBId());
        return instance && instance->isValid() &&
            instance->getPrintable();
    }

    const DBInstanceID printBedId = document->getOwner(
        preview->getDBInstanceID());
    if (!printBedId.isValid()) return false;
    for (const auto& object : document->getDBInstancesByType(
             TypeID::MODEL_INSTANCE_DB)) {
        const auto instance =
            std::dynamic_pointer_cast<ModelInstanceDB>(object);
        if (instance && instance->isValid() &&
            instance->getPrintable() &&
            instance->getParentPrintBedDBId() == printBedId) {
            return true;
        }
    }
    return false;
}

float SlicingPreviewBridge::totalTime() const {
    return getCurrentPreview() ? getCurrentPreview()->getTotalTime() : 0.0f;
}

float SlicingPreviewBridge::totalExtrusion() const {
    return getCurrentPreview() ? getCurrentPreview()->getTotalExtrusion() : 0.0f;
}

float SlicingPreviewBridge::totalPrintDistance() const {
    return getCurrentPreview() ? getCurrentPreview()->getTotalPrintDistance() : 0.0f;
}

float SlicingPreviewBridge::totalTravelDistance() const {
    return getCurrentPreview() ? getCurrentPreview()->getTotalTravelDistance() : 0.0f;
}

int SlicingPreviewBridge::colorMode() const {
    return getCurrentPreview()
        ? static_cast<int>(getCurrentPreview()->getColorMode())
        : static_cast<int>(GPlatform::ToolpathPreviewColorMode::PreviewColor);
}

void SlicingPreviewBridge::setColorMode(int mode) {
    if (auto preview = getCurrentPreview()) {
        const auto colorMode = static_cast<GPlatform::ToolpathPreviewColorMode>(mode);
        if (preview->getColorMode() == colorMode) {
            return;
        }
        preview->setColorMode(colorMode);
        emit colorModeChanged();
    }
}

bool SlicingPreviewBridge::showTravel() const {
    return getCurrentPreview() ? getCurrentPreview()->getShowTravel() : false;
}

void SlicingPreviewBridge::setShowTravel(bool show) {
    if (auto preview = getCurrentPreview()) {
        if (preview->getShowTravel() == show) {
            return;
        }
        preview->setShowTravel(show);
        emit pathVisibilityChanged();
        emit previewLegendItemsChanged();
    }
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    SlicingPreviewBridge, "SlicingPreviewBridge", &SlicingPreviewBridge::create)
