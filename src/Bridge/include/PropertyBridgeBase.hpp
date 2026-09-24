#pragma once

#include <QObject>
#include <QQmlEngine>
#include <memory>
#include <functional>
#include <any>
#include <DocumentManager.hpp>
#include <Transaction.hpp>
#include <TransactionManager.hpp>

/**
 * @brief Base class for property bridges between QML and C++ with transaction support
 *
 * This class provides a reusable framework for creating property bridges that:
 * - Support Qt6 property binding with change notifications
 * - Integrate with the transaction system for undo/redo
 * - Listen to DocumentManager for property changes
 * - Can be exposed as QML singletons
 *
 * Usage:
 * 1. Inherit from this class
 * 2. Add Q_PROPERTY declarations with BINDABLE
 * 3. Implement onPropertyChanged to handle DB property updates
 * 4. Register as QML singleton
 */
class PropertyBridgeBase : public QObject {
    Q_OBJECT

public:
    explicit PropertyBridgeBase(QObject* parent = nullptr);
    virtual ~PropertyBridgeBase();

protected:
    /**
     * @brief Called when a property changes in the database
     * Derived classes should implement this to update their bindable properties
     * Note: newValue is not provided - derived classes should fetch the current value from the DB
     */
    virtual void onPropertyChanged(const DBInstanceID& id,
                                   const std::string& propertyName,
                                   const std::any& newValue) = 0;

    /**
     * @brief Called when an object is created in the database
     * Derived classes can override this to handle object creation
     */
    virtual void onObjectCreated(const DBInstanceID& id) {}

    /**
     * @brief Called when an object is deleted from the database
     * Derived classes can override this to handle object deletion
     */
    virtual void onObjectDeleted(const DBInstanceID& id) {}

    /**
     * @brief Start listening to changes for a specific DB instance
     * @param id The database instance ID to monitor
     */
    void startListeningTo(const DBInstanceID& id);

    /**
     * @brief Stop listening to changes for a specific DB instance
     * @param id The database instance ID to stop monitoring
     */
    void stopListeningTo(const DBInstanceID& id);

    /**
     * @brief Clear all listeners
     */
    void clearListeners();

    /**
     * @brief Execute a property change within a transaction
     * @param transactionName Name for the undo/redo operation
     * @param action The action to execute within the transaction
     */
    void executeInTransaction(const QString& transactionName,
                              std::function<void()> action);

    /**
     * @brief Helper to create QML singleton factory function
     * Usage: qmlRegisterSingletonType<DerivedClass>("Module", 1, 0, "Name",
     *                                                createSingletonInstance<DerivedClass>);
     */
    template<typename T>
    static QObject* createSingletonInstance(QQmlEngine* engine, QJSEngine* scriptEngine) {
        Q_UNUSED(engine)
        Q_UNUSED(scriptEngine)
        return new T();
    }

private:
    void setupListeners();
    void removeListeners();

    using ListenerID = DocumentManager::ListenerID;
    ListenerID m_listenerId = 0;
    std::set<DBInstanceID> m_listeningToIds;
};