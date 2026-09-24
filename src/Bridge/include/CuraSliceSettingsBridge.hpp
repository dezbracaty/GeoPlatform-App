#pragma once

#include <BridgeBase.hpp>

#include <QHash>
#include <QJSEngine>
#include <QQmlEngine>
#include <QReadWriteLock>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>

struct CuraSliceSettingsSnapshot {
    QVariantMap settings;
    QVariantMap engineSettings;
    bool enableBeltPrinter{true};
};

class CuraSliceSettingsBridge : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(bool enableBeltPrinter READ enableBeltPrinter WRITE setEnableBeltPrinter NOTIFY enableBeltPrinterChanged)
    Q_PROPERTY(bool hasPendingReslice READ hasPendingReslice NOTIFY hasPendingResliceChanged)
    Q_PROPERTY(QVariantMap settings READ settings WRITE setSettings NOTIFY settingsChanged)
    Q_PROPERTY(bool schemaLoaded READ schemaLoaded NOTIFY schemaChanged)
    Q_PROPERTY(int schemaRevision READ schemaRevision NOTIFY schemaChanged)
    QML_ELEMENT
    QML_SINGLETON

public:
    static CuraSliceSettingsBridge* instance();
    static CuraSliceSettingsBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    bool enableBeltPrinter() const;
    void setEnableBeltPrinter(bool enabled);

    QVariantMap settings() const;
    void setSettings(const QVariantMap& newSettings);

    Q_INVOKABLE QVariant getSetting(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE void ensureSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE void ensureMachineGCodeDefaultsIfNeeded();
    Q_INVOKABLE bool hasSetting(const QString& key) const;
    Q_INVOKABLE void removeSetting(const QString& key);
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void markCurrentAsSliced();
    Q_INVOKABLE void markNeedsReslice();

    Q_INVOKABLE bool reloadSchema(const QString& resourcePath = QString());
    Q_INVOKABLE QVariantMap getSettingSchema(const QString& key) const;
    Q_INVOKABLE QVariantList getTabPages(const QString& tab, bool showAdvancedMode = false) const;
    Q_INVOKABLE QVariantList getPageSchema(const QString& tab,
                                           const QString& page,
                                           bool showAdvancedMode = false,
                                           const QString& searchText = QString()) const;
    Q_INVOKABLE QVariantList getGlobalSearchResults(const QString& searchText,
                                                    bool showAdvancedMode = false) const;

    bool hasPendingReslice() const;
    bool schemaLoaded() const;
    int schemaRevision() const;

    CuraSliceSettingsSnapshot snapshotForExecutor() const;

signals:
    void enableBeltPrinterChanged();
    void hasPendingResliceChanged();
    void settingsChanged();
    void settingChanged(const QString& key, const QVariant& value);
    void schemaChanged();

private:
    explicit CuraSliceSettingsBridge(QObject* parent = nullptr);

    QVariantMap defaultSettings() const;
    bool loadSchemaLocked(const QString& resourcePath);
    void ensureSchemaDefaultsLocked();
    QVariant normalizeValueBySchemaLocked(const QString& key, const QVariant& value, bool* ok) const;
    QVariantMap buildEngineSettingsLocked() const;

private:
    mutable QReadWriteLock m_lock;
    QVariantMap m_settings;

    QVariantList m_schemaSettings;
    QVariantList m_schemaPages;
    QHash<QString, QVariantMap> m_schemaByKey;
    bool m_schemaLoaded{false};
    int m_schemaRevision{0};

    bool m_enableBeltPrinter{true};
    bool m_hasPendingReslice{false};
};
