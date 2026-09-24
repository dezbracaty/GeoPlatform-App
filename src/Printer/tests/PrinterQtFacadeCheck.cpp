#include "Printer/IPrinterAgent.hpp"
#include "Printer/PrinterQtFacade.hpp"
#include "Printer/PrinterRegistration.hpp"
#include "../src/MoonrakerEndpointInput.hpp"

#include <QCoreApplication>
#include <QMetaProperty>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QThread>
#include <QTimer>
#include <limits>
#include <thread>
#include <QtQml/qqml.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

struct AgentLifetime {
    int initializeCalls{0};
    int releaseCalls{0};
    int shutdownCalls{0};
};

class FakePrinterAgent final : public GPlatform::PrinterCore::IPrinterAgent {
public:
    explicit FakePrinterAgent(std::shared_ptr<AgentLifetime> lifetime)
        : lifetime(std::move(lifetime)) {}

    void setEventSink(EventSink sink) override { m_sink = std::move(sink); }
    void initialize() override {
        ++initializeCalls;
        ++lifetime->initializeCalls;
    }
    void shutdown() override {
        ++shutdownCalls;
        ++lifetime->shutdownCalls;
    }
    void scan() override { ++scanCalls; }
    void login(const std::string& username, const std::string& password) override {
        lastUsername = username;
        lastPassword = password;
        ++loginCalls;
    }
    void logout() override { ++logoutCalls; }
    void selectDevice(const GPlatform::PrinterCore::Device& device) override {
        lastDevice = device;
        ++selectCalls;
    }
    void provideDeviceAccess(const std::string& credential) override {
        lastCredential = credential;
        ++accessCalls;
    }
    void releaseDevice() override {
        ++releaseCalls;
        ++lifetime->releaseCalls;
    }
    GPlatform::PrinterCore::Capabilities capabilities() const override {
        return caps;
    }
    std::vector<std::string> supportedPrintFormats() const override { return formats; }
    void acknowledgeUnknownOutcome() override { ++acknowledgements; }
    void submitJob(const GPlatform::PrinterCore::PrintJob& job) override {
        lastJob = job;
        ++submittedJobs;
    }
    void controlJob(const std::string& action, uint64_t requestId) override {
        lastControlRequestId = requestId;
        lastAction = action;
        ++controlCalls;
    }
    void setCameraStreamEnabled(bool enabled) override {
        cameraEnabled = enabled;
        ++cameraControlCalls;
    }

    GPlatform::PrinterCore::Event contextual(GPlatform::PrinterCore::Event event) const {
        event.deviceId = lastDevice.deviceId;
        event.sessionId = lastDevice.sessionId;
        if (event.requestId == 0)
            event.requestId = event.command == "send" ? lastJob.requestId : lastControlRequestId;
        return event;
    }
    void publish(const GPlatform::PrinterCore::Event& event) { publishRaw(contextual(event)); }
    void publishRaw(const GPlatform::PrinterCore::Event& event) { if (m_sink) m_sink(event); }
    EventSink sinkCopy() const { return m_sink; }

