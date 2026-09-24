#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QMap>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QtQml/qqml.h>
#include <QtQml/QQmlEngine>

class DownloadItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString url READ url CONSTANT)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY totalSizeChanged)
    Q_PROPERTY(qint64 downloadedSize READ downloadedSize NOTIFY downloadedSizeChanged)
    Q_PROPERTY(float progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)

public:
    explicit DownloadItem(const QString& id, const QString& name, const QString& url, QObject* parent = nullptr);
    ~DownloadItem();

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString url() const { return m_url; }
    qint64 totalSize() const { return m_totalSize; }
    qint64 downloadedSize() const { return m_downloadedSize; }
    float progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString filePath() const { return m_filePath; }

    void start(QNetworkAccessManager* manager);
    void pause();
    void resume();
    void cancel();

signals:
    void totalSizeChanged();
    void downloadedSizeChanged();
    void progressChanged();
    void statusChanged();
    void errorChanged();
    void finished();

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onReadyRead();
    void onFinished();

private:
    QString m_id;
    QString m_name;
    QString m_url;
    qint64 m_totalSize = 0;
    qint64 m_downloadedSize = 0;
    float m_progress = 0.0f;
    QString m_status = "pending";  // pending, downloading, paused, completed, failed
    QString m_error;

    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QString m_filePath;
};

class DownloadManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<DownloadItem*> downloads READ downloads NOTIFY downloadsChanged)
    Q_PROPERTY(QList<QJsonObject> downloadHistory READ downloadHistory NOTIFY downloadHistoryChanged)
    Q_PROPERTY(QString downloadPath READ downloadPath WRITE setDownloadPath NOTIFY downloadPathChanged)

public:
    static DownloadManager* getInstance() {
        static DownloadManager* instance = nullptr;
        if (!instance) {
            instance = new DownloadManager();
        }
        return instance;
    }

    static DownloadManager* create(QQmlEngine* engine, QJSEngine*) {
        DownloadManager* instance = getInstance();
        QObject* engineObject = qobject_cast<QObject*>(engine);
        if (engineObject && !instance->parent()) {
            instance->setParent(engineObject);
        }
        return instance;
    }

    QList<DownloadItem*> downloads() const { return m_downloads; }
    QList<QJsonObject> downloadHistory() const { return m_downloadHistory; }
    QString downloadPath() const { return m_downloadPath; }
    void setDownloadPath(const QString& path);

    Q_INVOKABLE void downloadModel(const QJsonObject& modelData);
    Q_INVOKABLE DownloadItem* getDownload(const QString& id);
    Q_INVOKABLE void pauseDownload(const QString& id);
    Q_INVOKABLE void resumeDownload(const QString& id);
    Q_INVOKABLE void cancelDownload(const QString& id);
    Q_INVOKABLE void clearCompleted();
    Q_INVOKABLE void loadDownloadHistory();
    Q_INVOKABLE void clearDownloadHistory();
    Q_INVOKABLE void scanDownloadDirectory();
    Q_INVOKABLE bool deleteDownloadedModel(const QString& filePath);

    Q_INVOKABLE void downloadThumbnail(const QString& modelId, const QString& thumbnailUrl);
    Q_INVOKABLE QString getCachedThumbnailPath(const QString& modelId) const;
    Q_INVOKABLE bool isThumbnailCached(const QString& modelId) const;
    Q_INVOKABLE QString getOrDownloadThumbnail(const QString& modelId, const QString& thumbnailUrl);

signals:
    void downloadsChanged();
    void downloadHistoryChanged();
    void downloadPathChanged();
    void downloadStarted(const QString& id);
    void downloadCompleted(const QString& id, const QString& filePath);
    void downloadFailed(const QString& id, const QString& error);
    void thumbnailReady(const QString& modelId, const QString& localPath);

private:
    explicit DownloadManager(QObject* parent = nullptr);
    ~DownloadManager() = default;

    QNetworkAccessManager* m_networkManager;
    QList<DownloadItem*> m_downloads;
    QList<QJsonObject> m_downloadHistory;
    QString m_downloadPath;
    QString m_historyFilePath;
    QString m_thumbnailCachePath;
    QMap<QString, QString> m_pendingThumbnails;
    QMap<QString, QJsonObject> m_pendingModelMetadata;

    void fetchPolyHavenDownloadUrl(const QJsonObject& modelData);
    void startDownload(const QString& id, const QString& name, const QString& downloadUrl);
    void saveDownloadHistory();
    void addToHistory(const QString& id, const QString& name, const QString& filePath,
                      qint64 fileSize, const QJsonObject& sourceMetadata = {});
    void ensureThumbnailCacheDir();
    static QString makeLibraryItemId(const QString& filePath);
};
