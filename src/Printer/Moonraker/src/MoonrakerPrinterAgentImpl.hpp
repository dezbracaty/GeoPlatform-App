#pragma once

#include "Printer/IPrinterAgent.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QFile>
#include <QTemporaryFile>
#include <QDateTime>
#include <memory>

namespace GPlatform::Printer {

// Owner-thread-only HTTP adapter. Credentials and endpoint are intentionally never persisted.
class MoonrakerPrinterAgentImpl : public QObject, public PrinterCore::IPrinterAgent {
public:
    explicit MoonrakerPrinterAgentImpl(QUrl endpoint, QByteArray apiKey = {}, int timeoutMs = 15000,
                                  int pollMs = 2000, int uploadTimeoutMs = 600000);
    ~MoonrakerPrinterAgentImpl() override;
    static bool validEndpoint(const QUrl& endpoint);
    void setEventSink(EventSink sink) override { m_sink = std::move(sink); }
    void initialize() override;
    void shutdown() override;
    void scan() override;
    void refreshDevices() override;
    void login(const std::string&, const std::string&) override;
    void logout() override;
    void selectDevice(const PrinterCore::Device& device) override;
    void provideDeviceAccess(const std::string& credential) override;
    void releaseDevice() override;
    std::vector<std::string> supportedPrintFormats() const override;
    PrinterCore::Capabilities capabilities() const override {
        auto caps = m_caps;
        caps.cancelSubmission = caps.automaticStatusRecovery = true;
        return caps;
    }
    void submitJob(const PrinterCore::PrintJob& job) override;
    bool cancelSubmission(uint64_t requestId) override;
    void acknowledgeDeviceControlOutcome() override { if (!m_busy) m_uncertain = false; }
    bool refreshStatus() override;
    void controlJob(const std::string& action, uint64_t requestId) override;
    void setCameraStreamEnabled(bool enabled) override;
    void controlDevice(const PrinterCore::DeviceCommand& command) override;
private:
    struct Response {
        bool ok{false};
        bool unknown{false};
        bool cancelled{false};
        int code{0};
        QJsonValue result;
        QString error;
    };
    using Completion = std::function<void(const Response&)>;
    QNetworkRequest request(const QString& path) const;
    void json(const QString& path, const QJsonObject& body, bool post, bool mutating, Completion done, uint64_t requestId = 0);
    void watch(QNetworkReply* reply, bool mutating, Completion done, int timeoutMs = 0, bool allowBareUpload = false, uint64_t requestId = 0);
    void publish(PrinterCore::Event event);
    void connectionFailed(const QString& error, bool access = false);
    void handshake();
    void publishConfiguredDevice(bool online, const QString& error = {});
    void queryStatus(bool initial = false);
    void updateCapabilities();
    void rejectCommand(const std::string& command, uint64_t id, const QString& error);
    void finishCommand(const std::string& command, uint64_t id, const Response& response,
                       const QString& fileName = {});
    void snapshotChunk(PrinterCore::PrintJob job, std::shared_ptr<QFile> input,
                       std::shared_ptr<QTemporaryFile> snapshot, uint64_t generation,
                       qint64 size, QDateTime modified);
    void upload(PrinterCore::PrintJob job, std::shared_ptr<QTemporaryFile> snapshot);
    QUrl m_endpoint;
    QByteArray m_apiKey;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply*> m_replies;
    QTimer m_poll;
    EventSink m_sink;
    PrinterCore::Device m_device;
    PrinterCore::Capabilities m_caps{false, false, false, false, false, false, false, false, false};
    QSet<QString> m_objects;
    uint64_t m_generation{0};
    bool m_connected{false};
    bool m_fileManager{false};
    bool m_queryPending{false};
    bool m_refreshPending{false};
    bool m_busy{false};
    uint64_t m_activeRequestId{0};
    bool m_statusFresh{false};
    bool m_handshakePending{false};
    bool m_startDispatched{false};
    uint64_t m_querySerial{0};
    uint64_t m_statusFence{0};
    QPointer<QNetworkReply> m_activeReply;
    std::string m_activeCommand;
    QString m_activeFileName;
    QString m_currentFileName;
    bool m_uncertain{false};
    PrinterCore::Status m_lastStatus;
    QString m_homedAxes;
    QString m_activeExtruder{QStringLiteral("extruder")};
    QString m_state;
    int m_timeoutMs;
    int m_uploadTimeoutMs;
};
} // namespace GPlatform::Printer
