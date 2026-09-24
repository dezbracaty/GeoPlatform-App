#include "DownloadManager.h"
#include "ServicesRegistration.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDateTime>
#include <QQmlEngine>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QNetworkInformation>
#include <QCryptographicHash>
#include <algorithm>
#include "Foundation/Log.h"

DownloadItem::DownloadItem(const QString& id, const QString& name, const QString& url, QObject* parent)
    : QObject(parent), m_id(id), m_name(name), m_url(url) {
}

DownloadItem::~DownloadItem() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file) {
        m_file->close();
        delete m_file;
    }
}

void DownloadItem::start(QNetworkAccessManager* manager) {
    if (m_status == "downloading") return;

    // Create downloads directory if it doesn't exist
    QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QString modelPath = downloadsPath + "/GPlatform/Models";
    QDir dir(modelPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Generate unique filename based on the URL
    QString baseFileName = m_name.simplified().replace(" ", "_");

    // Extract extension from URL
    QString urlPath = QUrl(m_url).path();
    QString extension = "." + urlPath.section('.', -1);
    if (extension == "." || extension.length() > 5) {
        // Fallback to common 3D formats
        if (m_url.contains(".stl", Qt::CaseInsensitive)) extension = ".stl";
        else if (m_url.contains(".obj", Qt::CaseInsensitive)) extension = ".obj";
        else if (m_url.contains(".gltf", Qt::CaseInsensitive)) extension = ".gltf";
        else if (m_url.contains(".glb", Qt::CaseInsensitive)) extension = ".glb";
        else extension = ".3d"; // Generic 3D file
    }

    m_filePath = modelPath + "/" + baseFileName + extension;

    // Check if file already exists and add timestamp if needed
    if (QFile::exists(m_filePath)) {
        QString timestamp = QDateTime::currentDateTime().toString("_yyyyMMdd_HHmmss");
        m_filePath = modelPath + "/" + baseFileName + timestamp + extension;
    }

    m_file = new QFile(m_filePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_error = "Failed to open file for writing: " + m_file->errorString();
        m_status = "failed";
        emit errorChanged();
        emit statusChanged();
        LOG_ERROR("Failed to open file for writing: {}", m_file->errorString().toStdString());
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_url));
    request.setRawHeader("User-Agent", "GPlatform/1.0");

    m_reply = manager->get(request);

    connect(m_reply, &QNetworkReply::downloadProgress, this, &DownloadItem::onDownloadProgress);
    connect(m_reply, &QNetworkReply::readyRead, this, &DownloadItem::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadItem::onFinished);

    m_status = "downloading";
    emit statusChanged();
    LOG_INFO("Started download: {} -> {}", m_url.toStdString(), m_filePath.toStdString());
}

void DownloadItem::pause() {
    if (m_status == "downloading" && m_reply) {
        m_reply->abort();
        m_status = "paused";
        emit statusChanged();
    }
}

void DownloadItem::resume() {
    // For simplicity, we restart the download
    // In production, you'd implement proper range requests for resume
    if (m_status == "paused") {
        m_status = "pending";
        emit statusChanged();
    }
}

void DownloadItem::cancel() {
    if (m_reply) {
        m_reply->abort();
    }
    if (m_file) {
        m_file->remove();
    }
    m_status = "cancelled";
    emit statusChanged();
}

void DownloadItem::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    m_downloadedSize = bytesReceived;
    m_totalSize = bytesTotal;

    if (bytesTotal > 0) {
        m_progress = static_cast<float>(bytesReceived) / bytesTotal;
    }

    emit downloadedSizeChanged();
    emit totalSizeChanged();
    emit progressChanged();
}

void DownloadItem::onReadyRead() {
    if (m_file && m_reply) {
        m_file->write(m_reply->readAll());
    }
}

