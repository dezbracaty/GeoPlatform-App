#include "ModelService.h"
#include "ServicesRegistration.hpp"

#include "FlashForgeModelService.h"

#include <algorithm>

ModelService::ModelService(QObject* parent)
    : QObject(parent)
{
    setBackend(std::make_unique<FlashForgeModelService>());
}

ModelService* ModelService::getInstance()
{
    static ModelService* instance = new ModelService();
    return instance;
}

ModelService* ModelService::create(QQmlEngine* engine, QJSEngine*)
{
    ModelService* instance = getInstance();
    if (engine && !instance->parent()) {
        instance->setParent(engine);
    }
    return instance;
}

void ModelService::setBackend(std::unique_ptr<IModelService> backend)
{
    if (m_backend) {
        m_backend->disconnect(this);
    }

    m_backend = std::move(backend);
    if (!m_backend) {
        return;
    }
    connect(m_backend.get(), &IModelService::modelsChanged,
            this, &ModelService::modelsChanged);
    connect(m_backend.get(), &IModelService::categoriesChanged,
            this, &ModelService::categoriesChanged);
    connect(m_backend.get(), &IModelService::discoveryChanged,
            this, &ModelService::discoveryChanged);
    connect(m_backend.get(), &IModelService::loadingChanged,
            this, &ModelService::loadingChanged);
    connect(m_backend.get(), &IModelService::paginationChanged,
            this, &ModelService::paginationChanged);
    connect(m_backend.get(), &IModelService::errorMessageChanged,
            this, &ModelService::errorMessageChanged);
    connect(m_backend.get(), &IModelService::modelsFetched,
            this, &ModelService::modelsFetched);
}

QJsonArray ModelService::models() const
{
    return m_backend ? m_backend->models() : QJsonArray{};
}

QJsonArray ModelService::categories() const
{
    return m_backend ? m_backend->categories() : QJsonArray{};
}

QJsonArray ModelService::searchSuggestions() const
{
    return m_backend ? m_backend->searchSuggestions() : QJsonArray{};
}

QJsonArray ModelService::hotSearches() const
{
    return m_backend ? m_backend->hotSearches() : QJsonArray{};
}

QJsonArray ModelService::hotPrints() const
{
    return m_backend ? m_backend->hotPrints() : QJsonArray{};
}

bool ModelService::loading() const
{
    return m_backend && m_backend->loading();
}

bool ModelService::hasMore() const
{
    return m_backend && m_backend->hasMore();
}

int ModelService::totalCount() const
{
    return m_backend ? m_backend->totalCount() : 0;
}

QString ModelService::errorMessage() const
{
    return m_backend ? m_backend->errorMessage() : QString{};
}

void ModelService::fetchModels(const QString& category, const QString& searchQuery)
{
    if (m_backend) {
        m_backend->fetchModels(category, searchQuery);
    }
}

void ModelService::fetchDiscovery()
{
    if (m_backend) {
        m_backend->fetchDiscovery();
    }
}

void ModelService::fetchCategories()
{
    if (m_backend) {
        m_backend->fetchCategories();
    }
}

void ModelService::loadMoreModels()
{
    if (m_backend) {
        m_backend->loadMoreModels();
    }
}

QJsonObject ModelService::getModelById(const QString& id) const
{
    return m_backend ? m_backend->getModelById(id) : QJsonObject{};
}

bool ModelService::isPrintableLicense(const QString& license) const
{
    static const QStringList allowedLicenses{
        QStringLiteral("CC0"), QStringLiteral("CC BY"), QStringLiteral("Public Domain"),
        QStringLiteral("MIT"), QStringLiteral("Apache"), QStringLiteral("GPL"),
        QStringLiteral("BSD")
    };
    return std::any_of(allowedLicenses.cbegin(), allowedLicenses.cend(),
                       [&license](const QString& allowed) {
        return license.contains(allowed, Qt::CaseInsensitive);
    });
}

QJsonObject ModelService::getLicenseInfo(const QString& license) const
{
    const bool printable = isPrintableLicense(license);
    return QJsonObject{
        {QStringLiteral("type"), license},
        {QStringLiteral("printSafe"), printable},
        {QStringLiteral("commercialUse"), printable && !license.contains(
             QStringLiteral("NC"), Qt::CaseInsensitive)},
        {QStringLiteral("modification"), printable && !license.contains(
             QStringLiteral("ND"), Qt::CaseInsensitive)},
        {QStringLiteral("attributionRequired"), !license.contains(
             QStringLiteral("CC0"), Qt::CaseInsensitive)}
    };
}

bool ModelService::isCommercialUseAllowed(const QString& license) const
{
    return getLicenseInfo(license).value(QStringLiteral("commercialUse")).toBool();
}

bool ModelService::isModificationAllowed(const QString& license) const
{
    return getLicenseInfo(license).value(QStringLiteral("modification")).toBool();
}

QJsonArray ModelService::getModelsBySource(const QString& source) const
{
    QJsonArray filtered;
    for (const QJsonValue& value : models()) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("source")).toString().compare(
                source, Qt::CaseInsensitive) == 0) {
            filtered.append(model);
        }
    }
    return filtered;
}

QJsonArray ModelService::getPrintReadyModels() const
{
    QJsonArray filtered;
    for (const QJsonValue& value : models()) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("printReady")).toBool()) {
            filtered.append(model);
        }
    }
    return filtered;
}

REGISTER_SERVICES_QML_SINGLETON_CUSTOM(
    ModelService, "ModelService", &ModelService::create)
