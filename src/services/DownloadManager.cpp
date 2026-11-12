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
#include <QTemporaryFile>
#include <QUuid>
#include "Foundation/Log.h"

DownloadItem::DownloadItem(const QString& id, const QString& name, const QString& url, QObject* parent)
    : QObject(parent), m_id(id), m_name(name), m_url(url) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish("failed", "Model download timed out"); });
}

DownloadItem::~DownloadItem() {
    detachReply();
    delete m_file;
}

void DownloadItem::detachReply() {
    ++m_generation;
    if (!m_reply) return;
    auto* reply = m_reply;
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void DownloadItem::finish(const QString& status, const QString& error) {
    if (m_terminal) return;
    m_terminal = true;
    m_timeout.stop();
    detachReply();
    m_status = status;
    m_error = error;
    if (status != "completed" && m_file && m_ownsFile) m_file->remove();
    emit errorChanged();
    emit statusChanged();
    emit finished();
}

void DownloadItem::start(QNetworkAccessManager* manager) {
    if (m_status == "downloading" || m_terminal) return;
    m_manager = manager;
    delete m_file; m_file = nullptr;
    m_ownsFile = false;
    const QString modelPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/GPlatform/Models";
    if (!QDir().mkpath(modelPath)) { finish("failed", "Cannot create the download directory"); return; }
    if (!m_externalFileName.isEmpty()) {
        const QUrl url(m_url, QUrl::StrictMode);
        if (url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty() ||
            QFileInfo(m_externalFileName).fileName() != m_externalFileName ||
            m_externalFileName.contains('\\') || m_externalFileName.contains(QChar(0))) {
            finish("failed", "Invalid external download"); return;
        }
        auto* temporary = new QTemporaryFile(modelPath + "/.gplatform-XXXXXX");
        m_file = temporary;
        if (!temporary->open()) { finish("failed", "Cannot open the temporary download file"); return; }
        m_ownsFile = true;
        const QFileInfo name(m_externalFileName);
        const auto tail = '-' + QUuid::createUuid().toString(QUuid::WithoutBraces) + '.' + name.suffix();
        auto base = name.completeBaseName();
        while ((base + tail).toUtf8().size() > 255) {
            const int count = base.back().isLowSurrogate() && base.size() > 1 ? 2 : 1;
            base.chop(count);
        }
        m_filePath = modelPath + '/' + base + tail;
        m_timeout.start(m_remainingMs);
    } else {
        QString baseFileName = m_name.simplified().replace(" ", "_");
        QString extension = "." + QUrl(m_url).path().section('.', -1);
        if (extension == "." || extension.length() > 5) {
            if (m_url.contains(".stl", Qt::CaseInsensitive)) extension = ".stl";
            else if (m_url.contains(".obj", Qt::CaseInsensitive)) extension = ".obj";
            else if (m_url.contains(".gltf", Qt::CaseInsensitive)) extension = ".gltf";
            else if (m_url.contains(".glb", Qt::CaseInsensitive)) extension = ".glb";
            else extension = ".3d";
        }
        m_filePath = modelPath + "/" + baseFileName + extension;
        if (QFile::exists(m_filePath))
            m_filePath = modelPath + "/" + baseFileName + QDateTime::currentDateTime().toString("_yyyyMMdd_HHmmss") + extension;
        m_file = new QFile(m_filePath);
        if (!m_file->open(QIODevice::WriteOnly)) { finish("failed", "Cannot open the download file"); return; }
        m_ownsFile = true;
    }
    m_status = "downloading";
    m_progress = 0; m_downloadedSize = 0; m_redirects = 0;
    emit statusChanged();
    beginRequest(QUrl(m_url));
}

void DownloadItem::beginRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "GPlatform/1.0");
    if (!m_externalFileName.isEmpty())
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_reply = m_manager->get(request);
    const auto generation = ++m_generation;
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, generation](qint64 got, qint64 total) {
        if (generation == m_generation && !m_terminal) onDownloadProgress(got, total);
    });
    connect(m_reply, &QNetworkReply::readyRead, this, [this, generation] {
        if (generation == m_generation && !m_terminal) onReadyRead();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
        if (generation == m_generation && !m_terminal) onFinished();
    });
}

