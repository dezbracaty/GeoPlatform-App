#include "SliceSettingsBridge.hpp"
#include "BridgeRegistration.hpp"

#include "Foundation/Log.h"
#include "DocumentManager.hpp"
#include "SlicingConfigDB.hpp"
#include "SlicingBackend.hpp"
#include "TransactionManager.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
    constexpr auto kMachineModelSetting = "slicing/selection/machineModelId";
    constexpr auto kMachineVariantSetting = "slicing/selection/machineVariantId";
    constexpr auto kProcessPresetSetting = "slicing/selection/processPresetId";
    constexpr auto kFilamentPresetsSetting = "slicing/selection/filamentPresetIds";
    constexpr auto kFilamentToolsSetting = "slicing/selection/filamentPhysicalTools";

    bool selectionPersistenceEnabled() {
        return qEnvironmentVariableIntValue("GPLATFORM_JOURNAL_SESSION") == 0;
    }

    libslicer::ConfigSelection restoredSelection() {
        libslicer::ConfigSelection selection;
        if (!selectionPersistenceEnabled()) {
            return selection;
        }

        QSettings settings;
        selection.machine_model_id = settings.value(kMachineModelSetting).toString().toStdString();
        selection.machine_variant_id = settings.value(kMachineVariantSetting).toString().toStdString();
        selection.process_preset_id = settings.value(kProcessPresetSetting).toString().toStdString();
        const QStringList filaments = settings.value(kFilamentPresetsSetting).toStringList();
        selection.filament_preset_ids.reserve(static_cast<std::size_t>(filaments.size()));
        for (const QString& filament : filaments) {
            if (!filament.isEmpty()) {
                selection.filament_preset_ids.push_back(filament.toStdString());
            }
        }
        const auto tools = settings.value(kFilamentToolsSetting).toList();
        for (const auto& tool : tools) selection.filament_physical_tools.push_back(tool.toUInt());
        return selection;
    }

    void persistSelection(const libslicer::ResolvedSelection& selection) {
        if (!selectionPersistenceEnabled()) {
            return;
        }

        QStringList filaments;
        filaments.reserve(static_cast<qsizetype>(selection.filament_preset_ids.size()));
        for (const std::string& filament : selection.filament_preset_ids) {
            filaments.push_back(QString::fromStdString(filament));
        }
        QSettings settings;
        settings.setValue(kMachineModelSetting,
                          QString::fromStdString(selection.machine_model_id));
        settings.setValue(kMachineVariantSetting,
                          QString::fromStdString(selection.machine_variant_id));
        settings.setValue(kProcessPresetSetting,
                          QString::fromStdString(selection.process_preset_id));
        settings.setValue(kFilamentPresetsSetting, filaments);
        QVariantList tools;
        for (unsigned tool : selection.filament_physical_tools) tools.push_back(tool);
        settings.setValue(kFilamentToolsSetting, tools);
    }

    QVariantList toolCapacityVariant(const libslicer::MachineVariantOption& variant) {
        QVariantList result;
        for (int capacity : variant.toolhead_filament_capacity) result.push_back(capacity);
        return result;
    }

    QVariantList printableAreaVariant(const libslicer::MachineVariantOption& variant) {
        QVariantList points;
        points.reserve(static_cast<qsizetype>(variant.printable_area.size()));
        for (const auto& point : variant.printable_area) {
            points.push_back(QVariantMap{
                {QStringLiteral("x"), point.x},
                {QStringLiteral("y"), point.y}
            });
        }
        return points;
    }

    std::pair<double, double> printableAreaOrigin(
        const libslicer::MachineVariantOption& variant) {
        if (variant.printable_area.empty()) {
            return {0.0, 0.0};
        }
        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        for (const auto& point : variant.printable_area) {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
        }
        return {minX, minY};
    }

    bool configurationMutationBlocked(const char* operation) {
        if (GPlatform::SlicingBackend::instance().configurationChangesAllowed()) {
            return false;
        }
        LOG_WARN("SliceSettingsBridge: rejected {} while the slicing backend is busy", operation);
        return true;
    }

    QString translatedSettingText(const QString& source) {
        const QByteArray utf8 = source.toUtf8();
        return QCoreApplication::translate("LibSlicerSettings", utf8.constData());
    }

    QString groupTab(libslicer::SettingGroup group) {
        switch (group) {
            case libslicer::SettingGroup::Filament:
                return QStringLiteral("filament");
            case libslicer::SettingGroup::Printer:
                return QStringLiteral("printer");
            default:
                return QStringLiteral("process");
        }
    }

    QString modeName(libslicer::SettingLevel level) {
        switch (level) {
            case libslicer::SettingLevel::Advanced:
                return QStringLiteral("advanced");
            case libslicer::SettingLevel::Expert:
            case libslicer::SettingLevel::Developer:
                return QStringLiteral("expert");
            default:
                return QStringLiteral("basic");
        }
    }

    QString pageId(QString category) {
        category = category.trimmed().toLower();
        category.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        category.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
        return category.isEmpty() ? QStringLiteral("other") : category;
    }

    int tabOrder(const QString& tab) {
        if (tab == QStringLiteral("process"))
            return 0;
        if (tab == QStringLiteral("filament"))
            return 1;
        if (tab == QStringLiteral("printer"))
            return 2;
        return 100;
    }

    int categoryOrder(const QString& tab, const QString& category) {
        const QString name = category.trimmed().toLower();
        if (tab == QStringLiteral("process")) {
            if (name.contains(QStringLiteral("quality")))
                return 0;
            if (name.contains(QStringLiteral("strength")))
                return 10;
            if (name.contains(QStringLiteral("continuous fiber")))
                return 15;
            if (name.contains(QStringLiteral("speed")))
                return 20;
            if (name.contains(QStringLiteral("support")))
                return 30;
            if (name.contains(QStringLiteral("skirt")) || name.contains(QStringLiteral("brim")))
                return 40;
            if (name.contains(QStringLiteral("flush")))
                return 50;
            if (name.contains(QStringLiteral("advanced")))
                return 90;
        } else if (tab == QStringLiteral("filament")) {
            if (name.contains(QStringLiteral("filament")))
                return 0;
            if (name.contains(QStringLiteral("temperature")))
                return 10;
            if (name.contains(QStringLiteral("cool")))
                return 20;
            if (name.contains(QStringLiteral("advanced")))
                return 90;
        } else if (tab == QStringLiteral("printer")) {
            if (name.contains(QStringLiteral("basic")))
                return 0;
            if (name.contains(QStringLiteral("machine")))
                return 10;
            if (name.contains(QStringLiteral("extruder")))
                return 20;
            if (name.contains(QStringLiteral("multi")))
                return 30;
        }
        return 1000;
    }

    QString typeName(libslicer::SettingType type) {
        switch (type) {
            case libslicer::SettingType::Boolean:
                return QStringLiteral("bool");
            case libslicer::SettingType::Integer:
                return QStringLiteral("int");
            case libslicer::SettingType::Float:
            case libslicer::SettingType::Percent:
                return QStringLiteral("float");
            case libslicer::SettingType::Enum:
                return QStringLiteral("enum");
            default:
                return QStringLiteral("string");
        }
    }

    QString controlName(const libslicer::SettingItem& item) {
        if (item.multiline)
            return QStringLiteral("multiline");
        switch (item.type) {
            case libslicer::SettingType::Boolean:
                return QStringLiteral("switch");
            case libslicer::SettingType::Integer:
            case libslicer::SettingType::Float:
            case libslicer::SettingType::Percent:
                return QStringLiteral("number");
            case libslicer::SettingType::Enum:
                return QStringLiteral("combo");
            default:
                return QStringLiteral("text");
        }
    }

    GPlatform::SlicingConfigActivationResult bridgeActivationResult(
        libslicer::ActiveConfigView&& view) {
        GPlatform::SlicingConfigActivationResult result;
        result.success = view.valid();
        result.revision = view.revision;
        result.selection = std::move(view.selection);
        result.compatibleProcesses = std::move(view.compatible_processes);
        result.compatibleFilaments = std::move(view.compatible_filaments);
        result.settings = std::move(view.settings);
        result.filamentSlots = std::move(view.filament_slots);
        return result;
    }

    GPlatform::SlicingConfigActivationResult bridgeActivationResult(
        libslicer::ConfigActivationResult&& activated) {
        if (!activated) {
            GPlatform::SlicingConfigActivationResult result;
            result.diagnostics = std::move(activated.diagnostics);
            return result;
        }
        return bridgeActivationResult(std::move(activated.view));
    }

    std::vector<GPlatform::SlicingConfigDB::Filament> documentFilaments(
        const std::vector<libslicer::FilamentSlotInfo>& filamentSlots) {
        std::vector<GPlatform::SlicingConfigDB::Filament> result;
        result.reserve(filamentSlots.size());
        for (const auto& slot : filamentSlots) {
            result.push_back({
                slot.index,
                slot.preset_id,
                slot.preset_name,
                slot.vendor,
                slot.material_type,
                QColor(slot.color.red,
                       slot.color.green,
                       slot.color.blue,
                       slot.color.alpha),
                slot.diameter_mm
            });
        }
        return result;
    }

    std::shared_ptr<GPlatform::SlicingConfigDB> activeSlicingConfigDB() {
        auto* document = DocumentManager::instance();
        if (!document) return {};
        for (const auto& object :
             document->getDBInstancesByType(TypeID::SLICING_CONFIG_DB)) {
            if (auto config =
                    std::dynamic_pointer_cast<GPlatform::SlicingConfigDB>(object)) {
                return config;
            }
        }
        return GPlatform::SlicingConfigDB::createDefault();
    }

    void publishActiveSlicingConfig(
        std::uint64_t revision,
        const std::vector<libslicer::FilamentSlotInfo>& filamentSlots) {
        if (filamentSlots.empty()) return;
        // The configuration projection includes its initial object creation;
        // neither creation nor refresh belongs in document Undo/Redo.
        TransientUpdateGuard guard;
        const auto config = activeSlicingConfigDB();
        if (!config) return;

        config->setFilaments(documentFilaments(filamentSlots));
        config->setActiveRevision(revision);
    }
} // namespace

