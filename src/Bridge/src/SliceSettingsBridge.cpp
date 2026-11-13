#include <ModelGraphUtil.hpp>
#include "SliceSettingsBridge.hpp"
#include "BridgeRegistration.hpp"

#include "Foundation/Log.h"
#include "DocumentManager.hpp"
#include "SlicingConfigDB.hpp"
#include "PrintBedDB.hpp"
#include "ModelInstanceDB.hpp"
#include "ModelPartDB.hpp"
#include "ModelObjectDB.hpp"
#include "ModelSurfaceColorCodec.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "SelectionBridge.hpp"
#include "SlicingBackend.hpp"
#include "TransactionManager.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace {
    constexpr auto kMachineModelSetting = "slicing/selection/machineModelId";
    constexpr auto kMachineVariantSetting = "slicing/selection/machineVariantId";
    constexpr auto kPrinterPresetSetting = "slicing/selection/printerPresetId";
    constexpr auto kProcessPresetSetting = "slicing/selection/processPresetId";
    constexpr auto kFilamentPresetsSetting = "slicing/selection/filamentPresetIds";
    constexpr auto kFilamentToolsSetting = "slicing/selection/filamentPhysicalTools";

    bool selectionPersistenceEnabled() {
        return qEnvironmentVariableIntValue("GPLATFORM_JOURNAL_SESSION") == 0;
    }

    void readPresetSelection(QSettings& settings, libslicer::ConfigSelection& selection) {
        selection.process_preset_id = settings.value(kProcessPresetSetting).toString().toStdString();
        for (const auto& filament : settings.value(kFilamentPresetsSetting).toStringList())
            selection.filament_preset_ids.push_back(filament.toStdString());
        for (const auto& tool : settings.value(kFilamentToolsSetting).toList())
            selection.filament_physical_tools.push_back(tool.toUInt());
    }

    libslicer::ConfigSelection restoredSelection() {
        libslicer::ConfigSelection selection;
        if (!selectionPersistenceEnabled()) return selection;
        QSettings settings;
        selection.machine_model_id = settings.value(kMachineModelSetting).toString().toStdString();
        selection.machine_variant_id = settings.value(kMachineVariantSetting).toString().toStdString();
        selection.printer_preset_id = settings.value(kPrinterPresetSetting).toString().toStdString();
        readPresetSelection(settings, selection);
        return selection;
    }

    QString printerPresetGroup(const std::string& preset) {
        return QStringLiteral("slicing/printerPresets/") +
            QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(preset)));
    }

    // Orca PresetBundle::export_selections stores these with the printer preset.
    constexpr const char* printerSelectionKeys[] = {
        "curr_bed_type", "filament_colour", "filament_multi_colour", "filament_colour_type",
        "flush_volumes_matrix", "flush_volumes_vector", "flush_multiplier"};

    std::vector<std::pair<std::string, std::string>> rememberedPrinterSettings(QSettings& settings) {
        std::vector<std::pair<std::string, std::string>> patch;
        for (const char* key : printerSelectionKeys) {
            const auto value = settings.value(QString::fromLatin1(key));
            if (value.isValid()) patch.emplace_back(key, value.toString().toStdString());
        }
        return patch;
    }

    std::vector<std::pair<std::string, std::string>> rememberedPrinterSettings(
        const libslicer::ConfigSelection& selection) {
        const bool systemP3 = selection.machine_model_id == "P3" && selection.machine_variant_id == "0.4" &&
            (selection.printer_preset_id.empty() || selection.printer_preset_id == "P3 0.4 nozzle");
        if (!systemP3 && selection.printer_preset_id.empty()) return {};
        QSettings settings;
        settings.beginGroup(printerPresetGroup(systemP3 ? "P3 0.4 nozzle" : selection.printer_preset_id));
        auto patch = rememberedPrinterSettings(settings);
        if (!systemP3 || settings.contains("filament_colour"))
            return patch;

        settings.endGroup();
        settings.beginGroup(QStringLiteral("slicing/printers/P3/0.4"));
        const auto tools = settings.value(kFilamentToolsSetting).toList();
        const auto palette = settings.value(QStringLiteral("filament_colour")).toString();
        const auto colors = palette.split(QLatin1Char(';'));
        static const QRegularExpression rgb(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
        if (tools.size() != 6 || colors.size() != 6)
            return patch;
        for (qsizetype slot = 0; slot < tools.size(); ++slot) {
            bool ok = false;
            if (tools[slot].toUInt(&ok) != unsigned(slot) || !ok || !rgb.match(colors[slot]).hasMatch())
                return patch;
        }
        patch.emplace_back("filament_colour", palette.toStdString());
        return patch;
    }

    void persistSelection(const libslicer::ResolvedSelection& selection, bool active = true,
                          std::optional<libslicer::ConfigSnapshot> config = {},
                          bool rememberColors = true) {
        if (!selectionPersistenceEnabled()) return;

        QStringList filaments;
        for (const auto& filament : selection.filament_preset_ids)
            filaments.push_back(QString::fromStdString(filament));
        QVariantList tools;
        for (unsigned tool : selection.filament_physical_tools) tools.push_back(tool);

        QSettings settings;
        const auto writePresets = [&] {
            settings.setValue(kProcessPresetSetting, QString::fromStdString(selection.process_preset_id));
            settings.setValue(kFilamentPresetsSetting, filaments);
            settings.setValue(kFilamentToolsSetting, tools);
        };
        if (active) {
            settings.setValue(kMachineModelSetting, QString::fromStdString(selection.machine_model_id));
            settings.setValue(kMachineVariantSetting, QString::fromStdString(selection.machine_variant_id));
            settings.setValue(kPrinterPresetSetting, QString::fromStdString(selection.printer_preset_id));
            writePresets();
        }
        settings.beginGroup(printerPresetGroup(selection.printer_preset_id));
        writePresets();
        if (!config) config = GPlatform::SlicingBackend::instance().configSnapshot();
        if (config) {
            for (const char* key : printerSelectionKeys) {
                if (!rememberColors && std::string_view(key).rfind("filament_", 0) == 0)
                    continue;
                if (const auto value = config->value(key))
                    settings.setValue(QString::fromLatin1(key), QString::fromStdString(*value));
            }
        }
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
        return {view.valid(), std::move(view), {}};
    }

    std::vector<GPlatform::SlicingConfigDB::Filament> convertDocumentFilaments(
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

        config->setFilaments(convertDocumentFilaments(filamentSlots));
        config->setActiveRevision(revision);
    }
} // namespace

