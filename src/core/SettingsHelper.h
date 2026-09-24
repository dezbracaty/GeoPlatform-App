#pragma once

#include <QtCore/qobject.h>
#include <QtQml/qqml.h>
#include <QSettings>
#include <QScopedPointer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QVariantMap>

class SettingsHelper : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT

private:
    explicit SettingsHelper(QObject* parent = nullptr);
    static SettingsHelper* m_instance;

public:
    static SettingsHelper* getInstance() {
        if (!m_instance) {
            m_instance = new SettingsHelper();
        }
        return m_instance;
    }

    static SettingsHelper* create(QQmlEngine*, QJSEngine*) {
        return getInstance();
    }

    ~SettingsHelper() override;
    void init(char* argv[]);

    Q_INVOKABLE void saveDarkMode(int darkModel) {
        save("darkMode", darkModel);
    }
    Q_INVOKABLE int getDarkMode() {
        return get("darkMode", QVariant(0)).toInt();
    }
    Q_INVOKABLE void saveLocale(const QString& locale) {
        save("locale", locale);
    }
    Q_INVOKABLE QString getLocale() {
        return get("locale", QVariant("en_US")).toString();
    }
    Q_INVOKABLE void saveDisplayMode(int mode) {
        save("displayMode", mode);
    }
    Q_INVOKABLE int getDisplayMode() {
        return get("displayMode", QVariant(4)).toInt(); // 4 = Auto
    }
    Q_INVOKABLE void savePrimaryColor(const QString& color) {
        save("primaryColor", color);
    }
    Q_INVOKABLE QString getPrimaryColor() {
        return get("primaryColor", QVariant("")).toString();
    }
    Q_INVOKABLE void saveWindowEffect(int effect) {
        save("windowEffect", effect);
    }
    Q_INVOKABLE int getWindowEffect() {
        return get("windowEffect", QVariant(0)).toInt(); // 0 = Normal
    }
    Q_INVOKABLE void saveUseSystemAppBar(bool useSystemAppBar) {
        save("useSystemAppBar", useSystemAppBar);
    }
    Q_INVOKABLE bool getUseSystemAppBar() {
        return get("useSystemAppBar", QVariant(false)).toBool();
    }

    // Remember last page settings
    Q_INVOKABLE void saveRememberLastPage(bool remember) {
        save("rememberLastPage", remember);
    }
    Q_INVOKABLE bool getRememberLastPage() {
        return get("rememberLastPage", QVariant(false)).toBool();
    }
    Q_INVOKABLE void saveLastPage(const QString& pageUrl) {
        save("lastPage", pageUrl);
    }
    Q_INVOKABLE QString getLastPage() {
        return get("lastPage", QVariant("")).toString();
    }

    Q_INVOKABLE QVariantMap getImportFileDialogState();
    Q_INVOKABLE void saveImportFileDialogState(const QVariantMap& state);

private:
    void save(const QString& key, QVariant val);
    QVariant get(const QString& key, QVariant def = {});

private:
    QScopedPointer<QSettings> m_settings;
};