SliceSettingsBridge::SliceSettingsBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    auto& backend = GPlatform::SlicingBackend::instance();
    if (!backend.available()) {
        LOG_ERROR("SliceSettingsBridge: slicing backend is unavailable");
        return;
    }

    libslicer::ConfigSelection selection = restoredSelection();
    if (selection.machine_model_id.empty() || selection.machine_variant_id.empty()) {
        selection.machine_model_id = "Flashforge AD5X";
        selection.machine_variant_id = "0.4";
    }
    auto result = backend.activateConfig(selection);
    if (!result && (!selection.process_preset_id.empty() ||
                    !selection.filament_preset_ids.empty())) {
        LOG_WARN("SliceSettingsBridge: saved process or filament selection is no longer compatible; selecting compatible defaults");
        selection.process_preset_id.clear();
        selection.filament_preset_ids.clear();
        result = backend.activateConfig(selection);
    }
    if (!result && selection.machine_model_id != "Flashforge AD5X") {
        LOG_WARN("SliceSettingsBridge: saved machine '{} / {}' is unavailable; using fallback machine",
                 selection.machine_model_id, selection.machine_variant_id);
        selection = {};
        selection.machine_model_id = "Flashforge AD5X";
        selection.machine_variant_id = "0.4";
        result = backend.activateConfig(selection);
    }
    if (!replaceConfig(std::move(result), false)) {
        LOG_ERROR("SliceSettingsBridge: failed to restore or select fallback machine '{} / {}'",
                  selection.machine_model_id, selection.machine_variant_id);
    }
}

SliceSettingsBridge* SliceSettingsBridge::instance() {
    static SliceSettingsBridge* bridge = new SliceSettingsBridge();
    return bridge;
}

SliceSettingsBridge* SliceSettingsBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

QVariantMap SliceSettingsBridge::settings() const {
    QReadLocker locker(&m_lock);
    QVariantMap result;
    for (const auto& item : m_items) {
        if (item.visible) {
            result.insert(QString::fromStdString(item.key), displayValueLocked(item, item.value));
        }
    }
    return result;
}

