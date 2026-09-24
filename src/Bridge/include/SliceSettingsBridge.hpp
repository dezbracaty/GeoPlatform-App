#pragma once

#include <BridgeBase.hpp>
#include "SlicingBackend.hpp"

#include <libslicer/Library.hpp>

#include <QHash>
#include <QColor>
#include <QJSEngine>
#include <QQmlEngine>
#include <QReadWriteLock>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>

#include <unordered_map>

class SliceSettingsBridge final : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(bool hasPendingReslice READ hasPendingReslice NOTIFY hasPendingResliceChanged)
    Q_PROPERTY(QVariantMap settings READ settings WRITE setSettings NOTIFY settingsChanged)
    Q_PROPERTY(bool schemaLoaded READ schemaLoaded NOTIFY schemaChanged)
    Q_PROPERTY(int schemaRevision READ schemaRevision NOTIFY schemaChanged)
    Q_PROPERTY(QString selectedMachineModelId READ selectedMachineModelId NOTIFY machineSelectionChanged)
    Q_PROPERTY(QString selectedMachineVariantId READ selectedMachineVariantId NOTIFY machineSelectionChanged)
    Q_PROPERTY(QVariantMap selectedMachine READ selectedMachine NOTIFY machineSelectionChanged)
    Q_PROPERTY(QString selectedProcessPresetId READ selectedProcessPresetId NOTIFY presetSelectionChanged)
    Q_PROPERTY(QString selectedProcessPresetName READ selectedProcessPresetName NOTIFY presetSelectionChanged)
    Q_PROPERTY(QString selectedFilamentPresetId READ selectedFilamentPresetId NOTIFY presetSelectionChanged)
    Q_PROPERTY(QString selectedFilamentPresetName READ selectedFilamentPresetName NOTIFY presetSelectionChanged)
    Q_PROPERTY(QVariantList compatibleProcessPresets READ compatibleProcessPresets NOTIFY presetSelectionChanged)
    Q_PROPERTY(QVariantList compatibleFilamentPresets READ compatibleFilamentPresets NOTIFY presetSelectionChanged)
    Q_PROPERTY(QVariantList filamentSlots READ filamentSlots NOTIFY filamentSlotsChanged)
    Q_PROPERTY(QString configurationError READ configurationError NOTIFY configurationErrorChanged)
    QML_ELEMENT
    QML_SINGLETON

public:
    static SliceSettingsBridge* instance();
    static SliceSettingsBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    QVariantMap settings() const;
    void setSettings(const QVariantMap& settings);

    Q_INVOKABLE QVariant getSetting(const QString& key,
                                    const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE void ensureSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE bool hasSetting(const QString& key) const;
    Q_INVOKABLE void removeSetting(const QString& key);
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void markCurrentAsSliced();
    Q_INVOKABLE void markNeedsReslice();
    Q_INVOKABLE QVariantList machineModels() const;
    Q_INVOKABLE QVariantList buildPlateOptions() const;
    Q_INVOKABLE bool selectMachine(const QString& modelId, const QString& variantId);
    Q_INVOKABLE bool selectProcessPreset(const QString& presetId);
    Q_INVOKABLE bool selectFilamentPreset(const QString& presetId);
    Q_INVOKABLE bool selectFilamentPresetForSlot(int slotIndex, const QString& presetId);
    Q_INVOKABLE bool setFilamentToolMapping(const QVariantList& tools, bool automatic);
    Q_INVOKABLE bool setFilamentSlotCount(int slotCount);
    Q_INVOKABLE bool addFilamentToTool(int physicalTool);
    Q_INVOKABLE bool setFilamentColor(int slotIndex, const QColor& color);
    bool refreshActiveConfig();

    Q_INVOKABLE QVariantMap getSettingSchema(const QString& key) const;
    Q_INVOKABLE QVariantList getTabPages(const QString& tab,
                                         bool showAdvancedMode = false) const;
    Q_INVOKABLE QVariantList getPageSchema(const QString& tab,
                                           const QString& page,
                                           bool showAdvancedMode = false,
                                           const QString& searchText = QString()) const;
    Q_INVOKABLE QVariantList getGlobalSearchResults(const QString& searchText,
                                                    bool showAdvancedMode = false) const;

    bool hasPendingReslice() const;
    bool schemaLoaded() const;
    int schemaRevision() const;
    QString selectedMachineModelId() const;
    QString selectedMachineVariantId() const;
    QVariantMap selectedMachine() const;
    QString selectedProcessPresetId() const;
    QString selectedProcessPresetName() const;
    QString selectedFilamentPresetId() const;
    QString selectedFilamentPresetName() const;
    QVariantList compatibleProcessPresets() const;
    QVariantList compatibleFilamentPresets() const;
    QVariantList filamentSlots() const;
    QString configurationError() const;

signals:
    void hasPendingResliceChanged();
    void settingsChanged();
    void settingChanged(const QString& key, const QVariant& value);
    void schemaChanged();
    void machineSelectionChanged();
    void presetSelectionChanged();
    void filamentSlotsChanged();
    void configurationErrorChanged();

private:
    explicit SliceSettingsBridge(QObject* parent = nullptr);

    void buildSchemaLocked();
    const libslicer::SettingItem* findItemLocked(std::string_view key) const;
    bool applyChangedItemsLocked(const std::vector<libslicer::SettingItem>& changedItems);
    QVariant displayValueLocked(const libslicer::SettingItem& item,
                                const std::string& serialized) const;
    std::string serializeValueLocked(const libslicer::SettingItem& item,
                                     const QVariant& value) const;
    QVariantMap rowForDefinitionLocked(const QVariantMap& definition) const;
    static QVariantList presetOptions(const std::vector<libslicer::PresetOption>& options);
    static QVariantList filamentSlotItems(
        const std::vector<libslicer::FilamentSlotInfo>& filamentSlots);
    static QString presetName(const std::vector<libslicer::PresetOption>& options,
                              const std::string& selectedId);
    bool isVisibleInMode(const QVariantMap& definition, bool showAdvancedMode) const;
    void setPendingLocked(bool* changed);
    void setConfigurationError(const QString& message);
    bool replaceConfig(GPlatform::SlicingConfigActivationResult&& result, bool markPending);

    mutable QReadWriteLock m_lock;
    libslicer::ResolvedSelection m_selection;
    std::vector<libslicer::PresetOption> m_compatibleProcesses;
    std::vector<libslicer::PresetOption> m_compatibleFilaments;
    std::vector<libslicer::SettingItem> m_items;
    std::unordered_map<std::string, std::size_t> m_itemIndex;
    QVariantList m_schemaSettings;
    QVariantList m_schemaPages;
    QVariantList m_filamentSlots;
    QString m_configurationError;
    QHash<QString, QVariantMap> m_schemaByKey;
    bool m_schemaLoaded{false};
    int m_schemaRevision{0};
    bool m_hasPendingReslice{false};
};
