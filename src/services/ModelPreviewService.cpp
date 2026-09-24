#include "ModelPreviewService.h"
#include "ModelPreviewDBSession.h"
#include "ServicesRegistration.hpp"

#include "Foundation/Log.h"
#include "GlbDBImporter.hpp"
#include "GlbDB.hpp"
#include "ModelLoaderUtil.hpp"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

#include <miniz.h>

namespace {

    constexpr int kPreviewSize = 512;
    constexpr int kRasterSize = 768;
    constexpr qint64 kMaximumCacheBytes = 500LL * 1024LL * 1024LL;
    constexpr qint64 kFailureCacheLifetimeMs = 60LL * 60LL * 1000LL;
    constexpr auto kRenderStyleVersion = "model-preview-v1";

    struct Vec3 {
        double x{0.0};
        double y{0.0};
        double z{0.0};
    };

    struct ProjectedVertex {
        double x{0.0};
        double y{0.0};
        double depth{0.0};
    };

    Vec3 toVec3(const Vector3& value) {
        return {value.x, value.y, value.z};
    }

    Vec3 subtract(const Vec3& left, const Vec3& right) {
        return {left.x - right.x, left.y - right.y, left.z - right.z};
    }

    Vec3 cross(const Vec3& left, const Vec3& right) {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x,
        };
    }

    double dot(const Vec3& left, const Vec3& right) {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    Vec3 normalized(const Vec3& value) {
        const double length = std::sqrt(dot(value, value));
        if (length <= std::numeric_limits<double>::epsilon()) {
            return {};
        }
        return {value.x / length, value.y / length, value.z / length};
    }

    double edge(double ax, double ay, double bx, double by, double px, double py) {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    }

    QByteArray imageToPng(const QImage& image) {
        QByteArray data;
        QBuffer buffer(&data);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            return {};
        }
        return data;
    }

    QString normalizedPath(const QString& filePath) {
        const QFileInfo info(filePath);
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
    }

} // namespace

ModelPreviewService::ModelPreviewService(QObject* parent)
    : QObject(parent) {
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_cacheDirectory = dataPath + QStringLiteral("/model_previews/v1");
    QDir().mkpath(m_cacheDirectory);
}

ModelPreviewService::~ModelPreviewService() = default;

ModelPreviewService* ModelPreviewService::getInstance() {
    static ModelPreviewService* instance = new ModelPreviewService();
    return instance;
}

ModelPreviewService* ModelPreviewService::create(QQmlEngine* engine, QJSEngine*) {
    ModelPreviewService* instance = getInstance();
    if (engine && !instance->parent()) {
        instance->setParent(engine);
    }
    return instance;
}

QString ModelPreviewService::requestPreview(const QString& filePath,
                                            const QString& sourceThumbnailUrl) {
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return {};
    }

    const QString cachedPath = cachePathFor(filePath);
    if (QFileInfo::exists(cachedPath)) {
        QFile cachedFile(cachedPath);
        if (cachedFile.open(QIODevice::ReadOnly)) {
            cachedFile.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        }
        return localFileUrl(cachedPath);
    }

    const QString failedPath = cachedPath + QStringLiteral(".failed");
    const QFileInfo failedInfo(failedPath);
    if (failedInfo.exists() && failedInfo.lastModified().msecsTo(QDateTime::currentDateTime()) < kFailureCacheLifetimeMs) {
        QFile failureFile(failedPath);
        QString errorMessage = QStringLiteral("Preview generation previously failed");
        if (failureFile.open(QIODevice::ReadOnly)) {
            errorMessage = QString::fromUtf8(failureFile.readAll());
        }
        QTimer::singleShot(0, this, [this, filePath, errorMessage] {
            emit previewFailed(filePath, errorMessage);
        });
        return {};
    }
    if (failedInfo.exists()) {
        QFile::remove(failedPath);
    }

    enqueue({filePath, sourceThumbnailUrl, cachedPath});
    return {};
}

QString ModelPreviewService::previewUrl(const QString& filePath) const {
    const QString cachedPath = cachePathFor(filePath);
    return QFileInfo::exists(cachedPath) ? localFileUrl(cachedPath) : QString();
}

bool ModelPreviewService::isPreviewCached(const QString& filePath) const {
    return QFileInfo::exists(cachePathFor(filePath));
}

int ModelPreviewService::temporaryPreviewDBCount() const noexcept {
    return m_previewDBJobs.size();
}

