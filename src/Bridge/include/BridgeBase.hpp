#pragma once

#include <QObject>
#include <QQmlEngine>
#include <memory>

namespace bridge {

/**
 * @brief Base class for all Bridge classes
 *
 * Provides common functionality for QML-exposed Bridge objects:
 * - QObject inheritance for Qt meta-object system
 * - QML registration support
 * - Basic lifecycle management
 * - Singleton pattern support (optional)
 *
 * This base class does NOT provide DocumentManager listening.
 * Use ObservableBridgeBase if you need to observe database changes.
 */
class BridgeBase : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("BridgeBase is abstract")

public:
    explicit BridgeBase(QObject* parent = nullptr);
    ~BridgeBase() override;

    // Disable copy and move
    BridgeBase(const BridgeBase&) = delete;
    BridgeBase& operator=(const BridgeBase&) = delete;
    BridgeBase(BridgeBase&&) = delete;
    BridgeBase& operator=(BridgeBase&&) = delete;

protected:
    /**
     * @brief Called when the bridge is initialized
     * Override in derived classes for custom initialization
     */
    virtual void initialize();

    /**
     * @brief Called when the bridge is being destroyed
     * Override in derived classes for custom cleanup
     */
    virtual void cleanup();

signals:
    /**
     * @brief Emitted when the bridge has been initialized
     */
    void initialized();

    /**
     * @brief Emitted when the bridge is about to be destroyed
     */
    void aboutToDestroy();

private:
    bool m_initialized = false;
};

} // namespace bridge