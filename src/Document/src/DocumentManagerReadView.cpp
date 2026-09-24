#include "DocumentManagerReadView.hpp"

#include "DocumentManager.hpp"

#include <utility>

DocumentManagerReadView::DocumentManagerReadView(
    DocumentManager& document) noexcept
    : m_document(document) {
}

std::shared_ptr<const AutoRegisterDB> DocumentManagerReadView::getDB(
    const DBInstanceID& id) const {
    return m_document.getDBInstance(id);
}

std::vector<std::shared_ptr<const AutoRegisterDB>>
DocumentManagerReadView::getDBs(TypeID type) const {
    const auto mutableInstances = m_document.getDBInstancesByType(type);
    std::vector<std::shared_ptr<const AutoRegisterDB>> instances;
    instances.reserve(mutableInstances.size());
    for (const auto& instance : mutableInstances) {
        instances.emplace_back(instance);
    }
    return instances;
}

TypeID DocumentManagerReadView::getDBType(const DBInstanceID& id) const {
    return m_document.getDBInstanceType(id);
}

DBInstanceID DocumentManagerReadView::getOwner(
    const DBInstanceID& childId) const {
    return m_document.getOwner(childId);
}

std::vector<DBInstanceID> DocumentManagerReadView::getOwnedChildren(
    const DBInstanceID& ownerId,
    const std::string& relationName) const {
    return m_document.getOwnedChildren(ownerId, relationName);
}

IDocumentReadView::SubscriptionId DocumentManagerReadView::subscribe(
    ChangeCallback callback) {
    if (!callback) {
        return 0;
    }

    return m_document.addChangeListener(
        [callback = std::move(callback)](
            const DocumentManager::ChangeNotification& notification) {
            callback(DocumentChange{
                notification.id,
                notification.dbType,
                notification.changeType,
                notification.propertyName,
                notification.ownerId});
        });
}

void DocumentManagerReadView::unsubscribe(SubscriptionId id) {
    if (id != 0) {
        m_document.removeChangeListener(id);
    }
}