qlonglong ModelPreviewService::temporaryPreviewDBId(
    const QString& filePath) const {
    const auto job = m_previewDBJobs.value(normalizedPath(filePath));
    const auto db = job ? job->db() : std::shared_ptr<GlbDB>{};
    return db ? static_cast<qlonglong>(db->getDBInstanceID().getValue()) : 0;
}

void ModelPreviewService::invalidatePreview(const QString& filePath) {
    removeStaleVersions(filePath);
}

void ModelPreviewService::pruneCache() {
    QDir directory(m_cacheDirectory);
    QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.png"), QStringLiteral("*.failed")},
        QDir::Files, QDir::Time | QDir::Reversed);

    qint64 totalBytes = 0;
    for (const QFileInfo& file : files) {
        totalBytes += file.size();
    }

    for (const QFileInfo& file : files) {
        if (totalBytes <= kMaximumCacheBytes) {
            break;
        }
        const qint64 size = file.size();
        if (QFile::remove(file.absoluteFilePath())) {
            totalBytes -= size;
        }
    }
}

QString ModelPreviewService::cachePathFor(const QString& filePath) const {
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return {};
    }

    const QString path = normalizedPath(filePath);
    QByteArray signature;
    signature.reserve(path.size() + 96);
    signature.append(path.toUtf8());
    signature.append('|');
    signature.append(QByteArray::number(info.size()));
    signature.append('|');
    signature.append(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    signature.append('|');
    signature.append(kRenderStyleVersion);

    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex().left(32));
    return m_cacheDirectory + QLatin1Char('/') + pathPrefixFor(filePath) + QLatin1Char('-') + fingerprint + QStringLiteral(".png");
}

QString ModelPreviewService::pathPrefixFor(const QString& filePath) const {
    return QString::fromLatin1(QCryptographicHash::hash(
                                   normalizedPath(filePath).toUtf8(), QCryptographicHash::Sha256)
                                   .toHex()
                                   .left(16));
}

QString ModelPreviewService::localFileUrl(const QString& path) {
    return QUrl::fromLocalFile(path).toString();
}

void ModelPreviewService::enqueue(PreviewRequest request) {
    request.jobKey = normalizedPath(request.filePath);
    if (request.cachePath.isEmpty() || request.jobKey.isEmpty()) {
        return;
    }

    // Multiple list entries may name the same file through a symlink or path
    // alias. Coalesce the work, but retain every caller for completion fan-out.
    m_requestAliases[request.jobKey].insert(request.filePath);
    if (m_pendingPaths.contains(request.jobKey)) return;

    m_pendingPaths.insert(request.jobKey);
    m_queue.enqueue(std::move(request));
    processNext();
}

void ModelPreviewService::processNext() {
    if (m_busy || m_queue.isEmpty()) {
        return;
    }

    m_busy = true;
    const PreviewRequest request = m_queue.dequeue();
    if (QFileInfo(request.filePath).suffix().compare(
            QStringLiteral("glb"), Qt::CaseInsensitive) == 0) {
        startGlbGeneration(request);
        return;
    }
    const QUrl thumbnailUrl(request.sourceThumbnailUrl);
    if (thumbnailUrl.isValid() && (thumbnailUrl.scheme() == QStringLiteral("http") || thumbnailUrl.scheme() == QStringLiteral("https"))) {
        startRemoteThumbnail(request);
        return;
    }

    startLocalGeneration(request);
}

void ModelPreviewService::startRemoteThumbnail(const PreviewRequest& request) {
    QNetworkRequest networkRequest(QUrl(request.sourceThumbnailUrl));
    networkRequest.setRawHeader("User-Agent", "GPlatform/1.0");
    networkRequest.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    networkRequest.setTransferTimeout(15000);

    QNetworkReply* reply = m_networkManager.get(networkRequest);
    connect(reply, &QNetworkReply::finished, this, [this, request, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QImage image = QImage::fromData(reply->readAll());
            const PreviewResult result = normalizeImage(image);
            if (result.success) {
                reply->deleteLater();
                finishRequest(request, result);
                return;
            }
        }

        LOG_WARN("Model preview thumbnail download failed for {}: {}",
                 request.filePath.toStdString(), reply->errorString().toStdString());
        reply->deleteLater();
        startLocalGeneration(request);
    });
}

void ModelPreviewService::startLocalGeneration(const PreviewRequest& request) {
    auto* watcher = new QFutureWatcher<PreviewResult>(this);
    connect(watcher, &QFutureWatcher<PreviewResult>::finished, this, [this, request, watcher]() {
        const PreviewResult result = watcher->result();
        watcher->deleteLater();
        finishRequest(request, result);
    });
    watcher->setFuture(QtConcurrent::run([path = request.filePath]() {
        return generateLocalPreview(path);
    }));
}

