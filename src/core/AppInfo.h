#pragma once

#include <QObject>
#include <QQmlApplicationEngine>

class AppInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version WRITE setVersion NOTIFY versionChanged)
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged)
    Q_PROPERTY(QStringList locales READ locales NOTIFY localesChanged)
    QML_SINGLETON
    QML_ELEMENT

private:
    explicit AppInfo(QObject* parent = nullptr);
    void initTranslator();

public:
    static AppInfo* getInstance();
    static AppInfo* create(QQmlEngine*, QJSEngine*) {
        return getInstance();
    }

    QString version() const;
    void setVersion(const QString& version);
    QString locale() const;
    void setLocale(const QString& locale);
    QStringList locales() const;

    void init(QQmlApplicationEngine*);

signals:
    void versionChanged();
    void localeChanged();
    void localesChanged();

private:
    QString m_version;
    QString m_locale;
    QStringList m_locales;
};