void DownloadItem::onFinished() {
    if (m_file) {
        m_file->flush();
        m_file->close();
    }

    if (m_reply->error() == QNetworkReply::NoError) {
        m_status = "completed";
        m_progress = 1.0f;
        emit progressChanged();
        LOG_INFO("Download completed: {}", m_filePath.toStdString());
    } else {
        m_status = "failed";
        m_error = m_reply->errorString();
        emit errorChanged();
        if (m_file) {
            m_file->remove();
        }
        LOG_ERROR("Download failed: {}", m_error.toStdString());
    }

    emit statusChanged();
    emit finished();

    m_reply->deleteLater();
    m_reply = nullptr;
}

DownloadManager::DownloadManager(QObject* parent)
    : QObject(parent) {
    m_networkManager = new QNetworkAccessManager(this);

    QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_downloadPath = downloadsPath + "/GPlatform/Models";
    QDir().mkpath(m_downloadPath);

    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    m_historyFilePath = dataPath + "/download_history.json";

    m_thumbnailCachePath = dataPath + "/thumbnail_cache";
    QDir().mkpath(m_thumbnailCachePath);

    loadDownloadHistory();
}

QString DownloadManager::makeLibraryItemId(const QString& filePath) {
    const QString normalizedPath = QFileInfo(filePath).absoluteFilePath();
    const QByteArray digest = QCryptographicHash::hash(
        normalizedPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("local-%1").arg(QString::fromLatin1(digest.left(24)));
}

void DownloadManager::setDownloadPath(const QString& path) {
    if (m_downloadPath != path) {
        m_downloadPath = path;
        QDir().mkpath(m_downloadPath);
        emit downloadPathChanged();
    }
}

void DownloadManager::downloadModel(const QJsonObject& modelData) {
    QString modelId = modelData["id"].toString();
    QString modelName = modelData["name"].toString();

    // Check if already downloading
    for (auto* item : m_downloads) {
        if (item->id() == modelId) {
            LOG_WARN("Model {} already in download queue", modelId.toStdString());
            return;
        }
    }

    // Get the actual download URL from model data
    QString downloadUrl = modelData["downloadUrl"].toString();
    if (downloadUrl.isEmpty()) {
        // Fallback to the general URL
        downloadUrl = modelData["url"].toString();
    }

    if (downloadUrl.isEmpty()) {
        LOG_ERROR("No download URL found for model: {}", modelName.toStdString());
        emit downloadFailed(modelId, "No download URL available");
        return;
    }

    // Keep provider metadata until the file is complete. The local model
    // library can then cache the provider's real thumbnail without rendering
    // the downloaded mesh a second time.
    m_pendingModelMetadata[modelId] = modelData;

    // 启动下载
    startDownload(modelId, modelName, downloadUrl);
}

void DownloadManager::fetchPolyHavenDownloadUrl(const QJsonObject& modelData) {
    QString modelId = modelData["id"].toString();
    QString modelName = modelData["name"].toString();

    // Poly Haven API endpoint for getting download links
    QUrl url(QString("https://api.polyhaven.com/files/%1").arg(modelId));

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "GPlatform/1.0");

    auto* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply, modelId, modelName]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);

            if (doc.isObject()) {
                QJsonObject fileData = doc.object();

                // Look for the blend file with 1k resolution (smaller size)
                // Structure: fileData > blend > 1k > blend > url
                QString downloadUrl;

                if (fileData.contains("blend")) {
                    QJsonObject blendData = fileData["blend"].toObject();
                    if (blendData.contains("1k")) {
                        QJsonObject resData = blendData["1k"].toObject();
                        if (resData.contains("blend")) {
                            QJsonObject formatData = resData["blend"].toObject();
                            downloadUrl = formatData["url"].toString();
                        }
                    } else if (blendData.contains("2k")) {
                        // Fallback to 2k if 1k not available
                        QJsonObject resData = blendData["2k"].toObject();
                        if (resData.contains("blend")) {
                            QJsonObject formatData = resData["blend"].toObject();
                            downloadUrl = formatData["url"].toString();
                        }
                    }
                }

                // If no blend file, try to get FBX
                if (downloadUrl.isEmpty() && fileData.contains("fbx")) {
                    QJsonObject fbxData = fileData["fbx"].toObject();
                    if (fbxData.contains("1k")) {
                        QJsonObject resData = fbxData["1k"].toObject();
                        if (resData.contains("fbx")) {
                            QJsonObject formatData = resData["fbx"].toObject();
                            downloadUrl = formatData["url"].toString();
                        }
                    }
                }

                if (!downloadUrl.isEmpty()) {
                    startDownload(modelId, modelName, downloadUrl);
                } else {
                    LOG_ERROR("No suitable download format found for model: {}", modelId.toStdString());
                    emit downloadFailed(modelId, "No suitable download format available");
                }
            }
        } else {
            LOG_ERROR("Failed to fetch download URL for model {}: {}",
                     modelId.toStdString(), reply->errorString().toStdString());
            emit downloadFailed(modelId, reply->errorString());
        }
        reply->deleteLater();
    });
}