void ModelPreviewService::startGlbGeneration(const PreviewRequest& request) {
    using SharedImportResult = std::shared_ptr<GlbImportResult>;
    auto* importerWatcher = new QFutureWatcher<SharedImportResult>(this);
    connect(importerWatcher, &QFutureWatcher<SharedImportResult>::finished, this,
            [this, request, importerWatcher]() {
        SharedImportResult imported;
        try {
            imported = importerWatcher->result();
        } catch (const std::exception& exception) {
            importerWatcher->deleteLater();
            finishRequest(
                request,
                {false,
                 QStringLiteral("GLB import failed: %1")
                     .arg(QString::fromUtf8(exception.what())),
                 {}});
            return;
        } catch (...) {
            importerWatcher->deleteLater();
            finishRequest(
                request,
                {false, QStringLiteral("GLB import failed with an unknown error"), {}});
            return;
        }
        importerWatcher->deleteLater();
        if (!imported) {
            finishRequest(
                request,
                {false, QStringLiteral("GLB import returned no result"), {}});
            return;
        }
        if (!imported->success) {
            finishRequest(
                request,
                {false, QString::fromStdString(imported->errorMessage), {}});
            return;
        }

        auto job = std::make_shared<ModelPreviewDBSession>();
        std::shared_ptr<GlbDB> glb;
        try {
            glb = job->publish(request.filePath, std::move(*imported));
        } catch (const std::exception& exception) {
            finishRequest(
                request,
                {false,
                 QStringLiteral("Unable to publish temporary GlbDB: %1")
                     .arg(QString::fromUtf8(exception.what())),
                 {}});
            return;
        } catch (...) {
            finishRequest(
                request,
                {false,
                 QStringLiteral("Unable to publish temporary GlbDB: unknown error"),
                 {}});
            return;
        }
        if (!glb) {
            finishRequest(
                request,
                {false, QStringLiteral("Unable to publish temporary GlbDB"), {}});
            return;
        }
        m_previewDBJobs.insert(request.jobKey, job);
        emit temporaryPreviewDBCountChanged();
        const qlonglong dbInstanceId = static_cast<qlonglong>(
            glb->getDBInstanceID().getValue());
        emit temporaryPreviewDBReady(
            request.filePath, dbInstanceId);

        const std::shared_ptr<const GlbDB> previewDB = glb;
        auto* previewWatcher = new QFutureWatcher<PreviewResult>(this);
        connect(previewWatcher, &QFutureWatcher<PreviewResult>::finished, this,
                [this, request, dbInstanceId, previewWatcher]() {
            PreviewResult result;
            try {
                result = previewWatcher->result();
            } catch (const std::exception& exception) {
                result = {
                    false,
                    QStringLiteral("GLB preview rendering failed: %1")
                        .arg(QString::fromUtf8(exception.what())),
                    {}};
            } catch (...) {
                result = {
                    false,
                    QStringLiteral("GLB preview rendering failed with an unknown error"),
                    {}};
            }
            previewWatcher->deleteLater();
            releaseTemporaryPreviewDB(
                request.jobKey, request.filePath, dbInstanceId);
            finishRequest(request, result);
        });
        previewWatcher->setFuture(QtConcurrent::run([previewDB] {
            return renderTrianglesPreview(previewDB->printableTriangles());
        }));
    });
    importerWatcher->setFuture(QtConcurrent::run([path = request.filePath] {
        return std::make_shared<GlbImportResult>(
            GlbDBImporter{}.importFile(path.toStdString()));
    }));
}

void ModelPreviewService::releaseTemporaryPreviewDB(
    const QString& jobKey,
    const QString& filePath,
    qlonglong dbInstanceId) {
    if (m_previewDBJobs.remove(jobKey) == 0) return;
    emit temporaryPreviewDBReleased(filePath, dbInstanceId);
    emit temporaryPreviewDBCountChanged();
}

