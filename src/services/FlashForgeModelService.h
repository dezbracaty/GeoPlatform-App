#pragma once

#include "IModelService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

class FlashForgeModelService final : public IModelService {
    Q_OBJECT

public:
    explicit FlashForgeModelService(QObject* parent = nullptr);
    ~FlashForgeModelService() override = default;

    QJsonArray models() const override {
        return m_models;
    }
    QJsonArray categories() const override {
        return m_categories;
    }
    QJsonArray searchSuggestions() const override {
        return m_searchSuggestions;
    }
    QJsonArray hotSearches() const override {
        return m_hotSearches;
    }
    QJsonArray hotPrints() const override {
        return m_hotPrints;
    }
    bool loading() const override {
        return m_loading;
    }
    bool hasMore() const override {
        return m_hasMore;
    }
    int totalCount() const override {
        return m_totalCount;
    }
    QString errorMessage() const override {
        return m_errorMessage;
    }

    void fetchModels(const QString& category, const QString& searchQuery) override;
    void fetchDiscovery() override;
    void fetchCategories() override;
    void loadMoreModels() override;
    QJsonObject getModelById(const QString& id) const override;

private:
    void setLoading(bool loading);
    void setErrorMessage(const QString& message);
    void requestModels(int page, bool append);
    void requestHotRankings();
    void requestSearchSuggestions();
    QNetworkRequest createRequest(const QUrl& url) const;
    QJsonObject mapModel(const QJsonObject& source) const;
    QString effectiveSearchQuery() const;
    void updatePagination(int page, int pageSize, int total, int receivedCount);

    QNetworkAccessManager* m_networkManager;
    QPointer<QNetworkReply> m_modelsReply;
    QPointer<QNetworkReply> m_rankingsReply;
    QPointer<QNetworkReply> m_suggestionsReply;
    QJsonArray m_models;
    QJsonArray m_categories;
    QJsonArray m_searchSuggestions;
    QJsonArray m_hotSearches;
    QJsonArray m_hotPrints;
    bool m_loading = false;
    bool m_hasMore = false;
    bool m_discoveryLoaded = false;
    QString m_errorMessage;
    QString m_anonymousDeviceId;
    int m_currentPage = 1;
    int m_totalCount = 0;
    int m_requestSerial = 0;
    QString m_currentCategory;
    QString m_currentSearch;
};
