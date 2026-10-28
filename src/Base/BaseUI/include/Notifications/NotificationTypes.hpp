#pragma once
#include <QObject>
#include <QString>

namespace GPlatform::Notifications {
Q_NAMESPACE
enum class Severity { Info, Success, Warning, Error };
Q_ENUM_NS(Severity)
// A one-shot value. It carries no task identity or scheduling state.
struct Notification {
    QString title;
    QString body;
    Severity severity = Severity::Info;
};
}