void DownloadManager::startDownload(const QString& id, const QString& name, const QString& downloadUrl) {
    // 🔴 CRITICAL FIX: Check network availability before starting download
    auto netInfo = QNetworkInformation::instance();
    if (netInfo && netInfo->reachability() == QNetworkInformation::Reachability::Disconnected) {
        m_pendingModelMetadata.remove(id);
        LOG_ERROR("Network is completely unavailable - cannot start download: {}", id.toStdString());
        emit downloadFailed(id, "Network not available. Please check your internet connection and try again later.");
        return;
    }

    auto* item = new DownloadItem(id, name, downloadUrl, this);

    connect(item, &DownloadItem::finished, [this, item]() {
        const QJsonObject sourceMetadata = m_pendingModelMetadata.take(item->id());
        if (item->status() == "completed") {
            // Add to history
            QFileInfo fileInfo(item->filePath());
            addToHistory(item->id(), item->name(), item->filePath(), fileInfo.size(), sourceMetadata);
            emit downloadCompleted(item->id(), item->filePath());
        }
        emit downloadsChanged();
    });

    m_downloads.append(item);
    emit downloadsChanged();
    emit downloadStarted(id);

    LOG_INFO("Starting download: {} (network available)", id.toStdString());
    item->start(m_networkManager);
}

DownloadItem* DownloadManager::getDownload(const QString& id) {
    for (auto* item : m_downloads) {
        if (item->id() == id) {
            return item;
        }
    }
    return nullptr;
}

void DownloadManager::pauseDownload(const QString& id) {
    if (auto* item = getDownload(id)) {
        item->pause();
    }
}

void DownloadManager::resumeDownload(const QString& id) {
    // 🔴 CRITICAL FIX: Check network availability before resuming download
    auto netInfo = QNetworkInformation::instance();
    if (netInfo && netInfo->reachability() == QNetworkInformation::Reachability::Disconnected) {
        LOG_ERROR("Network is completely unavailable - cannot resume download: {}", id.toStdString());
        emit downloadFailed(id, "Network not available. Please check your internet connection and try again later.");
        return;
    }

    if (auto* item = getDownload(id)) {
        // Only proceed if network is available
        item->resume();
        item->start(m_networkManager);
        LOG_INFO("Resuming download: {} (network available)", id.toStdString());
    }
}

void DownloadManager::cancelDownload(const QString& id) {
    if (auto* item = getDownload(id)) {
        item->cancel();
        m_downloads.removeOne(item);
        item->deleteLater();
        emit downloadsChanged();
    }
}

void DownloadManager::clearCompleted() {
    auto it = m_downloads.begin();
    while (it != m_downloads.end()) {
        if ((*it)->status() == "completed" || (*it)->status() == "cancelled") {
            (*it)->deleteLater();
            it = m_downloads.erase(it);
        } else {
            ++it;
        }
    }
    emit downloadsChanged();
}

