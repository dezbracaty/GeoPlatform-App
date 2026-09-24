#include "Printer/PrinterQtFacade.hpp"
#include "Printer/PrinterRegistration.hpp"

#include "Foundation/Log.h"
#include "Printer/IPrinterAgent.hpp"
#include "FixturePrinterAgent.hpp"
#include "MoonrakerEndpointInput.hpp"
#include "Flashforge/FlashforgePrinterAgent.hpp"
#include "Moonraker/MoonrakerPrinterAgent.hpp"

#include <QFileInfo>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <QUrl>
#include <stdexcept>
#include <utility>

namespace GPlatform::Printer {

namespace {

QString fromStd(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

PrinterDevice fromCore(const PrinterCore::Device& value) {
    PrinterDevice device;
    device.deviceId = fromStd(value.deviceId);
    device.name = fromStd(value.name);
    device.model = fromStd(value.model);
    device.state = fromStd(value.state);
    device.ip = fromStd(value.ip);
    device.online = value.online;
    device.route = static_cast<int>(value.route);
    return device;
}

PrinterCore::Device toCore(const PrinterDevice& value) {
    PrinterCore::Device device;
    device.deviceId = value.deviceId.toStdString();
    device.name = value.name.toStdString();
    device.model = value.model.toStdString();
    device.state = value.state.toStdString();
    device.ip = value.ip.toStdString();
    device.online = value.online;
    device.route = static_cast<PrinterCore::DeviceRoute>(value.route);
    return device;
}

PrinterStatus fromCore(const PrinterCore::Status& value) {
    PrinterStatus status;
    status.deviceId = fromStd(value.deviceId);
    status.name = fromStd(value.name);
    status.ip = fromStd(value.ip);
    status.state = fromStd(value.state);
    status.jobId = fromStd(value.jobId);
    status.fileName = fromStd(value.fileName);
    status.errorCode = fromStd(value.errorCode);
    status.cameraStreamUrl = fromStd(value.cameraStreamUrl);
    status.progress = value.progress;
    status.nozzleTemperature = value.nozzleTemperature;
    status.targetNozzleTemperature = value.targetNozzleTemperature;
    status.bedTemperature = value.bedTemperature;
    status.targetBedTemperature = value.targetBedTemperature;
    status.estimatedTimeSeconds = value.estimatedTimeSeconds;
    status.printDurationSeconds = value.printDurationSeconds;
    status.currentLayer = value.currentLayer;
    status.totalLayers = value.totalLayers;
    status.cameraAvailable = value.cameraAvailable;
    return status;
}

std::unique_ptr<PrinterCore::IPrinterAgent> createPrinterAgent() {
    const bool journalSession =
        qEnvironmentVariableIntValue("GPLATFORM_JOURNAL_SESSION") == 1;
    const QString fixture =
        qEnvironmentVariable("GPLATFORM_JOURNAL_PRINTER_FIXTURE").trimmed();
    if (journalSession && !fixture.isEmpty()) {
        return std::make_unique<FixturePrinterAgent>(fixture.toStdString());
    }
    return std::make_unique<FlashforgePrinterAgent>();
}

} // namespace

PrinterQtFacade* PrinterQtFacade::instance() {
    static auto* facade = new PrinterQtFacade();
    return facade;
}

PrinterQtFacade* PrinterQtFacade::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    auto* facade = instance();
    QJSEngine::setObjectOwnership(facade, QJSEngine::CppOwnership);
    return facade;
}

PrinterQtFacade::PrinterQtFacade(QObject* parent)
    : QObject(parent),
      m_devices(this),
      m_agent(createPrinterAgent()) {
    initializeCameraSession();
    connect(this, &PrinterQtFacade::jobChanged, this, &PrinterQtFacade::connectionChanged);
    initializeAgent();
}

PrinterQtFacade::PrinterQtFacade(std::unique_ptr<PrinterCore::IPrinterAgent> agent,
                                 QObject* parent)
    : QObject(parent),
      m_devices(this),
      m_agent(std::move(agent)) {
    if (!m_agent) {
        throw std::invalid_argument("PrinterQtFacade requires one printer agent");
    }
    initializeCameraSession();
    connect(this, &PrinterQtFacade::jobChanged, this, &PrinterQtFacade::connectionChanged);
    initializeAgent();
}

void PrinterQtFacade::initializeAgent() {
    // A retained relay outlives callbacks already copied by an agent. QPointer is
    // inspected only on the GUI thread, never dereferenced on a network thread.
    m_eventContext = std::shared_ptr<QObject>(new QObject(), [](QObject* object) {
        object->deleteLater();
    });
    const auto generation = ++m_adapterGeneration;
    const QPointer<PrinterQtFacade> receiver(this);
    m_agent->setEventSink([receiver, generation, relay = m_eventContext](const PrinterCore::Event& event) {
        auto deliver = [receiver, generation, event] {
            if (receiver && receiver->m_adapterGeneration == generation) receiver->handleAgentEvent(event);
        };
        if (QThread::currentThread() == relay->thread()) deliver();
        else QMetaObject::invokeMethod(relay.get(), std::move(deliver), Qt::QueuedConnection);
    });
    m_startConfirmationTimer.setParent(this);
    m_startConfirmationTimer.setObjectName(QStringLiteral("printStartConfirmationTimer"));
    m_startConfirmationTimer.setSingleShot(true);
    m_startConfirmationTimer.setInterval(30000);
    disconnect(&m_startConfirmationTimer, nullptr, this, nullptr);
    connect(&m_startConfirmationTimer, &QTimer::timeout, this, [this] {
        if (m_transferState == TransferStarting)
            markTransferUnknown(tr("The file was sent, but printing could not be confirmed. Check the printer before sending again."));
    });
    refreshCapabilities();
    m_agent->initialize();
}

bool PrinterQtFacade::canConfigureBackend() const {
    return !transferBusy() && !printing() && m_controlRequestId == 0
        && !(qEnvironmentVariableIntValue("GPLATFORM_JOURNAL_SESSION") == 1
             && !qEnvironmentVariable("GPLATFORM_JOURNAL_PRINTER_FIXTURE").trimmed().isEmpty());
}

void PrinterQtFacade::refreshCapabilities() {
    m_capabilities = m_agent->capabilities();
    emit capabilitiesChanged();
    emit jobChanged();
}

void PrinterQtFacade::replaceAgent(std::unique_ptr<PrinterCore::IPrinterAgent> agent, const QString& backend) {
    ++m_adapterGeneration; // Retire even copied non-device callbacks before shutting down.
    ++m_sessionId;
    m_agent->setEventSink({});
    resetCameraSession(true);
    m_agent->releaseDevice();
    m_agent->shutdown();
    m_agent = std::move(agent);
    m_backendName = backend;
    m_selectConfiguredMoonraker = false;
    m_devices.replaceDevices({});
    m_selectedDevice = {};
    m_status = {};
    m_agentAvailable = false;
    m_agentVersion.clear();
    m_agentError.clear();
    m_discoveryState = DiscoveryIdle;
    m_discoveryError.clear();
    m_authState = SignedOut;
    m_accountName.clear();
    m_authError.clear();
    m_connectionState = Disconnected;
    m_connectionError.clear();
    m_deviceAccessRequired = false;
    m_jobState = JobIdle;
    m_transferState = TransferIdle;
    m_transferProgress = 0.0;
    m_transferRequestId = m_controlRequestId = 0;
    m_controlOutcomeUnknown = false;
    m_submittedFileName.clear();
    m_jobError.clear();
    m_startConfirmationTimer.stop();
    initializeAgent();
    emit agentChanged();
    emit discoveryChanged();
    emit authChanged();
    emit selectedDeviceChanged();
    emit connectionChanged();
    emit statusChanged();
    emit jobChanged();
}

bool PrinterQtFacade::connectMoonraker(const QString& endpoint, const QString& apiKey) {
    if (!canConfigureBackend()) return false;
    MoonrakerConfig config{normalizeMoonrakerEndpointInput(endpoint).toStdString(),
                          apiKey.toStdString()};
    if (!MoonrakerPrinterAgent::validConfig(config)) {
        m_connectionError = tr("Enter an HTTP(S) Moonraker URL without credentials, query, or fragment, and a valid API key.");
        emit connectionChanged();
        return false;
    }
    replaceAgent(std::make_unique<MoonrakerPrinterAgent>(config), QStringLiteral("Moonraker"));
    m_selectConfiguredMoonraker = true;
    m_agent->scan(); // Enumerates only the explicitly supplied endpoint. No network scan.
    return true;
}

bool PrinterQtFacade::useFlashforge() {
    if (!canConfigureBackend()) return false;
    if (m_backendName == QStringLiteral("Flashforge")) return true;
    replaceAgent(std::make_unique<FlashforgePrinterAgent>(), QStringLiteral("Flashforge"));
    startDiscovery();
    return true;
}

QStringList PrinterQtFacade::supportedPrintFormats() const {
    QStringList formats;
    for (const auto& format : m_agent->supportedPrintFormats()) formats.append(fromStd(format));
    return formats;
}

void PrinterQtFacade::failTransfer(const QString& error) {
    m_startConfirmationTimer.stop();
    m_transferState = TransferFailed;
    m_transferRequestId = 0;
    m_jobError = error;
    emit jobChanged();
}

void PrinterQtFacade::markTransferUnknown(const QString& error) {
    m_startConfirmationTimer.stop();
    m_transferState = TransferUnknown;
    m_transferRequestId = 0;
    m_jobError = error;
    emit jobChanged();
}

PrinterQtFacade::~PrinterQtFacade() {
    m_startConfirmationTimer.stop();
    m_cameraRetryTimer.stop();
    m_cameraOpenTimeout.stop();
    if (cameraSessionActive() || m_cameraSessionState == CameraError) {
        m_agent->setCameraStreamEnabled(false);
    }
    m_agent->setEventSink({});
    m_agent->releaseDevice();
    m_agent->shutdown();
}

void PrinterQtFacade::handleAgentEvent(const PrinterCore::Event& event) {
    using PrinterCore::EventType;
    Q_ASSERT(QThread::currentThread() == thread());
    const bool deviceEvent = event.type == EventType::CapabilitiesChanged
        || event.type == EventType::ConnectionChanged
        || event.type == EventType::StatusChanged || event.type == EventType::TransferProgress
        || event.type == EventType::JobCommandFinished || event.type == EventType::CameraStreamCommandFinished;
    if (deviceEvent && (event.sessionId == 0 || event.sessionId != m_sessionId
        || fromStd(event.deviceId) != m_selectedDevice.deviceId)) return;
    QString error = fromStd(event.error);
    if (m_backendName == QStringLiteral("Moonraker") && !error.isEmpty()) {
        if (event.error == "Invalid Moonraker endpoint or API key") error = tr("Invalid Moonraker endpoint or API key");
        else if (event.error == "Moonraker uses the configured API key, not a cloud account") error = tr("Moonraker uses the configured API key, not a cloud account");
        else if (event.error == "Moonraker authorization failed. Check the API key.") error = tr("Moonraker authorization failed. Check the API key.");
        else if (event.error == "Klipper is not ready") error = tr("Klipper is not ready");
        else if (event.error == "Invalid printer object list") error = tr("Invalid printer object list");
        else if (event.error == "Klipper readiness status is unavailable") error = tr("Klipper readiness status is unavailable");
        else if (event.error == "Klipper is not ready or returned invalid status") error = tr("Klipper is not ready or returned invalid status");
        else if (event.error == "Invalid print status") error = tr("Invalid print status");
        else if (event.error == "Moonraker submission is unavailable or unsupported") error = tr("Moonraker submission is unavailable or unsupported");
        else if (event.error == "Cannot snapshot the G-code file") error = tr("Cannot snapshot the G-code file");
        else if (event.error == "Cannot snapshot the G-code file, or the source changed") error = tr("Cannot snapshot the G-code file, or the source changed");
        else if (event.error == "Upload outcome is unexpected. Check the printer before retrying.") error = tr("Upload outcome is unexpected. Check the printer before retrying.");
        else if (event.error == "File uploaded, but readiness could not be confirmed. Printing was not requested.") error = tr("File uploaded, but readiness could not be confirmed. Printing was not requested.");
        else if (event.error == "Print start could not be confirmed") error = tr("Print start could not be confirmed");
        else if (event.error == "Print control is unavailable. Check the printer state.") error = tr("Print control is unavailable. Check the printer state.");
        else if (event.error == "Print control outcome is unknown") error = tr("Print control outcome is unknown");
        else if (event.error == "Moonraker camera is not supported") error = tr("Moonraker camera is not supported");
        else error = tr("Moonraker HTTP request failed. Check the printer before retrying.");
    }
    switch (event.type) {
    case EventType::CapabilitiesChanged:
        refreshCapabilities();
        break;
    case EventType::Availability:
        m_agentAvailable = event.available;
        m_agentVersion = fromStd(event.version);
        m_agentError = error;
        LOG_INFO("Printer agent available={} version={} error={}",
                 event.available, event.version, event.error);
        emit agentChanged();
        break;
    case EventType::ScanStarted:
        m_discoveryState = DiscoveryScanning;
        m_discoveryError.clear();
        emit discoveryChanged();
        break;
    case EventType::ScanFinished: {
        QList<PrinterDevice> devices;
        devices.reserve(static_cast<qsizetype>(event.devices.size()));
        for (const auto& value : event.devices) {
            devices.push_back(fromCore(value));
        }
        m_devices.replaceDevices(devices);
        m_discoveryError = error;
        m_discoveryState = !error.isEmpty()
            ? (event.success ? DiscoveryWarning : DiscoveryError)
            : (devices.isEmpty() ? DiscoveryEmpty : DiscoveryReady);
        LOG_INFO("Printer discovery finished devices={} error={}",
                 devices.size(), event.error);
        emit discoveryChanged();
        if (m_selectConfiguredMoonraker && !devices.isEmpty()) {
            m_selectConfiguredMoonraker = false;
            selectDevice(devices.first().deviceId);
        }
        break;
    }
    case EventType::LoginFinished:
        m_accountName = event.success ? fromStd(event.accountName) : QString();
        m_authError = error;
        m_authState = event.success ? SignedIn : AuthError;
        LOG_INFO("Flashforge account login success={}", event.success);
        emit authChanged();
        break;
    case EventType::LogoutFinished:
        m_authState = SignedOut;
        m_accountName.clear();
        m_authError = error;
        emit authChanged();
        break;
    case EventType::ConnectionChanged: {
        const bool wasConnected = connected();
        m_connectionError = error;
        m_deviceAccessRequired = event.accessRequired;
        m_connectionState = event.success ? Connected
            : (error.isEmpty() ? Disconnected : ConnectionError);
        if (!event.success) {
            resetCameraSession(false);
            m_status = {};
            m_jobState = JobIdle;
            m_controlRequestId = 0;
            if (m_transferState == TransferUploading || m_transferState == TransferStarting)
                markTransferUnknown(tr("The connection was lost during submission. Check the printer before sending again."));
            if (wasConnected) ++m_sessionId; // Late events cannot revive a lost connection.
            emit statusChanged();
            emit jobChanged();
        }
        LOG_INFO("Printer connection success={} error={}", event.success, event.error);
        emit connectionChanged();
        break;
    }
    case EventType::StatusChanged:
        if (!connected()) break;
        applyStatus(fromCore(event.status));
        break;
    case EventType::TransferProgress:
        if (!connected() || m_transferState != TransferUploading
            || event.requestId == 0 || event.requestId != m_transferRequestId
            || !std::isfinite(event.progress)) break;
        m_transferProgress = std::max(m_transferProgress, std::clamp(event.progress, 0.0, 1.0));
        emit jobChanged();
        break;
    case EventType::JobCommandFinished:
        if (!connected()) break;
        if (event.command == "send") {
            if (m_transferState != TransferUploading || event.requestId == 0
                || event.requestId != m_transferRequestId) break;
            if (!event.success) {
                if (event.outcomeUnknown)
                    markTransferUnknown(tr("Submission could not be confirmed. Check the printer before sending again.")
                                        + (error.isEmpty() ? QString() : QStringLiteral("\n") + error));
                else failTransfer(error);
                break;
            }
            if (!event.status.fileName.empty()) m_submittedFileName = fromStd(event.status.fileName);
            m_transferProgress = 1.0;
            m_transferState = m_printNow ? TransferStarting : TransferUploaded;
            if (m_printNow) m_startConfirmationTimer.start();
            else m_transferRequestId = 0;
        } else {
            if (event.requestId == 0 || event.requestId != m_controlRequestId
                || fromStd(event.command) != m_controlCommand) break;
            m_controlRequestId = 0;
            m_controlOutcomeUnknown = event.outcomeUnknown;
            m_jobError = event.outcomeUnknown
                ? tr("Command outcome is unknown. Check the printer before reconnecting.") : error;
            if (!event.success) setJobStateFromPrinter(m_status.state);
            // Commands were already marked pending when dispatched. A newer
            // device status is authoritative; late success must not regress it.
        }
        emit jobChanged();
        break;
    case EventType::CameraStreamCommandFinished:
        if (event.command == "open") {
            if (m_cameraSessionState != CameraOpening
                && m_cameraSessionState != CameraLoading) {
                break;
            }
            if (!event.success) {
                scheduleCameraRetry(error);
            } else {
                // The detail available before open may contain an expired HLS
                // URL. Wait for the post-open status refresh before exposing a
                // source to the media player, even if the old URL was nonempty.
                m_cameraSessionState = CameraLoading;
                emit cameraChanged();
            }
        } else if (event.command == "close" && m_cameraSessionState == CameraClosing) {
            m_cameraSessionState = event.success ? CameraIdle : CameraError;
            m_cameraSessionError = event.success ? QString() : error;
            emit cameraChanged();
        }
        break;
    }
}

void PrinterQtFacade::initializeCameraSession() {
    m_cameraRetryTimer.setSingleShot(true);
    m_cameraOpenTimeout.setSingleShot(true);
    connect(&m_cameraRetryTimer, &QTimer::timeout, this, [this] {
        if (connected() && (m_cameraSessionState == CameraOpening
                            || m_cameraSessionState == CameraLoading)) {
            beginCameraOpen();
        }
    });
    connect(&m_cameraOpenTimeout, &QTimer::timeout, this, [this] {
        scheduleCameraRetry(tr("Timed out waiting for the camera stream"));
    });
}

void PrinterQtFacade::beginCameraOpen() {
    if (!connected() || !m_capabilities.camera || !m_status.cameraAvailable) {
        m_cameraSessionState = CameraError;
        m_cameraSessionError = tr("Video is unavailable for this printer");
        emit cameraChanged();
        return;
    }
    m_cameraSessionState = CameraOpening;
    m_cameraSessionError.clear();
    m_cameraOpenTimeout.start(15000);
    emit cameraChanged();
    m_agent->setCameraStreamEnabled(true);
}

void PrinterQtFacade::scheduleCameraRetry(const QString& error) {
    if (m_cameraSessionState != CameraOpening
        && m_cameraSessionState != CameraLoading) {
        return;
    }
    m_cameraOpenTimeout.stop();
    if (m_cameraRetryAttempt >= 2) {
        m_cameraSessionState = CameraError;
        m_cameraSessionError = error.isEmpty()
            ? tr("Unable to open the camera stream") : error;
        emit cameraChanged();
        return;
    }
    const int retryDelayMs = 1000 << m_cameraRetryAttempt;
    ++m_cameraRetryAttempt;
    m_cameraSessionState = CameraOpening;
    m_cameraSessionError = error;
    emit cameraChanged();
    m_cameraRetryTimer.start(retryDelayMs);
}

void PrinterQtFacade::resetCameraSession(bool requestClose) {
    m_cameraRetryTimer.stop();
    m_cameraOpenTimeout.stop();
    if (requestClose && (cameraSessionActive() || m_cameraSessionState == CameraError)) {
        m_agent->setCameraStreamEnabled(false);
    }
    const bool changed = m_cameraSessionState != CameraIdle
        || !m_cameraSessionError.isEmpty();
    m_cameraSessionState = CameraIdle;
    m_cameraSessionError.clear();
    m_cameraRetryAttempt = 0;
    if (changed) {
        emit cameraChanged();
    }
}

QString PrinterQtFacade::remainingTime() const {
    if (m_status.estimatedTimeSeconds < 0 || !std::isfinite(m_status.estimatedTimeSeconds)) return tr("Unknown");
    const double remaining = qMax(0.0, m_status.estimatedTimeSeconds - m_status.printDurationSeconds);
    const int seconds = static_cast<int>(remaining);
    return QStringLiteral("%1:%2")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'));
}

void PrinterQtFacade::startDiscovery() {
    if (!m_capabilities.discovery) return;
    m_connectionError.clear();
    m_deviceAccessRequired = false;
    if (m_connectionState == ConnectionError) {
        m_connectionState = Disconnected;
    }
    emit connectionChanged();
    m_agent->scan();
}

void PrinterQtFacade::login(const QString& username, const QString& password) {
    if (!m_capabilities.account || username.trimmed().isEmpty() || password.isEmpty() || m_authState == SigningIn) {
        return;
    }
    m_authState = SigningIn;
    m_authError.clear();
    emit authChanged();
    m_agent->login(username.trimmed().toStdString(), password.toStdString());
}

void PrinterQtFacade::logout() {
    if (transferBusy() || !m_capabilities.account) return;
    resetCameraSession(true);
    // Logout invalidates the selected connection immediately. The vendor may
    // finish old requests before its queued logout runs, so retire their epoch
    // before releasing the selection or accepting any more callbacks.
    ++m_sessionId;
    m_transferRequestId = 0;
    m_controlRequestId = 0;
    m_controlOutcomeUnknown = false;
    m_startConfirmationTimer.stop();
    m_connectionState = Disconnected;
    m_connectionError.clear();
    m_deviceAccessRequired = false;
    m_selectedDevice = {};
    m_status = {};
    m_jobState = JobIdle;
    m_transferState = TransferIdle;
    m_transferProgress = 0.0;
    m_submittedFileName.clear();
    m_jobError.clear();
    m_authState = SignedOut;
    m_accountName.clear();
    m_authError.clear();
    emit selectedDeviceChanged();
    emit connectionChanged();
    emit statusChanged();
    emit jobChanged();
    emit authChanged();
    m_agent->releaseDevice();
    m_agent->logout();
}

void PrinterQtFacade::selectDevice(const QString& deviceId) {
    if (transferBusy()) return;
    const PrinterDevice* device = m_devices.findById(deviceId);
    if (!device) {
        return;
    }

    const bool alreadyReady = connected() && m_selectedDevice.deviceId == deviceId;
    if (!alreadyReady) {
        resetCameraSession(true);
    }
    m_selectedDevice = *device;
    emit selectedDeviceChanged();
    if (alreadyReady) {
        return;
    }

    ++m_sessionId;
    m_transferRequestId = 0;
    m_controlRequestId = 0;
    m_controlOutcomeUnknown = false;
    m_startConfirmationTimer.stop();
    m_transferState = TransferIdle;
    m_submittedFileName.clear();
    m_jobError.clear();
    m_status = {};
    m_jobState = JobIdle;
    m_transferProgress = 0.0;
    m_connectionError.clear();
    m_deviceAccessRequired = false;
    emit statusChanged();
    emit jobChanged();
    if (!device->online) {
        m_agent->releaseDevice();
        m_connectionState = Disconnected;
        emit connectionChanged();
        return;
    }

    m_connectionState = Connecting;
    emit connectionChanged();
    auto selected = toCore(*device);
    selected.sessionId = m_sessionId;
    m_agent->selectDevice(selected);
}

void PrinterQtFacade::provideDeviceAccess(const QString& credential) {
    if (transferBusy() || m_selectedDevice.deviceId.isEmpty() || !m_selectedDevice.online
        || credential.trimmed().isEmpty()) {
        return;
    }
    m_connectionError.clear();
    m_deviceAccessRequired = false;
    m_connectionState = Connecting;
    emit connectionChanged();
    m_agent->provideDeviceAccess(credential.trimmed().toStdString());
}

void PrinterQtFacade::submitJob(const QString& filePath,
                                const QString& destinationName,
                                const QString& formatId,
                                bool printNow,
                                bool levelingBeforePrint,
                                bool flowCalibration,
                                bool firstLayerInspection,
                                bool timeLapseVideo,
                                bool useMaterialStation) {
    if (transferBusy()) return; // Never replace a pending or unresolved submission.
    m_transferProgress = 0.0;
    m_submittedFileName.clear();
    if (!connected() || printing() || !m_capabilities.upload || (printNow && !m_capabilities.start)) {
        failTransfer(tr("Connect to an idle printer before sending a file."));
        return;
    }
    if (!m_capabilities.vendorOptions && (levelingBeforePrint || flowCalibration
        || firstLayerInspection || timeLapseVideo || useMaterialStation)) {
        failTransfer(tr("The selected printer does not support these print options."));
        return;
    }
    if (!supportedPrintFormats().contains(formatId)
        || (formatId != QStringLiteral("gcode") && formatId != QStringLiteral("gcode-3mf"))) {
        failTransfer(tr("The selected printer does not support this file format."));
        return;
    }
    const QUrl url(filePath);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : filePath;
    const QFileInfo info(localPath);
    const QString extension = formatId == QStringLiteral("gcode")
        ? QStringLiteral(".gcode") : QStringLiteral(".gcode.3mf");
    QFile source(localPath);
    if (!info.isFile() || info.size() <= 0 || !source.open(QIODevice::ReadOnly)
        || !info.fileName().endsWith(extension, Qt::CaseInsensitive)) {
        failTransfer(tr("The print file is missing, unreadable, or does not match the selected format."));
        return;
    }
    const bool zip = source.peek(4).startsWith("PK\x03\x04");
    if ((formatId == QStringLiteral("gcode-3mf")) != zip) {
        failTransfer(tr("The print file content does not match the selected format."));
        return;
    }
    QString destination = destinationName.trimmed();
    if (destination.isEmpty()) destination = info.fileName();
    if (!destination.endsWith(extension, Qt::CaseInsensitive)) {
        if (!QFileInfo(destination).suffix().isEmpty()) {
            failTransfer(tr("The destination filename does not match the selected format."));
            return;
        }
        destination += extension;
    }
    bool invalidName = destination.contains('/') || destination.contains('\\')
        || destination.toUtf8().size() > 255 || destination.startsWith('.');
    for (const auto ch : destination) invalidName |= ch.unicode() < 32 || ch.unicode() == 127;
    if (invalidName) {
        failTransfer(tr("Use a filename without directories or control characters."));
        return;
    }
    PrinterCore::PrintJob job;
    job.requestId = ++m_nextRequestId;
    job.formatId = formatId.toStdString();
    job.filePath = info.absoluteFilePath().toStdString();
    job.destinationName = destination.toStdString();
    job.printNow = printNow;
    job.levelingBeforePrint = levelingBeforePrint;
    job.flowCalibration = flowCalibration;
    job.firstLayerInspection = firstLayerInspection;
    job.timeLapseVideo = timeLapseVideo;
    job.useMaterialStation = useMaterialStation;
    m_transferRequestId = job.requestId;
    m_submittedFileName = destination;
    m_printNow = printNow;
    m_jobError.clear();
    m_transferState = TransferUploading;
    emit jobChanged();
    m_agent->submitJob(job);
}

void PrinterQtFacade::pausePrint() {
    if (!m_capabilities.pauseResume || !jobControllable() || m_controlRequestId != 0 || m_jobState != Printing) return;
    m_jobError.clear();
    m_jobState = Pausing;
    emit jobChanged();
    m_controlRequestId = ++m_nextRequestId;
    m_controlCommand = QStringLiteral("pause");
    m_agent->controlJob("pause", m_controlRequestId);
}
void PrinterQtFacade::resumePrint() {
    if (!m_capabilities.pauseResume || !jobControllable() || m_controlRequestId != 0 || m_jobState != Paused) return;
    m_jobError.clear();
    m_jobState = Resuming;
    emit jobChanged();
    m_controlRequestId = ++m_nextRequestId;
    m_controlCommand = QStringLiteral("continue");
    m_agent->controlJob("continue", m_controlRequestId);
}
void PrinterQtFacade::cancelPrint() {
    if (!m_capabilities.cancel || !jobControllable() || m_controlRequestId != 0) return;
    m_jobError.clear();
    m_jobState = Stopping;
    emit jobChanged();
    m_controlRequestId = ++m_nextRequestId;
    m_controlCommand = QStringLiteral("cancel");
    m_agent->controlJob("cancel", m_controlRequestId);
}

void PrinterQtFacade::dismissJobStatus() {
    if (m_transferState == TransferUploading || m_transferState == TransferStarting || m_controlRequestId != 0) return;
    if (m_transferState == TransferUnknown || m_controlOutcomeUnknown) m_agent->acknowledgeUnknownOutcome();
    m_controlOutcomeUnknown = false;
    if (m_transferState != TransferIdle) {
        m_transferState = TransferIdle;
        m_transferRequestId = 0;
        m_submittedFileName.clear();
        m_transferProgress = 0.0;
    }
    if (m_jobState == Completed || m_jobState == JobError) m_jobState = JobIdle;
    m_jobError.clear();
    emit jobChanged();
}

void PrinterQtFacade::startCameraStream() {
    if (m_cameraSessionState == CameraOpening
        || m_cameraSessionState == CameraLoading
        || m_cameraSessionState == CameraReady
        || m_cameraSessionState == CameraClosing) {
        return;
    }
    m_cameraRetryTimer.stop();
    m_cameraOpenTimeout.stop();
    m_cameraRetryAttempt = 0;
    beginCameraOpen();
}

void PrinterQtFacade::stopCameraStream() {
    if (m_cameraSessionState == CameraIdle || m_cameraSessionState == CameraClosing) {
        return;
    }
    m_cameraRetryTimer.stop();
    m_cameraOpenTimeout.stop();
    m_cameraRetryAttempt = 0;
    if (!connected()) {
        resetCameraSession(false);
        return;
    }
    m_cameraSessionState = CameraClosing;
    m_cameraSessionError.clear();
    emit cameraChanged();
    m_agent->setCameraStreamEnabled(false);
}

void PrinterQtFacade::applyStatus(const PrinterStatus& status) {
    m_status = status;
    if (m_cameraSessionState == CameraLoading
        && !status.cameraStreamUrl.isEmpty()) {
        m_cameraRetryTimer.stop();
        m_cameraOpenTimeout.stop();
        m_cameraRetryAttempt = 0;
        m_cameraSessionState = CameraReady;
        m_cameraSessionError.clear();
        emit cameraChanged();
    } else if ((m_cameraSessionState == CameraOpening
                || m_cameraSessionState == CameraLoading
                || m_cameraSessionState == CameraReady)
               && !status.cameraAvailable) {
        m_cameraRetryTimer.stop();
        m_cameraOpenTimeout.stop();
        m_cameraSessionState = CameraError;
        m_cameraSessionError = tr("The printer camera became unavailable");
        emit cameraChanged();
    }
    setJobStateFromPrinter(status.state);
    if (m_transferState == TransferStarting && printing()
        && status.fileName == m_submittedFileName) {
        m_transferState = TransferIdle;
        m_transferRequestId = 0;
        m_startConfirmationTimer.stop();
    }
    emit statusChanged();
    emit connectionChanged();
    // jobControllable also depends on the status job id, which may change
    // without a printer-state transition.
    emit jobChanged();
}

void PrinterQtFacade::setJobStateFromPrinter(const QString& state) {
    const QString normalized = state.trimmed().toLower();
    JobState next = m_jobState;
    if (normalized == QStringLiteral("printing") || normalized == QStringLiteral("heating")
        || normalized == QStringLiteral("busy")
        || normalized == QStringLiteral("calibrate_doing")) {
        next = Printing;
    } else if (normalized == QStringLiteral("pause")) {
        next = Paused;
    } else if (normalized == QStringLiteral("pausing")) {
        next = Pausing;
    } else if (normalized == QStringLiteral("completed")) {
        next = Completed;
    } else if (normalized == QStringLiteral("canceling")) {
        next = Stopping;
    } else if (normalized == QStringLiteral("cancel") || normalized == QStringLiteral("ready")) {
        next = JobIdle;
    } else if (normalized == QStringLiteral("error")) {
        next = JobError;
    }
    if (next != m_jobState) {
        m_jobState = next;
        emit jobChanged();
    }
}

} // namespace GPlatform::Printer

REGISTER_PRINTER_QML_SINGLETON_CUSTOM(
    GPlatform::Printer::PrinterQtFacade,
    "PrinterService",
    &GPlatform::Printer::PrinterQtFacade::create)