    GPlatform::PrinterCore::Capabilities caps{true, true, true, true, true, true, true, true, true};
    std::vector<std::string> formats{"gcode-3mf", "gcode"};
    int acknowledgements{0};
    uint64_t lastControlRequestId{0};
    std::string lastUsername;
    std::string lastPassword;
    std::string lastCredential;
    std::string lastAction;
    GPlatform::PrinterCore::Device lastDevice;
    GPlatform::PrinterCore::PrintJob lastJob;
    int initializeCalls{0};
    int shutdownCalls{0};
    int scanCalls{0};
    int loginCalls{0};
    int logoutCalls{0};
    int selectCalls{0};
    int accessCalls{0};
    int releaseCalls{0};
    int submittedJobs{0};
    int controlCalls{0};
    int cameraControlCalls{0};
    bool cameraEnabled{false};

private:
    std::shared_ptr<AgentLifetime> lifetime;
    EventSink m_sink;
};

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    GPlatform::Printer::registerModule();
    if (!require(qmlTypeId("GPlatform", 1, 0, "PrinterService") >= 0,
                 "PrinterService was not registered as a GPlatform QML singleton")) {
        return EXIT_FAILURE;
    }

    auto lifetime = std::make_shared<AgentLifetime>();
    auto agent = std::make_unique<FakePrinterAgent>(lifetime);
    auto* fake = agent.get();
    auto facade = std::make_unique<GPlatform::Printer::PrinterQtFacade>(std::move(agent));
    const int jobFileNameIndex = facade->metaObject()->indexOfProperty("jobFileName");
    if (!require(jobFileNameIndex >= 0
                     && facade->metaObject()->property(jobFileNameIndex).notifySignal().name() == "jobChanged",
                 "jobFileName must notify on jobChanged for submitted filename/transfer updates")) {
        return EXIT_FAILURE;
    }
    if (!require(lifetime->initializeCalls == 1,
                 "the facade did not initialize exactly one printer agent")) {
        return EXIT_FAILURE;
    }
    facade->startDiscovery();
    if (!require(fake->scanCalls == 1,
                 "device refresh was not forwarded to the printer agent")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event discovered;
    discovered.type = GPlatform::PrinterCore::EventType::ScanFinished;
    GPlatform::PrinterCore::Device offlineDevice;
    offlineDevice.deviceId = "offline-printer";
    offlineDevice.name = "A offline printer";
    offlineDevice.online = false;
    GPlatform::PrinterCore::Device onlineDevice;
    onlineDevice.deviceId = "online-printer";
    onlineDevice.name = "Z online printer";
    onlineDevice.online = true;
    onlineDevice.route = GPlatform::PrinterCore::DeviceRoute::LocalNetwork;
    discovered.devices = {offlineDevice, onlineDevice};
    fake->publish(discovered);
    const QModelIndex firstDevice = facade->devices()->index(0, 0);
    if (!require(facade->devices()->data(
                     firstDevice, GPlatform::Printer::PrinterDeviceModel::OnlineRole).toBool(),
                 "online printers were not grouped before offline printers")
        || !require(facade->devices()->data(
                        firstDevice, GPlatform::Printer::PrinterDeviceModel::RouteRole).toInt()
                        == static_cast<int>(
                            GPlatform::PrinterCore::DeviceRoute::LocalNetwork),
                    "the generic device route was not exposed by the model")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event partialRefresh = discovered;
    partialRefresh.success = true;
    partialRefresh.error = "cloud refresh failed";
    fake->publish(partialRefresh);
    if (!require(facade->discoveryState()
                     == GPlatform::Printer::PrinterQtFacade::DiscoveryWarning,
                 "partial refresh did not expose a warning state")
        || !require(facade->devices()->rowCount() == 2,
                    "partial refresh discarded the available device results")) {
        return EXIT_FAILURE;
    }
    facade->selectDevice(QStringLiteral("online-printer"));
    facade->provideDeviceAccess(QStringLiteral("2468"));
    if (!require(fake->selectCalls == 1
                     && fake->lastDevice.deviceId == "online-printer",
                 "the facade did not select the online printer")
        || !require(fake->accessCalls == 1 && fake->lastCredential == "2468",
                    "the facade did not forward the printer access credential")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event connected;
    connected.type = GPlatform::PrinterCore::EventType::ConnectionChanged;
    connected.success = true;
    fake->publish(connected);

    GPlatform::PrinterCore::Event cameraStatus;
    cameraStatus.type = GPlatform::PrinterCore::EventType::StatusChanged;
    cameraStatus.status.state = "ready";
    cameraStatus.status.cameraAvailable = true;
    cameraStatus.status.cameraStreamUrl = "http://127.0.0.1/expired.m3u8";
    fake->publish(cameraStatus);
    facade->startCameraStream();
    if (!require(fake->cameraControlCalls == 1 && fake->cameraEnabled,
                 "starting video did not send the camera open command")
        || !require(facade->cameraSessionState()
                        == GPlatform::Printer::PrinterQtFacade::CameraOpening,
                    "camera session did not enter the opening state")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event cameraOpened;
    cameraOpened.type = GPlatform::PrinterCore::EventType::CameraStreamCommandFinished;
    cameraOpened.command = "open";
    cameraOpened.success = true;
    fake->publish(cameraOpened);
    if (!require(facade->cameraSessionState()
                     == GPlatform::Printer::PrinterQtFacade::CameraLoading,
                 "camera session did not wait for its stream URL")) {
        return EXIT_FAILURE;
    }

    cameraStatus.status.cameraStreamUrl = "rtsp://127.0.0.1/live";
    fake->publish(cameraStatus);
    if (!require(facade->cameraSessionState()
                     == GPlatform::Printer::PrinterQtFacade::CameraReady,
                 "camera session did not become ready when its URL arrived")
        || !require(facade->cameraStreamUrl()
                        == QStringLiteral("rtsp://127.0.0.1/live"),
                    "camera stream URL was not exposed without scheme filtering")) {
        return EXIT_FAILURE;
    }

    facade->stopCameraStream();
    if (!require(fake->cameraControlCalls == 2 && !fake->cameraEnabled,
                 "stopping video did not send the camera close command")
        || !require(facade->cameraSessionState()
                        == GPlatform::Printer::PrinterQtFacade::CameraClosing,
                    "camera session did not enter the closing state")) {
        return EXIT_FAILURE;
    }
    GPlatform::PrinterCore::Event cameraClosed;
    cameraClosed.type = GPlatform::PrinterCore::EventType::CameraStreamCommandFinished;
    cameraClosed.command = "close";
    cameraClosed.success = true;
    fake->publish(cameraClosed);
    fake->publish(cameraOpened);
    if (!require(facade->cameraSessionState()
                     == GPlatform::Printer::PrinterQtFacade::CameraIdle,
                 "a late camera-open completion revived a stopped video session")) {
        return EXIT_FAILURE;
    }

    QTemporaryDir printDirectory;
    if (!require(printDirectory.isValid(), "could not create the print-package test directory")) {
        return EXIT_FAILURE;
    }
    const QString packagePath = printDirectory.filePath(QStringLiteral("print package.gcode.3mf"));
    QFile package(packagePath);
    if (!require(package.open(QIODevice::WriteOnly) && package.write("PK\x03\x04", 4) == 4,
                 "could not create the print-package test file")) {
        return EXIT_FAILURE;
    }
    package.close();
    facade->submitJob(QUrl::fromLocalFile(packagePath).toString(), QStringLiteral("print package"),
                      QStringLiteral("gcode-3mf"), true, true, true, true, true, true);
    if (!require(fake->submittedJobs == 1, "the facade did not submit the print package")
        || !require(QString::fromStdString(fake->lastJob.filePath) == packagePath,
                    "the submitted print-package path was not decoded losslessly")
        || !require(fake->lastJob.destinationName == "print package.gcode.3mf",
                    "the submitted printer filename has the wrong extension")
        || !require(fake->lastJob.levelingBeforePrint && fake->lastJob.flowCalibration
                        && fake->lastJob.firstLayerInspection && fake->lastJob.timeLapseVideo
                        && fake->lastJob.useMaterialStation,
                    "the submitted print options were not forwarded")) {
        return EXIT_FAILURE;
    }

    using Facade = GPlatform::Printer::PrinterQtFacade;
    using Event = GPlatform::PrinterCore::Event;
    using EventType = GPlatform::PrinterCore::EventType;
    if (!require(!facade->printing() && facade->transferBusy(), "upload was mistaken for physical printing")) return EXIT_FAILURE;
    auto readyDuringUpload = cameraStatus;
    readyDuringUpload.status.cameraStreamUrl.clear();
    fake->publish(readyDuringUpload);
    if (!require(facade->transferState() == Facade::TransferUploading,
                 "idle status erased the active transfer")) return EXIT_FAILURE;
    const int beforeDuplicate = fake->submittedJobs;
    facade->submitJob(packagePath, "duplicate", "gcode-3mf", true, false, false, false, false, false);
    const int beforeSwitch = fake->selectCalls;
    facade->logout();
    if (!require(fake->logoutCalls == 0 && facade->connected(),
                 "logout interrupted an active submission")) return EXIT_FAILURE;
    facade->selectDevice("offline-printer");
    if (!require(fake->submittedJobs == beforeDuplicate && fake->selectCalls == beforeSwitch,
                 "duplicate send or device switch was accepted while uploading")) return EXIT_FAILURE;
    Event sent;
    sent.type = EventType::JobCommandFinished;
    sent.command = "send";
    sent.success = true;
    fake->publish(sent);
    if (!require(facade->transferState() == Facade::TransferStarting && !facade->printing(),
                 "SDK acceptance was mistaken for confirmed printing")) return EXIT_FAILURE;

    GPlatform::PrinterCore::Event printing;
    printing.type = GPlatform::PrinterCore::EventType::StatusChanged;
    printing.status.state = "printing";
    printing.status.jobId = "job-1";
    printing.status.fileName = "print package.gcode.3mf";
    printing.status.progress = 37.0;
    fake->publish(printing);

    if (!require(facade->connected(), "connection event did not mark the facade connected")
        || !require(facade->printing(), "printing status did not activate the job")
        || !require(facade->jobControllable(), "active job should be controllable")
        || !require(facade->jobFileName() == QStringLiteral("print package.gcode.3mf"),
                    "the active job filename was not exposed to the UI")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event commandFailure;
    commandFailure.type = GPlatform::PrinterCore::EventType::JobCommandFinished;
    commandFailure.command = "pause";
    commandFailure.error = "temporary control failure";
    facade->pausePrint();
    fake->publish(commandFailure);
    if (!require(facade->printing(), "control failure incorrectly discarded the active job")
        || !require(facade->jobControllable(), "control failure incorrectly disabled the active job")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event connectionLost;
    connectionLost.type = GPlatform::PrinterCore::EventType::ConnectionChanged;
    connectionLost.error = "status polling failed";
    fake->publish(connectionLost);
    if (!require(!facade->connected(), "connection failure left the facade connected")
        || !require(!facade->printing(), "connection failure left stale printing state")
        || !require(!facade->jobControllable(), "disconnected job remained controllable")
        || !require(facade->printProgress() == 0.0, "connection failure left stale progress")) {
        return EXIT_FAILURE;
    }

    GPlatform::PrinterCore::Event upload;
    upload.type = GPlatform::PrinterCore::EventType::TransferProgress;
    upload.progress = 0.5;
    fake->publish(upload);
    if (!require(!facade->printing(), "late upload progress revived a disconnected job")) {
        return EXIT_FAILURE;
    }

    // Reconnecting assigns a new session. Late old-connection events must stay stale.
    const auto staleConnected = fake->contextual(connected);
    facade->selectDevice("online-printer");
    fake->publishRaw(staleConnected);
    if (!require(!facade->connected(), "old connection completion revived a new session")) return EXIT_FAILURE;
    fake->publish(connected);
    fake->publish(upload);
    if (!require(!facade->printing() && !facade->transferBusy(), "unsolicited progress revived a transfer")) return EXIT_FAILURE;

    auto submit = [&](bool printNow, const QString& path = QString(), const QString& name = "sent",
                      const QString& format = "gcode-3mf") {
        facade->submitJob(path.isEmpty() ? packagePath : path, name, format, printNow,
                          false, false, false, false, false);
    };
    submit(false);
    const auto firstId = fake->lastJob.requestId;
    if (!require(!fake->lastJob.printNow && fake->lastJob.formatId == "gcode-3mf", "explicit upload-only intent was lost")) return EXIT_FAILURE;
    upload.requestId = firstId;
    upload.progress = 0.6;
    fake->publish(upload);
    upload.progress = 0.2;
    fake->publish(upload);
    upload.progress = std::numeric_limits<double>::quiet_NaN();
    fake->publish(upload);
    if (!require(facade->transferProgress() == 0.6, "progress regressed or accepted NaN")) return EXIT_FAILURE;
    fake->publish(sent);
    fake->publish(readyDuringUpload);
    fake->publish(upload);
    if (!require(facade->transferState() == Facade::TransferUploaded && !facade->printing()
                 && facade->transferProgress() == 1.0, "upload-only completed as printing or late progress revived it")) return EXIT_FAILURE;
    facade->dismissJobStatus();

    submit(false);
    const auto secondId = fake->lastJob.requestId;
    auto staleSend = fake->contextual(sent);
    staleSend.requestId = firstId;
    fake->publishRaw(staleSend);
    upload.requestId = firstId;
    upload.progress = 0.9;
    fake->publish(upload);
    if (!require(secondId != firstId && facade->transferState() == Facade::TransferUploading
                 && facade->transferProgress() == 0.0, "old task event changed a new upload")) return EXIT_FAILURE;
    Event sendFailure;
    sendFailure.type = EventType::JobCommandFinished;
    sendFailure.command = "send";
    sendFailure.error = "transfer rejected";
    fake->publish(sendFailure);
    if (!require(facade->transferState() == Facade::TransferFailed && facade->jobState() == Facade::JobIdle,
                 "transfer failure polluted physical printer state")) return EXIT_FAILURE;
    facade->dismissJobStatus();
    if (!require(facade->transferState() == Facade::TransferIdle && facade->jobError().isEmpty(), "dismiss failed")) return EXIT_FAILURE;

    // Uncertain SDK outcomes are explicit and never automatically retried.
    submit(true);
    sendFailure.outcomeUnknown = true;
    fake->publish(sendFailure);
    int submitted = fake->submittedJobs;
    submit(true);
    if (!require(facade->transferState() == Facade::TransferUnknown && fake->submittedJobs == submitted,
                 "uncertain outcome allowed an automatic/repeated submission")) return EXIT_FAILURE;
    facade->dismissJobStatus(); // Represents explicit user acknowledgement, not a retry.
    submit(true);
    fake->publish(sent);
    auto otherPrint = printing;
    otherPrint.status.fileName = "another-job.gcode.3mf";
    fake->publish(otherPrint);
    if (!require(facade->transferState() == Facade::TransferStarting, "another job falsely confirmed this submission")) return EXIT_FAILURE;
    // Exercise the production timeout handler without a 30-second wall-clock delay.
    auto* startTimer = facade->findChild<QTimer*>("printStartConfirmationTimer");
    if (!require(startTimer && startTimer->isActive(), "missing print-start confirmation deadline")) return EXIT_FAILURE;
    QMetaObject::invokeMethod(startTimer, "timeout");
    if (!require(facade->transferState() == Facade::TransferUnknown, "unconfirmed start remained pending indefinitely")) return EXIT_FAILURE;
    facade->dismissJobStatus();
    if (facade->connected()) fake->publish(connectionLost);
    facade->selectDevice("online-printer");
    fake->publish(connected);
    fake->publish(readyDuringUpload);

    // Format handling belongs to capabilities, not a forced .3mf suffix.
    const QString gcodePath = printDirectory.filePath("plain.gcode");
    QFile gcode(gcodePath);
    if (!require(gcode.open(QIODevice::WriteOnly) && gcode.write("G90\nG1 X1\n") > 0, "gcode fixture failed")) return EXIT_FAILURE;
    gcode.close();
    submit(false, gcodePath, "plain", "gcode");
    if (!require(fake->lastJob.destinationName == "plain.gcode" && !fake->lastJob.printNow,
                 "generic GCode was rewritten into a 3MF package")) return EXIT_FAILURE;
    fake->publish(sent);
    facade->dismissJobStatus();
    submitted = fake->submittedJobs;
    submit(false, gcodePath, "plain.3mf", "gcode");
    if (!require(fake->submittedJobs == submitted && facade->transferState() == Facade::TransferFailed,
                 "mismatched destination extension was accepted")) return EXIT_FAILURE;
    submit(false, gcodePath, "../bad", "gcode");
    if (!require(fake->submittedJobs == submitted, "remote path traversal was accepted")) return EXIT_FAILURE;
    submit(false, packagePath, "bad", "gcode");
    if (!require(fake->submittedJobs == submitted, "package was submitted as GCode")) return EXIT_FAILURE;
    submit(false, gcodePath, "bad", "unsupported");
    if (!require(fake->submittedJobs == submitted, "unsupported format was sent")) return EXIT_FAILURE;
    facade->dismissJobStatus();

    // Worker-thread agents cannot modify the UI model from their callback thread.
    bool deliveredOnOwnerThread = false;
    QObject::connect(facade.get(), &Facade::statusChanged, [&] {
        deliveredOnOwnerThread = QThread::currentThread() == app.thread();
    });
    auto callback = fake->sinkCopy();
    auto workerEvent = fake->contextual(readyDuringUpload);
    workerEvent.status.nozzleTemperature = 123.0;
    std::thread network([&] { callback(workerEvent); });
    network.join();
    if (!require(facade->nozzleTemperature() != 123.0, "worker mutated facade synchronously")) return EXIT_FAILURE;
    QCoreApplication::processEvents();
    if (!require(deliveredOnOwnerThread && facade->nozzleTemperature() == 123.0, "worker event not marshalled to GUI thread")) return EXIT_FAILURE;

    // Logout must retire the connection before asynchronous SDK completion.
    fake->publish(printing);
    fake->publish(cameraStatus);
    facade->startCameraStream();
    const auto preLogoutConnection = fake->contextual(connected);
    const auto preLogoutStatus = fake->contextual(printing);
    const auto preLogoutCamera = fake->contextual(cameraOpened);
    const auto previousSession = fake->lastDevice.sessionId;
    facade->logout();
    if (!require(fake->logoutCalls == 1 && fake->releaseCalls == 1,
                 "logout did not release the selected device")
        || !require(!facade->connected() && !facade->printing() && !facade->jobControllable()
                    && facade->selectedDeviceId().isEmpty() && !facade->deviceAccessRequired()
                    && facade->cameraSessionState() == Facade::CameraIdle
                    && facade->cameraStreamUrl().isEmpty() && facade->printProgress() == 0.0,
                    "logout left stale connection, job, selection or camera state")) return EXIT_FAILURE;
    fake->publishRaw(preLogoutConnection);
    fake->publishRaw(preLogoutStatus);
    fake->publishRaw(preLogoutCamera);
    Event loggedOut;
    loggedOut.type = EventType::LogoutFinished;
    loggedOut.success = true;
    fake->publish(loggedOut);
    if (!require(!facade->connected() && !facade->printing() && !facade->signedIn()
                 && facade->cameraSessionState() == Facade::CameraIdle,
                 "old session callbacks revived a logged-out connection")) return EXIT_FAILURE;

    // A fresh selection has a new epoch, but an access-code retry within that
    // connection attempt must keep it so the agent's success remains valid.
    facade->selectDevice("online-printer");
    const auto retrySession = fake->lastDevice.sessionId;
    Event accessRequired;
    accessRequired.type = EventType::ConnectionChanged;
    accessRequired.error = "access code required";
    accessRequired.accessRequired = true;
    fake->publish(accessRequired);
    if (!require(retrySession != previousSession && facade->deviceAccessRequired(),
                 "new connection did not retain its access challenge")) return EXIT_FAILURE;
    const auto beforeAccessRetry = fake->accessCalls;
    facade->provideDeviceAccess(" 1357 ");
    fake->publishRaw(preLogoutConnection);
    if (!require(!facade->connected(), "old logout-session success completed a new credential retry")) return EXIT_FAILURE;
    fake->publish(connected);
    if (!require(fake->accessCalls == beforeAccessRetry + 1 && fake->lastCredential == "1357"
                 && fake->lastDevice.sessionId == retrySession && facade->connected(),
                 "valid access-code retry was invalidated by logout epoch handling")) return EXIT_FAILURE;

    facade.reset();
    // A callback copied before destruction must not dereference the old facade.
    std::thread late([&] { callback(workerEvent); });
    late.join();
    QCoreApplication::processEvents();
    if (!require(lifetime->releaseCalls == 2,
                 "the facade did not release its sole printer agent")
        || !require(lifetime->shutdownCalls == 1,
                    "the facade did not shut down its sole printer agent")) {
        return EXIT_FAILURE;
    }

    // Runtime capability changes must be observable and enforced at the action boundary.
    {
        auto nextLifetime = std::make_shared<AgentLifetime>();
        auto next = std::make_unique<FakePrinterAgent>(nextLifetime);
        auto* neutral = next.get();
        Facade changing(std::move(next));
        changing.startDiscovery();
        neutral->publish(discovered);
        changing.selectDevice("online-printer");
        neutral->publish(connected);
        const int formatsIndex = changing.metaObject()->indexOfProperty("supportedPrintFormats");
        const auto formatsProperty = changing.metaObject()->property(formatsIndex);
        if (!require(!formatsProperty.isConstant() && formatsProperty.hasNotifySignal(),
                     "formats must notify when backend/capabilities change")) return EXIT_FAILURE;
        changing.submitJob(gcodePath, "user-name.gcode", "gcode", true, false, false, false, false, false);
        Event renamed = sent;
        renamed.status.fileName = "gplatform-unique.gcode";
        neutral->publish(renamed);
        Event renamedStatus = printing;
        renamedStatus.status.fileName = "user-name.gcode";
        neutral->publish(renamedStatus);
        if (!require(changing.transferState() == Facade::TransferStarting,
                     "original filename falsely confirmed a uniquely named remote job")) return EXIT_FAILURE;
        renamedStatus.status.fileName = renamed.status.fileName;
        renamedStatus.status.estimatedTimeSeconds = -1;
        neutral->publish(renamedStatus);
        if (!require(changing.remainingTime() == "Unknown", "missing time estimate was presented as zero remaining")) return EXIT_FAILURE;
        if (!require(changing.transferState() == Facade::TransferIdle
                     && changing.jobFileName() == "gplatform-unique.gcode",
                     "upload-returned unique filename did not confirm physical printing")) return EXIT_FAILURE;
        neutral->publish(readyDuringUpload);
        int capabilitiesChanged = 0;
        QObject::connect(&changing, &Facade::capabilitiesChanged, [&] { ++capabilitiesChanged; });
        neutral->caps = {};
        neutral->formats = {"gcode"};
        Event capsChanged; capsChanged.type = EventType::CapabilitiesChanged;
        neutral->publish(capsChanged);
        changing.startDiscovery();
        changing.login("user", "secret");
        changing.submitJob(gcodePath, "plain.gcode", "gcode", false, false, false, false, false, false);
        if (!require(capabilitiesChanged == 1 && !changing.supportsUpload()
                     && !changing.supportsAccount() && neutral->scanCalls == 1
                     && neutral->loginCalls == 0 && neutral->submittedJobs == 1
                     && changing.supportedPrintFormats() == QStringList{"gcode"},
                     "missing capabilities did not gate discovery/account/upload")) return EXIT_FAILURE;
        changing.dismissJobStatus();
        neutral->caps.pauseResume = true;
        neutral->caps.cancel = true;
        neutral->publish(capsChanged);
        Event noVendorId = printing;
        noVendorId.status.jobId.clear();
        neutral->publish(noVendorId);
        if (!require(changing.jobControllable(), "neutral backend was incorrectly forced to have a vendor jobId")) return EXIT_FAILURE;
        changing.pausePrint();
        Event ambiguous; ambiguous.type = EventType::JobCommandFinished;
        ambiguous.command = "pause"; ambiguous.outcomeUnknown = true;
        neutral->publish(ambiguous);
        if (!require(changing.commandOutcomeUnknown() && !changing.jobControllable(),
                     "unknown control outcome did not lock controls")) return EXIT_FAILURE;
        changing.pausePrint();
        if (!require(neutral->controlCalls == 1, "unknown control was retried")) return EXIT_FAILURE;
        changing.dismissJobStatus();
        if (!require(neutral->acknowledgements == 1 && !changing.commandOutcomeUnknown()
                     && changing.jobControllable(), "explicit acknowledgement failed to restore backend controls")) return EXIT_FAILURE;
        neutral->publish(readyDuringUpload);

        // A copied old-agent callback cannot change even non-device global state.
        const auto oldSink = neutral->sinkCopy();
        const auto oldId = changing.selectedDeviceId();
        if (!require(!changing.connectMoonraker("http://user:secret@127.0.0.1:1", "key")
                     && changing.selectedDeviceId() == oldId,
                     "invalid endpoint replaced the working backend")) return EXIT_FAILURE;
        if (!require(changing.connectMoonraker("http://127.0.0.1:1", "memory-key")
                     && changing.backendName() == "Moonraker"
                     && !changing.supportsAccount() && changing.supportedPrintFormats().isEmpty(),
                     "Moonraker did not replace/reset runtime capabilities")) return EXIT_FAILURE;
        Event staleAvailability; staleAvailability.type = EventType::Availability;
        staleAvailability.available = true; staleAvailability.version = "stale vendor";
        oldSink(staleAvailability);
        oldSink(discovered);
        Event staleLogin; staleLogin.type = EventType::LoginFinished;
        staleLogin.success = true; staleLogin.accountName = "stale account";
        oldSink(staleLogin);
        if (!require(changing.agentVersion() != "stale vendor" && !changing.signedIn()
                     && changing.devices()->rowCount() == 0,
                     "retired adapter changed global availability/discovery/auth")) return EXIT_FAILURE;
        // Destruction occurs before queued discovery can issue any HTTP request.
    }

    // No event-loop processing here: each adapter is destroyed before its
    // queued discovery can issue a request, including for non-loopback input.
    const std::pair<QString, QString> endpoints[] = {
        {"192.168.2.124", "http://192.168.2.124"},
        {" 192.168.2.124:7125 ", "http://192.168.2.124:7125"},
        {"printer.local", "http://printer.local"},
        {"printer.local:7125", "http://printer.local:7125"},
        {"localhost:7125/moonraker", "http://localhost:7125/moonraker"},
        {"[::1]:7125", "http://[::1]:7125"},
        {"http://printer.local", "http://printer.local"},
        {"https://printer.local:443/moonraker", "https://printer.local:443/moonraker"},
        {" HTTPS://printer.local/moonraker ", "HTTPS://printer.local/moonraker"}
    };
    for (const auto& endpoint : endpoints) {
        if (!require(GPlatform::Printer::normalizeMoonrakerEndpointInput(endpoint.first)
                         == endpoint.second,
                     "endpoint completion changed an explicit scheme, port or path")) return EXIT_FAILURE;
        auto lifetime = std::make_shared<AgentLifetime>();
        GPlatform::Printer::PrinterQtFacade inputFacade(std::make_unique<FakePrinterAgent>(lifetime));
        if (!require(inputFacade.connectMoonraker(endpoint.first, ""),
                     "completed endpoint was rejected by facade")) return EXIT_FAILURE;
    }
    const QString invalidEndpoints[] = {
        "", "   ", "ftp://printer.local", "file:///tmp/printer", "http:/printer.local",
        "https:printer.local", "//printer.local", "printer.local:invalid",
        "printer.local:99999", "user:secret@printer.local", "printer.local?key=secret",
        "printer.local/#fragment", "http://user:secret@printer.local"
    };
    for (const auto& endpoint : invalidEndpoints) {
        auto lifetime = std::make_shared<AgentLifetime>();
        GPlatform::Printer::PrinterQtFacade inputFacade(std::make_unique<FakePrinterAgent>(lifetime));
        if (!require(!inputFacade.connectMoonraker(endpoint, ""),
                     "endpoint completion bypassed strict validation")) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