void DownloadManager::loadDownloadHistory() {
    m_downloadHistory.clear();
    bool historyChanged = false;

    // First load from history file if exists
    QFile file(m_historyFilePath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            QJsonArray array = doc.array();
            for (const QJsonValue& value : array) {
                if (value.isObject()) {
                    QJsonObject obj = value.toObject();
                    // Check if file still exists
                    QString filePath = obj["filePath"].toString();
                    if (QFile::exists(filePath)) {
                        if (obj.value("libraryItemId").toString().isEmpty()) {
                            obj["libraryItemId"] = makeLibraryItemId(filePath);
                            historyChanged = true;
                        }
                        m_downloadHistory.append(obj);
                    }
                }
            }
        }
    }

    // Then scan the download directory for any additional files
    scanDownloadDirectory();
    if (historyChanged) {
        saveDownloadHistory();
        emit downloadHistoryChanged();
    }
}

void DownloadManager::saveDownloadHistory() {
    QFile file(m_historyFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("Failed to save download history to {}", m_historyFilePath.toStdString());
        return;
    }

    QJsonArray array;
    for (const QJsonObject& obj : m_downloadHistory) {
        array.append(obj);
    }

    QJsonDocument doc(array);
    file.write(doc.toJson());
    file.close();
}

void DownloadManager::addToHistory(const QString& id, const QString& name,
                                   const QString& filePath, qint64 fileSize,
                                   const QJsonObject& sourceMetadata) {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["filePath"] = filePath;
    obj["libraryItemId"] = makeLibraryItemId(filePath);
    obj["fileSize"] = QString::number(fileSize);
    obj["downloadDate"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString thumbnailUrl = sourceMetadata.value(QStringLiteral("thumbnail")).toString();
    if (!thumbnailUrl.isEmpty()) {
        obj["sourceThumbnailUrl"] = thumbnailUrl;
    }
    const QString source = sourceMetadata.value(QStringLiteral("source")).toString();
    if (!source.isEmpty()) {
        obj["source"] = source;
    }

    // Add at the beginning
    m_downloadHistory.prepend(obj);

    // Keep only last 100 items
    while (m_downloadHistory.size() > 100) {
        m_downloadHistory.removeLast();
    }

    saveDownloadHistory();
    emit downloadHistoryChanged();
}

void DownloadManager::clearDownloadHistory() {
    m_downloadHistory.clear();
    saveDownloadHistory();
    emit downloadHistoryChanged();
}

bool DownloadManager::deleteDownloadedModel(const QString& filePath) {
    if (!QFile::remove(filePath)) {
        LOG_ERROR("Failed to delete downloaded model file: {}", filePath.toStdString());
        return false;
    }

    // Only mutate the catalog after the file operation succeeds, so an I/O
    // failure cannot leave the in-memory history inconsistent with disk.
    m_downloadHistory.removeIf([&filePath](const QJsonObject& item) {
        return item.value(QStringLiteral("filePath")).toString() == filePath;
    });
    saveDownloadHistory();
    emit downloadHistoryChanged();
    return true;
}

void DownloadManager::scanDownloadDirectory() {
    QDir dir(m_downloadPath);
    if (!dir.exists()) {
        return;
    }

    // Define supported 3D model extensions
    QStringList extensions;
    extensions << "*.stl" << "*.STL"
               << "*.obj" << "*.OBJ"
               << "*.glb" << "*.GLB"
               << "*.gltf" << "*.GLTF"
               << "*.fbx" << "*.FBX"
               << "*.3ds" << "*.3DS"
               << "*.ply" << "*.PLY"
               << "*.3mf" << "*.3MF"
               << "*.dae" << "*.DAE"
               << "*.blend" << "*.BLEND";

    QFileInfoList fileList = dir.entryInfoList(extensions, QDir::Files);

    // Build a set of existing file paths in history
    QSet<QString> existingPaths;
    for (const QJsonObject& obj : m_downloadHistory) {
        existingPaths.insert(obj["filePath"].toString());
    }

    // Add new files that aren't in history
    bool historyChanged = false;
    for (const QFileInfo& fileInfo : fileList) {
        QString filePath = fileInfo.absoluteFilePath();

        if (!existingPaths.contains(filePath)) {
            QJsonObject obj;
            obj["id"] = fileInfo.baseName(); // Use filename as ID
            obj["name"] = fileInfo.baseName();
            obj["filePath"] = filePath;
            obj["libraryItemId"] = makeLibraryItemId(filePath);
            obj["fileSize"] = QString::number(fileInfo.size());
            obj["downloadDate"] = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss");

            m_downloadHistory.append(obj);
            historyChanged = true;

            LOG_INFO("Found new model in download directory: {}", filePath.toStdString());
        }
    }

    if (historyChanged) {
        // Sort by date (newest first)
        std::sort(m_downloadHistory.begin(), m_downloadHistory.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return QDateTime::fromString(a["downloadDate"].toString(), "yyyy-MM-dd hh:mm:ss") >
                   QDateTime::fromString(b["downloadDate"].toString(), "yyyy-MM-dd hh:mm:ss");
        });

        saveDownloadHistory();
        emit downloadHistoryChanged();
    }
}

