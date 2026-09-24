#include "LocalModelLibraryService.h"

#include "DownloadManager.h"
#include "Foundation/Log.h"

#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace {

int matchScore(const QString& normalizedName, const QString& normalizedQuery) {
    if (normalizedName == normalizedQuery) {
        return 300;
    }
    if (normalizedName.startsWith(normalizedQuery)) {
        return 200;
    }
    if (normalizedName.contains(normalizedQuery)) {
        return 100;
    }
    return 0;
}

} // namespace

LocalModelLibraryService* LocalModelLibraryService::instance() {
    static auto* service = new LocalModelLibraryService();
    return service;
}

LocalModelLibraryService::LocalModelLibraryService(QObject* parent)
    : QObject(parent) {
    auto* downloads = DownloadManager::getInstance();
    connect(downloads,
            &DownloadManager::downloadHistoryChanged,
            this,
            &LocalModelLibraryService::modelsChanged);
}

QString LocalModelLibraryService::normalizeSearchText(const QString& value) {
    QString normalized = value.simplified().toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[_\\-]+")), QStringLiteral(" "));
    return normalized.simplified();
}

QVector<LocalModelRecord> LocalModelLibraryService::records() const {
    QVector<LocalModelRecord> result;
    const QList<QJsonObject> history = DownloadManager::getInstance()->downloadHistory();
    result.reserve(history.size() + m_sessionRecords.size());

    for (auto it = m_sessionRecords.cbegin(); it != m_sessionRecords.cend(); ++it) {
        if (QFileInfo::exists(it.value().filePath)) {
            result.push_back(it.value());
        }
    }

    for (const QJsonObject& item : history) {
        LocalModelRecord record;
        record.libraryItemId = item.value(QStringLiteral("libraryItemId")).toString().trimmed();
        record.name = item.value(QStringLiteral("name")).toString().simplified();
        record.filePath = item.value(QStringLiteral("filePath")).toString();
        bool sizeValid = false;
        record.fileSize = item.value(QStringLiteral("fileSize")).toVariant().toLongLong(&sizeValid);
        record.downloadDate = QDateTime::fromString(
            item.value(QStringLiteral("downloadDate")).toString(),
            QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        record.source = item.value(QStringLiteral("source")).toString();
        record.sourceThumbnailUrl =
            item.value(QStringLiteral("sourceThumbnailUrl")).toString();

        const QFileInfo fileInfo(record.filePath);
        if (record.libraryItemId.isEmpty() || record.name.isEmpty() ||
            record.filePath.isEmpty() || !fileInfo.exists() || !sizeValid ||
            !record.downloadDate.isValid()) {
            LOG_ERROR("Invalid local model library record: id='{}' name='{}' path='{}'",
                      record.libraryItemId.toStdString(),
                      record.name.toStdString(),
                      record.filePath.toStdString());
            continue;
        }
        record.format = fileInfo.suffix().toLower();
        result.push_back(std::move(record));
    }
    return result;
}

QVariantMap LocalModelLibraryService::toVariantMap(const LocalModelRecord& record,
                                                   bool includeFilePath) {
    QVariantMap value;
    value.insert(QStringLiteral("libraryItemId"), record.libraryItemId);
    value.insert(QStringLiteral("name"), record.name);
    value.insert(QStringLiteral("format"), record.format);
    value.insert(QStringLiteral("fileSize"), record.fileSize);
    value.insert(QStringLiteral("downloadDate"),
                 record.downloadDate.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    if (!record.source.isEmpty()) {
        value.insert(QStringLiteral("source"), record.source);
    }
    if (!record.sourceThumbnailUrl.isEmpty()) {
        value.insert(QStringLiteral("sourceThumbnailUrl"), record.sourceThumbnailUrl);
    }
    if (includeFilePath) {
        value.insert(QStringLiteral("filePath"), record.filePath);
    }
    return value;
}

QVariantList LocalModelLibraryService::models(const QString& query,
                                              const QString& sortMode) const {
    QVector<LocalModelRecord> filtered;
    const QString normalizedQuery = normalizeSearchText(query);
    for (const LocalModelRecord& record : records()) {
        if (!normalizedQuery.isEmpty() &&
            !normalizeSearchText(record.name).contains(normalizedQuery)) {
            continue;
        }
        filtered.push_back(record);
    }

    if (sortMode == QStringLiteral("name")) {
        std::sort(filtered.begin(), filtered.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.name.localeAwareCompare(rhs.name) < 0;
        });
    } else if (sortMode == QStringLiteral("size")) {
        std::sort(filtered.begin(), filtered.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.fileSize != rhs.fileSize) {
                return lhs.fileSize > rhs.fileSize;
            }
            return lhs.name.localeAwareCompare(rhs.name) < 0;
        });
    } else if (sortMode == QStringLiteral("date")) {
        std::sort(filtered.begin(), filtered.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.downloadDate != rhs.downloadDate) {
                return lhs.downloadDate > rhs.downloadDate;
            }
            return lhs.name.localeAwareCompare(rhs.name) < 0;
        });
    } else {
        LOG_ERROR("Unsupported local model sort mode: {}", sortMode.toStdString());
        return {};
    }

    QVariantList result;
    result.reserve(filtered.size());
    for (const LocalModelRecord& record : filtered) {
        result.push_back(toVariantMap(record, true));
    }
    return result;
}

