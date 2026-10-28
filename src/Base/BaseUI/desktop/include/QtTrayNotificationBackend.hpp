#pragma once
#include <Notifications/IDesktopNotificationBackend.hpp>
#include <QIcon>
#include <memory>

class QSystemTrayIcon;

namespace GPlatform::Notifications {
class QtTrayNotificationBackend final : public IDesktopNotificationBackend {
public:
    explicit QtTrayNotificationBackend(QIcon icon);
    ~QtTrayNotificationBackend() override;
    void start() override;
    void stop() override;
    bool isAvailable() const override;
    bool send(const Notification& notification) override;
private:
    QIcon m_icon;
    std::unique_ptr<QSystemTrayIcon> m_tray;
};
}
