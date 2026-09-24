#pragma once

#include "PrinterDeviceModel.hpp"
#include "PrinterCoreTypes.hpp"

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <qqmlregistration.h>

namespace GPlatform::PrinterCore {
class IPrinterAgent;
struct Event;
}

namespace GPlatform::Printer {

class PrinterQtFacade final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool agentAvailable READ agentAvailable NOTIFY agentChanged)
    Q_PROPERTY(QString agentVersion READ agentVersion NOTIFY agentChanged)
    Q_PROPERTY(QString agentError READ agentError NOTIFY agentChanged)
    Q_PROPERTY(DiscoveryState discoveryState READ discoveryState NOTIFY discoveryChanged)
    Q_PROPERTY(QAbstractItemModel* devices READ devices CONSTANT)
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY discoveryChanged)
    Q_PROPERTY(AuthState authState READ authState NOTIFY authChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY authChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY authChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authChanged)
    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDeviceName READ selectedDeviceName NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDeviceModel READ selectedDeviceModel NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDeviceIp READ selectedDeviceIp NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString selectedDeviceState READ selectedDeviceState NOTIFY selectedDeviceChanged)
    Q_PROPERTY(bool selectedDeviceOnline READ selectedDeviceOnline NOTIFY selectedDeviceChanged)
    Q_PROPERTY(DeviceRoute selectedDeviceRoute READ selectedDeviceRoute NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString connectionError READ connectionError NOTIFY connectionChanged)
    Q_PROPERTY(bool deviceAccessRequired READ deviceAccessRequired NOTIFY connectionChanged)
    Q_PROPERTY(QStringList supportedPrintFormats READ supportedPrintFormats NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY agentChanged)
    Q_PROPERTY(bool canConfigureBackend READ canConfigureBackend NOTIFY connectionChanged)
    Q_PROPERTY(bool supportsDiscovery READ supportsDiscovery NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsAccount READ supportsAccount NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsUpload READ supportsUpload NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsStart READ supportsStart NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsPauseResume READ supportsPauseResume NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsCancel READ supportsCancel NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsVendorOptions READ supportsVendorOptions NOTIFY capabilitiesChanged)
    Q_PROPERTY(TransferState transferState READ transferState NOTIFY jobChanged)
    Q_PROPERTY(bool transferBusy READ transferBusy NOTIFY jobChanged)
    Q_PROPERTY(JobState jobState READ jobState NOTIFY jobChanged)
    Q_PROPERTY(bool printing READ printing NOTIFY jobChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY jobChanged)
    Q_PROPERTY(bool jobControllable READ jobControllable NOTIFY jobChanged)
    Q_PROPERTY(bool commandOutcomeUnknown READ commandOutcomeUnknown NOTIFY jobChanged)
    Q_PROPERTY(double transferProgress READ transferProgress NOTIFY jobChanged)
    Q_PROPERTY(double printProgress READ printProgress NOTIFY statusChanged)
    Q_PROPERTY(double nozzleTemperature READ nozzleTemperature NOTIFY statusChanged)
    Q_PROPERTY(double targetNozzleTemperature READ targetNozzleTemperature NOTIFY statusChanged)
    Q_PROPERTY(double bedTemperature READ bedTemperature NOTIFY statusChanged)
    Q_PROPERTY(double targetBedTemperature READ targetBedTemperature NOTIFY statusChanged)
    Q_PROPERTY(QString remainingTime READ remainingTime NOTIFY statusChanged)
    Q_PROPERTY(QString jobFileName READ jobFileName NOTIFY jobChanged)
    Q_PROPERTY(QString printerStateText READ printerStateText NOTIFY statusChanged)
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY statusChanged)
    Q_PROPERTY(QString cameraStreamUrl READ cameraStreamUrl NOTIFY statusChanged)
    Q_PROPERTY(CameraSessionState cameraSessionState READ cameraSessionState NOTIFY cameraChanged)
    Q_PROPERTY(QString cameraSessionError READ cameraSessionError NOTIFY cameraChanged)
    Q_PROPERTY(bool cameraSessionActive READ cameraSessionActive NOTIFY cameraChanged)
    Q_PROPERTY(QString jobError READ jobError NOTIFY jobChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    enum DiscoveryState {
        DiscoveryIdle,
        DiscoveryScanning,
        DiscoveryReady,
        DiscoveryEmpty,
        DiscoveryError,
        DiscoveryWarning,
    };
    Q_ENUM(DiscoveryState)
    enum AuthState { SignedOut, SigningIn, SignedIn, AuthError };
    Q_ENUM(AuthState)
    enum ConnectionState { Disconnected, Connecting, Connected, ConnectionError };
    Q_ENUM(ConnectionState)
    enum DeviceRoute { UnknownRoute, LocalNetwork, Cloud };
    Q_ENUM(DeviceRoute)
    enum JobState {
        JobIdle, Printing, Paused, Stopping, Completed, JobError,
        Pausing, Resuming
    };
    Q_ENUM(JobState)
    enum TransferState { TransferIdle, TransferUploading, TransferUploaded, TransferStarting,
                         TransferUnknown, TransferFailed };
    Q_ENUM(TransferState)
    enum CameraSessionState {
        CameraIdle, CameraOpening, CameraLoading, CameraReady, CameraClosing, CameraError
    };
    Q_ENUM(CameraSessionState)

    static PrinterQtFacade* instance();
    static PrinterQtFacade* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    explicit PrinterQtFacade(std::unique_ptr<PrinterCore::IPrinterAgent> agent,
                             QObject* parent = nullptr);
    ~PrinterQtFacade() override;

    QString backendName() const { return m_backendName; }
    bool canConfigureBackend() const;
    bool supportsDiscovery() const { return m_capabilities.discovery; }
    bool supportsAccount() const { return m_capabilities.account; }
    bool supportsUpload() const { return m_capabilities.upload; }
    bool supportsStart() const { return m_capabilities.start; }
    bool supportsPauseResume() const { return m_capabilities.pauseResume; }
    bool supportsCancel() const { return m_capabilities.cancel; }
    bool supportsVendorOptions() const { return m_capabilities.vendorOptions; }
    bool agentAvailable() const { return m_agentAvailable; }
    QString agentVersion() const { return m_agentVersion; }
    QString agentError() const { return m_agentError; }
    DiscoveryState discoveryState() const { return m_discoveryState; }
    QAbstractItemModel* devices() { return &m_devices; }
    QString discoveryError() const { return m_discoveryError; }
    AuthState authState() const { return m_authState; }
    bool signedIn() const { return m_authState == SignedIn; }
    QString accountName() const { return m_accountName; }
    QString authError() const { return m_authError; }
    ConnectionState connectionState() const { return m_connectionState; }
    bool connected() const { return m_connectionState == Connected; }
    QString selectedDeviceId() const { return m_selectedDevice.deviceId; }
    QString selectedDeviceName() const {
        return m_status.name.isEmpty() ? m_selectedDevice.name : m_status.name;
    }
    QString selectedDeviceModel() const { return m_selectedDevice.model; }
    QString selectedDeviceIp() const { return m_selectedDevice.ip; }
    QString selectedDeviceState() const { return m_selectedDevice.state; }
    bool selectedDeviceOnline() const { return m_selectedDevice.online; }
    DeviceRoute selectedDeviceRoute() const {
        return static_cast<DeviceRoute>(m_selectedDevice.route);
    }
    QString connectionError() const { return m_connectionError; }
    bool deviceAccessRequired() const { return m_deviceAccessRequired; }
    QStringList supportedPrintFormats() const;
    TransferState transferState() const { return m_transferState; }
    bool transferBusy() const {
        return m_transferState == TransferUploading || m_transferState == TransferStarting
            || m_transferState == TransferUnknown || m_controlOutcomeUnknown;
    }
    JobState jobState() const { return m_jobState; }
    bool printing() const {
        return connected()
            && (m_jobState == Printing || m_jobState == Paused
                || m_jobState == Stopping || m_jobState == Pausing
                || m_jobState == Resuming);
    }
    bool paused() const { return m_jobState == Paused; }
    bool commandOutcomeUnknown() const { return m_controlOutcomeUnknown; }
    bool jobControllable() const {
        return connected() && !m_controlOutcomeUnknown
            && (!m_capabilities.requiresJobId || !m_status.jobId.isEmpty())
            && (m_capabilities.pauseResume || m_capabilities.cancel)
            && (m_jobState == Printing || m_jobState == Paused);
    }
    double transferProgress() const { return m_transferProgress; }
    double printProgress() const { return m_status.progress; }
    double nozzleTemperature() const { return m_status.nozzleTemperature; }
    double targetNozzleTemperature() const { return m_status.targetNozzleTemperature; }
    double bedTemperature() const { return m_status.bedTemperature; }
    double targetBedTemperature() const { return m_status.targetBedTemperature; }
    QString remainingTime() const;
    QString jobFileName() const { return m_transferState != TransferIdle
        ? m_submittedFileName : m_status.fileName; }
    QString printerStateText() const { return m_status.state; }
    bool cameraAvailable() const { return m_capabilities.camera && m_status.cameraAvailable; }
    QString cameraStreamUrl() const { return m_status.cameraStreamUrl; }
    CameraSessionState cameraSessionState() const { return m_cameraSessionState; }
    QString cameraSessionError() const { return m_cameraSessionError; }
    bool cameraSessionActive() const {
        return m_cameraSessionState != CameraIdle && m_cameraSessionState != CameraError;
    }
    QString jobError() const { return m_jobError; }

    Q_INVOKABLE bool connectMoonraker(const QString& endpoint, const QString& apiKey);
    Q_INVOKABLE bool useFlashforge();
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void login(const QString& username, const QString& password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void selectDevice(const QString& deviceId);
    Q_INVOKABLE void provideDeviceAccess(const QString& credential);
    Q_INVOKABLE void submitJob(const QString& filePath,
                               const QString& destinationName,
                               const QString& formatId,
                               bool printNow,
                               bool levelingBeforePrint,
                               bool flowCalibration,
                               bool firstLayerInspection,
                               bool timeLapseVideo,
                               bool useMaterialStation);
    Q_INVOKABLE void pausePrint();
    Q_INVOKABLE void resumePrint();
    Q_INVOKABLE void cancelPrint();
    Q_INVOKABLE void dismissJobStatus();
    Q_INVOKABLE void startCameraStream();
    Q_INVOKABLE void stopCameraStream();

signals:
    void agentChanged();
    void capabilitiesChanged();
    void discoveryChanged();
    void authChanged();
    void selectedDeviceChanged();
    void connectionChanged();
    void statusChanged();
    void jobChanged();
    void cameraChanged();

private:
    explicit PrinterQtFacade(QObject* parent = nullptr);
    void initializeAgent();
    void replaceAgent(std::unique_ptr<PrinterCore::IPrinterAgent> agent, const QString& backend);
    void refreshCapabilities();
    void failTransfer(const QString& error);
    void markTransferUnknown(const QString& error);
    void handleAgentEvent(const PrinterCore::Event& event);
    void applyStatus(const PrinterStatus& status);
    void setJobStateFromPrinter(const QString& state);
    void initializeCameraSession();
    void beginCameraOpen();
    void scheduleCameraRetry(const QString& error);
    void resetCameraSession(bool requestClose);

    uint64_t m_adapterGeneration{0};
    QString m_backendName{QStringLiteral("Flashforge")};
    PrinterCore::Capabilities m_capabilities;
    bool m_controlOutcomeUnknown{false};
    bool m_selectConfiguredMoonraker{false};
    std::shared_ptr<QObject> m_eventContext;
    PrinterDeviceModel m_devices;
    std::unique_ptr<PrinterCore::IPrinterAgent> m_agent;
    PrinterDevice m_selectedDevice;
    PrinterStatus m_status;
    bool m_agentAvailable{false};
    QString m_agentVersion;
    QString m_agentError;
    DiscoveryState m_discoveryState{DiscoveryIdle};
    QString m_discoveryError;
    AuthState m_authState{SignedOut};
    QString m_accountName;
    QString m_authError;
    ConnectionState m_connectionState{Disconnected};
    QString m_connectionError;
    bool m_deviceAccessRequired{false};
    JobState m_jobState{JobIdle};
    TransferState m_transferState{TransferIdle};
    uint64_t m_sessionId{0};
    uint64_t m_nextRequestId{0};
    uint64_t m_transferRequestId{0};
    uint64_t m_controlRequestId{0};
    QString m_controlCommand;
    QString m_submittedFileName;
    bool m_printNow{false};
    QTimer m_startConfirmationTimer;
    double m_transferProgress{0.0};
    QString m_jobError;
    CameraSessionState m_cameraSessionState{CameraIdle};
    QString m_cameraSessionError;
    QTimer m_cameraRetryTimer;
    QTimer m_cameraOpenTimeout;
    int m_cameraRetryAttempt{0};
};

} // namespace GPlatform::Printer