namespace {
struct SchemaProjection { QVariantList settings, pages; QHash<QString, QVariantMap> byKey; };
QVariant displaySettingValue(const libslicer::SettingItem& item,
                                                 const std::string& serialized) {
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

SchemaProjection prepareSchema(const std::vector<libslicer::SettingItem>& items) {
    SchemaProjection projection;
    projection.settings.clear();
    projection.pages.clear();
    projection.byKey.clear();
    QHash<QString, QVariantMap> pages;
    int rowOrder = 0;
    for (const auto& itemDefinition : items) {
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
            {QStringLiteral("default"), displaySettingValue(itemDefinition, itemDefinition.default_value)},
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
        projection.settings.push_back(item);
        projection.byKey.insert(key, item);

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
        projection.pages.push_back(page);
    return projection;
}
}


std::vector<GPlatform::SlicingConfigDB::Filament> SliceSettingsBridge::documentFilaments(
    const std::vector<libslicer::FilamentSlotInfo>& filamentSlots) { return convertDocumentFilaments(filamentSlots); }

SliceSettingsBridge::PreparedConfiguration SliceSettingsBridge::prepareConfiguration(
    libslicer::ActiveConfigView view, const std::optional<libslicer::ProjectMachine>& machine) {
    PreparedConfiguration prepared;
    prepared.view = std::move(view);
    for (std::size_t index = 0; index < prepared.view.settings.size(); ++index) {
        const auto& item = prepared.view.settings[index];
        prepared.itemIndex.emplace(item.key, index);

    }
    for (const auto& key : prepared.view.modified_process_keys)
        prepared.modifiedProcessKeys.push_back(QString::fromStdString(key));
    prepared.filamentSlots = filamentSlotItems(prepared.view.filament_slots, prepared.view.settings);
    auto schema = prepareSchema(prepared.view.settings);
    prepared.schemaSettings = std::move(schema.settings);
    prepared.schemaPages = std::move(schema.pages);
    prepared.schemaByKey = std::move(schema.byKey);
    if (machine) {
        const auto& model = machine->model;
        const auto& variant = machine->variant;
        const auto origin = printableAreaOrigin(variant);
        prepared.projectLocal = machine->project_local;
        prepared.machine = {
            {"modelId", QString::fromStdString(model.id)}, {"variantId", QString::fromStdString(variant.id)},
            {"printerPresetId", QString::fromStdString(prepared.view.selection.printer_preset_id)},
            {"printerModel", QString::fromStdString(model.name)}, {"bedVendor", QString::fromStdString(model.vendor_id)},
            {"width", variant.printable_width}, {"height", variant.printable_depth},
            {"printHeight", variant.printable_height}, {"nozzleDiameter", variant.nozzle_diameter},
            {"x", origin.first}, {"y", origin.second}, {"printableArea", printableAreaVariant(variant)},
            {"physicalToolCount", QVariant::fromValue<qulonglong>(variant.physical_tool_count)},
            {"variableFilamentSlots", variant.variable_filament_slots},
            {"filamentSlotsBoundToPhysicalTools", variant.filament_slots_bound_to_physical_tools},
            {"maxFilamentSlots", QVariant::fromValue<qulonglong>(variant.max_filament_slots)},
            {"toolMaterialCapacities", toolCapacityVariant(variant)},
            {"bedModelPath", QString::fromStdString(model.bed_model_path)},
            {"bedTexturePath", QString::fromStdString(model.bed_texture_path)}, {"projectLocal", machine->project_local}
        };
    }
    return prepared;
}

void SliceSettingsBridge::installCommittedConfiguration(
    PreparedConfiguration&& prepared, std::optional<bool> projectPalette) noexcept {
    QWriteLocker locker(&m_lock);
    using std::swap;
    swap(m_selection, prepared.view.selection);
    m_compatibleProcesses.swap(prepared.view.compatible_processes);
    m_compatibleFilaments.swap(prepared.view.compatible_filaments);
    m_items.swap(prepared.view.settings);
    m_modifiedProcessKeys.swap(prepared.modifiedProcessKeys);
    m_printerPresetModified = prepared.view.printer_preset_modified;
    m_itemIndex.swap(prepared.itemIndex);
    m_schemaSettings.swap(prepared.schemaSettings);
    m_schemaPages.swap(prepared.schemaPages);
    m_schemaByKey.swap(prepared.schemaByKey);
    m_filamentSlots.swap(prepared.filamentSlots);
    m_projectMachine.swap(prepared.machine);
    if (projectPalette) m_projectPalette = *projectPalette;
    m_schemaLoaded = !m_schemaSettings.isEmpty();
    ++m_schemaRevision;
    m_hasPendingReslice = true;
    m_configurationError.clear();
}

void SliceSettingsBridge::publishCommittedConfiguration() {
    emit settingsChanged(); emit schemaChanged(); emit machineSelectionChanged();
    emit presetSelectionChanged(); emit filamentSlotsChanged(); emit hasPendingResliceChanged();
    emit configurationErrorChanged();
}

namespace {
std::optional<GPlatform::SlicingConfigActivationResult> startupConfiguration;

GPlatform::SlicingConfigActivationResult loadStartupConfiguration(libslicer::ConfigSelection selection) {
    auto& backend = GPlatform::SlicingBackend::instance();
    if (!backend.available()) {
        GPlatform::SlicingConfigActivationResult result;
        result.diagnostics.push_back({"initialization", "slicing backend is unavailable"});
        return result;
    }

    if (selection.machine_model_id.empty() || selection.machine_variant_id.empty()) {
        selection = {};
        selection.machine_model_id = "Flashforge AD5X";
        selection.machine_variant_id = "0.4";
    }
    const auto restore = [&backend](libslicer::ConfigSelection requested) {
        GPlatform::SlicingConfigActivationResult result;
        if (requested.printer_preset_id.empty()) {
            const auto presets = backend.printerPresets(requested.machine_model_id, requested.machine_variant_id);
            if (!presets.empty()) requested.printer_preset_id = presets.front().id;
        }
        const auto settings = selectionPersistenceEnabled()
            ? rememberedPrinterSettings(requested)
            : std::vector<std::pair<std::string, std::string>>{};
        const auto revision = backend.configurationRevision();
        auto prepared = backend.preparePrinterSwitch(requested, revision, {}, settings);
        if (!prepared) {
            result.diagnostics = std::move(prepared.diagnostics);
            return result;
        }
        auto lease = backend.beginProjectCommit(revision);
        if (!lease) {
            result.diagnostics.push_back({"selection", "The configuration changed during startup"});
            return result;
        }
        auto installed = lease.install(prepared.configuration);
        if (!installed) {
            result.diagnostics = std::move(installed.diagnostics);
            return result;
        }
        result.view = prepared.configuration.view();
        result.view.revision = installed.revision;
        result.success = true;
        return result;
    };
    auto result = restore(selection);
    if (!result && !selection.printer_preset_id.empty()) {
        selection.printer_preset_id.clear();
        selection.process_preset_id.clear();
        selection.filament_preset_ids.clear();
        selection.filament_physical_tools.clear();
        result = restore(selection);
    }
    if (!result && (selection.machine_model_id != "Flashforge AD5X" || selection.machine_variant_id != "0.4")) {
        LOG_WARN("SliceSettingsBridge: saved machine '{} / {}' is unavailable; using fallback machine",
                 selection.machine_model_id, selection.machine_variant_id);
        selection = {};
        selection.machine_model_id = "Flashforge AD5X";
        selection.machine_variant_id = "0.4";
        result = restore(selection);
    }
    return result;
}

}

QFuture<GPlatform::SlicingConfigActivationResult> SliceSettingsBridge::prepareStartupConfiguration() {
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    const auto selection = restoredSelection();
    // Prime the cached Qt path before entering the startup worker.
    QCoreApplication::applicationDirPath();
    return QtConcurrent::run([selection] { return loadStartupConfiguration(selection); });
}

void SliceSettingsBridge::supplyStartupConfiguration(GPlatform::SlicingConfigActivationResult result) {
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    startupConfiguration = std::move(result);
}

SliceSettingsBridge::SliceSettingsBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    auto result = startupConfiguration ? std::move(*startupConfiguration) : loadStartupConfiguration(restoredSelection());
    startupConfiguration.reset();
    if (!replaceConfig(std::move(result), false))
        LOG_ERROR("SliceSettingsBridge: failed to restore or select fallback machine");
    const QPointer<SliceSettingsBridge> self(this);
    m_documentListenerId = DocumentManager::instance()->addChangeListener([self](const auto& change) {
        if (!self || change.changeType != ChangeType::PROPERTY_CHANGED)
            return;
        const bool modelSetting = change.propertyName == "ProjectSemantics" &&
            (change.dbType == TypeID::MODEL_OBJECT_DB || change.dbType == TypeID::MODEL_PART_DB);
        const bool baselineChanged = (change.dbType == TypeID::PRINT_BED_DB &&
            change.propertyName == "ProjectSemantics") || (change.dbType == TypeID::MODEL_INSTANCE_DB &&
            change.propertyName == "ParentPrintBedDBId");
        if (modelSetting) ++self->m_localProcessRevision;
        if (modelSetting || baselineChanged)
            QMetaObject::invokeMethod(self.data(), [self, modelSetting] {
                if (!self) return;
                if (modelSetting) self->markNeedsReslice();
                emit self->localProcessSettingsChanged();
            }, Qt::QueuedConnection);
    });
}

SliceSettingsBridge::~SliceSettingsBridge() {
    if (m_documentListenerId)
        DocumentManager::instance()->removeChangeListener(m_documentListenerId);
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

QStringList SliceSettingsBridge::modifiedSettingKeys() const {
    QReadLocker locker(&m_lock);
    return m_modifiedProcessKeys;
}

bool SliceSettingsBridge::printerPresetModified() const {
    QReadLocker locker(&m_lock);
    return m_printerPresetModified;
}

void SliceSettingsBridge::refreshPresetState() {
    const auto active = GPlatform::SlicingBackend::instance().activeConfigView();
    if (!active) return;
    bool slotsChanged = false;
    bool selectionChanged = false;
    bool schemaChanged = false;
    {
        QWriteLocker locker(&m_lock);
        schemaChanged = applyChangedItemsLocked(active->settings);
        m_modifiedProcessKeys.clear();
        for (const auto& key : active->modified_process_keys)
            m_modifiedProcessKeys.push_back(QString::fromStdString(key));
        m_printerPresetModified = active->printer_preset_modified;
        selectionChanged = m_selection.filament_preset_ids != active->selection.filament_preset_ids;
        m_selection = active->selection;
        m_compatibleProcesses = active->compatible_processes;
        m_compatibleFilaments = active->compatible_filaments;
        auto projectedSlots = filamentSlotItems(active->filament_slots, m_items);
        slotsChanged = projectedSlots != m_filamentSlots;
        m_filamentSlots.swap(projectedSlots);
    }
    if (selectionChanged) {
        publishActiveSlicingConfig(active->revision, active->filament_slots);
        emit presetSelectionChanged();
    }
    if (slotsChanged) emit filamentSlotsChanged();
    if (schemaChanged) emit this->schemaChanged();
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
    if (valuesChanged) {
        refreshPresetState();
        emit settingsChanged();
    }
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
    if (configurationMutationBlocked("setting change")) {
        emit settingRejected(key,
            tr("Settings cannot be changed while slicing. Wait for the current task to finish."));
        return;
    }

    QVariant canonicalDisplay;
    QString rejection;
    QString settingLabel = key;
    bool pendingChanged = false;
    bool schemaChanged = false;
    bool valueChanged = false;
    {
        QWriteLocker locker(&m_lock);
        const auto* item = findItemLocked(key.toStdString());
        if (item == nullptr) {
            rejection = tr("This setting is no longer available.");
        } else {
            settingLabel = translatedSettingText(QString::fromStdString(item->label));
            const auto result = GPlatform::SlicingBackend::instance().setConfigValue(
                item->key, serializeValueLocked(*item, value));
            if (!result) {
                QStringList reasons;
                for (const auto& diagnostic : result.diagnostics) {
                    QString message = translatedSettingText(QString::fromStdString(diagnostic.message));
                    if (!diagnostic.key.empty() && diagnostic.key != item->key) {
                        const auto* related = findItemLocked(diagnostic.key);
                        const QString label = related
                            ? translatedSettingText(QString::fromStdString(related->label))
                            : QString::fromStdString(diagnostic.key);
                        message = label + QStringLiteral(": ") + message;
                    }
                    reasons.push_back(message);
                }
                rejection = reasons.isEmpty() ? tr("Invalid value.") : reasons.join(QLatin1Char('\n'));
            } else {
                valueChanged = !result.changed_items.empty();
                schemaChanged = applyChangedItemsLocked(result.changed_items);
                const auto* canonical = findItemLocked(key.toStdString());
                canonicalDisplay = canonical == nullptr ? value
                                                        : displayValueLocked(*canonical, canonical->value);
                if (valueChanged)
                    setPendingLocked(&pendingChanged);
            }
        }
    }
    // Notification observers may synchronously read settings; never notify under m_lock.
    if (!rejection.isEmpty()) {
        LOG_WARN("SliceSettingsBridge: rejected '{}': {}", key.toStdString(), rejection.toStdString());
        emit settingRejected(key, settingLabel + QStringLiteral(": ") + rejection);
        return;
    }
    if (valueChanged && key == QStringLiteral("curr_bed_type")) {
        auto& backend = GPlatform::SlicingBackend::instance();
        const auto machine = backend.activeProjectMachine();
        if (const auto active = backend.activeConfigView(); machine && !machine->project_local && active)
            persistSelection(active->selection, true, {}, !m_projectPalette);
    }
    if (valueChanged)
        emit settingChanged(key, canonicalDisplay);
    if (valueChanged) {
        refreshPresetState();
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
    if (valueChanged) {
        refreshPresetState();
        emit settingsChanged();
    }
    if (schemaChanged)
        emit this->schemaChanged();
    if (pendingChanged)
        emit hasPendingResliceChanged();
}

void SliceSettingsBridge::restoreCurrentPreset(const QString& tab, int filamentSlot) {
    if (configurationMutationBlocked("preset restore")) return;
    if (tab != QStringLiteral("process") && tab != QStringLiteral("filament") && tab != QStringLiteral("printer")) return;
    const auto group = tab == QStringLiteral("process") ? libslicer::SettingGroup::Process
                     : tab == QStringLiteral("filament") ? libslicer::SettingGroup::Filament
                                                         : libslicer::SettingGroup::Printer;
    auto& backend = GPlatform::SlicingBackend::instance();
    const auto revision = backend.configurationRevision();
    auto result = backend.restorePreset(group, filamentSlot < 0 ? std::nullopt
                                                              : std::optional<std::size_t>(filamentSlot));
    if (!result) {
        QStringList reasons;
        for (const auto& diagnostic : result.diagnostics)
            reasons.push_back(translatedSettingText(QString::fromStdString(diagnostic.message)));
        emit settingRejected({}, tr("Could not restore the current preset: %1").arg(reasons.join(QLatin1Char('\n'))));
        return;
    }
    if (result.view.revision == revision) return;
    replaceConfig(std::move(result), true);
}

QVariantList SliceSettingsBridge::machineModels() const {
    QVariantList models;
    auto& backend = GPlatform::SlicingBackend::instance();
    auto machines = backend.machineModels();
    for (const auto& project : backend.projectMachineModels()) {
        auto existing = std::find_if(machines.begin(), machines.end(),
            [&](const auto& machine) { return machine.id == project.id; });
        if (existing == machines.end())
            machines.push_back(project);
        else
            existing->variants.insert(existing->variants.end(), project.variants.begin(), project.variants.end());
    }
    for (const auto& machine : machines) {
        QVariantList variants;
        for (const auto& variant : machine.variants) {
            const auto [originX, originY] = printableAreaOrigin(variant);
            QVariantMap variantMap{
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
            };
            const auto presets = backend.printerPresets(machine.id, variant.id);
            if (presets.empty())
                variantMap.insert(QStringLiteral("projectPreset"), true);
            variants.push_back(variantMap);
            for (const auto& preset : presets) {
                if (preset.is_default) continue;
                variantMap.insert(QStringLiteral("printerPresetId"), QString::fromStdString(preset.id));
                variantMap.insert(QStringLiteral("name"), QString::fromStdString(preset.name));
                variantMap.insert(QStringLiteral("userPreset"), true);
                variants.push_back(variantMap);
            }
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

bool SliceSettingsBridge::selectMachine(const QString& modelId, const QString& variantId, const QString& bedType,
                                        const QString& printerPresetId) {
    if (configurationMutationBlocked("machine selection") || TransactionManager::instance().isInTransaction() ||
        m_printerSwitch)
        return false;
    auto& backend = GPlatform::SlicingBackend::instance();
    PendingPrinterSwitch pending;
    auto& selection = pending.target;
    selection.machine_model_id = modelId.toStdString();
    selection.machine_variant_id = variantId.toStdString();
    selection.printer_preset_id = printerPresetId.toStdString();
    if (selection.printer_preset_id.empty()) {
        const auto presets = backend.printerPresets(selection.machine_model_id, selection.machine_variant_id);
        if (!presets.empty()) selection.printer_preset_id = presets.front().id;
    }
    if (const auto current = backend.activeConfigView(); current &&
        current->selection.machine_model_id == selection.machine_model_id &&
        current->selection.machine_variant_id == selection.machine_variant_id &&
        current->selection.printer_preset_id == selection.printer_preset_id) {
        if (bedType.isEmpty()) return true;
        setSetting(QStringLiteral("curr_bed_type"), bedType);
        return getSetting(QStringLiteral("curr_bed_type")).toString() == bedType;
    }
    if (selectionPersistenceEnabled() && !selection.printer_preset_id.empty()) {
        QSettings settings;
        settings.beginGroup(printerPresetGroup(selection.printer_preset_id));
        readPresetSelection(settings, selection);
        pending.remembered = rememberedPrinterSettings(selection);
    }
    if (!bedType.isEmpty()) pending.remembered.emplace_back("curr_bed_type", bedType.toStdString());
    pending.review = backend.reviewPrinterSwitch(selection);
    if (!pending.review.diagnostics.empty()) {
        emit settingRejected(QStringLiteral("printer"), QString::fromStdString(pending.review.diagnostics.front().message));
        return false;
    }
    if (pending.review.presets.empty()) return applyPrinterSwitch(std::move(pending));
    m_printerSwitch = std::move(pending);
    showPrinterSwitchDecision();
    return true;
}

void SliceSettingsBridge::showPrinterSwitchDecision() {
    const auto& pending = *m_printerSwitch;
    const auto& preset = pending.review.presets.at(pending.decisions.size());
    QVariantList changes;
    for (const auto& item : preset.changes) {
        changes.push_back(QVariantMap{
            {"key", QString::fromStdString(item.key)}, {"before", QString::fromStdString(item.before)},
            {"after", QString::fromStdString(item.after)}, {"transferable", item.transferable}});
    }
    emit printerSwitchDecisionRequested({
        {"name", QString::fromStdString(preset.name)}, {"saveAs", preset.save_as},
        {"transferAllowed", preset.transfer_allowed}, {"changes", changes}});
}

QVariantMap SliceSettingsBridge::printerSwitchPresetNameStatus(const QString& name) {
    if (!m_printerSwitch) return {};
    const auto& pending = *m_printerSwitch;
    const auto& preset = pending.review.presets.at(pending.decisions.size());
    const auto status = GPlatform::SlicingBackend::instance().savePrinterSwitchPreset(
        pending.review.revision, preset.type, name.toStdString(), true, preset.name);
    if (!status.diagnostics.empty())
        return {{"valid", false}, {"message", tr(status.diagnostics.front().message.c_str())}};
    return {{"valid", true}, {"message", status.overwrite_warning.empty()
        ? QString{} : tr(status.overwrite_warning.c_str())}};
}

void SliceSettingsBridge::resolvePrinterSwitch(const QString& action, const QString& saveName) {
    if (!m_printerSwitch) return;
    using Action = libslicer::PrinterSwitchAction;
    auto& pending = *m_printerSwitch;
    const auto& preset = pending.review.presets.at(pending.decisions.size());
    Action decision = Action::Cancel;
    if (action == QStringLiteral("save")) {
        auto& backend = GPlatform::SlicingBackend::instance();
        auto saved = backend.savePrinterSwitchPreset(pending.review.revision, preset.type, saveName.toStdString(), false, preset.name);
        if (!saved) {
            const auto message = saved.diagnostics.empty() ? tr("Could not save preset")
                : QString::fromStdString(saved.diagnostics.front().message);
            m_printerSwitch.reset();
            emit settingRejected(QStringLiteral("printer"), message);
            emit machineSelectionChanged();
            return;
        }
        pending.presetSaved = true;
        replaceConfig(bridgeActivationResult(std::move(saved.view)), true);
        pending.review = backend.reviewPrinterSwitch(pending.target, true);
        if (!pending.review.diagnostics.empty()) {
            const auto message = QString::fromStdString(pending.review.diagnostics.front().message);
            m_printerSwitch.reset();
            emit settingRejected(QStringLiteral("printer"), tr("Preset saved. Printer switch failed. %1").arg(message));
            return;
        }
        // Saving changes the review list; replay decisions against its new identities.
        pending.decisions.clear();
    } else {
        if (action == QStringLiteral("transfer")) decision = Action::Transfer;
        else if (action == QStringLiteral("discard")) decision = Action::Discard;
        if (decision == Action::Cancel && preset.cancel_switch) {
            m_printerSwitch.reset();
            emit machineSelectionChanged();
            return;
        }
        pending.decisions.push_back({preset.type, decision});
    }
    if (pending.decisions.size() < pending.review.presets.size()) {
        showPrinterSwitchDecision();
        return;
    }
    auto confirmed = std::move(pending);
    m_printerSwitch.reset();
    applyPrinterSwitch(std::move(confirmed));
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
    return replaceConfig(std::move(result), true, true);
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
        backend.setFilamentPreset(static_cast<std::size_t>(slotIndex), presetId.toStdString()),
        true, true);
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
        GPlatform::SlicingBackend::instance().resizeFilamentSlots(static_cast<std::size_t>(slotCount)),
        true, true)) {
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
    return replaceConfig(GPlatform::SlicingBackend::instance()
        .addFilamentToTool(static_cast<std::size_t>(physicalTool)), true, true);
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
        m_filamentSlots = filamentSlotItems(active->filament_slots, m_items);
        setPendingLocked(&pendingChanged);
    }
    publishActiveSlicingConfig(active->revision, active->filament_slots);
    if (const auto machine = backend.activeProjectMachine(); machine && !machine->project_local)
        persistSelection(active->selection, true, {}, !m_projectPalette);
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

void SliceSettingsBridge::markAsSlicedForRevision(std::uint64_t sliced, std::optional<std::uint64_t> active) {
    if (sliced && active && sliced == *active) markCurrentAsSliced();
    else markNeedsReslice();
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

namespace {
std::shared_ptr<ModelInstanceDB> selectedParameterInstance(const QString& modelId) {
    bool ok = false;
    const DBInstanceID id(modelId.toULongLong(&ok));
    const auto* selection = SelectionBridge::instance();
    if (!ok || !id.isValid() || selection->activeInstanceId() != id)
        return {};
    return DocumentManager::instance()->getDB<ModelInstanceDB>(id);
}

struct LocalProcessTarget {
    std::shared_ptr<PrintBedDB> bed;
    std::shared_ptr<ModelObjectDB> object;
    std::shared_ptr<ModelPartDB> part;

    explicit operator bool() const { return bool(object); }
    std::string plateSemantics() const {
        return bed ? bed->getProjectSemantics() : std::string{};
    }
    std::string semantics() const {
        return part ? part->getProjectSemantics() : object->getProjectSemantics();
    }
    std::optional<std::string> parentSemantics() const {
        if (part) return object->getProjectSemantics();
        return std::nullopt;
    }
};

LocalProcessTarget localProcessTarget(const QString& modelId, const QString& partId) {
    const auto instance = selectedParameterInstance(modelId);
    if (!instance) return {};
    LocalProcessTarget target{
        DocumentManager::instance()->getDB<PrintBedDB>(instance->getParentPrintBedDBId()),
        instance->object(), {}};
    if (partId.isEmpty()) return target;
    for (const auto& part : instance->parts()) {
        if (part && part->getRole() == int(ModelPartRole::ParameterModifier) &&
            QString::number(part->getDBInstanceID().getValue()) == partId) {
            target.part = part;
            return target;
        }
    }
    return {};
}
}

QVariantList SliceSettingsBridge::parameterModifiers(const QString& modelId) const {
    QVariantList result;
    if (const auto instance = selectedParameterInstance(modelId)) {
        for (const auto& part : instance->parts()) {
            if (part && part->getRole() == int(ModelPartRole::ParameterModifier))
                result.push_back(QVariantMap{
                    {"id", QString::number(part->getDBInstanceID().getValue())},
                    {"name", QString::fromStdString(part->getDisplayName())}});
        }
    }
    return result;
}

QVariantList SliceSettingsBridge::localProcessSettings(const QString& modelId, const QString& partId) const {
    const auto target = localProcessTarget(modelId, partId);
    if (!target) return {};
    const auto base = GPlatform::SlicingBackend::instance().configSnapshot();
    if (!base) return {};
    try {
        const auto config = libslicer::Config::for_project_overrides(
            *base, target.plateSemantics(), target.semantics(), target.parentSemantics());
        const auto items = config.settings();
        const auto schema = prepareSchema(items);
        QVariantList rows;
        for (const auto& item : items) {
            auto row = schema.byKey.value(QString::fromStdString(item.key));
            if (row.isEmpty() || item.group != libslicer::SettingGroup::Process)
                continue;
            row.insert("value", displaySettingValue(item, item.value));
            row.insert("overridden", item.overridden);
            row.insert("enabled", item.enabled && !item.read_only);
            rows.push_back(row);
        }
        return rows;
    } catch (const std::exception& error) {
        LOG_WARN("Cannot read model slicing settings: {}", error.what());
        return {};
    }
}

QString SliceSettingsBridge::editLocalProcessSetting(const QString& modelId, const QString& partId,
    const QString& key, const QVariant& value, bool inherit) {
    if (configurationMutationBlocked("local process edit") || TransactionManager::instance().isInTransaction())
        return tr("Wait for the current task to finish.");
    const auto target = localProcessTarget(modelId, partId);
    if (!target) return tr("Select a model or modifier to edit its slicing parameters.");
    const auto base = GPlatform::SlicingBackend::instance().configSnapshot();
    if (!base) return tr("No active slicing profile is available.");

    const auto name = key.toStdString();
    std::string serialized;
    if (!inherit) {
        QReadLocker locker(&m_lock);
        const auto* item = findItemLocked(name);
        if (!item) return tr("Unknown slicing parameter.");
        serialized = serializeValueLocked(*item, value);
    }
    std::string changed = target.semantics();
    const auto result = libslicer::edit_project_override(*base, target.plateSemantics(), changed,
        target.parentSemantics(), name, inherit ? std::nullopt : std::optional<std::string_view>(serialized));
    if (!result)
        return result.diagnostics.empty() ? tr("Invalid slicing parameter.")
            : translatedSettingText(QString::fromStdString(result.diagnostics.front().message));
    if (changed == target.semantics()) return {};

    TransactionGuard transaction(target.part ? "Edit modifier slicing parameter" : "Edit model slicing parameter");
    const auto instance = selectedParameterInstance(modelId);
    if (!instance) {
        transaction.rollback();
        return tr("The selected model is no longer available.");
    }
    const auto unique = ModelGraphUtil::ensureUniqueObjectForInstance(instance->getDBInstanceID());
    if (!unique) {
        transaction.rollback();
        return tr("Unable to isolate the selected model for editing.");
    }
    auto* document = DocumentManager::instance();
    if (target.part) {
        const auto partId = unique.mapPart(target.part->getDBInstanceID());
        const auto part = document->getDB<ModelPartDB>(partId);
        if (!part) {
            transaction.rollback();
            return tr("The selected modifier is no longer available.");
        }
        part->setProjectSemantics(changed);
        const auto* selection = SelectionBridge::instance();
        if (selection->activeInstanceId() == instance->getDBInstanceID() &&
            selection->activePartId() == target.part->getDBInstanceID())
            SelectionBridge::instance()->setSelectedModifier(instance->getDBInstanceID(), partId);
    } else {
        document->getDB<ModelObjectDB>(unique.objectId)->setProjectSemantics(changed);
    }
    transaction.commit();
    markNeedsReslice();
    return {};
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
    {
        QReadLocker locker(&m_lock);
        if (!m_projectMachine.isEmpty()) return m_projectMachine;
    }
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
    const std::vector<libslicer::FilamentSlotInfo>& filamentSlots,
    const std::vector<libslicer::SettingItem>& settings) {
    // Read the material process from the existing settings contract instead of
    // extending the binary SDK's FilamentSlotInfo layout.
    const auto process = std::find_if(settings.begin(), settings.end(), [](const auto& item) {
        return item.key == "filament_process_type";
    });
    const auto processTypes = process == settings.end() ? QStringList{}
        : QString::fromStdString(process->value).split(';');
    QVariantList result;
    result.reserve(static_cast<qsizetype>(filamentSlots.size()));
    for (const auto& slot : filamentSlots) {
        result.push_back(QVariantMap{
            {QStringLiteral("index"), static_cast<qulonglong>(slot.index)},
            {QStringLiteral("number"), static_cast<qulonglong>(slot.index + 1)},
            {QStringLiteral("presetId"), QString::fromStdString(slot.preset_id)},
            {QStringLiteral("presetName"), QString::fromStdString(slot.preset_name)},
            {QStringLiteral("presetModified"), slot.preset_modified},
            {QStringLiteral("compatiblePresets"), presetOptions(slot.compatible_presets)},
            {QStringLiteral("vendor"), QString::fromStdString(slot.vendor)},
            {QStringLiteral("materialType"), QString::fromStdString(slot.material_type)},
            {QStringLiteral("processType"), slot.index < static_cast<std::size_t>(processTypes.size())
                ? processTypes.at(static_cast<qsizetype>(slot.index))
                : processTypes.size() == 1 ? processTypes.front() : QString::fromStdString(slot.physical_tool_role)},
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

namespace {
struct PrinterDocumentEdit {
    std::function<void()> apply;
    std::function<void()> restore;
};

template<typename Value, typename Setter>
void stagePrinterEdit(std::vector<PrinterDocumentEdit>& edits, Value oldValue, Value newValue, Setter setter)
{
    if (oldValue == newValue) return;
    edits.push_back({[setter, newValue = std::move(newValue)] { setter(newValue); },
        [setter, oldValue = std::move(oldValue)] { setter(oldValue); }});
}

void stageDocumentFilamentChanges(DocumentManager& document, std::size_t count,
    std::vector<PrinterDocumentEdit>& edits)
{
    const auto unavailable = [count](int slot) { return slot > 0 && std::size_t(slot) > count; };
    const auto stageSettings = [&](const auto& model, std::string_view scope) {
        const auto before = model->getProjectSemantics();
        auto after = libslicer::synchronize_project_model_materials(before, scope, count);
        stagePrinterEdit(edits, before, std::move(after),
            [model](const auto& value) { model->setProjectSemantics(value); });
    };
    for (const auto& node : document.getDBInstancesByType(TypeID::SLICING_CONFIG_DB)) {
        if (const auto config = std::dynamic_pointer_cast<GPlatform::SlicingConfigDB>(node);
            config && unavailable(config->getDefaultFilamentSlot()))
            stagePrinterEdit(edits, config->getDefaultFilamentSlot(), 1,
                [config](int value) { config->setDefaultFilamentSlot(value); });
    }
    for (const auto& node : document.getDBInstancesByType(TypeID::MODEL_OBJECT_DB)) {
        const auto object = std::dynamic_pointer_cast<ModelObjectDB>(node);
        if (!object) continue;
        if (unavailable(object->getDefaultFilamentSlot()))
            stagePrinterEdit(edits, object->getDefaultFilamentSlot(), 1,
                [object](int value) { object->setDefaultFilamentSlot(value); });
        stageSettings(object, "object");
    }
    for (const auto& node : document.getDBInstancesByType(TypeID::MODEL_PART_DB)) {
        const auto part = std::dynamic_pointer_cast<ModelPartDB>(node);
        if (!part) continue;
        if (part->getFilamentBindingMode() == int(ModelFilamentBindingMode::ExplicitSlot) &&
            unavailable(part->getDefaultFilamentSlot()))
            stagePrinterEdit(edits, part->getFilamentBindingMode(), int(ModelFilamentBindingMode::InheritObject),
                [part](int value) { part->setFilamentBindingMode(value); });
        stageSettings(part, "part");

        const auto colors = part->surfaceColors();
        if (!colors || colors->getSurfaceColorData().empty()) continue;
        ManualSupportOrcaScaffold::TriangleSplittingData paint;
        if (!ModelSurfaceColorCodec::decode(colors->getSurfaceColorData(), &paint))
            throw std::runtime_error("Invalid model paint data");
        if (std::none_of(paint.leafLabels.begin(), paint.leafLabels.end(), unavailable)) continue;
        for (auto& label : paint.leafLabels)
            if (unavailable(label)) label = ManualSupportOrcaScaffold::NoLabel;
        paint.update_used_states();
        std::string data;
        if (!ModelSurfaceColorCodec::encode(paint, &data))
            throw std::runtime_error("Could not update model paint data");
        stagePrinterEdit(edits, colors->getSurfaceColorData(), std::move(data),
            [colors](const auto& value) { colors->setSurfaceColorData(value); });
        stagePrinterEdit(edits, colors->getRevision(), colors->getRevision() + 1,
            [colors](auto value) { colors->setRevision(value); });
    }
}
} // namespace

bool SliceSettingsBridge::applyPrinterSwitch(PendingPrinterSwitch&& pending)
{
    if (configurationMutationBlocked("machine selection") || TransactionManager::instance().isInTransaction())
        return false;
    auto& backend = GPlatform::SlicingBackend::instance();
    const auto current = backend.activeConfigView();
    const auto machine = backend.activeProjectMachine();
    auto candidate = backend.preparePrinterSwitch(pending.target, pending.review.revision,
        pending.decisions, pending.remembered);
    const auto reject = [&](const QString& message) {
        emit settingRejected(QStringLiteral("printer"),
            pending.presetSaved ? tr("Preset saved. Printer switch failed. %1").arg(message) : message);
        emit machineSelectionChanged();
        return false;
    };
    if (!candidate)
        return reject(candidate.diagnostics.empty() ? tr("Could not switch printer")
            : QString::fromStdString(candidate.diagnostics.front().message));

    const auto& targetMachine = candidate.configuration.machine();
    const auto target = candidate.configuration.snapshot();
    auto prepared = prepareConfiguration(candidate.configuration.view(), targetMachine);
    auto* document = DocumentManager::instance();
    std::vector<PrinterDocumentEdit> edits;
    std::shared_ptr<GPlatform::SlicingConfigDB> projection;
    try {
        if (document) {
            const bool toolsChanged = machine &&
                machine->variant.physical_tool_count != targetMachine.variant.physical_tool_count;
            const bool updateCount = toolsChanged &&
                target.value("single_extruder_multi_material").value_or("0") == "0";
            if (updateCount)
                stageDocumentFilamentChanges(*document, prepared.view.filament_slots.size(), edits);
            for (const auto& object : document->getDBInstancesByType(TypeID::PRINT_BED_DB)) {
                const auto bed = std::dynamic_pointer_cast<PrintBedDB>(object);
                if (!bed || bed->getProjectSemantics().empty()) continue;
                auto before = bed->getProjectSemantics();
                auto after = libslicer::synchronize_project_plate_materials(before, target, nullptr, updateCount,
                    toolsChanged && targetMachine.variant.physical_tool_count == 1);
                stagePrinterEdit(edits, std::move(before), std::move(after),
                    [bed](const auto& value) { bed->setProjectSemantics(value); });
            }
            const auto configs = document->getDBInstancesByType(TypeID::SLICING_CONFIG_DB);
            if (!configs.empty()) projection = std::dynamic_pointer_cast<GPlatform::SlicingConfigDB>(configs.front());
            if (projection)
                stagePrinterEdit(edits, projection->getFilaments(), documentFilaments(prepared.view.filament_slots),
                    [projection](const auto& value) { projection->setFilaments(value); });
        }
    } catch (const std::exception& error) {
        return reject(QString::fromUtf8(error.what()));
    }

    auto lease = backend.beginProjectCommit(pending.review.revision);
    if (!lease) return reject(tr("The configuration has changed. Choose the printer again."));
    const auto installed = lease.install(candidate.configuration);
    if (!installed) {
        lease.release();
        return reject(installed.diagnostics.empty() ? tr("Could not switch printer")
            : QString::fromStdString(installed.diagnostics.front().message));
    }

    std::size_t applied = 0;
    try {
        TransientUpdateGuard transient;
        while (applied < edits.size()) {
            const auto& edit = edits[applied++];
            edit.apply();
        }
        if (projection) projection->setActiveRevision(installed.revision);
    } catch (const std::exception& error) {
        TransientUpdateGuard transient;
        while (applied > 0) {
            try { edits[--applied].restore(); }
            catch (const std::exception& rollbackError) {
                LOG_ERROR("SliceSettingsBridge: printer switch rollback: {}", rollbackError.what());
            }
        }
        const auto restored = lease.install(installed.previous);
        if (restored && projection) projection->setActiveRevision(restored.revision);
        lease.release();
        return reject(QString::fromUtf8(error.what()));
    }

    const auto targetSelection = prepared.view.selection;
    const bool projectLocal = prepared.projectLocal;
    const bool rememberSourceColors = !m_projectPalette;
    prepared.view.revision = installed.revision;
    installCommittedConfiguration(std::move(prepared), false);
    lease.release();
    if (current && machine && !machine->project_local)
        persistSelection(current->selection, false, installed.previous.snapshot(), rememberSourceColors);
    if (!projectLocal) persistSelection(targetSelection, true, target);
    publishCommittedConfiguration();
    return true;
}

bool SliceSettingsBridge::replaceConfig(GPlatform::SlicingConfigActivationResult&& result,
                                       bool markPending, bool synchronizePlateMaterials) {
    if (!result) {
        const auto message = result.diagnostics.empty() ? std::string("unknown preset selection error")
            : result.diagnostics.front().key + ": " + result.diagnostics.front().message;
        setConfigurationError(QString::fromStdString(message));
        return false;
    }
    auto prepared = prepareConfiguration(std::move(result.view), GPlatform::SlicingBackend::instance().activeProjectMachine());
    {
        QReadLocker locker(&m_lock);
        if (synchronizePlateMaterials) {
            bool domainChanged = m_selection.machine_model_id != prepared.view.selection.machine_model_id ||
                m_selection.machine_variant_id != prepared.view.selection.machine_variant_id ||
                m_selection.filament_preset_ids.size() != prepared.view.selection.filament_preset_ids.size();
            for (const char* key : {"filament_process_type", "physical_extruder_map",
                    "toolhead_process_capabilities", "toolhead_filament_capacity",
                    "filament_slots_bound_to_physical_tools", "filament_map"}) {
                const auto* previous = findItemLocked(key);
                const auto next = std::find_if(prepared.view.settings.begin(), prepared.view.settings.end(),
                    [key](const auto& item) { return item.key == key; });
                domainChanged = domainChanged ||
                    (previous ? previous->value : std::string{}) !=
                    (next != prepared.view.settings.end() ? next->value : std::string{});
            }
            synchronizePlateMaterials = domainChanged;
        }
    }
    if (synchronizePlateMaterials) {
        // Update every real plate on the document thread, before publishing the
        // revision or any Bridge notification. Import refresh and explicit map
        // edits deliberately do not repair bindings: slicing still validates them.
        const auto target = GPlatform::SlicingBackend::instance().configSnapshot();
        if (auto* document = DocumentManager::instance(); document && target) {
            TransientUpdateGuard guard;
            for (const auto& object : document->getDBInstancesByType(TypeID::PRINT_BED_DB)) {
                const auto bed = std::dynamic_pointer_cast<PrintBedDB>(object);
                if (!bed || bed->getProjectSemantics().empty()) continue;
                try {
                    std::vector<libslicer::ConfigDiagnostic> diagnostics;
                    const auto semantics = libslicer::synchronize_project_plate_materials(
                        bed->getProjectSemantics(), *target, &diagnostics);
                    if (semantics != bed->getProjectSemantics()) bed->setProjectSemantics(semantics);
                    for (const auto& diagnostic : diagnostics)
                        LOG_WARN("SliceSettingsBridge: plate {} {}: {}", bed->getDBInstanceID().getValue(),
                                 diagnostic.key, diagnostic.message);
                } catch (const std::exception& error) {
                    // Preserve malformed imported data for the strict slice
                    // diagnostics, rather than discarding the whole plate.
                    LOG_ERROR("SliceSettingsBridge: cannot synchronize plate {}: {}",
                              bed->getDBInstanceID().getValue(), error.what());
                }
            }
        }
    }
    publishActiveSlicingConfig(prepared.view.revision, prepared.view.filament_slots);
    if (!prepared.projectLocal) persistSelection(prepared.view.selection, true, {}, !m_projectPalette);
    installCommittedConfiguration(std::move(prepared));
    if (!markPending) { QWriteLocker locker(&m_lock); m_hasPendingReslice = false; }
    publishCommittedConfiguration();
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
    auto schema = prepareSchema(m_items);
    m_schemaSettings.swap(schema.settings);
    m_schemaPages.swap(schema.pages);
    m_schemaByKey.swap(schema.byKey);
    m_schemaLoaded = !m_schemaSettings.isEmpty();
    ++m_schemaRevision;
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
    const std::string& serialized) const { return displaySettingValue(item, serialized); }

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
