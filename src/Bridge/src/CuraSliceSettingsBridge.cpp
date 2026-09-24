#include "CuraSliceSettingsBridge.hpp"
#include "BridgeRegistration.hpp"
#include "MachineGCodeUtil.hpp"

#include "Foundation/Log.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
constexpr const char* kDefaultSchemaPath = ":/qt/qml/GPlatform/config/slice_settings.schema.json";

QString inferControlType(const QString& type) {
    if (type == "bool") {
        return "switch";
    }
    if (type == "int" || type == "float") {
        return "number";
    }
    if (type == "enum") {
        return "combo";
    }
    if (type == "multiline") {
        return "multiline";
    }
    return "text";
}

bool parseBoolLoose(const QVariant& value, bool* ok) {
    if (value.typeId() == QMetaType::Bool) {
        *ok = true;
        return value.toBool();
    }

    if (value.canConvert<int>()) {
        bool convertOk = false;
        int intValue = value.toInt(&convertOk);
        if (convertOk) {
            *ok = true;
            return intValue != 0;
        }
    }

    QString text = value.toString().trimmed().toLower();
    if (text == "true" || text == "1" || text == "yes" || text == "on") {
        *ok = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "no" || text == "off") {
        *ok = true;
        return false;
    }

    *ok = false;
    return false;
}

double parseDoubleLoose(const QVariant& value, bool* ok) {
    double number = value.toDouble(ok);
    if (*ok) {
        return number;
    }

    const QString text = value.toString();
    const QRegularExpression re("[-+]?\\d*\\.?\\d+");
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        *ok = false;
        return 0.0;
    }

    number = match.captured(0).toDouble(ok);
    return number;
}

int parseIntLoose(const QVariant& value, bool* ok) {
    int intValue = value.toInt(ok);
    if (*ok) {
        return intValue;
    }

    bool doubleOk = false;
    const double number = parseDoubleLoose(value, &doubleOk);
    if (!doubleOk) {
        *ok = false;
        return 0;
    }

    *ok = true;
    return static_cast<int>(number);
}

int tabOrder(const QString& tab) {
    if (tab == "process") {
        return 0;
    }
    if (tab == "filament") {
        return 1;
    }
    if (tab == "printer") {
        return 2;
    }
    return 100;
}
} // namespace

CuraSliceSettingsBridge::CuraSliceSettingsBridge(QObject* parent)
    : bridge::BridgeBase(parent)
    , m_settings(defaultSettings()) {
    QWriteLocker locker(&m_lock);
    if (!loadSchemaLocked(QString())) {
        LOG_WARN("CuraSliceSettingsBridge: schema file not found, continue with built-in defaults");
    }
}

CuraSliceSettingsBridge* CuraSliceSettingsBridge::instance() {
    static CuraSliceSettingsBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new CuraSliceSettingsBridge();
    }
    return s_instance;
}

CuraSliceSettingsBridge* CuraSliceSettingsBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    CuraSliceSettingsBridge* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

bool CuraSliceSettingsBridge::enableBeltPrinter() const {
    QReadLocker locker(&m_lock);
    return m_enableBeltPrinter;
}

