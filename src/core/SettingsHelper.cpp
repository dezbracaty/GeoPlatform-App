#include "SettingsHelper.h"
#include "CoreRegistration.hpp"
#include <QQmlEngine>
#include <QJSEngine>

#include <QDataStream>
#include <QStandardPaths>
#include <QUrl>

SettingsHelper* SettingsHelper::m_instance = nullptr;

SettingsHelper::SettingsHelper(QObject* parent)
    : QObject(parent) {
}

SettingsHelper::~SettingsHelper() = default;

void SettingsHelper::save(const QString& key, QVariant val) {
    m_settings->setValue(key, val);
}

QVariant SettingsHelper::get(const QString& key, QVariant def) {
    QVariant data = m_settings->value(key);
    if (!data.isNull() && data.isValid()) {
        return data;
    }
    return def;
}

void SettingsHelper::init(char* argv[]) {
    QString applicationPath = QString::fromStdString(argv[0]);
    const QFileInfo fileInfo(applicationPath);
    const QString iniFileName = fileInfo.completeBaseName() + ".ini";
    const QString iniFilePath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + iniFileName;
    m_settings.reset(new QSettings(iniFilePath, QSettings::IniFormat));
}

namespace {
bool validImportFolder(const QUrl& url) {
    const QFileInfo folder(url.toLocalFile());
    return url.isLocalFile() && folder.isDir() && folder.isReadable();
}
bool validSortKey(const QString& key) {
    return key == "name" || key == "modified" || key == "size" || key == "type";
}
}

QVariantMap SettingsHelper::getImportFileDialogState() {
    QUrl folder = get("importFile/currentFolder").toUrl();
    if (!validImportFolder(folder)) folder = QUrl::fromLocalFile(QDir::homePath());
    QString sortKey = get("importFile/sortKey", "name").toString();
    if (!validSortKey(sortKey)) sortKey = "name";
    return {{"currentFolder", folder}, {"sortKey", sortKey},
            {"sortReversed", get("importFile/sortReversed", false).toBool()}};
}

void SettingsHelper::saveImportFileDialogState(const QVariantMap& state) {
    const QUrl folder = state.value("currentFolder").toUrl();
    if (validImportFolder(folder)) save("importFile/currentFolder", folder);
    const QString sortKey = state.value("sortKey").toString();
    if (validSortKey(sortKey)) save("importFile/sortKey", sortKey);
    if (state.contains("sortReversed"))
        save("importFile/sortReversed", state.value("sortReversed").toBool());
    m_settings->sync();
}

REGISTER_CORE_QML_SINGLETON(SettingsHelper, "SettingsHelper")
