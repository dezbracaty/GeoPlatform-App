#include "FlashForgeModelService.h"

#include "Foundation/Log.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QNetworkRequest>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <vector>

namespace {

constexpr auto kApiBaseUrl = "https://api.voxelshare.com/api";
constexpr int kPageSize = 20;
constexpr int kRequestTimeoutMs = 15000;

bool parseEnvelope(const QByteArray& bytes, QJsonObject& data, QString& error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QObject::tr("The server returned invalid data");
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
        error = root.value(QStringLiteral("msg")).toString(QObject::tr("Model service request failed"));
        return false;
    }

    data = root.value(QStringLiteral("data")).toObject();
    return true;
}

QString languageCode()
{
    const QString code = QLocale().name().section(QLatin1Char('_'), 0, 0).toLower();
    return code.isEmpty() ? QStringLiteral("en") : code;
}

QString qtCompatibleImageUrl(QString url)
{
    // VoxelShare signs the image URL but allows the output transform to be
    // changed. The deployed Qt bundle has no WebP image plugin, so request
    // JPEG from the CDN while keeping the signed URL and thumbnail size.
    url.replace(QStringLiteral("%2Fformat%2Fwebp"),
                QStringLiteral("%2Fformat%2Fjpg"),
                Qt::CaseInsensitive);
    url.replace(QStringLiteral("/format/webp"),
                QStringLiteral("/format/jpg"),
                Qt::CaseInsensitive);
    return url;
}

} // namespace

FlashForgeModelService::FlashForgeModelService(QObject* parent)
    : IModelService(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_anonymousDeviceId(QStringLiteral("gplatform-%1").arg(
          QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    m_categories = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("featured")},
                    {QStringLiteral("name"), tr("Recommended")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("popular")},
                    {QStringLiteral("name"), tr("Trending")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("accessories")},
                    {QStringLiteral("name"), tr("Useful Accessories")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("art")},
                    {QStringLiteral("name"), tr("Decorative Art")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("education")},
                    {QStringLiteral("name"), tr("Education")}}
    };
}

void FlashForgeModelService::fetchModels(const QString& category, const QString& searchQuery)
{
    m_currentCategory = category.trimmed().toLower();
    if (m_currentCategory.isEmpty()) {
        m_currentCategory = QStringLiteral("featured");
    }
    m_currentSearch = searchQuery.trimmed();
    m_currentPage = 1;
    m_totalCount = 0;
    m_hasMore = false;
    emit paginationChanged();
    requestModels(1, false);
}

void FlashForgeModelService::fetchDiscovery()
{
    if (m_discoveryLoaded || m_rankingsReply || m_suggestionsReply) {
        return;
    }
    requestHotRankings();
    requestSearchSuggestions();
}

void FlashForgeModelService::fetchCategories()
{
    emit categoriesChanged();
}

void FlashForgeModelService::loadMoreModels()
{
    if (m_loading || !m_hasMore) {
        return;
    }
    requestModels(m_currentPage + 1, true);
}

QJsonObject FlashForgeModelService::getModelById(const QString& id) const
{
    for (const QJsonValue& value : m_models) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("id")).toString() == id) {
            return model;
        }
    }
    return {};
}

void FlashForgeModelService::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit loadingChanged();
}

void FlashForgeModelService::setErrorMessage(const QString& message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

QNetworkRequest FlashForgeModelService::createRequest(const QUrl& url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GPlatform/1.0"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Language", languageCode().toUtf8());
    request.setRawHeader("Language", languageCode().toUtf8());
    request.setTransferTimeout(kRequestTimeoutMs);
    return request;
}

QString FlashForgeModelService::effectiveSearchQuery() const
{
    if (!m_currentSearch.isEmpty()) {
        return m_currentSearch;
    }
    if (m_currentCategory == QStringLiteral("accessories")) {
        return QStringLiteral("3D printer accessories");
    }
    if (m_currentCategory == QStringLiteral("art")) {
        return QStringLiteral("art");
    }
    if (m_currentCategory == QStringLiteral("education")) {
        return QStringLiteral("education");
    }
    return {};
}