void SliceSettingsBridge::setSettings(const QVariantMap& settings) {
    if (configurationMutationBlocked("settings patch"))
        return;

    std::vector<std::pair<std::string, std::string>> patch;
    {
        QReadLocker locker(&m_lock);
        patch.reserve(static_cast<std::size_t>(settings.size()));
        for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
            const auto key = it.key().toStdString();
            const auto* item = findItemLocked(key);
            if (item != nullptr)
                patch.emplace_back(key, serializeValueLocked(*item, it.value()));
        }
    }

    bool pendingChanged = false;
    bool schemaChanged = false;
    bool valuesChanged = false;
    {
        QWriteLocker locker(&m_lock);
        const auto result = GPlatform::SlicingBackend::instance().applyConfigPatch(patch);
        if (!result) {
            const auto message = result.diagnostics.empty() ? std::string("invalid configuration patch")
                                                            : result.diagnostics.front().message;
            LOG_WARN("SliceSettingsBridge: rejected settings patch: {}", message);
            return;
        }
        valuesChanged = !result.changed_items.empty();
        schemaChanged = applyChangedItemsLocked(result.changed_items);
        if (valuesChanged)
            setPendingLocked(&pendingChanged);
    }
    if (valuesChanged)
        emit settingsChanged();
    if (schemaChanged)
        emit this->schemaChanged();
    if (pendingChanged)
        emit hasPendingResliceChanged();
}

QVariant SliceSettingsBridge::getSetting(const QString& key, const QVariant& defaultValue) const {
    QReadLocker locker(&m_lock);
    const auto* item = findItemLocked(key.toStdString());
    return item != nullptr && item->visible ? displayValueLocked(*item, item->value) : defaultValue;
}

void SliceSettingsBridge::setSetting(const QString& key, const QVariant& value) {
    if (configurationMutationBlocked("setting change"))
        return;

    QVariant canonicalDisplay;
    bool pendingChanged = false;
    bool schemaChanged = false;
    bool valueChanged = false;
    {
        QWriteLocker locker(&m_lock);
        const auto* item = findItemLocked(key.toStdString());
        if (item == nullptr) {
            LOG_WARN("SliceSettingsBridge: unknown setting '{}'", key.toStdString());
            return;
        }
        const auto result = GPlatform::SlicingBackend::instance().setConfigValue(
            item->key, serializeValueLocked(*item, value));
        if (!result) {
            const auto message = result.diagnostics.empty() ? std::string("invalid value")
                                                            : result.diagnostics.front().message;
            LOG_WARN("SliceSettingsBridge: rejected '{}': {}", key.toStdString(), message);
            return;
        }
        valueChanged = !result.changed_items.empty();
        schemaChanged = applyChangedItemsLocked(result.changed_items);
        const auto* canonical = findItemLocked(key.toStdString());
        canonicalDisplay = canonical == nullptr ? value
                                                : displayValueLocked(*canonical, canonical->value);
        if (valueChanged)
            setPendingLocked(&pendingChanged);
    }
    if (valueChanged) {
        emit settingChanged(key, canonicalDisplay);
        emit settingsChanged();
    }
    if (schemaChanged)
        emit this->schemaChanged();
    if (pendingChanged)
        emit hasPendingResliceChanged();
}

void SliceSettingsBridge::ensureSetting(const QString& key, const QVariant& value) {
    Q_UNUSED(value)
    if (!hasSetting(key))
        setSetting(key, value);
}

bool SliceSettingsBridge::hasSetting(const QString& key) const {
    QReadLocker locker(&m_lock);
    const auto* item = findItemLocked(key.toStdString());
    return item != nullptr && item->visible;
}

void SliceSettingsBridge::removeSetting(const QString& key) {
    if (configurationMutationBlocked("setting reset"))
        return;

    bool pendingChanged = false;
    bool schemaChanged = false;
    bool valueChanged = false;
    {
        QWriteLocker locker(&m_lock);
        const auto result = GPlatform::SlicingBackend::instance().resetConfigValue(key.toStdString());
        if (!result)
            return;
        valueChanged = !result.changed_items.empty();
        schemaChanged = applyChangedItemsLocked(result.changed_items);
        if (valueChanged)
            setPendingLocked(&pendingChanged);
    }
    if (valueChanged)
        emit settingsChanged();
    if (schemaChanged)
        emit this->schemaChanged();
    if (pendingChanged)
        emit hasPendingResliceChanged();
}

void SliceSettingsBridge::resetToDefaults() {
    if (configurationMutationBlocked("configuration reset"))
        return;

    auto& backend = GPlatform::SlicingBackend::instance();
    if (!backend.available())
        return;
    libslicer::ConfigSelection selection;
    {
        QReadLocker locker(&m_lock);
        selection.machine_model_id = m_selection.machine_model_id;
        selection.machine_variant_id = m_selection.machine_variant_id;
        selection.process_preset_id = m_selection.process_preset_id;
        selection.filament_preset_ids = m_selection.filament_preset_ids;
    }
    replaceConfig(backend.activateConfig(selection), true);
}