void DownloadManager::ensureThumbnailCacheDir() {
    if (!QDir(m_thumbnailCachePath).exists()) {
        QDir().mkpath(m_thumbnailCachePath);
    }
}

void DownloadManager::downloadThumbnail(const QString& modelId, const QString& thumbnailUrl) {
    if (thumbnailUrl.isEmpty()) {
        LOG_WARN("Empty thumbnail URL for model: {}", modelId.toStdString());
        return;
    }

    QString cachedPath = getCachedThumbnailPath(modelId);
    if (QFile::exists(cachedPath)) {
        emit thumbnailReady(modelId, cachedPath);
        return;
    }

    if (m_pendingThumbnails.contains(modelId)) {
        return;
    }

    m_pendingThumbnails[modelId] = thumbnailUrl;

    QNetworkRequest request;
    request.setUrl(QUrl(thumbnailUrl));
    request.setRawHeader("User-Agent", "GPlatform/1.0");

    auto* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply, modelId]() {
        m_pendingThumbnails.remove(modelId);

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QString cachedPath = getCachedThumbnailPath(modelId);

            QFile file(cachedPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(data);
                file.close();
                LOG_DEBUG("Cached thumbnail for model: {}", modelId.toStdString());
                emit thumbnailReady(modelId, cachedPath);
            } else {
                LOG_ERROR("Failed to save thumbnail cache: {}", cachedPath.toStdString());
            }
        } else {
            LOG_ERROR("Failed to download thumbnail for {}: {}", modelId.toStdString(), reply->errorString().toStdString());
        }

        reply->deleteLater();
    });
}

QString DownloadManager::getCachedThumbnailPath(const QString& modelId) const {
    return m_thumbnailCachePath + "/" + modelId + ".jpg";
}

bool DownloadManager::isThumbnailCached(const QString& modelId) const {
    return QFile::exists(getCachedThumbnailPath(modelId));
}

QString DownloadManager::getOrDownloadThumbnail(const QString& modelId, const QString& thumbnailUrl) {
    QString cachedPath = getCachedThumbnailPath(modelId);
    if (QFile::exists(cachedPath)) {
        return cachedPath;
    }

    downloadThumbnail(modelId, thumbnailUrl);
    return thumbnailUrl;
}

REGISTER_SERVICES_QML_TYPE(DownloadItem, "DownloadItem")
REGISTER_SERVICES_QML_SINGLETON_CUSTOM(
    DownloadManager, "DownloadManager", &DownloadManager::create)