void ModelPreviewService::finishRequest(const PreviewRequest& request,
                                        const PreviewResult& result) {
    bool persisted = false;
    QString errorMessage = result.errorMessage;
    if (result.success && !result.pngData.isEmpty()) {
        QSaveFile file(request.cachePath);
        if (file.open(QIODevice::WriteOnly) && file.write(result.pngData) == result.pngData.size() && file.commit()) {
            persisted = true;
        } else {
            errorMessage = QStringLiteral("Unable to persist preview cache");
        }
    }

    m_pendingPaths.remove(request.jobKey);
    QSet<QString> requestAliases = m_requestAliases.take(request.jobKey);
    if (requestAliases.isEmpty()) requestAliases.insert(request.filePath);
    m_busy = false;

    if (persisted) {
        QFile::remove(request.cachePath + QStringLiteral(".failed"));
        removeStaleVersions(request.filePath, request.cachePath);
        const QString previewUrl = localFileUrl(request.cachePath);
        for (const QString& filePath : std::as_const(requestAliases)) {
            emit previewReady(filePath, previewUrl);
        }
        pruneCache();
    } else {
        QSaveFile failureFile(request.cachePath + QStringLiteral(".failed"));
        if (failureFile.open(QIODevice::WriteOnly)) {
            failureFile.write(errorMessage.toUtf8());
            failureFile.commit();
        }
        for (const QString& filePath : std::as_const(requestAliases)) {
            emit previewFailed(filePath, errorMessage);
        }
    }

    processNext();
}

ModelPreviewService::PreviewResult ModelPreviewService::generateLocalPreview(
    const QString& filePath) {
    if (QFileInfo(filePath).suffix().compare(QStringLiteral("3mf"), Qt::CaseInsensitive) == 0) {
        const QImage embedded = extract3mfThumbnail(filePath);
        const PreviewResult embeddedResult = normalizeImage(embedded);
        if (embeddedResult.success) {
            return embeddedResult;
        }
    }
    return renderMeshPreview(filePath);
}

ModelPreviewService::PreviewResult ModelPreviewService::normalizeImage(const QImage& source) {
    if (source.isNull()) {
        return {false, QStringLiteral("Preview image is empty"), {}};
    }

    QImage canvas(kPreviewSize, kPreviewSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    const QImage scaled = source.scaled(
        canvas.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage((canvas.width() - scaled.width()) / 2,
                      (canvas.height() - scaled.height()) / 2, scaled);
    painter.end();

    const QByteArray data = imageToPng(canvas);
    return {!data.isEmpty(), data.isEmpty() ? QStringLiteral("PNG encoding failed") : QString(), data};
}

ModelPreviewService::PreviewResult ModelPreviewService::renderMeshPreview(
    const QString& filePath) {
    std::vector<GeomTriangle> triangles;
    QString loadError;
    auto result = ModelLoaderUtil::loadModel(filePath);
    if (result.success) {
        triangles = std::move(result.triangles);
    } else {
        loadError = result.errorMessage;
    }
    if (triangles.empty()) {
        return {false,
                loadError.isEmpty() ? QStringLiteral("Model contains no renderable geometry")
                                    : loadError,
                {}};
    }

    return renderTrianglesPreview(triangles);
}

ModelPreviewService::PreviewResult ModelPreviewService::renderTrianglesPreview(
    const std::vector<GeomTriangle>& triangles) {
    if (triangles.empty()) {
        return {false, QStringLiteral("Model contains no renderable geometry"), {}};
    }

    const Vec3 forward = normalized({-1.0, 1.0, -0.72});
    const Vec3 right = normalized(cross(forward, {0.0, 0.0, 1.0}));
    const Vec3 screenUp = normalized(cross(right, forward));
    const Vec3 light = normalized({-0.35, -0.55, 0.76});

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();

    auto projectRaw = [&](const Vector3& value) {
        const Vec3 point = toVec3(value);
        const ProjectedVertex projected{dot(point, right), dot(point, screenUp), dot(point, forward)};
        minX = std::min(minX, projected.x);
        minY = std::min(minY, projected.y);
        maxX = std::max(maxX, projected.x);
        maxY = std::max(maxY, projected.y);
        return projected;
    };

    for (const GeomTriangle& triangle : triangles) {
        projectRaw(triangle.vertex1);
        projectRaw(triangle.vertex2);
        projectRaw(triangle.vertex3);
    }

    const double width = maxX - minX;
    const double height = maxY - minY;
    if (width <= std::numeric_limits<double>::epsilon() || height <= std::numeric_limits<double>::epsilon()) {
        return {false, QStringLiteral("Model projection is degenerate"), {}};
    }

    const double scale = 0.86 * kRasterSize / std::max(width, height);
    const double centerX = (minX + maxX) * 0.5;
    const double centerY = (minY + maxY) * 0.5;
    auto project = [&](const Vector3& value) {
        const Vec3 point = toVec3(value);
        return ProjectedVertex{
            (dot(point, right) - centerX) * scale + kRasterSize * 0.5,
            kRasterSize * 0.5 - (dot(point, screenUp) - centerY) * scale,
            dot(point, forward),
        };
    };

    QImage raster(kRasterSize, kRasterSize, QImage::Format_ARGB32_Premultiplied);
    raster.fill(Qt::transparent);
    std::vector<double> depthBuffer(
        static_cast<size_t>(kRasterSize) * kRasterSize,
        std::numeric_limits<double>::max());

    for (const GeomTriangle& triangle : triangles) {
        const ProjectedVertex p0 = project(triangle.vertex1);
        const ProjectedVertex p1 = project(triangle.vertex2);
        const ProjectedVertex p2 = project(triangle.vertex3);
        const double area = edge(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
        if (std::abs(area) < 1e-8) {
            continue;
        }

        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))));
        const int x1 = std::min(kRasterSize - 1, static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))));
        const int y1 = std::min(kRasterSize - 1, static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))));

        Vec3 normal = normalized(cross(
            subtract(toVec3(triangle.vertex2), toVec3(triangle.vertex1)),
            subtract(toVec3(triangle.vertex3), toVec3(triangle.vertex1))));
        if (dot(normal, forward) > 0.0) {
            normal = {-normal.x, -normal.y, -normal.z};
        }
        const double intensity = std::clamp(0.36 + 0.64 * std::max(0.0, dot(normal, light)), 0.0, 1.0);
        const int red = static_cast<int>(92 + 78 * intensity);
        const int green = static_cast<int>(133 + 77 * intensity);
        const int blue = static_cast<int>(164 + 67 * intensity);
        const QRgb color = qRgba(red, green, blue, 255);

        for (int y = y0; y <= y1; ++y) {
            QRgb* scanLine = reinterpret_cast<QRgb*>(raster.scanLine(y));
            for (int x = x0; x <= x1; ++x) {
                const double sampleX = x + 0.5;
                const double sampleY = y + 0.5;
                const double w0 = edge(p1.x, p1.y, p2.x, p2.y, sampleX, sampleY) / area;
                const double w1 = edge(p2.x, p2.y, p0.x, p0.y, sampleX, sampleY) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < -1e-7 || w1 < -1e-7 || w2 < -1e-7) {
                    continue;
                }

                const double depth = w0 * p0.depth + w1 * p1.depth + w2 * p2.depth;
                const size_t index = static_cast<size_t>(y) * kRasterSize + x;
                if (depth < depthBuffer[index]) {
                    depthBuffer[index] = depth;
                    scanLine[x] = color;
                }
            }
        }
    }

    const QImage preview = raster.scaled(
        kPreviewSize, kPreviewSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QByteArray data = imageToPng(preview);
    return {!data.isEmpty(), data.isEmpty() ? QStringLiteral("PNG encoding failed") : QString(), data};
}

