#include "AppInfo.h"
#include "CoreRegistration.hpp"
#include "SettingsHelper.h"
#include "Foundation/Log.h"
#include "GettextBridge.h"
#include "Tr.hpp"
#include <QJSEngine>

#include <QQmlContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>

AppInfo::AppInfo(QObject* parent)
    : QObject{parent} {
    setVersion(PROJECT_VERSION);
    m_locales << "en_US";
    m_locales << "zh_CN";
    // Load saved locale from settings
    m_locale = SettingsHelper::getInstance()->getLocale();

    // Connect to locale property changes to reload translator
    connect(this, &AppInfo::localeChanged, this, [this]() {
        // Save the new locale to settings
        SettingsHelper::getInstance()->saveLocale(m_locale);
        // Reload the translator with new locale
        initTranslator();
        // Trigger QML retranslation
        QQmlEngine* engine = qmlEngine(this);
        if (engine) {
            engine->retranslate();
        }
    });
}

AppInfo* AppInfo::getInstance() {
    static auto* instance = new AppInfo();
    return instance;
}

QString AppInfo::version() const {
    return m_version;
}

void AppInfo::setVersion(const QString& version) {
    if (m_version == version) {
        return;
    }
    m_version = version;
    emit versionChanged();
}

QString AppInfo::locale() const {
    return m_locale;
}

void AppInfo::setLocale(const QString& locale) {
    if (m_locale == locale) {
        return;
    }
    m_locale = locale;
    emit localeChanged();
}

QStringList AppInfo::locales() const {
    return m_locales;
}

void AppInfo::init(QQmlApplicationEngine* engine) {
    initTranslator();
    if ("ON" == QString(PROJECT_HOTLOAD_ENABLED)) {
        engine->setBaseUrl(QUrl(QString("file:///%1/").arg(PROJECT_SOURCE_DIR)));
    } else {
        engine->setBaseUrl(QUrl("qrc:/qt/qml/GPlatform/"));
    }
}

void AppInfo::initTranslator() {
    auto& translator = tr::Translator::instance();
    translator.clear();
    translator.setLanguage(m_locale.toStdString());
    gettext_bridge::install();

    if (m_locale.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
        const QString basePath = QStringLiteral(":/i18n/zh_CN/");
        const bool appLoaded = gettext_bridge::loadCatalogFromResource(
            basePath + QStringLiteral("gplatform.mo"));
        const bool libslicerLoaded = gettext_bridge::loadCatalogFromResource(
            basePath + QStringLiteral("libslicer.mo"), "libslicer");
        if (appLoaded && libslicerLoaded) {
            LOG_DEBUG("Chinese gettext catalogs loaded");
        } else {
            LOG_WARN("Chinese gettext catalogs incomplete: app={}, libslicer={}",
                     appLoaded, libslicerLoaded);
        }
    }

    // Set the default locale
    QLocale::setDefault(QLocale(this->m_locale));
}

REGISTER_CORE_QML_SINGLETON(AppInfo, "AppInfo")