void CuraSliceSettingsBridge::setEnableBeltPrinter(bool enabled) {
    bool pendingChanged = false;
    {
        QWriteLocker locker(&m_lock);
        if (m_enableBeltPrinter == enabled) {
            return;
        }
        m_enableBeltPrinter = enabled;
        if (!m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }
    emit enableBeltPrinterChanged();
    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

QVariantMap CuraSliceSettingsBridge::settings() const {
    QReadLocker locker(&m_lock);
    return m_settings;
}

void CuraSliceSettingsBridge::setSettings(const QVariantMap& newSettings) {
    bool pendingChanged = false;
    QVariantMap normalized;

    {
        QWriteLocker locker(&m_lock);
        normalized.clear();

        for (auto it = newSettings.constBegin(); it != newSettings.constEnd(); ++it) {
            bool ok = false;
            const QVariant normalizedValue = normalizeValueBySchemaLocked(it.key(), it.value(), &ok);
            if (!ok) {
                LOG_WARN("CuraSliceSettingsBridge: skip invalid setting '{}': {}", it.key().toStdString(),
                         it.value().toString().toStdString());
                continue;
            }
            normalized.insert(it.key(), normalizedValue);
        }

        if (m_schemaLoaded) {
            for (const QVariant& defValue : m_schemaSettings) {
                const QVariantMap def = defValue.toMap();
                const QString key = def.value("key").toString();
                if (key.isEmpty() || normalized.contains(key) || !def.contains("default")) {
                    continue;
                }
                normalized.insert(key, def.value("default"));
            }
        }

        if (m_settings == normalized) {
            return;
        }

        m_settings = normalized;
        if (!m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }

    emit settingsChanged();
    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

QVariant CuraSliceSettingsBridge::getSetting(const QString& key, const QVariant& defaultValue) const {
    QReadLocker locker(&m_lock);
    if (m_settings.contains(key)) {
        return m_settings.value(key);
    }

    const auto defIt = m_schemaByKey.constFind(key);
    if (defIt != m_schemaByKey.constEnd() && defIt.value().contains("default")) {
        return defIt.value().value("default");
    }

    return defaultValue;
}

void CuraSliceSettingsBridge::setSetting(const QString& key, const QVariant& value) {
    bool pendingChanged = false;
    QVariant normalized;

    {
        QWriteLocker locker(&m_lock);
        bool ok = false;
        normalized = normalizeValueBySchemaLocked(key, value, &ok);
        if (!ok) {
            LOG_WARN("CuraSliceSettingsBridge: reject invalid value for '{}': {}", key.toStdString(),
                     value.toString().toStdString());
            return;
        }

        if (m_settings.value(key) == normalized) {
            return;
        }

        m_settings.insert(key, normalized);
        if (!m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }

    emit settingChanged(key, normalized);
    emit settingsChanged();
    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

void CuraSliceSettingsBridge::ensureSetting(const QString& key, const QVariant& value) {
    bool inserted = false;
    QVariant normalized;

    {
        QWriteLocker locker(&m_lock);
        if (m_settings.contains(key)) {
            return;
        }

        bool ok = false;
        normalized = normalizeValueBySchemaLocked(key, value, &ok);
        if (!ok) {
            return;
        }

        m_settings.insert(key, normalized);
        inserted = true;
    }

    if (inserted) {
        emit settingsChanged();
    }
}

void CuraSliceSettingsBridge::ensureMachineGCodeDefaultsIfNeeded() {
    bool startChanged = false;
    bool endChanged = false;
    bool pendingChanged = false;
    QString startValue;
    QString endValue;

    {
        QWriteLocker locker(&m_lock);

        const auto schemaDefaultFor = [this](const QString& key) -> QString {
            const auto it = m_schemaByKey.constFind(key);
            if (it == m_schemaByKey.constEnd()) {
                return QString();
            }
            return it.value().value("default").toString();
        };

        const QString startKey = QStringLiteral("machine_start_gcode");
        const QString endKey = QStringLiteral("machine_end_gcode");
        const QString currentStart = m_settings.value(startKey).toString();
        const QString currentEnd = m_settings.value(endKey).toString();
        const QString schemaDefaultStart = schemaDefaultFor(startKey);
        const QString schemaDefaultEnd = schemaDefaultFor(endKey);
        const QString defaultStart = GPlatform::MachineGCodeUtil::defaultMachineStartGCode();
        const QString defaultEnd = GPlatform::MachineGCodeUtil::defaultMachineEndGCode();

        const auto isMissingOrPlaceholder = [](const QString& value) -> bool {
            const QString normalized = value.trimmed();
            return normalized.compare("none", Qt::CaseInsensitive) == 0 ||
                   normalized.compare("null", Qt::CaseInsensitive) == 0;
        };

        const bool startMissing = !m_settings.contains(startKey) || isMissingOrPlaceholder(currentStart);
        const bool endMissing = !m_settings.contains(endKey) || isMissingOrPlaceholder(currentEnd);
        const bool startIsSchemaDefault = (!schemaDefaultStart.isEmpty() && currentStart == schemaDefaultStart);
        const bool endIsSchemaDefault = (!schemaDefaultEnd.isEmpty() && currentEnd == schemaDefaultEnd);

        if ((startMissing || startIsSchemaDefault) && currentStart != defaultStart) {
            m_settings.insert(startKey, defaultStart);
            startChanged = true;
            startValue = defaultStart;
        }

        if ((endMissing || endIsSchemaDefault) && currentEnd != defaultEnd) {
            m_settings.insert(endKey, defaultEnd);
            endChanged = true;
            endValue = defaultEnd;
        }

        if ((startChanged || endChanged) && !m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }

    if (startChanged) {
        emit settingChanged("machine_start_gcode", startValue);
    }
    if (endChanged) {
        emit settingChanged("machine_end_gcode", endValue);
    }
    if (startChanged || endChanged) {
        emit settingsChanged();
    }
    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

bool CuraSliceSettingsBridge::hasSetting(const QString& key) const {
    QReadLocker locker(&m_lock);
    return m_settings.contains(key);
}

void CuraSliceSettingsBridge::removeSetting(const QString& key) {
    bool removed = false;
    bool pendingChanged = false;
    {
        QWriteLocker locker(&m_lock);
        removed = m_settings.remove(key) > 0;
        if (removed && !m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }

    if (removed) {
        emit settingsChanged();
        if (pendingChanged) {
            emit hasPendingResliceChanged();
        }
    }
}

void CuraSliceSettingsBridge::resetToDefaults() {
    bool pendingChanged = false;
    {
        QWriteLocker locker(&m_lock);
        m_settings = defaultSettings();
        if (m_schemaLoaded) {
            ensureSchemaDefaultsLocked();
        }

        m_enableBeltPrinter = true;
        if (!m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }

    emit settingsChanged();
    emit enableBeltPrinterChanged();
    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

void CuraSliceSettingsBridge::markCurrentAsSliced() {
    bool pendingChanged = false;
    {
        QWriteLocker locker(&m_lock);
        if (m_hasPendingReslice) {
            m_hasPendingReslice = false;
            pendingChanged = true;
        }
    }

    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

void CuraSliceSettingsBridge::markNeedsReslice() {
    bool pendingChanged = false;
    {
        QWriteLocker locker(&m_lock);
        if (!m_hasPendingReslice) {
            m_hasPendingReslice = true;
            pendingChanged = true;
        }
    }

    if (pendingChanged) {
        emit hasPendingResliceChanged();
    }
}

bool CuraSliceSettingsBridge::reloadSchema(const QString& resourcePath) {
    bool loaded = false;
    bool defaultsMutated = false;

    {
        QWriteLocker locker(&m_lock);
        const QVariantMap before = m_settings;
        loaded = loadSchemaLocked(resourcePath);
        defaultsMutated = before != m_settings;
    }

    if (loaded) {
        emit schemaChanged();
        if (defaultsMutated) {
            emit settingsChanged();
        }
    }

    return loaded;
}

QVariantMap CuraSliceSettingsBridge::getSettingSchema(const QString& key) const {
    QReadLocker locker(&m_lock);
    return m_schemaByKey.value(key);
}

QVariantList CuraSliceSettingsBridge::getTabPages(const QString& tab, bool showAdvancedMode) const {
    QReadLocker locker(&m_lock);
    QVariantList pages;

    if (!m_schemaLoaded) {
        return pages;
    }

    const QString targetTab = tab.trimmed().toLower();
    for (const QVariant& pageValue : m_schemaPages) {
        const QVariantMap page = pageValue.toMap();
        const QString pageTab = page.value("tab").toString().trimmed().toLower();
        if (!targetTab.isEmpty() && pageTab != targetTab) {
            continue;
        }

        const QString mode = page.value("mode", "basic").toString().trimmed().toLower();
        if (!showAdvancedMode && (mode == "advanced" || mode == "expert")) {
            continue;
        }

        QVariantMap item;
        item.insert("id", page.value("page"));
        item.insert("title", page.value("title", page.value("page")));
        item.insert("order", page.value("order", 1000));
        pages.push_back(item);
    }

    std::sort(pages.begin(), pages.end(), [](const QVariant& left, const QVariant& right) {
        const QVariantMap l = left.toMap();
        const QVariantMap r = right.toMap();
        const int lo = l.value("order", 1000).toInt();
        const int ro = r.value("order", 1000).toInt();
        if (lo == ro) {
            return l.value("title").toString() < r.value("title").toString();
        }
        return lo < ro;
    });

    return pages;
}

QVariantList CuraSliceSettingsBridge::getPageSchema(const QString& tab,
                                                    const QString& page,
                                                    bool showAdvancedMode,
                                                    const QString& searchText) const {
    QReadLocker locker(&m_lock);
    QVariantList sections;

    if (!m_schemaLoaded) {
        return sections;
    }

    const QString targetTab = tab.trimmed().toLower();
    const QString targetPage = page.trimmed().toLower();
    const QString needle = searchText.trimmed().toLower();

    QHash<QString, int> sectionIndex;
    for (const QVariant& defValue : m_schemaSettings) {
        const QVariantMap def = defValue.toMap();
        const QString defTab = def.value("tab").toString().trimmed().toLower();
        const QString defPage = def.value("page").toString().trimmed().toLower();

        if (!targetTab.isEmpty() && defTab != targetTab) {
            continue;
        }
        if (!targetPage.isEmpty() && defPage != targetPage) {
            continue;
        }

        const QString mode = def.value("mode", "basic").toString().trimmed().toLower();
        if (!showAdvancedMode && (mode == "advanced" || mode == "expert")) {
            continue;
        }

        const QString key = def.value("key").toString();
        const QString label = def.value("label", key).toString();
        const QString labelZh = def.value("labelZh").toString();
        if (!needle.isEmpty() && !key.toLower().contains(needle) && !label.toLower().contains(needle) &&
            !labelZh.toLower().contains(needle)) {
            continue;
        }

        const QString sectionId = def.value("section", "misc").toString();
        if (!sectionIndex.contains(sectionId)) {
            QVariantMap section;
            section.insert("id", sectionId);
            section.insert("title", def.value("sectionTitle", sectionId));
            section.insert("titleZh", def.value("sectionTitleZh"));
            section.insert("sectionOrder", def.value("sectionOrder", 1000));
            section.insert("rows", QVariantList{});
            sections.push_back(section);
            sectionIndex.insert(sectionId, sections.size() - 1);
        }

        QVariantMap row;
        row.insert("key", key);
        row.insert("label", label);
        row.insert("labelZh", def.value("labelZh"));
        row.insert("type", def.value("type", "string"));
        row.insert("control", def.value("control", "text"));
        row.insert("unit", def.value("unit"));
        row.insert("min", def.value("min"));
        row.insert("max", def.value("max"));
        row.insert("decimals", def.value("decimals", 2));
        row.insert("options", def.value("options", QVariantList{}));
        row.insert("defaultValue", def.value("default"));
        const bool autoInit = def.contains("autoInit") ? def.value("autoInit").toBool() : def.contains("default");
        row.insert("autoInit", autoInit);
        row.insert("rowOrder", def.value("rowOrder", 1000));
        row.insert("value", m_settings.value(key, def.value("default")));

        QVariantMap section = sections.at(sectionIndex.value(sectionId)).toMap();
        QVariantList rows = section.value("rows").toList();
        rows.push_back(row);
        section.insert("rows", rows);
        sections[sectionIndex.value(sectionId)] = section;
    }

    for (int i = 0; i < sections.size(); ++i) {
        QVariantMap section = sections.at(i).toMap();
        QVariantList rows = section.value("rows").toList();
        std::sort(rows.begin(), rows.end(), [](const QVariant& left, const QVariant& right) {
            const QVariantMap l = left.toMap();
            const QVariantMap r = right.toMap();
            const int lo = l.value("rowOrder", 1000).toInt();
            const int ro = r.value("rowOrder", 1000).toInt();
            if (lo == ro) {
                return l.value("label").toString() < r.value("label").toString();
            }
            return lo < ro;
        });
        section.insert("rows", rows);
        sections[i] = section;
    }

    std::sort(sections.begin(), sections.end(), [](const QVariant& left, const QVariant& right) {
        const QVariantMap l = left.toMap();
        const QVariantMap r = right.toMap();
        const int lo = l.value("sectionOrder", 1000).toInt();
        const int ro = r.value("sectionOrder", 1000).toInt();
        if (lo == ro) {
            return l.value("title").toString() < r.value("title").toString();
        }
        return lo < ro;
    });

    return sections;
}

QVariantList CuraSliceSettingsBridge::getGlobalSearchResults(const QString& searchText,
                                                             bool showAdvancedMode) const {
    QReadLocker locker(&m_lock);
    QVariantList results;

    if (!m_schemaLoaded) {
        return results;
    }

    const QString needle = searchText.trimmed().toLower();
    if (needle.isEmpty()) {
        return results;
    }

    QHash<QString, QVariantMap> pageMetaById;
    for (const QVariant& pageValue : m_schemaPages) {
        const QVariantMap pageDef = pageValue.toMap();
        const QString tab = pageDef.value("tab").toString().trimmed().toLower();
        const QString page = pageDef.value("page").toString().trimmed().toLower();
        if (tab.isEmpty() || page.isEmpty()) {
            continue;
        }
        pageMetaById.insert(tab + "::" + page, pageDef);
    }

    for (const QVariant& defValue : m_schemaSettings) {
        const QVariantMap def = defValue.toMap();
        const QString key = def.value("key").toString();
        const QString label = def.value("label", key).toString();
        const QString labelZh = def.value("labelZh").toString();
        const QString defTab = def.value("tab").toString().trimmed().toLower();
        const QString defPage = def.value("page").toString().trimmed().toLower();
        const QString sectionId = def.value("section", "misc").toString();
        const QString sectionTitle = def.value("sectionTitle", sectionId).toString();
        const QString sectionTitleZh = def.value("sectionTitleZh").toString();

        const QString mode = def.value("mode", "basic").toString().trimmed().toLower();
        if (!showAdvancedMode && (mode == "advanced" || mode == "expert")) {
            continue;
        }

        if (!key.toLower().contains(needle) && !label.toLower().contains(needle) &&
            !labelZh.toLower().contains(needle)) {
            continue;
        }

        const QString pageMetaKey = defTab + "::" + defPage;
        const QVariantMap pageMeta = pageMetaById.value(pageMetaKey);

        QVariantMap row;
        row.insert("key", key);
        row.insert("label", label);
        row.insert("labelZh", labelZh);
        row.insert("type", def.value("type", "string"));
        row.insert("control", def.value("control", "text"));
        row.insert("unit", def.value("unit"));
        row.insert("min", def.value("min"));
        row.insert("max", def.value("max"));
        row.insert("decimals", def.value("decimals", 2));
        row.insert("options", def.value("options", QVariantList{}));
        row.insert("defaultValue", def.value("default"));
        const bool autoInit = def.contains("autoInit") ? def.value("autoInit").toBool() : def.contains("default");
        row.insert("autoInit", autoInit);
        row.insert("value", m_settings.value(key, def.value("default")));
        row.insert("rowOrder", def.value("rowOrder", 1000));
        row.insert("sectionOrder", def.value("sectionOrder", 1000));
        row.insert("tabOrder", tabOrder(defTab));
        row.insert("tab", defTab);
        row.insert("page", defPage);
        row.insert("section", sectionId);
        row.insert("sectionTitle", sectionTitle);
        row.insert("sectionTitleZh", sectionTitleZh);
        row.insert("pageTitle", pageMeta.value("title", defPage));
        row.insert("pageTitleZh", pageMeta.value("titleZh"));
        row.insert("pageOrder", pageMeta.value("order", def.value("pageOrder", 1000)));
        results.push_back(row);
    }

    std::sort(results.begin(), results.end(), [](const QVariant& left, const QVariant& right) {
        const QVariantMap l = left.toMap();
        const QVariantMap r = right.toMap();

        const int lTabOrder = l.value("tabOrder", 100).toInt();
        const int rTabOrder = r.value("tabOrder", 100).toInt();
        if (lTabOrder != rTabOrder) {
            return lTabOrder < rTabOrder;
        }

        const int lPageOrder = l.value("pageOrder", 1000).toInt();
        const int rPageOrder = r.value("pageOrder", 1000).toInt();
        if (lPageOrder != rPageOrder) {
            return lPageOrder < rPageOrder;
        }

        const int lSectionOrder = l.value("sectionOrder", 1000).toInt();
        const int rSectionOrder = r.value("sectionOrder", 1000).toInt();
        if (lSectionOrder != rSectionOrder) {
            return lSectionOrder < rSectionOrder;
        }

        const int lRowOrder = l.value("rowOrder", 1000).toInt();
        const int rRowOrder = r.value("rowOrder", 1000).toInt();
        if (lRowOrder != rRowOrder) {
            return lRowOrder < rRowOrder;
        }

        return l.value("label").toString() < r.value("label").toString();
    });

    return results;
}

bool CuraSliceSettingsBridge::hasPendingReslice() const {
    QReadLocker locker(&m_lock);
    return m_hasPendingReslice;
}

bool CuraSliceSettingsBridge::schemaLoaded() const {
    QReadLocker locker(&m_lock);
    return m_schemaLoaded;
}

int CuraSliceSettingsBridge::schemaRevision() const {
    QReadLocker locker(&m_lock);
    return m_schemaRevision;
}

CuraSliceSettingsSnapshot CuraSliceSettingsBridge::snapshotForExecutor() const {
    QReadLocker locker(&m_lock);

    CuraSliceSettingsSnapshot snapshot;
    snapshot.settings = m_settings;
    snapshot.engineSettings = buildEngineSettingsLocked();
    snapshot.enableBeltPrinter = m_enableBeltPrinter;
    return snapshot;
}

QVariantMap CuraSliceSettingsBridge::defaultSettings() const {
    QVariantMap defaults;

    defaults.insert("config_profile", "blackbelt");
    defaults.insert("machine_nozzle_size", "0.40 mm");
    defaults.insert("blackbelt_gantry_angle", "45°");
    defaults.insert("layer_height", 0.2);
    defaults.insert("line_width", 0.4);
    defaults.insert("material_print_temperature", 210);
    defaults.insert("material_bed_temperature", 55);
    defaults.insert("speed_print", 35);
    defaults.insert("support_enable", true);

    return defaults;
}

bool CuraSliceSettingsBridge::loadSchemaLocked(const QString& resourcePath) {
    const QString path = resourcePath.trimmed().isEmpty() ? QString::fromUtf8(kDefaultSchemaPath) : resourcePath.trimmed();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_WARN("CuraSliceSettingsBridge: failed to open schema {}", path.toStdString());
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN("CuraSliceSettingsBridge: invalid schema json {}", path.toStdString());
        return false;
    }

    const QJsonObject rootObj = doc.object();
    const QJsonArray settingsArray = rootObj.value("settings").toArray();
    const QJsonArray pagesArray = rootObj.value("pages").toArray();
    QVariantList parsedSettings;
    QVariantList parsedPages;
    QHash<QString, QVariantMap> parsedMap;

    parsedSettings.reserve(settingsArray.size());
    for (const QJsonValue& entry : settingsArray) {
        if (!entry.isObject()) {
            continue;
        }

        QVariantMap def = entry.toObject().toVariantMap();
        const QString key = def.value("key").toString().trimmed();
        if (key.isEmpty()) {
            continue;
        }

        const QString type = def.value("type", "string").toString().trimmed().toLower();
        def.insert("key", key);
        def.insert("type", type.isEmpty() ? "string" : type);

        if (!def.contains("label") || def.value("label").toString().trimmed().isEmpty()) {
            def.insert("label", key);
        }
        if (!def.contains("control") || def.value("control").toString().trimmed().isEmpty()) {
            def.insert("control", inferControlType(def.value("type").toString()));
        }
        if (!def.contains("mode")) {
            def.insert("mode", "basic");
        }
        if (!def.contains("sectionOrder")) {
            def.insert("sectionOrder", 1000);
        }
        if (!def.contains("rowOrder")) {
            def.insert("rowOrder", 1000);
        }

        parsedSettings.push_back(def);
        parsedMap.insert(key, def);
    }

    if (!pagesArray.isEmpty()) {
        parsedPages.reserve(pagesArray.size());
        for (const QJsonValue& pageEntry : pagesArray) {
            if (!pageEntry.isObject()) {
                continue;
            }

            QVariantMap pageDef = pageEntry.toObject().toVariantMap();
            const QString tab = pageDef.value("tab").toString().trimmed().toLower();
            const QString page = pageDef.value("page").toString().trimmed().toLower();
            if (tab.isEmpty() || page.isEmpty()) {
                continue;
            }

            pageDef.insert("tab", tab);
            pageDef.insert("page", page);
            if (!pageDef.contains("title") || pageDef.value("title").toString().trimmed().isEmpty()) {
                pageDef.insert("title", page);
            }
            if (!pageDef.contains("mode")) {
                pageDef.insert("mode", "basic");
            }
            if (!pageDef.contains("order")) {
                pageDef.insert("order", 1000);
            }
            parsedPages.push_back(pageDef);
        }
    }

    if (parsedPages.isEmpty()) {
        QSet<QString> seenPages;
        for (const QVariant& settingValue : parsedSettings) {
            const QVariantMap def = settingValue.toMap();
            const QString tab = def.value("tab").toString().trimmed().toLower();
            const QString page = def.value("page").toString().trimmed().toLower();
            if (tab.isEmpty() || page.isEmpty()) {
                continue;
            }

            const QString pageKey = tab + "::" + page;
            if (seenPages.contains(pageKey)) {
                continue;
            }

            seenPages.insert(pageKey);
            QVariantMap pageDef;
            pageDef.insert("tab", tab);
            pageDef.insert("page", page);
            pageDef.insert("title", def.value("pageTitle", page));
            pageDef.insert("mode", def.value("mode", "basic"));
            pageDef.insert("order", def.value("pageOrder", 1000));
            parsedPages.push_back(pageDef);
        }
    }

    if (parsedSettings.isEmpty()) {
        LOG_WARN("CuraSliceSettingsBridge: schema {} has no settings", path.toStdString());
        return false;
    }

    m_schemaSettings = parsedSettings;
    m_schemaPages = parsedPages;
    m_schemaByKey = parsedMap;
    m_schemaLoaded = true;
    ++m_schemaRevision;

    ensureSchemaDefaultsLocked();
    LOG_INFO("CuraSliceSettingsBridge: loaded schema {} with {} settings", path.toStdString(), parsedSettings.size());
    return true;
}

void CuraSliceSettingsBridge::ensureSchemaDefaultsLocked() {
    for (const QVariant& defValue : m_schemaSettings) {
        const QVariantMap def = defValue.toMap();
        const QString key = def.value("key").toString();
        if (key.isEmpty() || m_settings.contains(key) || !def.contains("default")) {
            continue;
        }

        bool ok = false;
        QVariant normalized = normalizeValueBySchemaLocked(key, def.value("default"), &ok);
        if (!ok) {
            normalized = def.value("default");
        }
        m_settings.insert(key, normalized);
    }

}

QVariant CuraSliceSettingsBridge::normalizeValueBySchemaLocked(const QString& key, const QVariant& value, bool* ok) const {
    if (ok != nullptr) {
        *ok = true;
    }

    const auto defIt = m_schemaByKey.constFind(key);
    if (defIt == m_schemaByKey.constEnd()) {
        return value;
    }

    const QVariantMap def = defIt.value();
    const QString type = def.value("type", "string").toString().trimmed().toLower();

    if (type == "bool") {
        bool parsedOk = false;
        const bool parsed = parseBoolLoose(value, &parsedOk);
        if (ok != nullptr) {
            *ok = parsedOk;
        }
        return parsed;
    }

    if (type == "int") {
        bool parsedOk = false;
        int parsed = parseIntLoose(value, &parsedOk);
        if (!parsedOk) {
            if (ok != nullptr) {
                *ok = false;
            }
            return QVariant();
        }

        if (def.contains("min")) {
            parsed = std::max(parsed, def.value("min").toInt());
        }
        if (def.contains("max")) {
            parsed = std::min(parsed, def.value("max").toInt());
        }
        return parsed;
    }

    if (type == "float") {
        bool parsedOk = false;
        double parsed = parseDoubleLoose(value, &parsedOk);
        if (!parsedOk) {
            if (ok != nullptr) {
                *ok = false;
            }
            return QVariant();
        }

        if (def.contains("min")) {
            parsed = std::max(parsed, def.value("min").toDouble());
        }
        if (def.contains("max")) {
            parsed = std::min(parsed, def.value("max").toDouble());
        }
        return parsed;
    }

    if (type == "enum") {
        const QVariantList options = def.value("options").toList();
        if (options.isEmpty()) {
            return value.toString();
        }

        if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong ||
            value.typeId() == QMetaType::UInt || value.typeId() == QMetaType::ULongLong) {
            bool idxOk = false;
            const int idx = value.toInt(&idxOk);
            if (idxOk && idx >= 0 && idx < options.size()) {
                return options.at(idx);
            }
        }

        const QString target = value.toString().trimmed();
        for (const QVariant& option : options) {
            const QString optionText = option.toString();
            if (optionText == target) {
                return option;
            }
        }
        for (const QVariant& option : options) {
            const QString optionText = option.toString();
            if (optionText.compare(target, Qt::CaseInsensitive) == 0) {
                return option;
            }
        }

        if (ok != nullptr) {
            *ok = false;
        }
        return QVariant();
    }

    return value.toString();
}

QVariantMap CuraSliceSettingsBridge::buildEngineSettingsLocked() const {
    QVariantMap engine;

    for (auto it = m_settings.constBegin(); it != m_settings.constEnd(); ++it) {
        const QString key = it.key();
        const auto defIt = m_schemaByKey.constFind(key);
        if (defIt == m_schemaByKey.constEnd()) {
            engine.insert(key, it.value());
            continue;
        }

        const QVariantMap def = defIt.value();
        if (def.contains("engineExposed") && !def.value("engineExposed").toBool()) {
            continue;
        }

        QString engineKey = def.value("engineKey").toString().trimmed();
        if (engineKey.isEmpty()) {
            engineKey = key;
        }
        engine.insert(engineKey, it.value());
    }

    return engine;
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    CuraSliceSettingsBridge,
    "CuraSliceSettingsBridge",
    &CuraSliceSettingsBridge::create)