QVariantList SliceSettingsBridge::machineModels() const {
    QVariantList models;
    for (const auto& machine : GPlatform::SlicingBackend::instance().machineModels()) {
        QVariantList variants;
        for (const auto& variant : machine.variants) {
            const auto [originX, originY] = printableAreaOrigin(variant);
            variants.push_back(QVariantMap{
                {QStringLiteral("id"), QString::fromStdString(variant.id)},
                {QStringLiteral("name"), QString::fromStdString(variant.name)},
                {QStringLiteral("nozzleDiameter"), variant.nozzle_diameter},
                {QStringLiteral("printableWidth"), variant.printable_width},
                {QStringLiteral("printableDepth"), variant.printable_depth},
                {QStringLiteral("printableHeight"), variant.printable_height},
                {QStringLiteral("physicalToolCount"), static_cast<qulonglong>(variant.physical_tool_count)},
                {QStringLiteral("variableFilamentSlots"), variant.variable_filament_slots},
                {QStringLiteral("filamentSlotsBoundToPhysicalTools"), variant.filament_slots_bound_to_physical_tools},
                {QStringLiteral("maxFilamentSlots"), static_cast<qulonglong>(variant.max_filament_slots)},
                {QStringLiteral("toolMaterialCapacities"), toolCapacityVariant(variant)},
                {QStringLiteral("printableArea"), printableAreaVariant(variant)},
                {QStringLiteral("originX"), originX},
                {QStringLiteral("originY"), originY},
                {QStringLiteral("printerPresetId"), QString::fromStdString(variant.printer_preset_id)}
            });
        }
        models.push_back(QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(machine.id)},
            {QStringLiteral("vendorId"), QString::fromStdString(machine.vendor_id)},
            {QStringLiteral("name"), QString::fromStdString(machine.name)},
            {QStringLiteral("family"), QString::fromStdString(machine.family)},
            {QStringLiteral("coverImageUrl"), QUrl::fromLocalFile(QString::fromStdString(machine.cover_image_path))},
            {QStringLiteral("bedModelPath"), QString::fromStdString(machine.bed_model_path)},
            {QStringLiteral("bedTexturePath"), QString::fromStdString(machine.bed_texture_path)},
            {QStringLiteral("variants"), variants}
        });
    }
    return models;
}

QVariantList SliceSettingsBridge::buildPlateOptions() const {
    QVariantList plates;
    for (const auto& plate : GPlatform::SlicingBackend::instance().buildPlateOptions()) {
        plates.push_back(QVariantMap{
            {QStringLiteral("value"), QString::fromStdString(plate.value)},
            {QStringLiteral("name"), QString::fromStdString(plate.name)},
            {QStringLiteral("imageUrl"), QUrl::fromLocalFile(QString::fromStdString(plate.image_path))}
        });
    }
    return plates;
}

bool SliceSettingsBridge::selectMachine(const QString& modelId, const QString& variantId) {
    if (configurationMutationBlocked("machine selection"))
        return false;

    auto& backend = GPlatform::SlicingBackend::instance();
    if (!backend.available())
        return false;
    libslicer::ConfigSelection selection;
    selection.machine_model_id = modelId.toStdString();
    selection.machine_variant_id = variantId.toStdString();
    return replaceConfig(backend.activateConfig(selection), true);
}

bool SliceSettingsBridge::selectProcessPreset(const QString& presetId) {
    if (configurationMutationBlocked("process preset selection"))
        return false;

    auto& backend = GPlatform::SlicingBackend::instance();
    if (!backend.available())
        return false;

    libslicer::ConfigSelection selection;
    {
        QReadLocker locker(&m_lock);
        selection.machine_model_id = m_selection.machine_model_id;
        selection.machine_variant_id = m_selection.machine_variant_id;
        selection.process_preset_id = presetId.toStdString();
        selection.filament_preset_ids = m_selection.filament_preset_ids;
    }

    auto result = backend.activateConfig(selection);
    if (!result) {
        LOG_WARN("SliceSettingsBridge: current filament is not compatible with process '{}'; "
                 "selecting a compatible filament", selection.process_preset_id);
        selection.filament_preset_ids.clear();
        result = backend.activateConfig(selection);
    }
    return replaceConfig(std::move(result), true);
}

bool SliceSettingsBridge::selectFilamentPreset(const QString& presetId) {
    return selectFilamentPresetForSlot(0, presetId);
}

bool SliceSettingsBridge::selectFilamentPresetForSlot(
    int slotIndex, const QString& presetId) {
    if (configurationMutationBlocked("filament preset selection"))
        return false;

    auto& backend = GPlatform::SlicingBackend::instance();
    if (!backend.available() || slotIndex < 0)
        return false;
    return replaceConfig(
        bridgeActivationResult(backend.setFilamentPreset(
            static_cast<std::size_t>(slotIndex), presetId.toStdString())),
        true);
}

bool SliceSettingsBridge::setFilamentToolMapping(const QVariantList& tools, bool automatic) {
    if (configurationMutationBlocked("filament tool mapping"))
        return false;

    std::string mapping;
    for (const QVariant& tool : tools) {
        if (!mapping.empty())
            mapping += ',';
        mapping += std::to_string(tool.toInt());
    }
    const auto result = GPlatform::SlicingBackend::instance().applyConfigPatch({
        {"filament_map_mode", automatic ? "Auto For Flush" : "Manual"},
        {"filament_map", mapping}
    });
    if (!result) {
        const QString message = result.diagnostics.empty()
            ? tr("Unable to apply tool mapping")
            : QString::fromStdString(result.diagnostics.front().message);
        setConfigurationError(message);
        LOG_WARN("SliceSettingsBridge: rejected filament tool mapping: {}",
                 message.toStdString());
        return false;
    }
    setConfigurationError({});
    return refreshActiveConfig();
}

bool SliceSettingsBridge::setFilamentSlotCount(int slotCount) {
    if (configurationMutationBlocked("filament slot count") || slotCount < 1)
        return false;
    int previousCount = 0;
    {
        QReadLocker locker(&m_lock);
        previousCount = static_cast<int>(m_filamentSlots.size());
    }
    if (!replaceConfig(
        bridgeActivationResult(GPlatform::SlicingBackend::instance()
            .resizeFilamentSlots(static_cast<std::size_t>(slotCount))),
        true)) {
        return false;
    }

    // Newly added slots receive stable, distinguishable display colors, but
    // each write still goes through libslicer and immediately returns as the
    // canonical active configuration.
    static const QColor addedSlotColors[] = {
        QColor(QStringLiteral("#F9734D")), QColor(QStringLiteral("#2474D8")),
        QColor(QStringLiteral("#43A047")), QColor(QStringLiteral("#8E24AA"))};
    for (int index = previousCount; index < slotCount; ++index) {
        if (!setFilamentColor(
                index, addedSlotColors[index % std::size(addedSlotColors)])) {
            return false;
        }
    }
    return true;
}

bool SliceSettingsBridge::addFilamentToTool(int physicalTool) {
    if (configurationMutationBlocked("add material to physical tool") || physicalTool < 0)
        return false;
    return replaceConfig(bridgeActivationResult(GPlatform::SlicingBackend::instance()
        .addFilamentToTool(static_cast<std::size_t>(physicalTool))), true);
}