void FlashForgeModelService::requestModels(int page, bool append)
{
    if (m_modelsReply) {
        m_modelsReply->abort();
    }

    const int serial = ++m_requestSerial;
    const QString query = effectiveSearchQuery();
    QNetworkReply* reply = nullptr;

    if (query.isEmpty()) {
        QUrl url(QStringLiteral("%1/v3/model/featured/list").arg(
            QString::fromLatin1(kApiBaseUrl)));
        QUrlQuery urlQuery;
        urlQuery.addQueryItem(QStringLiteral("pageNumber"), QString::number(page));
        urlQuery.addQueryItem(QStringLiteral("pageSize"), QString::number(kPageSize));
        url.setQuery(urlQuery);
        reply = m_networkManager->get(createRequest(url));
    } else {
        const QUrl url(QStringLiteral("%1/v3/model/search/models").arg(
            QString::fromLatin1(kApiBaseUrl)));
        QNetworkRequest request = createRequest(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        const QJsonObject body{
            {QStringLiteral("pageNumber"), page},
            {QStringLiteral("pageSize"), kPageSize},
            {QStringLiteral("keyword"), query},
            {QStringLiteral("did"), m_anonymousDeviceId}
        };
        reply = m_networkManager->post(
            request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }

    m_modelsReply = reply;
    setErrorMessage({});
    setLoading(true);

    connect(reply, &QNetworkReply::finished, this, [this, reply, serial, page, append]() {
        const bool currentRequest = serial == m_requestSerial;
        if (m_modelsReply == reply) {
            m_modelsReply = nullptr;
        }

        if (!currentRequest) {
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            setLoading(false);
            setErrorMessage(tr("Unable to load online models: %1").arg(reply->errorString()));
            LOG_WARN("VoxelShare model request failed: {}", reply->errorString().toStdString());
            reply->deleteLater();
            emit modelsFetched(false);
            return;
        }

        QJsonObject data;
        QString error;
        if (!parseEnvelope(reply->readAll(), data, error)) {
            setLoading(false);
            setErrorMessage(error);
            reply->deleteLater();
            emit modelsFetched(false);
            return;
        }

        const QJsonArray items = data.value(QStringLiteral("items")).toArray();
        QJsonArray mapped;
        for (const QJsonValue& item : items) {
            if (item.isObject()) {
                mapped.append(mapModel(item.toObject()));
            }
        }

        if (m_currentCategory == QStringLiteral("popular") && m_currentSearch.isEmpty()) {
            std::vector<QJsonObject> sorted;
            sorted.reserve(mapped.size());
            for (const QJsonValue& value : mapped) {
                sorted.push_back(value.toObject());
            }
            std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject& left,
                                                               const QJsonObject& right) {
                const qint64 leftScore = left.value(QStringLiteral("downloads")).toInteger()
                    + left.value(QStringLiteral("prints")).toInteger() * 2
                    + left.value(QStringLiteral("likes")).toInteger();
                const qint64 rightScore = right.value(QStringLiteral("downloads")).toInteger()
                    + right.value(QStringLiteral("prints")).toInteger() * 2
                    + right.value(QStringLiteral("likes")).toInteger();
                return leftScore > rightScore;
            });
            mapped = {};
            for (const QJsonObject& model : sorted) {
                mapped.append(model);
            }
        }

        if (append) {
            QSet<QString> knownIds;
            for (const QJsonValue& value : m_models) {
                knownIds.insert(value.toObject().value(QStringLiteral("id")).toString());
            }
            for (const QJsonValue& value : mapped) {
                const QString id = value.toObject().value(QStringLiteral("id")).toString();
                if (!knownIds.contains(id)) {
                    knownIds.insert(id);
                    m_models.append(value);
                }
            }
        } else {
            m_models = mapped;
        }

        const int responsePage = data.value(QStringLiteral("pageNumber")).toInt(page);
        const int responsePageSize = data.value(QStringLiteral("pageSize")).toInt(kPageSize);
        const int total = data.value(QStringLiteral("total")).toInt(m_models.size());
        updatePagination(responsePage, responsePageSize, total, items.size());
        setLoading(false);
        emit modelsChanged();
        emit modelsFetched(true);
        reply->deleteLater();
    });
}

void FlashForgeModelService::updatePagination(int page, int pageSize, int total, int receivedCount)
{
    m_currentPage = page;
    m_totalCount = std::max(total, static_cast<int>(m_models.size()));
    m_hasMore = receivedCount >= pageSize && m_models.size() < m_totalCount;
    emit paginationChanged();
}

