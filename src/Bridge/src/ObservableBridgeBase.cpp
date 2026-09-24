#include <ObservableBridgeBase.hpp>
#include "Foundation/Log.h"

namespace bridge {

ObservableBridgeBase::ObservableBridgeBase(QObject* parent)
    : BridgeBase(parent) {
    LOG_DEBUG("ObservableBridgeBase constructed");
}

ObservableBridgeBase::~ObservableBridgeBase() {
    LOG_DEBUG("ObservableBridgeBase destructor called");
    // Cleanup will be called by BridgeBase destructor
}

void ObservableBridgeBase::initialize() {
    BridgeBase::initialize();
    setupListener();
    LOG_DEBUG("ObservableBridgeBase initialized with listener");
}

void ObservableBridgeBase::cleanup() {
    removeListener();
    clearListeningIds();
    BridgeBase::cleanup();
    LOG_DEBUG("ObservableBridgeBase cleaned up");
}

void ObservableBridgeBase::listenToId(const DBInstanceID& id) {
    std::lock_guard<std::mutex> lock(m_listeningMutex);
    m_listeningToIds.insert(id);
    LOG_DEBUG("Started listening to ID: {}", id.getValue());
}

void ObservableBridgeBase::stopListeningToId(const DBInstanceID& id) {
    std::lock_guard<std::mutex> lock(m_listeningMutex);
    m_listeningToIds.erase(id);
    LOG_DEBUG("Stopped listening to ID: {}", id.getValue());
}

void ObservableBridgeBase::clearListeningIds() {
    std::lock_guard<std::mutex> lock(m_listeningMutex);
    m_listeningToIds.clear();
    LOG_DEBUG("Cleared all listening IDs");
}

bool ObservableBridgeBase::isListeningToId(const DBInstanceID& id) const {
    std::lock_guard<std::mutex> lock(m_listeningMutex);
    return m_listeningToIds.find(id) != m_listeningToIds.end();
}

std::unordered_set<DBInstanceID> ObservableBridgeBase::getListeningIds() const {
    std::lock_guard<std::mutex> lock(m_listeningMutex);
    return m_listeningToIds;
}

void ObservableBridgeBase::processChangeNotification(const DocumentManager::ChangeNotification& notification) {
    // Check if this bridge wants all notifications or is listening to this specific ID
    if (shouldReceiveAllNotifications() || isListeningToId(notification.id)) {
        handleDatabaseChange(notification);
    }
}

void ObservableBridgeBase::setupListener() {
    auto* docManager = DocumentManager::instance();

    // 使用QPointer确保对象生命周期安全
    // QPointer会在QObject析构时自动置空，避免悬空指针访问
    QPointer<ObservableBridgeBase> selfPtr(this);

    m_listenerId = docManager->addChangeListener(
        [selfPtr](const DocumentManager::ChangeNotification& notification) {
            // QPointer会在对象析构时自动置空，这里检查对象是否仍然存在
            if (selfPtr) {
                selfPtr->processChangeNotification(notification);
            }
            // 如果selfPtr为空，说明对象已析构，lambda安全退出
        });

    LOG_DEBUG("ObservableBridgeBase listener setup with ID: {}", m_listenerId);
}

void ObservableBridgeBase::removeListener() {
    if (m_listenerId != 0) {
        auto* docManager = DocumentManager::instance();
        docManager->removeChangeListener(m_listenerId);
        LOG_DEBUG("ObservableBridgeBase listener removed, ID was: {}", m_listenerId);
        m_listenerId = 0;
    }
}

} // namespace bridge