bool SliceSettingsBridge::setFilamentColor(int slotIndex, const QColor& color) {
    if (configurationMutationBlocked("filament color selection") ||
        slotIndex < 0 || !color.isValid()) {
        return false;
    }

    auto& backend = GPlatform::SlicingBackend::instance();
    const auto result = backend.setFilamentColor(
        static_cast<std::size_t>(slotIndex),
        {static_cast<std::uint8_t>(color.red()),
         static_cast<std::uint8_t>(color.green()),
         static_cast<std::uint8_t>(color.blue()),
         static_cast<std::uint8_t>(color.alpha())});
    if (!result) {
        const QString message = result.diagnostics.empty()
            ? tr("Unable to change filament color")
            : QString::fromStdString(result.diagnostics.front().message);
        setConfigurationError(message);
        LOG_WARN("SliceSettingsBridge: rejected filament slot {} color: {}",
                 slotIndex, message.toStdString());
        return false;
    }
    const auto active = backend.activeConfigView();
    if (!active) {
        setConfigurationError(tr("The active filament configuration is unavailable"));
        return false;
    }

    bool pendingChanged = false;
    bool schemaChanged = false;
    {
        QWriteLocker locker(&m_lock);
        schemaChanged = applyChangedItemsLocked(result.changed_items);
        m_filamentSlots = filamentSlotItems(active->filament_slots);
        setPendingLocked(&pendingChanged);
    }
    publishActiveSlicingConfig(active->revision, active->filament_slots);
    setConfigurationError({});
    emit filamentSlotsChanged();
    emit settingsChanged();
    if (schemaChanged)
        emit this->schemaChanged();
    if (pendingChanged)
        emit hasPendingResliceChanged();
    return true;
}

bool SliceSettingsBridge::refreshActiveConfig() {
    auto active = GPlatform::SlicingBackend::instance().activeConfigView();
    if (!active) {
        LOG_ERROR("SliceSettingsBridge: libslicer has no active configuration after project import");
        return false;
    }
    return replaceConfig(bridgeActivationResult(std::move(*active)), true);
}

void SliceSettingsBridge::markCurrentAsSliced() {
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        changed = m_hasPendingReslice;
        m_hasPendingReslice = false;
    }
    if (changed)
        emit hasPendingResliceChanged();
}

void SliceSettingsBridge::markNeedsReslice() {
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        setPendingLocked(&changed);
    }
    if (changed)
        emit hasPendingResliceChanged();
}

QVariantMap SliceSettingsBridge::getSettingSchema(const QString& key) const {
    QReadLocker locker(&m_lock);
    return m_schemaByKey.value(key);
}

QVariantList SliceSettingsBridge::getTabPages(const QString& tab, bool showAdvancedMode) const {
    QReadLocker locker(&m_lock);
    QVariantList pages;
    const QString target = tab.trimmed().toLower();
    for (const QVariant& value : m_schemaPages) {
        const QVariantMap page = value.toMap();
        if (page.value(QStringLiteral("tab")).toString() != target)
            continue;
        if (!showAdvancedMode && page.value(QStringLiteral("mode")).toString() != QStringLiteral("basic"))
            continue;
        pages.push_back(QVariantMap{
            {QStringLiteral("id"),    page.value(QStringLiteral("page")) },
            {QStringLiteral("title"), page.value(QStringLiteral("title"))},
            {QStringLiteral("order"), page.value(QStringLiteral("order"))}
        });
    }
    std::sort(pages.begin(), pages.end(), [](const QVariant& left, const QVariant& right) {
        const auto l = left.toMap();
        const auto r = right.toMap();
        if (l.value(QStringLiteral("order")) == r.value(QStringLiteral("order")))
            return l.value(QStringLiteral("title")).toString() < r.value(QStringLiteral("title")).toString();
        return l.value(QStringLiteral("order")).toInt() < r.value(QStringLiteral("order")).toInt();
    });
    return pages;
}

QVariantList SliceSettingsBridge::getPageSchema(const QString& tab, const QString& page,
                                                bool showAdvancedMode, const QString& searchText) const {
    QReadLocker locker(&m_lock);
    QVariantList rows;
    const QString needle = searchText.trimmed().toLower();
    for (const QVariant& value : m_schemaSettings) {
        const QVariantMap definition = value.toMap();
        if (definition.value(QStringLiteral("tab")).toString() != tab.trimmed().toLower() ||
            definition.value(QStringLiteral("page")).toString() != page.trimmed().toLower() ||
            !isVisibleInMode(definition, showAdvancedMode))
            continue;
        const QString label = definition.value(QStringLiteral("label")).toString();
        const QString description = definition.value(QStringLiteral("description")).toString();
        const QString haystack = definition.value(QStringLiteral("key")).toString() + QLatin1Char(' ') + label +
                                 QLatin1Char(' ') + description + QLatin1Char(' ') + translatedSettingText(label) +
                                 QLatin1Char(' ') + translatedSettingText(description);
        if (!needle.isEmpty() && !haystack.toLower().contains(needle))
            continue;
        rows.push_back(rowForDefinitionLocked(definition));
    }
    std::sort(rows.begin(), rows.end(), [](const QVariant& left, const QVariant& right) {
        return left.toMap().value(QStringLiteral("label")).toString() <
               right.toMap().value(QStringLiteral("label")).toString();
    });
    if (rows.isEmpty())
        return {};
    const QVariantMap first = rows.first().toMap();
    return {
        QVariantMap{{QStringLiteral("id"), first.value(QStringLiteral("section"))},
                    {QStringLiteral("title"), first.value(QStringLiteral("sectionTitle"))},
                    {QStringLiteral("sectionOrder"), 0},
                    {QStringLiteral("rows"), rows}}
    };
}

