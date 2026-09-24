#pragma once

#include "IModelService.h"

#include <QQmlEngine>
#include <QJSEngine>
#include <QStringList>
#include <QtQml/qqml.h>

#include <memory>

class ModelService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QJsonArray models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QJsonArray categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QJsonArray searchSuggestions READ searchSuggestions NOTIFY discoveryChanged)
    Q_PROPERTY(QJsonArray hotSearches READ hotSearches NOTIFY discoveryChanged)
    Q_PROPERTY(QJsonArray hotPrints READ hotPrints NOTIFY discoveryChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY paginationChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY paginationChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    static ModelService* getInstance();
    static ModelService* create(QQmlEngine* engine, QJSEngine*);

    QJsonArray models() const;
    QJsonArray categories() const;
    QJsonArray searchSuggestions() const;
    QJsonArray hotSearches() const;
    QJsonArray hotPrints() const;
    bool loading() const;
    bool hasMore() const;
    int totalCount() const;
    QString errorMessage() const;

    Q_INVOKABLE void fetchModels(const QString& category = {}, const QString& searchQuery = {});
    Q_INVOKABLE void fetchDiscovery();
    Q_INVOKABLE void fetchCategories();
    Q_INVOKABLE void loadMoreModels();
    Q_INVOKABLE QJsonObject getModelById(const QString& id) const;

    Q_INVOKABLE bool isPrintableLicense(const QString& license) const;
    Q_INVOKABLE QJsonObject getLicenseInfo(const QString& license) const;
    Q_INVOKABLE bool isCommercialUseAllowed(const QString& license) const;
    Q_INVOKABLE bool isModificationAllowed(const QString& license) const;
    Q_INVOKABLE QJsonArray getModelsBySource(const QString& source) const;
    Q_INVOKABLE QJsonArray getPrintReadyModels() const;

signals:
    void modelsChanged();
    void categoriesChanged();
    void discoveryChanged();
    void loadingChanged();
    void paginationChanged();
    void errorMessageChanged();
    void modelsFetched(bool success);

private:
    explicit ModelService(QObject* parent = nullptr);
    ~ModelService() override = default;

    void setBackend(std::unique_ptr<IModelService> backend);

    std::unique_ptr<IModelService> m_backend;
};