void DownloadItem::pause() {
    if (m_status != "downloading") return;
    if (!m_externalFileName.isEmpty()) { m_remainingMs = qMax(1, m_timeout.remainingTime()); m_timeout.stop(); }
    detachReply();
    if (m_file && m_ownsFile) m_file->remove();
    m_status = "paused";
    emit statusChanged();
}

void DownloadItem::resume() {
    if (m_status == "paused") { m_status = "pending"; emit statusChanged(); }
}

void DownloadItem::cancel() {
    if (!m_terminal) finish("cancelled");
}

void DownloadItem::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    m_downloadedSize = bytesReceived;
    m_totalSize = bytesTotal;
    if (bytesTotal > 0) m_progress = static_cast<float>(bytesReceived) / bytesTotal;
    emit downloadedSizeChanged(); emit totalSizeChanged(); emit progressChanged();
}

void DownloadItem::onReadyRead() {
    if (!m_file || !m_reply) return;
    if (!m_externalFileName.isEmpty() && !m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isNull()) {
        m_reply->readAll(); return;
    }
    const auto bytes = m_reply->readAll();
    if (m_file->write(bytes) != bytes.size()) finish("failed", "Cannot write the model download");
}

void DownloadItem::onFinished() {
    if (!m_reply || m_terminal) return;
    if (m_reply->error() != QNetworkReply::NoError) { finish("failed", "Network download failed"); return; }
    const auto redirect = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!m_externalFileName.isEmpty() && !redirect.isNull()) {
        const auto next = m_reply->url().resolved(redirect.toUrl());
        if (++m_redirects > 5 || next.scheme() != "https" || next.host().isEmpty() || !next.userInfo().isEmpty()) {
            finish("failed", "Invalid or excessive download redirects"); return;
        }
        detachReply();
        beginRequest(next); return;
    }
    onReadyRead();
    if (m_terminal) return;
    if (!m_file->flush()) { finish("failed", "Cannot flush the model download"); return; }
    m_file->close();
    if (m_file->error() != QFileDevice::NoError) { finish("failed", "Cannot close the model download"); return; }
    if (auto* temporary = dynamic_cast<QTemporaryFile*>(m_file)) {
        if (!temporary->rename(m_filePath)) { finish("failed", "Cannot publish the downloaded model"); return; }
        temporary->setAutoRemove(false);
    }
    m_progress = 1; emit progressChanged();
    finish("completed");
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

void DownloadManager::startExternalDownload(const QString& id, const QUrl& url, const QString& fileName) {
    if (getDownload(id)) { emit downloadFailed(id, "Duplicate download request"); return; }
    startDownload(id, fileName, url.toString(QUrl::FullyEncoded), fileName);
}

void DownloadManager::startDownload(const QString& id, const QString& name, const QString& downloadUrl,
                                    const QString& externalFileName) {
    // 🔴 CRITICAL FIX: Check network availability before starting download
    auto netInfo = QNetworkInformation::instance();
    if (netInfo && netInfo->reachability() == QNetworkInformation::Reachability::Disconnected) {
        m_pendingModelMetadata.remove(id);
        LOG_ERROR("Network is completely unavailable - cannot start download: {}", id.toStdString());
        emit downloadFailed(id, "Network not available. Please check your internet connection and try again later.");
        return;
    }

    auto* item = new DownloadItem(id, name, downloadUrl, this);
    if (!externalFileName.isEmpty()) item->setExternalFileName(externalFileName);

    connect(item, &DownloadItem::finished, [this, item]() {
        const QJsonObject sourceMetadata = m_pendingModelMetadata.take(item->id());
        if (item->status() == "completed") {
            // Add to history
            QFileInfo fileInfo(item->filePath());
            addToHistory(item->id(), item->name(), item->filePath(), fileInfo.size(), sourceMetadata);
            emit downloadCompleted(item->id(), item->filePath());
        } else if (item->status() == "cancelled") {
            emit downloadCancelled(item->id());
        } else {
            emit downloadFailed(item->id(), item->error());
        }
        emit downloadsChanged();
    });

    m_downloads.append(item);
    emit downloadsChanged();
    emit downloadStarted(id);

    LOG_INFO("Starting download request: {}", id.toStdString());
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
        if (auto* item = getDownload(id))
            item->finish("failed", "Network not available. Please check your internet connection and try again later.");
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