QVariantList SliceSettingsBridge::getGlobalSearchResults(const QString& searchText,
                                                         bool showAdvancedMode) const {
    QReadLocker locker(&m_lock);
    const QString needle = searchText.trimmed().toLower();
    if (needle.isEmpty())
        return {};
    QVariantList results;
    for (const QVariant& value : m_schemaSettings) {
        const QVariantMap definition = value.toMap();
        if (!isVisibleInMode(definition, showAdvancedMode))
            continue;
        const QString label = definition.value(QStringLiteral("label")).toString();
        const QString description = definition.value(QStringLiteral("description")).toString();
        const QString haystack = definition.value(QStringLiteral("key")).toString() + QLatin1Char(' ') + label +
                                 QLatin1Char(' ') + description + QLatin1Char(' ') + translatedSettingText(label) +
                                 QLatin1Char(' ') + translatedSettingText(description);
        if (!haystack.toLower().contains(needle))
            continue;
        QVariantMap row = rowForDefinitionLocked(definition);
        row.insert(QStringLiteral("tabOrder"), tabOrder(row.value(QStringLiteral("tab")).toString()));
        results.push_back(row);
    }
    std::sort(results.begin(), results.end(), [](const QVariant& left, const QVariant& right) {
        const auto l = left.toMap();
        const auto r = right.toMap();
        if (l.value(QStringLiteral("tabOrder")) != r.value(QStringLiteral("tabOrder")))
            return l.value(QStringLiteral("tabOrder")).toInt() < r.value(QStringLiteral("tabOrder")).toInt();
        return l.value(QStringLiteral("label")).toString() < r.value(QStringLiteral("label")).toString();
    });
    return results;
}

bool SliceSettingsBridge::hasPendingReslice() const {
    QReadLocker locker(&m_lock);
    return m_hasPendingReslice;
}

bool SliceSettingsBridge::schemaLoaded() const {
    QReadLocker locker(&m_lock);
    return m_schemaLoaded;
}

int SliceSettingsBridge::schemaRevision() const {
    QReadLocker locker(&m_lock);
    return m_schemaRevision;
}

QString SliceSettingsBridge::selectedMachineModelId() const {
    QReadLocker locker(&m_lock);
    return QString::fromStdString(m_selection.machine_model_id);
}

QString SliceSettingsBridge::selectedMachineVariantId() const {
    QReadLocker locker(&m_lock);
    return QString::fromStdString(m_selection.machine_variant_id);
}

QVariantMap SliceSettingsBridge::selectedMachine() const {
    std::string modelId;
    std::string variantId;
    {
        QReadLocker locker(&m_lock);
        modelId = m_selection.machine_model_id;
        variantId = m_selection.machine_variant_id;
    }
    for (const auto& machine : GPlatform::SlicingBackend::instance().machineModels()) {
        if (machine.id != modelId)
            continue;
        const auto variant = std::find_if(machine.variants.begin(), machine.variants.end(),
                                          [&variantId](const libslicer::MachineVariantOption& item) {
                                              return item.id == variantId;
                                          });
        if (variant == machine.variants.end())
            return {};
        const auto [originX, originY] = printableAreaOrigin(*variant);
        return {
            {QStringLiteral("modelId"), QString::fromStdString(machine.id)},
            {QStringLiteral("printerModel"), QString::fromStdString(machine.name)},
            {QStringLiteral("bedVendor"), QString::fromStdString(machine.vendor_id)},
            {QStringLiteral("variantId"), QString::fromStdString(variant->id)},
            {QStringLiteral("nozzleDiameter"), variant->nozzle_diameter},
            {QStringLiteral("width"), variant->printable_width},
            {QStringLiteral("height"), variant->printable_depth},
            {QStringLiteral("printHeight"), variant->printable_height},
            {QStringLiteral("physicalToolCount"), static_cast<qulonglong>(variant->physical_tool_count)},
            {QStringLiteral("variableFilamentSlots"), variant->variable_filament_slots},
            {QStringLiteral("filamentSlotsBoundToPhysicalTools"), variant->filament_slots_bound_to_physical_tools},
            {QStringLiteral("maxFilamentSlots"), static_cast<qulonglong>(variant->max_filament_slots)},
            {QStringLiteral("toolMaterialCapacities"), toolCapacityVariant(*variant)},
            {QStringLiteral("x"), originX},
            {QStringLiteral("y"), originY},
            {QStringLiteral("printableArea"), printableAreaVariant(*variant)},
            {QStringLiteral("coverImageUrl"), QUrl::fromLocalFile(QString::fromStdString(machine.cover_image_path))},
            {QStringLiteral("bedModelPath"), QString::fromStdString(machine.bed_model_path)},
            {QStringLiteral("bedTexturePath"), QString::fromStdString(machine.bed_texture_path)}
        };
    }
    return {};
}

QString SliceSettingsBridge::selectedProcessPresetId() const {
    QReadLocker locker(&m_lock);
    return QString::fromStdString(m_selection.process_preset_id);
}

QString SliceSettingsBridge::selectedProcessPresetName() const {
    QReadLocker locker(&m_lock);
    return presetName(m_compatibleProcesses, m_selection.process_preset_id);
}

QString SliceSettingsBridge::selectedFilamentPresetId() const {
    QReadLocker locker(&m_lock);
    return m_selection.filament_preset_ids.empty()
        ? QString{}
        : QString::fromStdString(m_selection.filament_preset_ids.front());
}

QString SliceSettingsBridge::selectedFilamentPresetName() const {
    QReadLocker locker(&m_lock);
    const std::string id = m_selection.filament_preset_ids.empty()
        ? std::string{}
        : m_selection.filament_preset_ids.front();
    return presetName(m_compatibleFilaments, id);
}

QVariantList SliceSettingsBridge::compatibleProcessPresets() const {
    QReadLocker locker(&m_lock);
    return presetOptions(m_compatibleProcesses);
}

QVariantList SliceSettingsBridge::compatibleFilamentPresets() const {
    QReadLocker locker(&m_lock);
    return presetOptions(m_compatibleFilaments);
}

QVariantList SliceSettingsBridge::filamentSlots() const {
    QReadLocker locker(&m_lock);
    return m_filamentSlots;
}

QString SliceSettingsBridge::configurationError() const {
    QReadLocker locker(&m_lock);
    return m_configurationError;
}

