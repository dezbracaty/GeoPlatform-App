#pragma once
#include "ExternalOpenRequest.hpp"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <functional>
#include <memory>
class QLocalServer;
class QLockFile;

// Local desktop handoff, independent of Journal's transport and instances.
class LocalInstanceEndpoint final : public QObject {
    Q_OBJECT
public:
    enum class StartResult { Primary, Forwarded, Failed };
    using Receiver = std::function<bool(const QVector<ExternalOpenRequest>&, QString&)>;
    explicit LocalInstanceEndpoint(QObject* parent = nullptr);
    ~LocalInstanceEndpoint() override;
    StartResult startOrForward(const QVector<ExternalOpenRequest>& requests,
                               Receiver receiver, QString& error);
    void markFinished(const QString& requestId);
    void stop();
signals:
    void activationRequested();
private:
    struct RequestRecord { QByteArray fingerprint; qint64 expires{0}; };
    struct BatchRecord { QByteArray fingerprint; QStringList ids; QJsonObject receipt; qint64 expires{0}; };
    void acceptConnections();
    QJsonObject receive(const QJsonObject& message);
    void prune();
    std::unique_ptr<QLocalServer> m_server;
    std::unique_ptr<QLockFile> m_lock;
    Receiver m_receiver;
    QHash<QString, RequestRecord> m_requests;
    QHash<QString, BatchRecord> m_batches;
    QString m_endpoint;
    bool m_stopped{false};
    int m_connections{0};
};
