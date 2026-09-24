#include "ModelColorPaintBridge.hpp"
#include "BridgeRegistration.hpp"
#include "ActionManager.hpp"
#include "SelectionBridge.hpp"
#include "SliceSettingsBridge.hpp"
#include "Foundation/Log.h"
#include <QColor>
#include <algorithm>

ModelColorPaintBridge::ModelColorPaintBridge(QObject* parent)
    : bridge::BridgeBase(parent),
      m_palette{QStringLiteral("#e53935"), QStringLiteral("#fdd835"),
                QStringLiteral("#43a047"), QStringLiteral("#1e88e5"),
                QStringLiteral("#8e24aa")} {
    auto* settings = SliceSettingsBridge::instance();
    connect(settings, &SliceSettingsBridge::filamentSlotsChanged,
            this, &ModelColorPaintBridge::syncPaletteFromFilamentSlots);
    syncPaletteFromFilamentSlots();
}

ModelColorPaintBridge* ModelColorPaintBridge::instance() {
    static auto* bridge = new ModelColorPaintBridge();
    return bridge;
}

ModelColorPaintBridge* ModelColorPaintBridge::create(QQmlEngine*, QJSEngine*) {
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

void ModelColorPaintBridge::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
}

void ModelColorPaintBridge::setCommitState(bool finishing, bool pending) {
    if (m_finishing == finishing && m_commitPending == pending) return;
    m_finishing = finishing;
    m_commitPending = pending;
    emit commitStateChanged();
}

void ModelColorPaintBridge::setCurrentColorIndex(int index) {
    index = std::clamp(index, 0, static_cast<int>(m_palette.size()));
    if (m_currentColorIndex == index) return;
    m_currentColorIndex = index;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("colorIndex"), index}});
}

void ModelColorPaintBridge::setBrushSize(double size) {
    size = std::clamp(size, 0.5, 100.0);
    if (qFuzzyCompare(m_brushSize, size)) return;
    m_brushSize = size;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("brushSize"), size}});
}

void ModelColorPaintBridge::setBrushShape(const QString& shape) {
    const QString normalized = shape.trimmed().toLower();
    if (m_brushShape == normalized) return;
    m_brushShape = normalized;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("brushShape"), normalized}});
}

void ModelColorPaintBridge::setEdgeDetection(bool enabled) {
    if (m_edgeDetection == enabled) return;
    m_edgeDetection = enabled;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("edgeDetection"), enabled}});
}

void ModelColorPaintBridge::setSmartFillAngle(double angle) {
    angle = std::clamp(angle, 0.0, 90.0);
    if (qFuzzyCompare(m_smartFillAngle, angle)) return;
    m_smartFillAngle = angle;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("smartFillAngle"), angle}});
}

void ModelColorPaintBridge::setHeightRange(double height) {
    height = std::clamp(height, 0.1, 8.0);
    if (qFuzzyCompare(m_heightRange, height)) return;
    m_heightRange = height;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("heightRange"), height}});
}

void ModelColorPaintBridge::setGapArea(double area) {
    area = std::clamp(area, 0.0, 5.0);
    if (qFuzzyCompare(m_gapArea, area)) return;
    m_gapArea = area;
    emit settingsChanged();
    emit applySettingsRequested({{QStringLiteral("gapArea"), area}});
}

void ModelColorPaintBridge::setGapPreviewStatus(bool pending, int candidateCount) {
    candidateCount = std::max(0, candidateCount);
    if (m_gapPreviewPending == pending && m_gapCandidateCount == candidateCount) return;
    m_gapPreviewPending = pending;
    m_gapCandidateCount = candidateCount;
    emit gapPreviewChanged();
}

void ModelColorPaintBridge::setVerticalOnly(bool enabled) {
    if (m_verticalOnly == enabled && (!enabled || !m_horizontalOnly)) return;
    m_verticalOnly = enabled;
    if (enabled) m_horizontalOnly = false;
    emit settingsChanged();
    emit applySettingsRequested({
        {QStringLiteral("verticalOnly"), m_verticalOnly},
        {QStringLiteral("horizontalOnly"), m_horizontalOnly}});
}

void ModelColorPaintBridge::setHorizontalOnly(bool enabled) {
    if (m_horizontalOnly == enabled && (!enabled || !m_verticalOnly)) return;
    m_horizontalOnly = enabled;
    if (enabled) m_verticalOnly = false;
    emit settingsChanged();
    emit applySettingsRequested({
        {QStringLiteral("verticalOnly"), m_verticalOnly},
        {QStringLiteral("horizontalOnly"), m_horizontalOnly}});
}