QImage ModelPreviewService::extract3mfThumbnail(const QString& filePath) {
    mz_zip_archive archive{};
    const QByteArray encodedPath = QFile::encodeName(filePath);
    if (!mz_zip_reader_init_file(&archive, encodedPath.constData(), 0)) {
        return {};
    }

    const std::array<const char*, 7> candidates{
        "Auxiliaries/.thumbnails/thumbnail_middle.png",
        "Auxiliaries/.thumbnails/thumbnail_3mf.png",
        "Metadata/plate_1.png",
        "Metadata/thumbnail.png",
        "Metadata/bbl_thumbnail.png",
        "thumbnail.png",
        "3D/thumbnail.png",
    };

    QImage image;
    for (const char* candidate : candidates) {
        size_t size = 0;
        void* data = mz_zip_reader_extract_file_to_heap(&archive, candidate, &size, 0);
        if (!data) {
            continue;
        }
        image = QImage::fromData(static_cast<const uchar*>(data), static_cast<int>(size));
        mz_free(data);
        if (!image.isNull()) {
            break;
        }
    }

    mz_zip_reader_end(&archive);
    return image;
}

void ModelPreviewService::removeStaleVersions(const QString& filePath,
                                              const QString& keepPath) {
    QDir directory(m_cacheDirectory);
    const QString pattern = pathPrefixFor(filePath) + QStringLiteral("-*");
    const QFileInfoList files = directory.entryInfoList({pattern}, QDir::Files);
    for (const QFileInfo& file : files) {
        if (file.absoluteFilePath() != keepPath) {
            QFile::remove(file.absoluteFilePath());
        }
    }
}

REGISTER_SERVICES_QML_SINGLETON_CUSTOM(
    ModelPreviewService, "ModelPreviewService", &ModelPreviewService::create)
