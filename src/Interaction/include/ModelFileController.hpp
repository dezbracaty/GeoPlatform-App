#pragma once
#include <QObject>
#include <QVariantList>
#include <QQmlEngine>

// UI projection and current editable-project location. Geometry remains in DB;
// writing and its terminal result belong to ModelFileExportHandler.
class ModelFileController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectFile READ projectFile NOTIFY projectFileChanged)
    Q_PROPERTY(bool canExportSelection READ canExportSelection NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canSaveProject READ canSaveProject NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canExportPlate READ canExportPlate NOTIFY capabilitiesChanged)
public:
    static ModelFileController* instance();
    static ModelFileController* create(QQmlEngine*, QJSEngine*);
    QString projectFile() const;
    bool canExportSelection() const;
    bool canExportPlate() const;
    bool canSaveProject() const;
    Q_INVOKABLE QVariantList exportTargets(const QString& scope) const;
    void setProjectPath(const QString& path);
signals:
    void projectFileChanged();
    void capabilitiesChanged();
private:
    ModelFileController();
    QString m_projectPath;
};