void ModelColorPaintBridge::setPalette(const QVariantList& palette) {
    // A model stores only facet labels. Once an active libslicer configuration
    // exists, its filament slots are the color truth and a per-model palette
    // must not replace it. This fallback is retained only for startup without
    // an available slicing profile.
    if (!SliceSettingsBridge::instance()->filamentSlots().isEmpty()) {
        syncPaletteFromFilamentSlots();
        return;
    }
    if (m_palette == palette) return;
    m_palette = palette;
    emit paletteChanged();
}

QVariantList ModelColorPaintBridge::filamentSlots() const {
    return SliceSettingsBridge::instance()->filamentSlots();
}

void ModelColorPaintBridge::setDefaultFilamentSlot(int slot) {
    slot = m_palette.isEmpty()
        ? 0
        : std::clamp(slot, 1, static_cast<int>(m_palette.size()));
    if (m_defaultFilamentSlot == slot) return;
    setDefaultFilamentSlotProjection(slot);
    emit defaultFilamentSlotRequested(slot);
}

void ModelColorPaintBridge::setDefaultFilamentSlotProjection(int slot) {
    slot = m_palette.isEmpty()
        ? 0
        : std::clamp(slot, 1, static_cast<int>(m_palette.size()));
    if (m_defaultFilamentSlot == slot) return;
    m_defaultFilamentSlot = slot;
    emit defaultFilamentSlotChanged();
}

void ModelColorPaintBridge::syncPaletteFromFilamentSlots() {
    const QVariantList slotItems = SliceSettingsBridge::instance()->filamentSlots();
    if (slotItems.isEmpty()) return;

    QVariantList palette;
    palette.reserve(slotItems.size());
    for (const QVariant& value : slotItems) {
        const QColor color = value.toMap().value(QStringLiteral("color")).value<QColor>();
        palette.push_back(color.isValid()
            ? color.name(QColor::HexArgb)
            : QStringLiteral("#ffb0bec5"));
    }
    const bool paletteWasChanged = m_palette != palette;
    m_palette = std::move(palette);

    const int paletteSize = static_cast<int>(m_palette.size());
    const int clampedIndex = std::clamp(m_currentColorIndex, 0, paletteSize);
    if (m_currentColorIndex != clampedIndex) {
        m_currentColorIndex = clampedIndex;
        emit settingsChanged();
        emit applySettingsRequested({{QStringLiteral("colorIndex"), clampedIndex}});
    }
    const int clampedDefault = std::clamp(m_defaultFilamentSlot, 1, paletteSize);
    if (m_defaultFilamentSlot != clampedDefault) {
        m_defaultFilamentSlot = clampedDefault;
        emit defaultFilamentSlotChanged();
        emit defaultFilamentSlotRequested(clampedDefault);
    }
    if (paletteWasChanged)
        emit paletteChanged();
    emit filamentSlotsChanged();
}

bool ModelColorPaintBridge::enterSelectedModel() {
    const DBInstanceID modelId = SelectionBridge::instance()->selectedId();
    if (!modelId.isValid()) {
        LOG_WARN("ModelColorPaintBridge: cannot enter painting without a selected model");
        return false;
    }
    const bool accepted = ActionManager::getInstance()->triggerAction(
        QStringLiteral("model.color.paint.enter"),
        QVariantMap{{QStringLiteral("modelId"),
                     QVariant::fromValue<qulonglong>(modelId.getValue())}});
    return accepted && m_active;
}

bool ModelColorPaintBridge::leavePainting() {
    if (!m_active) return true;
    emit finishRequested();
    return m_finishing || !m_active;
}

bool ModelColorPaintBridge::applySettings(const QVariantMap& settings) {
    emit applySettingsRequested(settings);
    return true;
}

bool ModelColorPaintBridge::setPaletteColor(int index, const QString& color) {
    const QColor parsed(color);
    if (index < 1 || index > m_palette.size() || !parsed.isValid()) return false;
    return SliceSettingsBridge::instance()->setFilamentColor(index - 1, parsed);
}

bool ModelColorPaintBridge::clearPainting() {
    emit clearPaintingRequested();
    return true;
}

bool ModelColorPaintBridge::performGapFill() {
    emit performGapFillRequested();
    return true;
}

bool ModelColorPaintBridge::remapFilaments(const QVariantList& mapping) {
    if (mapping.size() != m_palette.size()) return false;
    emit remapFilamentsRequested(mapping);
    return true;
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    ModelColorPaintBridge, "ModelColorPaintBridge", &ModelColorPaintBridge::create)
