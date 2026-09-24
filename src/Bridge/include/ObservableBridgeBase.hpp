#pragma once

#include "BridgeBase.hpp"
#include <DocumentManager.hpp>
#include <QPointer>
#include <unordered_set>

namespace bridge {

/**
 * @brief Base class for Bridge classes that need to observe DocumentManager changes
 *
 * Extends BridgeBase with DocumentManager listening capabilities:
 * - Automatic listener registration/unregistration
 * - ID-based filtering of change notifications
 * - Thread-safe operation
 * - Derived classes override handleDatabaseChange() to respond to changes
 */
class ObservableBridgeBase : public BridgeBase {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ObservableBridgeBase is abstract")

public:
    explicit ObservableBridgeBase(QObject* parent = nullptr);
    ~ObservableBridgeBase() override;

protected:
    // Override from BridgeBase
    void initialize() override;
    void cleanup() override;

    /**
     * @brief Add an ID to listen for changes
     */
    void listenToId(const DBInstanceID& id);

    /**
     * @brief Remove an ID from listening
     */
    void stopListeningToId(const DBInstanceID& id);

    /**
     * @brief Clear all listening IDs
     */
    void clearListeningIds();

    /**
     * @brief Check if listening to a specific ID
     */
    bool isListeningToId(const DBInstanceID& id) const;

    /**
     * @brief Get all IDs being listened to
     */
    std::unordered_set<DBInstanceID> getListeningIds() const;

    /**
     * @brief Called when a database change notification is received
     * Override in derived classes to handle specific changes
     */
    virtual void handleDatabaseChange(const DocumentManager::ChangeNotification& notification) = 0;

    /**
     * @brief Process a change notification
     * Called by the listener callback
     */
    void processChangeNotification(const DocumentManager::ChangeNotification& notification);

    /**
     * @brief Override to receive all notifications regardless of ID
     * Default returns false - only listen to specific IDs
     * Return true to receive all DocumentManager notifications
     */
    virtual bool shouldReceiveAllNotifications() const { return false; }

private:
    void setupListener();
    void removeListener();

    DocumentManager::ListenerID m_listenerId{0};
    std::unordered_set<DBInstanceID> m_listeningToIds;
    mutable std::mutex m_listeningMutex;
};

} // namespace bridge