#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class IModelService : public QObject {
    Q_OBJECT

public:
    explicit IModelService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IModelService() override = default;

    virtual QJsonArray models() const = 0;
    virtual QJsonArray categories() const = 0;
    virtual QJsonArray searchSuggestions() const = 0;
    virtual QJsonArray hotSearches() const = 0;
    virtual QJsonArray hotPrints() const = 0;
    virtual bool loading() const = 0;
    virtual bool hasMore() const = 0;
    virtual int totalCount() const = 0;
    virtual QString errorMessage() const = 0;

    virtual void fetchModels(const QString& category, const QString& searchQuery) = 0;
    virtual void fetchDiscovery() = 0;
    virtual void fetchCategories() = 0;
    virtual void loadMoreModels() = 0;
    virtual QJsonObject getModelById(const QString& id) const = 0;

signals:
    void modelsChanged();
    void categoriesChanged();
    void discoveryChanged();
    void loadingChanged();
    void paginationChanged();
    void errorMessageChanged();
    void modelsFetched(bool success);
};
