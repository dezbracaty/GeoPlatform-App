#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

struct LocalModelRecord final {
    QString libraryItemId;
    QString name;
    QString filePath;
    QString format;
    qint64 fileSize = 0;
    QDateTime downloadDate;
    QString source;
    QString sourceThumbnailUrl;
};

class LocalModelLibraryService final : public QObject {
    Q_OBJECT

public:
    static LocalModelLibraryService* instance();

    QVector<LocalModelRecord> records() const;
    QVariantList models(const QString& query = {},
                        const QString& sortMode = QStringLiteral("date")) const;
    QVariantMap search(const QString& query, int limit = 5) const;
    bool resolve(const QString& libraryItemId,
                 LocalModelRecord& record,
                 QString& error) const;
    bool refresh(QString& error);
    bool deleteModel(const QString& libraryItemId, QString& error);

    /** Register a process-lifetime local model without mutating download history. */
    QString registerSessionModel(const QString& filePath,
                                 const QString& displayName = {});
    void unregisterSessionModel(const QString& libraryItemId);

signals:
    void modelsChanged();

private:
    explicit LocalModelLibraryService(QObject* parent = nullptr);

    static QString normalizeSearchText(const QString& value);
    static QVariantMap toVariantMap(const LocalModelRecord& record, bool includeFilePath);

    QHash<QString, LocalModelRecord> m_sessionRecords;
};