QVariantMap LocalModelLibraryService::search(const QString& query, int limit) const {
    QVariantMap result;
    const QString normalizedQuery = normalizeSearchText(query);
    if (normalizedQuery.isEmpty()) {
        result.insert(QStringLiteral("success"), false);
        result.insert(QStringLiteral("status"), QStringLiteral("invalid_query"));
        result.insert(QStringLiteral("error"), QStringLiteral("query must not be empty"));
        result.insert(QStringLiteral("error_code"), QStringLiteral("QUERY_EMPTY"));
        return result;
    }
    if (limit < 1 || limit > 20) {
        result.insert(QStringLiteral("success"), false);
        result.insert(QStringLiteral("status"), QStringLiteral("invalid_limit"));
        result.insert(QStringLiteral("error"),
                      QStringLiteral("limit must be between 1 and 20"));
        result.insert(QStringLiteral("error_code"), QStringLiteral("LIMIT_OUT_OF_RANGE"));
        return result;
    }

    struct Match final {
        int score = 0;
        LocalModelRecord record;
    };
    QVector<Match> matches;
    for (const LocalModelRecord& record : records()) {
        const int score = matchScore(normalizeSearchText(record.name), normalizedQuery);
        if (score > 0) {
            matches.push_back(Match{score, record});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match& lhs, const Match& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.record.name.localeAwareCompare(rhs.record.name) < 0;
    });

    QVariantList publicMatches;
    for (int index = 0; index < matches.size() && index < limit; ++index) {
        publicMatches.push_back(toVariantMap(matches.at(index).record, false));
    }

    QString status = QStringLiteral("not_found");
    if (!matches.isEmpty()) {
        const int topScore = matches.first().score;
        const int topCount = static_cast<int>(std::count_if(
            matches.cbegin(), matches.cend(), [topScore](const Match& match) {
                return match.score == topScore;
            }));
        status = topCount == 1 ? QStringLiteral("unique") : QStringLiteral("ambiguous");
    }

    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("status"), status);
    result.insert(QStringLiteral("query"), query);
    result.insert(QStringLiteral("matches"), publicMatches);
    result.insert(QStringLiteral("matchCount"), matches.size());
    return result;
}

bool LocalModelLibraryService::resolve(const QString& libraryItemId,
                                       LocalModelRecord& record,
                                       QString& error) const {
    record = {};
    error.clear();
    const QString requestedId = libraryItemId.trimmed();
    if (requestedId.isEmpty()) {
        error = QStringLiteral("libraryItemId must not be empty");
        return false;
    }

    for (const LocalModelRecord& candidate : records()) {
        if (candidate.libraryItemId == requestedId) {
            record = candidate;
            return true;
        }
    }
    error = QStringLiteral("Local model library item was not found");
    return false;
}

bool LocalModelLibraryService::refresh(QString& error) {
    error.clear();
    DownloadManager::getInstance()->loadDownloadHistory();
    emit modelsChanged();
    return true;
}

bool LocalModelLibraryService::deleteModel(const QString& libraryItemId, QString& error) {
    if (m_sessionRecords.contains(libraryItemId)) {
        error = QStringLiteral("Session model records cannot delete their source files");
        return false;
    }
    LocalModelRecord record;
    if (!resolve(libraryItemId, record, error)) {
        return false;
    }
    if (!DownloadManager::getInstance()->deleteDownloadedModel(record.filePath)) {
        error = QStringLiteral("Failed to delete local model file");
        return false;
    }
    return true;
}

QString LocalModelLibraryService::registerSessionModel(
    const QString& filePath,
    const QString& displayName) {
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return {};
    const QString canonical = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath()
        : info.canonicalFilePath();
    const QString id = QStringLiteral("session-%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(
            canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)));

    LocalModelRecord record;
    record.libraryItemId = id;
    record.name = displayName.simplified().isEmpty()
        ? info.completeBaseName()
        : displayName.simplified();
    record.filePath = canonical;
    record.format = info.suffix().toLower();
    record.fileSize = info.size();
    record.downloadDate = QDateTime::currentDateTime();
    record.source = QStringLiteral("session");
    m_sessionRecords.insert(id, std::move(record));
    emit modelsChanged();
    return id;
}

void LocalModelLibraryService::unregisterSessionModel(
    const QString& libraryItemId) {
    if (m_sessionRecords.remove(libraryItemId) > 0) {
        emit modelsChanged();
    }
}