QVariantList SliceSettingsBridge::presetOptions(
    const std::vector<libslicer::PresetOption>& options) {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(options.size()));
    for (const auto& option : options) {
        result.push_back(QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(option.id)},
            {QStringLiteral("name"), QString::fromStdString(option.name)},
            {QStringLiteral("isDefault"), option.is_default}
        });
    }
    return result;
}

QVariantList SliceSettingsBridge::filamentSlotItems(
    const std::vector<libslicer::FilamentSlotInfo>& filamentSlots) {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(filamentSlots.size()));
    for (const auto& slot : filamentSlots) {
        result.push_back(QVariantMap{
            {QStringLiteral("index"), static_cast<qulonglong>(slot.index)},
            {QStringLiteral("number"), static_cast<qulonglong>(slot.index + 1)},
            {QStringLiteral("presetId"), QString::fromStdString(slot.preset_id)},
            {QStringLiteral("presetName"), QString::fromStdString(slot.preset_name)},
            {QStringLiteral("compatiblePresets"), presetOptions(slot.compatible_presets)},
            {QStringLiteral("vendor"), QString::fromStdString(slot.vendor)},
            {QStringLiteral("materialType"), QString::fromStdString(slot.material_type)},
            {QStringLiteral("diameter"), slot.diameter_mm},
            {QStringLiteral("physicalToolIndex"), static_cast<qulonglong>(slot.physical_tool_index)},
            {QStringLiteral("physicalToolName"), QString::fromStdString(slot.physical_tool_name)},
            {QStringLiteral("physicalToolRole"), QString::fromStdString(slot.physical_tool_role)},
            {QStringLiteral("physicalToolSide"), QString::fromStdString(slot.physical_tool_side)},
            {QStringLiteral("nozzleDiameter"), slot.nozzle_diameter_mm},
            {QStringLiteral("color"), QColor(slot.color.red, slot.color.green,
                                                slot.color.blue, slot.color.alpha)}
        });
    }
    return result;
}

QString SliceSettingsBridge::presetName(
    const std::vector<libslicer::PresetOption>& options,
    const std::string& selectedId) {
    const auto selected = std::find_if(options.begin(), options.end(),
                                       [&selectedId](const auto& option) {
                                           return option.id == selectedId;
                                       });
    return selected == options.end()
        ? QString::fromStdString(selectedId)
        : QString::fromStdString(selected->name);
}

bool SliceSettingsBridge::replaceConfig(GPlatform::SlicingConfigActivationResult&& result,
                                        bool markPending) {
    if (!result) {
        const std::string message = result.diagnostics.empty()
            ? std::string("unknown preset selection error")
            : result.diagnostics.front().key + ": " + result.diagnostics.front().message;
        LOG_ERROR("SliceSettingsBridge: unable to create preset-backed config: {}", message);
        setConfigurationError(QString::fromStdString(message));
        return false;
    }

    const std::uint64_t activeRevision = result.revision;
    const auto activeFilamentSlots = result.filamentSlots;
    bool pendingChanged = false;
    bool machineChanged = false;
    libslicer::ResolvedSelection appliedSelection;
    {
        QWriteLocker locker(&m_lock);
        machineChanged = m_selection.machine_model_id != result.selection.machine_model_id ||
                         m_selection.machine_variant_id != result.selection.machine_variant_id;
        m_selection = std::move(result.selection);
        appliedSelection = m_selection;
        m_compatibleProcesses = std::move(result.compatibleProcesses);
        m_compatibleFilaments = std::move(result.compatibleFilaments);
        m_items = std::move(result.settings);
        m_filamentSlots = filamentSlotItems(result.filamentSlots);
        m_itemIndex.clear();
        for (std::size_t index = 0; index < m_items.size(); ++index) {
            m_itemIndex.emplace(m_items[index].key, index);
        }
        buildSchemaLocked();
        if (markPending)
            setPendingLocked(&pendingChanged);
    }

    setConfigurationError({});
    publishActiveSlicingConfig(activeRevision, activeFilamentSlots);
    persistSelection(appliedSelection);
    LOG_INFO("SliceSettingsBridge: selected machine '{} / {}', process '{}', filament '{}'",
             appliedSelection.machine_model_id, appliedSelection.machine_variant_id,
             appliedSelection.process_preset_id,
             appliedSelection.filament_preset_ids.empty() ? std::string{} : appliedSelection.filament_preset_ids.front());
    emit settingsChanged();
    emit schemaChanged();
    if (machineChanged)
        emit machineSelectionChanged();
    emit presetSelectionChanged();
    emit filamentSlotsChanged();
    if (pendingChanged)
        emit hasPendingResliceChanged();
    return true;
}

void SliceSettingsBridge::setConfigurationError(const QString& message) {
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        changed = m_configurationError != message;
        m_configurationError = message;
    }
    if (changed)
        emit configurationErrorChanged();
}

