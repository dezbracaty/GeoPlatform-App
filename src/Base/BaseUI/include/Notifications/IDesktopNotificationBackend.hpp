#pragma once
#include "NotificationTypes.hpp"

namespace GPlatform::Notifications {
// GUI-thread synchronous submission port. A Qt submission is not a delivery receipt.
class IDesktopNotificationBackend : public QObject {
    Q_OBJECT
public:
    explicit IDesktopNotificationBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~IDesktopNotificationBackend() override = default;
    virtual void start() = 0;
    virtual bool isAvailable() const = 0;
    // True means submitted to the backend, not confirmed visible by the OS.
    virtual bool send(const Notification& notification) = 0;
    virtual void stop() = 0;
signals:
    void activationRequested();
};
}