QJsonObject FlashForgeModelService::mapModel(const QJsonObject& source) const
{
    const QJsonObject mainImage = source.value(QStringLiteral("mainImage")).toObject();
    const QJsonObject creator = source.value(QStringLiteral("creator")).toObject();
    const QString modelUrl = source.value(QStringLiteral("modelUrl")).toString();

    QJsonObject model{
        {QStringLiteral("id"), source.value(QStringLiteral("modelId")).toVariant().toString()},
        {QStringLiteral("name"), source.value(QStringLiteral("modelName")).toString()},
        {QStringLiteral("thumbnail"), qtCompatibleImageUrl(
             mainImage.value(QStringLiteral("url")).toString())},
        {QStringLiteral("imageAspectRatio"), mainImage.value(QStringLiteral("aspectRatio")).toDouble()},
        // The anonymous list API exposes the preview image format, not the
        // downloadable model file format. Do not present JPEG/PNG as model types.
        {QStringLiteral("format"), QStringLiteral("3D")},
        {QStringLiteral("author"), creator.value(QStringLiteral("nickname")).toString()},
        {QStringLiteral("authorAvatar"), qtCompatibleImageUrl(
             creator.value(QStringLiteral("avatar")).toString())},
        {QStringLiteral("authorId"), creator.value(QStringLiteral("id")).toString()},
        {QStringLiteral("likes"), source.value(QStringLiteral("likeCount")).toInt()},
        {QStringLiteral("downloads"), source.value(QStringLiteral("downloadCount")).toInt()},
        {QStringLiteral("prints"), source.value(QStringLiteral("printCount")).toInt()},
        {QStringLiteral("comments"), source.value(QStringLiteral("commentCount")).toInt()},
        {QStringLiteral("category"), source.value(QStringLiteral("source")).toString()},
        {QStringLiteral("source"), source.value(QStringLiteral("source")).toString()},
        {QStringLiteral("license"), source.value(QStringLiteral("license")).toString()},
        {QStringLiteral("free"), source.value(QStringLiteral("free")).toBool(true)},
        {QStringLiteral("printReady"), source.value(QStringLiteral("oneClickPrint")).toBool(false)},
        {QStringLiteral("url"), modelUrl},
        {QStringLiteral("downloadUrl"), QString()},
        {QStringLiteral("downloadType"), source.value(QStringLiteral("downloadType")).toString()},
        {QStringLiteral("description"), source.value(QStringLiteral("description")).toString()},
        {QStringLiteral("previewColor"), QStringLiteral("#E9EDF2")},
        {QStringLiteral("dataSource"), QStringLiteral("VoxelShare")}
    };

    if (model.value(QStringLiteral("name")).toString().isEmpty()) {
        model[QStringLiteral("name")] = tr("Untitled Model");
    }
    if (model.value(QStringLiteral("author")).toString().isEmpty()) {
        model[QStringLiteral("author")] = tr("VoxelShare Creator");
    }
    return model;
}

void FlashForgeModelService::requestHotRankings()
{
    QUrl url(QStringLiteral("%1/v3/model/search/hotword/rankings").arg(
        QString::fromLatin1(kApiBaseUrl)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
    url.setQuery(query);

    QNetworkReply* reply = m_networkManager->get(createRequest(url));
    m_rankingsReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_rankingsReply == reply) {
            m_rankingsReply = nullptr;
        }
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject data;
            QString error;
            if (parseEnvelope(reply->readAll(), data, error)) {
                const QJsonArray rankings = data.value(QStringLiteral("items")).toArray();
                for (const QJsonValue& value : rankings) {
                    const QJsonObject ranking = value.toObject();
                    const QString id = ranking.value(QStringLiteral("rankingId")).toString();
                    if (id == QStringLiteral("search")) {
                        m_hotSearches = ranking.value(QStringLiteral("words")).toArray();
                    } else if (id == QStringLiteral("print")) {
                        m_hotPrints = ranking.value(QStringLiteral("words")).toArray();
                    }
                }
                m_discoveryLoaded = !m_searchSuggestions.isEmpty();
                emit discoveryChanged();
            }
        } else {
            LOG_WARN("VoxelShare ranking request failed: {}", reply->errorString().toStdString());
        }
        reply->deleteLater();
    });
}

void FlashForgeModelService::requestSearchSuggestions()
{
    QUrl url(QStringLiteral("%1/v3/model/search/bottomword/recommend").arg(
        QString::fromLatin1(kApiBaseUrl)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("10"));
    query.addQueryItem(QStringLiteral("did"), m_anonymousDeviceId);
    url.setQuery(query);

    QNetworkReply* reply = m_networkManager->get(createRequest(url));
    m_suggestionsReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_suggestionsReply == reply) {
            m_suggestionsReply = nullptr;
        }
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject data;
            QString error;
            if (parseEnvelope(reply->readAll(), data, error)) {
                QJsonArray suggestions;
                const QJsonArray items = data.value(QStringLiteral("items")).toArray();
                for (const QJsonValue& value : items) {
                    const QString word = value.toObject().value(QStringLiteral("word")).toString();
                    if (!word.isEmpty()) {
                        suggestions.append(word);
                    }
                }
                m_searchSuggestions = suggestions;
                m_discoveryLoaded = !m_hotSearches.isEmpty() || !m_hotPrints.isEmpty();
                emit discoveryChanged();
            }
        } else {
            LOG_WARN("VoxelShare suggestion request failed: {}", reply->errorString().toStdString());
        }
        reply->deleteLater();
    });
}
