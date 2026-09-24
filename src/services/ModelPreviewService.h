#pragma once

#include "IModelPreviewService.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QQmlEngine>

#include <memory>
#include <vector>

struct GeomTriangle;

class ModelPreviewService final : public QObject, public IModelPreviewService {
    Q_OBJECT
    Q_PROPERTY(int temporaryPreviewDBCount READ temporaryPreviewDBCount NOTIFY temporaryPreviewDBCountChanged)

public:
    static ModelPreviewService* getInstance();
    static ModelPreviewService* create(QQmlEngine* engine, QJSEngine*);

    Q_INVOKABLE QString requestPreview(
        const QString& filePath,
        const QString& sourceThumbnailUrl = {}) override;
    Q_INVOKABLE QString previewUrl(const QString& filePath) const override;
    Q_INVOKABLE void invalidatePreview(const QString& filePath) override;
    Q_INVOKABLE void pruneCache() override;
    Q_INVOKABLE bool isPreviewCached(const QString& filePath) const;
    Q_INVOKABLE qlonglong temporaryPreviewDBId(const QString& filePath) const;

    int temporaryPreviewDBCount() const noexcept;

signals:
    void previewReady(const QString& filePath, const QString& previewUrl);
    void previewFailed(const QString& filePath, const QString& errorMessage);
    void temporaryPreviewDBReady(const QString& filePath, qlonglong dbInstanceId);
    void temporaryPreviewDBReleased(const QString& filePath, qlonglong dbInstanceId);
    void temporaryPreviewDBCountChanged();

private:
    struct PreviewRequest {
        QString filePath;
        QString sourceThumbnailUrl;
        QString cachePath;
        QString jobKey;
    };

    struct PreviewResult {
        bool success{false};
        QString errorMessage;
        QByteArray pngData;
    };

    explicit ModelPreviewService(QObject* parent = nullptr);
    ~ModelPreviewService() override;

    QString cachePathFor(const QString& filePath) const;
    QString pathPrefixFor(const QString& filePath) const;
    static QString localFileUrl(const QString& path);
    static PreviewResult generateLocalPreview(const QString& filePath);
    static PreviewResult normalizeImage(const QImage& source);
    static PreviewResult renderMeshPreview(const QString& filePath);
    static PreviewResult renderTrianglesPreview(
        const std::vector<GeomTriangle>& triangles);
    static QImage extract3mfThumbnail(const QString& filePath);

    void enqueue(PreviewRequest request);
    void processNext();
    void startRemoteThumbnail(const PreviewRequest& request);
    void startLocalGeneration(const PreviewRequest& request);
    void startGlbGeneration(const PreviewRequest& request);
    void releaseTemporaryPreviewDB(
        const QString& jobKey,
        const QString& filePath,
        qlonglong dbInstanceId);
    void finishRequest(const PreviewRequest& request, const PreviewResult& result);
    void removeStaleVersions(const QString& filePath, const QString& keepPath = {});

    QNetworkAccessManager m_networkManager;
    QQueue<PreviewRequest> m_queue;
    QSet<QString> m_pendingPaths;
    QHash<QString, QSet<QString>> m_requestAliases;
    QString m_cacheDirectory;
    QHash<QString, std::shared_ptr<class ModelPreviewDBSession>> m_previewDBJobs;
    bool m_busy{false};
};
