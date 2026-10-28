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
    Q_PROPERTY(bool printGuideHandled READ printGuideHandled NOTIFY printGuideHandledChanged)
    Q_PROPERTY(bool printGuideAutoAttempted READ printGuideAutoAttempted NOTIFY printGuideAutoAttemptedChanged)

private:
    explicit SettingsHelper(QObject* parent = nullptr);
    static SettingsHelper* m_instance;

public:
    static SettingsHelper* getInstance() {
        if (!m_instance) {
            m_instance = new SettingsHelper(QCoreApplication::instance());
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
        return get("locale", QVariant("zh_CN")).toString();
    }
    Q_INVOKABLE void saveDisplayMode(int mode) {
        save("displayMode", mode);
    }
    Q_INVOKABLE int getDisplayMode() {
        return get("displayMode", QVariant(2)).toInt(); // 2 = Minimal
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

    bool getDesktopNotificationsEnabled() {
        return get("notifications/desktopEnabled", false).toBool();
    }
    void saveDesktopNotificationsEnabled(bool enabled) {
        save("notifications/desktopEnabled", enabled);
        m_settings->sync();
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

    bool printGuideHandled() { return get("printGuide/handled", false).toBool(); }
    bool printGuideAutoAttempted() const { return m_printGuideAutoAttempted; }
    Q_INVOKABLE bool claimPrintGuideAutoAttempt() {
        if (m_printGuideAutoAttempted || printGuideHandled())
            return false;
        m_printGuideAutoAttempted = true;
        emit printGuideAutoAttemptedChanged();
        return true;
    }
    Q_INVOKABLE void markPrintGuideHandled() {
        if (printGuideHandled())
            return;
        save("printGuide/handled", true);
        m_settings->sync();
        emit printGuideHandledChanged();
    }

    Q_INVOKABLE QVariantMap getImportFileDialogState();
    Q_INVOKABLE void saveImportFileDialogState(const QVariantMap& state);

signals:
    void printGuideHandledChanged();
    void printGuideAutoAttemptedChanged();

private:
    void save(const QString& key, QVariant val);
    QVariant get(const QString& key, QVariant def = {});

private:
    QScopedPointer<QSettings> m_settings;
    bool m_printGuideAutoAttempted{false};
};
