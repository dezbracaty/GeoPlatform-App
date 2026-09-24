#include "../include/PropertyBridgeBase.hpp"
#include "Foundation/Log.h"

PropertyBridgeBase::PropertyBridgeBase(QObject* parent)
    : QObject(parent) {
    setupListeners();
}

PropertyBridgeBase::~PropertyBridgeBase() {
    removeListeners();
}

void PropertyBridgeBase::setupListeners() {
    auto& docManager = *DocumentManager::instance();

    // Listen for all changes
    m_listenerId = docManager.addChangeListener(
        [this](const DocumentManager::ChangeNotification& notification) {
            if (notification.changeType == ChangeType::OBJECT_CREATED) {
                onObjectCreated(notification.id);
                return;
            }

            // Only handle existing-object changes if we're listening to this ID.
            if (m_listeningToIds.find(notification.id) == m_listeningToIds.end()) {
                return;
            }

            switch (notification.changeType) {
                case ChangeType::PROPERTY_CHANGED:
                    // The derived class should fetch the current value from the DB.
                    onPropertyChanged(notification.id, notification.propertyName, std::any{});
                    break;
                case ChangeType::OBJECT_DELETED:
                    onObjectDeleted(notification.id);
                    m_listeningToIds.erase(notification.id);
                    break;
                default:
                    break;
            }
        });

    // LOG_INFO("PropertyBridgeBase listeners setup completed");
}

void PropertyBridgeBase::removeListeners() {
    auto& docManager = *DocumentManager::instance();

    if (m_listenerId != 0) {
        docManager.removeChangeListener(m_listenerId);
        m_listenerId = 0;
    }

    m_listeningToIds.clear();
}

void PropertyBridgeBase::startListeningTo(const DBInstanceID& id) {
    m_listeningToIds.insert(id);
    // LOG_INFO("Started listening to DB instance: {}", id.getValue());
}

void PropertyBridgeBase::stopListeningTo(const DBInstanceID& id) {
    m_listeningToIds.erase(id);
    // LOG_INFO("Stopped listening to DB instance: {}", id.getValue());
}

void PropertyBridgeBase::clearListeners() {
    m_listeningToIds.clear();
    // LOG_INFO("Cleared all DB instance listeners");
}

void PropertyBridgeBase::executeInTransaction(const QString& transactionName,
                                              std::function<void()> action) {
    TransactionGuard guard(transactionName.toStdString());
    action();
}