void SliceSettingsBridge::buildSchemaLocked() {
    m_schemaSettings.clear();
    m_schemaPages.clear();
    m_schemaByKey.clear();
    QHash<QString, QVariantMap> pages;
    int rowOrder = 0;
    for (const auto& itemDefinition : m_items) {
        if (!itemDefinition.visible || itemDefinition.read_only)
            continue;
        const QString key = QString::fromStdString(itemDefinition.key);
        const QString tab = groupTab(itemDefinition.group);
        const QString category = itemDefinition.category.empty()
                                     ? QStringLiteral("Other")
                                     : QString::fromStdString(itemDefinition.category);
        const QString page = pageId(category);
        const QString mode = modeName(itemDefinition.level);
        QVariantMap item{
            {QStringLiteral("key"), key},
            {QStringLiteral("label"), itemDefinition.label.empty() ? key : QString::fromStdString(itemDefinition.label)},
            {QStringLiteral("description"), QString::fromStdString(itemDefinition.description)},
            {QStringLiteral("tab"), tab},
            {QStringLiteral("page"), page},
            {QStringLiteral("pageTitle"), category},
            {QStringLiteral("pageOrder"), categoryOrder(tab, category)},
            {QStringLiteral("section"), page},
            {QStringLiteral("sectionTitle"), category},
            {QStringLiteral("sectionOrder"), 0},
            {QStringLiteral("mode"), mode},
            {QStringLiteral("type"), typeName(itemDefinition.type)},
            {QStringLiteral("elementType"), typeName(itemDefinition.element_type)},
            {QStringLiteral("control"), controlName(itemDefinition)},
            {QStringLiteral("unit"), QString::fromStdString(itemDefinition.unit)},
            {QStringLiteral("decimals"), itemDefinition.precision.value_or(
                                             itemDefinition.type == libslicer::SettingType::Integer ? 0 : 3)},
            {QStringLiteral("visible"), itemDefinition.visible},
            {QStringLiteral("enabled"), itemDefinition.enabled},
            {QStringLiteral("internal"), false},
            {QStringLiteral("default"), displayValueLocked(itemDefinition, itemDefinition.default_value)},
            {QStringLiteral("autoInit"), true},
            {QStringLiteral("rowOrder"), rowOrder++}
        };
        if (itemDefinition.minimum)
            item.insert(QStringLiteral("min"), *itemDefinition.minimum);
        if (itemDefinition.maximum)
            item.insert(QStringLiteral("max"), *itemDefinition.maximum);
        if (itemDefinition.fixed_size)
            item.insert(QStringLiteral("fixedSize"),
                        static_cast<qulonglong>(*itemDefinition.fixed_size));
        if (itemDefinition.step)
            item.insert(QStringLiteral("step"), *itemDefinition.step);
        QVariantList options;
        for (const auto& enumItem : itemDefinition.enum_items) {
            options.push_back(QVariantMap{{QStringLiteral("value"), QString::fromStdString(enumItem.value)},
                                          {QStringLiteral("label"), QString::fromStdString(enumItem.label)}});
        }
        item.insert(QStringLiteral("options"), options);
        m_schemaSettings.push_back(item);
        m_schemaByKey.insert(key, item);

        const QString pageKey = tab + QStringLiteral("::") + page;
        if (!pages.contains(pageKey)) {
            pages.insert(pageKey, QVariantMap{
                                      {QStringLiteral("tab"), tab},
                                      {QStringLiteral("page"), page},
                                      {QStringLiteral("title"), category},
                                      {QStringLiteral("mode"), mode},
                                      {QStringLiteral("order"), categoryOrder(tab, category)}
            });
        } else if (mode == QStringLiteral("basic")) {
            auto existing = pages.value(pageKey);
            existing.insert(QStringLiteral("mode"), QStringLiteral("basic"));
            pages.insert(pageKey, existing);
        }
    }
    for (const auto& page : pages)
        m_schemaPages.push_back(page);
    m_schemaLoaded = !m_schemaSettings.isEmpty();
    ++m_schemaRevision;
    LOG_INFO("SliceSettingsBridge: loaded {} libslicer definitions across {} pages",
             m_schemaSettings.size(), m_schemaPages.size());
}

const libslicer::SettingItem* SliceSettingsBridge::findItemLocked(std::string_view key) const {
    const auto found = m_itemIndex.find(std::string(key));
    return found == m_itemIndex.end() ? nullptr : &m_items[found->second];
}

bool SliceSettingsBridge::applyChangedItemsLocked(
    const std::vector<libslicer::SettingItem>& changedItems) {
    bool schemaChanged = false;
    for (const auto& changed : changedItems) {
        const auto found = m_itemIndex.find(changed.key);
        if (found == m_itemIndex.end()) {
            m_itemIndex.emplace(changed.key, m_items.size());
            m_items.push_back(changed);
            schemaChanged = true;
            continue;
        }

        auto& current = m_items[found->second];
        schemaChanged = schemaChanged || current.visible != changed.visible ||
                        current.enabled != changed.enabled || current.group != changed.group ||
                        current.label != changed.label || current.description != changed.description ||
                        current.category != changed.category || current.type != changed.type ||
                        current.element_type != changed.element_type || current.read_only != changed.read_only;
        current = changed;
    }
    if (schemaChanged)
        buildSchemaLocked();
    return schemaChanged;
}

QVariant SliceSettingsBridge::displayValueLocked(const libslicer::SettingItem& item,
                                                 const std::string& serialized) const {
    QString value = QString::fromStdString(serialized);
    switch (item.type) {
        case libslicer::SettingType::Boolean:
            return value == QStringLiteral("1") || value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        case libslicer::SettingType::Integer:
            return value.toInt();
        case libslicer::SettingType::Float:
            return value.toDouble();
        case libslicer::SettingType::Percent:
            value.remove(QLatin1Char('%'));
            return value.toDouble();
        default:
            return value;
    }
}

std::string SliceSettingsBridge::serializeValueLocked(const libslicer::SettingItem& item,
                                                      const QVariant& value) const {
    switch (item.type) {
        case libslicer::SettingType::Boolean:
            return value.toBool() ? "1" : "0";
        case libslicer::SettingType::Integer:
            return QString::number(value.toInt()).toStdString();
        case libslicer::SettingType::Float:
            return QLocale::c().toString(value.toDouble(), 'g', 15).toStdString();
        case libslicer::SettingType::Percent: {
            QString text = value.toString().trimmed();
            if (!text.endsWith(QLatin1Char('%')))
                text += QLatin1Char('%');
            return text.toStdString();
        }
        default:
            return value.toString().toStdString();
    }
}

QVariantMap SliceSettingsBridge::rowForDefinitionLocked(const QVariantMap& definition) const {
    QVariantMap row = definition;
    const QString key = definition.value(QStringLiteral("key")).toString();
    const auto* item = findItemLocked(key.toStdString());
    if (item != nullptr) {
        row.insert(QStringLiteral("value"), displayValueLocked(*item, item->value));
    }
    row.insert(QStringLiteral("defaultValue"), definition.value(QStringLiteral("default")));
    return row;
}

bool SliceSettingsBridge::isVisibleInMode(const QVariantMap& definition, bool showAdvancedMode) const {
    return showAdvancedMode || definition.value(QStringLiteral("mode")).toString() == QStringLiteral("basic");
}

void SliceSettingsBridge::setPendingLocked(bool* changed) {
    *changed = !m_hasPendingReslice;
    m_hasPendingReslice = true;
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    SliceSettingsBridge, "SliceSettingsBridge", &SliceSettingsBridge::create)
