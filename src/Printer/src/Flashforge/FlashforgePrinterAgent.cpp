#include "FlashforgePrinterAgent.hpp"

#include "Foundation/Log.h"
#include "FlashNetwork.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMap>
#include <QMetaObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <miniz.h>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace GPlatform::Printer {

using PrinterCore::Device;
using PrinterCore::Event;
using PrinterCore::EventType;
using PrinterCore::PrintJob;
using PrinterCore::Status;

namespace {

// FlashNetwork-native data stays entirely inside the Flashforge agent
// translation unit and never crosses IPrinterAgent.
struct FlashforgeDevice {
    std::string deviceId;
    std::uint64_t sessionId{0};
    std::string name;
    std::string model;
    std::string wanDeviceId;
    std::string wanTopic;
    std::string state;
    std::string ip;
    std::uint16_t port{0};
    std::uint16_t vid{0};
    std::uint16_t pid{0};
    std::uint16_t connectMode{0};
    std::uint16_t bindStatus{0};
    std::uint16_t bindType{0};
    bool online{true};
};

template <typename T>
struct FlashforgeResult {
    bool success{false};
    bool accessRequired{false};
    bool outcomeUnknown{false};
    QString error;
    T value{};
};

using FlashforgeVoidResult = FlashforgeResult<std::monostate>;
using FlashforgeDeviceListResult = FlashforgeResult<std::vector<FlashforgeDevice>>;
using FlashforgeStringResult = FlashforgeResult<QString>;
using FlashforgeStatusResult = FlashforgeResult<PrinterCore::Status>;

struct FlashforgeAvailability {
    bool available{false};
    QString version;
    QString error;
};

struct FlashforgeWanStatus {
    PrinterCore::Status status;
    QString wanDeviceId;
};

using FlashforgeWanStatusResult = FlashforgeResult<FlashforgeWanStatus>;

QString flashNetworkLibraryPath() {
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libFlashNetwork.dylib");
#elif defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/FlashNetwork.dll");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/libFlashNetwork.so");
#endif
}

QString flashNetworkSettingsPath() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/FLASHNETWORK9.DAT");
}

QString flashNetworkLogDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/logs/FlashNetwork");
}

Device publicDevice(const FlashforgeDevice& value) {
    Device device;
    device.deviceId = value.deviceId;
    device.name = value.name;
    device.model = value.model;
    device.state = value.state;
    device.ip = value.ip;
    device.online = value.online;
    device.route = value.connectMode == 1
        ? PrinterCore::DeviceRoute::Cloud
        : PrinterCore::DeviceRoute::LocalNetwork;
    return device;
}

} // namespace

struct FlashforgePrinterAgent::Impl : std::enable_shared_from_this<Impl> {
    Impl() {
        statusTimer->setInterval(3000);
        QObject::connect(statusTimer.get(), &QTimer::timeout,
                         ownerContext.get(), [this] { requestStatus(); });
        sdkCreateState();
        workerContext = new QObject();
        workerContext->moveToThread(workerThread);
        QObject::connect(workerThread, &QThread::finished,
                         workerContext, &QObject::deleteLater);
        QObject::connect(workerThread, &QThread::finished,
                         workerThread, &QObject::deleteLater);
        workerThread->setObjectName(QStringLiteral("FlashforgePrinterAgent"));
        workerThread->start();
    }

    void post(std::function<void()> callback) {
        // Shutdown destroys the GUI receiver under this lock. A vendor callback
        // may race it, but can never invokeMethod on a dangling QObject.
        std::lock_guard<std::mutex> lock(ownerMutex);
        if (stopping || !ownerContext) return;
        const std::weak_ptr<Impl> weak = shared_from_this();
        QMetaObject::invokeMethod(ownerContext.get(),
            [weak, callback = std::move(callback)]() mutable {
                if (auto self = weak.lock(); self && !self->stopping) callback();
            }, Qt::QueuedConnection);
    }

    template <typename Operation>
    void enqueue(Operation operation) {
        if (stopping || !workerContext) return;
        QMetaObject::invokeMethod(workerContext,
            [self = shared_from_this(), operation = std::move(operation)]() mutable {
                // Work queued before shutdown must not start another SDK call.
                if (!self->stopping) operation();
            }, Qt::QueuedConnection);
    }

    void publish(Event event) {
        if (stopping) return;
        if (event.deviceId.empty() && (event.type == EventType::ConnectionChanged
            || event.type == EventType::StatusChanged
            || event.type == EventType::CameraStreamCommandFinished
            || event.type == EventType::JobCommandFinished)) {
            event.deviceId = device.deviceId;
            event.sessionId = device.sessionId;
        }
        if (sink) {
            sink(event);
        }
    }

    void publishError(EventType type, const QString& error,
                      const std::string& command = {}, bool accessRequired = false,
                      std::uint64_t requestId = 0,
                      const FlashforgeDevice* selection = nullptr) {
        Event event;
        event.type = type;
        event.error = error.toStdString();
        event.command = command;
        event.accessRequired = accessRequired;
        event.requestId = requestId;
        if (selection) {
            event.deviceId = selection->deviceId;
            event.sessionId = selection->sessionId;
        }
        publish(event);
    }

    void initialize() {
        if (initializeRequested) {
            return;
        }
        initializeRequested = true;
        const QString libraryPath = flashNetworkLibraryPath();
        const QString settingsPath = flashNetworkSettingsPath();
        const QString logDirectory = flashNetworkLogDirectory();
        enqueue([this, libraryPath, settingsPath, logDirectory] {
            const FlashforgeAvailability availability =
                sdkInitialize(libraryPath, settingsPath, logDirectory);
            post([this, availability] {
                available = availability.available;
                Event event;
                event.type = EventType::Availability;
                event.available = availability.available;
                event.success = availability.available;
                event.version = availability.version.toStdString();
                event.error = availability.error.toStdString();
                publish(event);
            });
        });
    }

    void shutdown() {
        if (stopping.exchange(true)) return;
        sdkRequestStop();
        sink = {};
        statusTimer.reset();
        {
            std::lock_guard<std::mutex> lock(ownerMutex);
            ownerContext.reset();
        }
        // Keep the implementation alive until the running vendor call returns.
        // Cleanup runs after queued operations (which now skip execution). Never
        // wait on the GUI or destroy/terminate a running QThread.
        QMetaObject::invokeMethod(workerContext, [self = shared_from_this()] {
            self->sdkShutdown();
            self->workerThread->quit();
        }, Qt::QueuedConnection);
        workerContext = nullptr;
        available = false;
    }

    void resetConnectionSession() {
        ++sessionGeneration;
        if (statusTimer) statusTimer->stop();
        statusPending = false;
        connected = false;
        jobId.clear();
    }

    void publishMergedDevices(bool success = true, const QString& error = {}) {
        QMap<QString, FlashforgeDevice> merged;
        for (const FlashforgeDevice& wan : wanDevices) {
            merged.insert(QString::fromStdString(wan.deviceId), wan);
        }
        for (const FlashforgeDevice& lan : lanDevices) {
            const QString key = QString::fromStdString(lan.deviceId);
            FlashforgeDevice value = lan;
            const auto wan = merged.constFind(key);
            if (wan != merged.cend()) {
                value.wanDeviceId = wan->wanDeviceId;
                value.wanTopic = wan->wanTopic;
                value.state = wan->state;
                if (value.name.empty()) value.name = wan->name;
                if (value.model.empty()) value.model = wan->model;
                value.bindStatus = 1;
                value.online = wan->online;
            }
            merged.insert(key, std::move(value));
        }
        Event event;
        event.type = EventType::ScanFinished;
        event.success = success;
        event.error = error.toStdString();
        event.devices.reserve(static_cast<std::size_t>(merged.size()));
        for (auto it = merged.cbegin(); it != merged.cend(); ++it) {
            event.devices.push_back(publicDevice(it.value()));
        }
        publish(event);
    }

    void updateSelectedWanDevice() {
        for (const FlashforgeDevice& wan : wanDevices) {
            if (wan.deviceId != device.deviceId) {
                continue;
            }
            device.wanDeviceId = wan.wanDeviceId;
            device.wanTopic = wan.wanTopic;
            device.state = wan.state;
            device.name = wan.name;
            device.model = wan.model;
            device.bindStatus = 1;
            device.online = wan.online;
            wanDeviceId = QString::fromStdString(wan.wanDeviceId);
            break;
        }
    }

    void runDeviceRefresh(bool announce) {
        scanPending = true;
        const bool includeWan = authenticated;
        if (announce) {
            Event started;
            started.type = EventType::ScanStarted;
            publish(started);
        }
        enqueue([this, includeWan] {
            auto lanResult = sdkScanLan();
            std::optional<FlashforgeDeviceListResult> wanResult;
            if (includeWan) {
                wanResult = sdkWanDevices();
            }
            post([this, includeWan, lanResult = std::move(lanResult),
                  wanResult = std::move(wanResult)]() mutable {
                if (lanResult.success) {
                    lanDevices = std::move(lanResult.value);
                }
                if (wanResult && wanResult->success) {
                    wanDevices = std::move(wanResult->value);
                    updateSelectedWanDevice();
                }

                // A login can complete while a LAN-only refresh is already in
                // flight, or the user can click Refresh again. Apply useful
                // cache updates, then run the newest transaction without
                // exposing an intermediate device list to QML.
                if (refreshRequested) {
                    refreshRequested = false;
                    runDeviceRefresh(false);
                    return;
                }

                scanPending = false;
                QStringList errors;
                if (!lanResult.success) {
                    errors.push_back(QStringLiteral("LAN: %1").arg(lanResult.error));
                }
                if (includeWan && wanResult && !wanResult->success) {
                    errors.push_back(QStringLiteral("Cloud: %1").arg(wanResult->error));
                }
                const bool anySuccess = lanResult.success
                    || (wanResult && wanResult->success);
                const QString error = errors.join(QStringLiteral("; "));
                if (anySuccess && !error.isEmpty()) {
                    LOG_WARN("Flashforge device refresh partially failed: {}",
                             error.toStdString());
                }
                publishMergedDevices(anySuccess, error);
            });
        });
    }

    void scan() {
        if (scanPending) {
            refreshRequested = true;
            return;
        }
        if (!available) {
            publishError(EventType::ScanFinished,
                         QStringLiteral("FlashNetwork is not available"));
            return;
        }
        runDeviceRefresh(true);
    }

    void refreshWanDevices(std::function<void(bool)> completion = {}) {
        enqueue([this, completion = std::move(completion)]() mutable {
            auto result = sdkWanDevices();
            post([this, result = std::move(result),
                  completion = std::move(completion)]() mutable {
                if (result.success) {
                    wanDevices = std::move(result.value);
                    updateSelectedWanDevice();
                }
                publishMergedDevices(result.success, result.error);
                if (completion) {
                    completion(result.success);
                }
            });
        });
    }

    void login(const QString& username, const QString& password) {
        if (!available) {
            publishError(EventType::LoginFinished,
                         QStringLiteral("FlashNetwork is not available"));
            return;
        }
        enqueue([this, username, password] {
            const auto result = sdkLogin(username, password);
            post([this, result] {
                authenticated = result.success;
                Event event;
                event.type = EventType::LoginFinished;
                event.success = result.success;
                event.accountName = result.value.toStdString();
                event.error = result.error.toStdString();
                publish(event);
                if (result.success) {
                    scan();
                }
            });
        });
    }

    void logout() {
        resetConnectionSession();
        authenticated = false;
        enqueue([this] {
            const auto result = sdkLogout();
            post([this, result] {
                wanDevices.clear();
                Event event;
                event.type = EventType::LogoutFinished;
                event.success = result.success;
                event.error = result.error.toStdString();
                publish(event);
                publishMergedDevices(true);
            });
        });
    }

    std::optional<FlashforgeDevice> nativeDevice(const std::string& deviceId) const {
        for (const FlashforgeDevice& lan : lanDevices) {
            if (lan.deviceId != deviceId) {
                continue;
            }
            FlashforgeDevice result = lan;
            for (const FlashforgeDevice& wan : wanDevices) {
                if (wan.deviceId == deviceId) {
                    result.wanDeviceId = wan.wanDeviceId;
                    result.wanTopic = wan.wanTopic;
                    result.bindStatus = 1;
                    if (result.name.empty()) result.name = wan.name;
                    if (result.model.empty()) result.model = wan.model;
                    break;
                }
            }
            return result;
        }
        for (const FlashforgeDevice& wan : wanDevices) {
            if (wan.deviceId == deviceId) {
                return wan;
            }
        }
        return std::nullopt;
    }

    void connectSelected(const std::string& code) {
        resetConnectionSession();
        checkCode = QString::fromStdString(code);
        wanDeviceId = QString::fromStdString(device.wanDeviceId);
        const quint64 generation = sessionGeneration.load();
        if (device.connectMode == 0) {
            const FlashforgeDevice selected = device;
            const QString codeCopy = checkCode;
            enqueue([this, generation, selected, codeCopy] {
                if (generation != sessionGeneration.load()) return;
                const auto verified = sdkVerifyLanProduct(selected, codeCopy);
                FlashforgeStatusResult status;
                if (verified.success) {
                    status = sdkLanStatus(selected, codeCopy);
                } else {
                    status.error = verified.error;
                    status.accessRequired = verified.accessRequired;
                }
                post([this, generation, selected, status = std::move(status)]() mutable {
                    if (generation == sessionGeneration.load()) {
                        handleStatusResult(std::move(status), true, generation, selected);
                    }
                });
            });
        } else if (device.connectMode == 1) {
            if (device.bindStatus == 0) {
                bindSelected(generation);
            } else {
                connectWan(generation);
            }
        } else {
            publishError(EventType::ConnectionChanged,
                         QStringLiteral("The printer reported an unsupported connection mode"));
        }
    }

    void connectWan(quint64 generation) {
        const FlashforgeDevice selected = device;
        enqueue([this, generation, selected] {
            if (generation != sessionGeneration.load()) return;
            auto result = sdkWanStatus(selected);
            post([this, generation, selected, result = std::move(result)]() mutable {
                if (generation != sessionGeneration.load()) {
                    return;
                }
                if (result.success) {
                    wanDeviceId = result.value.wanDeviceId;
                    device.wanDeviceId = wanDeviceId.toStdString();
                }
                FlashforgeStatusResult status;
                status.success = result.success;
                status.error = result.error;
                status.value = std::move(result.value.status);
                handleStatusResult(std::move(status), true, generation, selected);
            });
        });
    }

    void bindSelected(quint64 generation) {
        const FlashforgeDevice selected = device;
        enqueue([this, generation, selected] {
            if (generation != sessionGeneration.load()) return;
            const auto result = sdkBindDevice(selected);
            post([this, generation, selected, result] {
                if (generation != sessionGeneration.load()) {
                    return;
                }
                if (result.success) {
                    device.bindStatus = 1;
                    device.wanDeviceId = result.value.toStdString();
                    wanDeviceId = result.value;
                    refreshWanDevices([this, generation](bool success) {
                        if (success && generation == sessionGeneration.load()) {
                            connectWan(generation);
                        }
                    });
                } else {
                    publishError(EventType::ConnectionChanged, result.error, {}, false, 0, &selected);
                }
            });
        });
    }

    void handleStatusResult(FlashforgeStatusResult result,
                            bool connectionAttempt,
                            quint64 generation, const FlashforgeDevice& selected) {
        if (generation != sessionGeneration.load()) {
            return;
        }
        statusPending = false;
        if (!result.success) {
            if (connectionAttempt || connected) {
                resetConnectionSession();
                publishError(EventType::ConnectionChanged, result.error, {},
                             result.accessRequired, 0, &selected);
            }
            return;
        }
        jobId = QString::fromStdString(result.value.jobId);
        if (!connected) {
            connected = true;
            statusTimer->start();
            Event connection;
            connection.type = EventType::ConnectionChanged;
            connection.deviceId = selected.deviceId;
            connection.sessionId = selected.sessionId;
            connection.success = true;
            publish(connection);
        }
        Event event;
        event.type = EventType::StatusChanged;
        event.deviceId = selected.deviceId;
        event.sessionId = selected.sessionId;
        event.success = true;
        event.status = std::move(result.value);
        publish(event);
    }

    void requestStatus() {
        if (!connected || statusPending) {
            return;
        }
        statusPending = true;
        const quint64 generation = sessionGeneration.load();
        const FlashforgeDevice selected = device;
        const QString code = checkCode;
        enqueue([this, generation, selected, code] {
            if (generation != sessionGeneration.load()) return;
            FlashforgeStatusResult result;
            if (selected.connectMode == 0) {
                result = sdkLanStatus(selected, code);
            } else {
                auto wan = sdkWanStatus(selected);
                result.success = wan.success;
                result.error = wan.error;
                result.value = std::move(wan.value.status);
            }
            post([this, generation, selected, result = std::move(result)]() mutable {
                handleStatusResult(std::move(result), false, generation, selected);
            });
        });
    }

    void submitJob(const PrintJob& job) {
        if (job.formatId != "gcode-3mf") {
            publishError(EventType::JobCommandFinished,
                         QStringLiteral("Flashforge requires a gcode-3mf package"),
                         "send", false, job.requestId);
            return;
        }
        if (!connected) {
            publishError(EventType::JobCommandFinished,
                         QStringLiteral("No printer is connected"), "send", false, job.requestId);
            return;
        }
        if (!QFileInfo(QString::fromStdString(job.filePath)).isFile()) {
            publishError(EventType::JobCommandFinished,
                         QStringLiteral("The print file does not exist"), "send", false, job.requestId);
            return;
        }
        const quint64 generation = sessionGeneration.load();
        const FlashforgeDevice selected = device;
        const QString code = checkCode;
        enqueue([this, generation, selected, code, job] {
            if (generation != sessionGeneration.load()) return;
            const auto result = sdkSendJob(selected, code, job, generation);
            post([this, generation, selected, requestId = job.requestId, result] {
                if (generation != sessionGeneration.load()) {
                    return;
                }
                if (result.success && !result.value.isEmpty()) {
                    jobId = result.value;
                }
                Event event;
                event.type = EventType::JobCommandFinished;
                event.success = result.success;
                event.command = "send";
                event.requestId = requestId;
                event.deviceId = selected.deviceId;
                event.sessionId = selected.sessionId;
                event.outcomeUnknown = result.outcomeUnknown;
                event.error = result.error.toStdString();
                publish(event);
                if (result.success) {
                    requestStatus();
                }
            });
        });
    }

    void controlJob(const std::string& action, std::uint64_t requestId) {
        if (!connected || jobId.isEmpty()) {
            publishError(EventType::JobCommandFinished,
                         QStringLiteral("No active print job"), action, false, requestId);
            return;
        }
        const quint64 generation = sessionGeneration.load();
        const FlashforgeDevice selected = device;
        const QString code = checkCode;
        const QString currentJobId = jobId;
        const QString command = QString::fromStdString(action);
        enqueue([this, generation, selected, code, currentJobId, command, requestId] {
            if (generation != sessionGeneration.load()) return;
            const auto result = selected.connectMode == 0
                ? sdkControlLanJob(selected, code, currentJobId, command)
                : sdkControlWanJob(selected, currentJobId, command, generation);
            post([this, generation, selected, command, requestId, result] {
                if (generation != sessionGeneration.load()) {
                    return;
                }
                Event event;
                event.type = EventType::JobCommandFinished;
                event.success = result.success;
                event.command = command.toStdString();
                event.requestId = requestId;
                event.deviceId = selected.deviceId;
                event.sessionId = selected.sessionId;
                event.error = result.error.toStdString();
                publish(event);
                if (result.success) {
                    requestStatus();
                }
            });
        });
    }

    void setCameraStreamEnabled(bool enabled) {
        const std::string command = enabled ? "open" : "close";
        if (!connected) {
            publishError(EventType::CameraStreamCommandFinished,
                         QStringLiteral("No printer is connected"), command);
            return;
        }

        const quint64 generation = sessionGeneration.load();
        const FlashforgeDevice selected = device;
        enqueue([this, generation, selected, enabled, command] {
            if (generation != sessionGeneration.load()) return;
            FlashforgeVoidResult result;
            if (selected.connectMode == 0) {
                // FlashNetwork exposes camera stream control through its WAN
                // connection only. LAN detail already carries the local stream
                // URL, so opening/closing is a lifecycle no-op for LAN devices.
                result.success = true;
            } else {
                result = sdkControlWanCameraStream(selected, enabled, generation);
            }
            post([this, generation, selected, enabled, command, result] {
                if (generation != sessionGeneration.load()) {
                    return;
                }
                Event event;
                event.type = EventType::CameraStreamCommandFinished;
                event.deviceId = selected.deviceId;
                event.sessionId = selected.sessionId;
                event.success = result.success;
                event.command = command;
                event.error = result.error.toStdString();
                publish(event);
                if (result.success && enabled) {
                    requestStatus();
                }
            });
        });
    }

    struct SdkState;

    void sdkCreateState();
    FlashforgeAvailability sdkInitialize(const QString& libraryPath,
                                         const QString& settingsPath,
                                         const QString& logDirectory);
    void sdkRequestStop();
    void sdkShutdown();
    FlashforgeDeviceListResult sdkScanLan(int timeoutMs = 500);
    FlashforgeStringResult sdkLogin(const QString& username,
                                    const QString& password,
                                    const QString& language = QStringLiteral("zh"));
    FlashforgeVoidResult sdkLogout();
    FlashforgeDeviceListResult sdkWanDevices();
    FlashforgeStringResult sdkBindDevice(const FlashforgeDevice& selectedDevice);
    FlashforgeVoidResult sdkVerifyLanProduct(const FlashforgeDevice& selectedDevice,
                                             const QString& accessCode);
    FlashforgeStatusResult sdkLanStatus(const FlashforgeDevice& selectedDevice,
                                        const QString& accessCode);
    FlashforgeWanStatusResult sdkWanStatus(const FlashforgeDevice& selectedDevice);
    FlashforgeStringResult sdkSendJob(const FlashforgeDevice& selectedDevice,
                                      const QString& accessCode,
                                      const PrintJob& printJob, quint64 generation);
    FlashforgeVoidResult sdkControlLanJob(const FlashforgeDevice& selectedDevice,
                                          const QString& accessCode,
                                          const QString& activeJobId,
                                          const QString& action);
    FlashforgeVoidResult sdkControlWanJob(const FlashforgeDevice& selectedDevice,
                                          const QString& activeJobId,
                                          const QString& action, quint64 generation);
    FlashforgeVoidResult sdkControlWanCameraStream(
        const FlashforgeDevice& selectedDevice, bool enabled, quint64 generation);

    std::mutex ownerMutex;
    std::unique_ptr<QObject> ownerContext{std::make_unique<QObject>()};
    QObject* workerContext{nullptr};
    QThread* workerThread{new QThread()};
    // The vendor MQTT implementation may invoke callbacks after
    // fnet_freeConnection(). Keep this process-lifetime callback state alive.
    SdkState* sdkState{nullptr};
    std::unique_ptr<QTimer> statusTimer{std::make_unique<QTimer>()};
    EventSink sink;
    FlashforgeDevice device;
    QString checkCode;
    QString jobId;
    QString wanDeviceId;
    std::vector<FlashforgeDevice> lanDevices;
    std::vector<FlashforgeDevice> wanDevices;
    std::atomic<quint64> sessionGeneration{0};
    bool initializeRequested{false};
    bool available{false};
    bool authenticated{false};
    bool connected{false};
    bool scanPending{false};
    bool refreshRequested{false};
    bool statusPending{false};
    std::atomic_bool stopping{false};
};

// The optional FlashNetwork DLL loader and every native SDK call are compiled
// as part of FlashforgePrinterAgent's private implementation. This is not a
// separately consumable runtime layer.
namespace {

QString stringValue(const char* value) {
    return value ? QString::fromUtf8(value) : QString();
}

QString resultError(int code, const QString& fallback = {}) {
    if (!fallback.isEmpty()) {
        return fallback;
    }
    switch (code) {
    case FNET_OK: return {};
    case -2: return QStringLiteral("The printer rejected the LAN connection mode");
    case FNET_VERIFY_LAN_DEV_FAILED:
        return QStringLiteral("The printer check code is invalid");
    case FNET_UNAUTHORIZED:
        return QStringLiteral("The Flashforge account session is no longer authorized");
    case FNET_INVALID_VALIDATION:
        return QStringLiteral("The Flashforge account name or password is invalid");
    case FNET_DEVICE_HAS_BEEN_BOUND:
        return QStringLiteral("The printer has already been bound");
    case FNET_CONN_SEND_ERROR:
        return QStringLiteral("The printer connection could not send data");
    default:
        return QStringLiteral("FlashNetwork error %1").arg(code);
    }
}

FlashforgeDevice lanDevice(const fnet_lan_dev_info_t& info) {
    FlashforgeDevice device;
    device.deviceId = stringValue(info.serialNumber).toStdString();
    device.name = stringValue(info.name).toStdString();
    device.ip = stringValue(info.ip).toStdString();
    device.port = info.port;
    device.vid = info.vid;
    device.pid = info.pid;
    device.connectMode = info.connectMode;
    device.bindStatus = info.bindStatus;
    device.bindType = info.bindType;
    return device;
}

FlashforgeDevice wanDevice(const fnet_wan_dev_info_t& info) {
    FlashforgeDevice device;
    device.deviceId = stringValue(info.serialNumber).toStdString();
    device.name = stringValue(info.name).toStdString();
    device.model = stringValue(info.model).toStdString();
    device.wanDeviceId = stringValue(info.devId).toStdString();
    device.wanTopic = stringValue(info.devTopic).toStdString();
    const QString state = stringValue(info.status).trimmed().toLower();
    device.state = state.toStdString();
    device.connectMode = 1;
    device.bindStatus = 1;
    device.online = !state.isEmpty() && state != QStringLiteral("offline");
    return device;
}

PrinterCore::Status statusFromDetail(const fnet_dev_detail_t& value,
                                     const FlashforgeDevice& device) {
    PrinterCore::Status status;
    status.deviceId = device.deviceId;
    status.name = stringValue(value.name).toStdString();
    if (status.name.empty()) {
        status.name = device.name;
    }
    status.ip = stringValue(value.ipAddr).toStdString();
    if (status.ip.empty()) {
        status.ip = device.ip;
    }
    status.state = stringValue(value.status).toStdString();
    status.jobId = stringValue(value.jobId).toStdString();
    status.fileName = stringValue(value.printFileName).toStdString();
    status.errorCode = stringValue(value.errorCode).toStdString();
    status.cameraStreamUrl = stringValue(value.cameraStreamUrl).toStdString();
    status.cameraAvailable = value.camera == 1;
    status.progress = std::clamp(value.printProgress * 100.0, 0.0, 100.0);
    status.nozzleTemperature = value.nozzleCnt > 0 && value.nozzleTemps
        ? value.nozzleTemps[0] : value.rightTemp;
    status.targetNozzleTemperature = value.nozzleCnt > 0 && value.nozzleTargetTemps
        ? value.nozzleTargetTemps[0] : value.rightTargetTemp;
    status.bedTemperature = value.platTemp;
    status.targetBedTemperature = value.platTargetTemp;
    status.estimatedTimeSeconds = value.estimatedTime;
    status.printDurationSeconds = value.printDuration;
    status.currentLayer = value.printLayer;
    status.totalLayers = value.targetPrintLayer;
    return status;
}

QByteArray fileMd5(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}

bool extractThumbnail(const QString& packagePath, QTemporaryFile& output) {
    mz_zip_archive archive{};
    const QByteArray encodedPath = QFile::encodeName(packagePath);
    if (!mz_zip_reader_init_file(&archive, encodedPath.constData(), 0)) {
        return false;
    }
    const std::array<const char*, 7> candidates{
        "Auxiliaries/.thumbnails/thumbnail_middle.png",
        "Auxiliaries/.thumbnails/thumbnail_3mf.png",
        "Metadata/plate_1.png",
        "Metadata/thumbnail.png",
        "Metadata/bbl_thumbnail.png",
        "thumbnail.png",
        "3D/thumbnail.png",
    };
    bool extracted = false;
    for (const char* candidate : candidates) {
        size_t size = 0;
        void* bytes = mz_zip_reader_extract_file_to_heap(&archive, candidate, &size, 0);
        if (!bytes) {
            continue;
        }
        extracted = output.open()
            && output.write(static_cast<const char*>(bytes), static_cast<qint64>(size))
                == static_cast<qint64>(size);
        output.flush();
        mz_free(bytes);
        if (extracted) {
            break;
        }
    }
    mz_zip_reader_end(&archive);
    return extracted;
}

} // namespace

struct FlashforgePrinterAgent::Impl::SdkState {
    struct Symbols {
        decltype(&fnet_initlize) initialize{};
        decltype(&fnet_uninitlize) uninitialize{};
        decltype(&fnet_getVersion) getVersion{};
        decltype(&fnet_setUserAgent) setUserAgent{};
        decltype(&fnet_getLanDevList) getLanDeviceList{};
        decltype(&fnet_freeLanDevInfos) freeLanDeviceInfos{};
        decltype(&fnet_getLanDevProduct) getLanDeviceProduct{};
        decltype(&fnet_freeDevProduct) freeDeviceProduct{};
        decltype(&fnet_getLanDevDetail) getLanDeviceDetail{};
        decltype(&fnet_freeDevDetail) freeDeviceDetail{};
        decltype(&fnet_lanDevSendGcode) sendLanGcode{};
        decltype(&fnet_ctrlLanDevJob) controlLanJob{};
        decltype(&fnet_notifyLanDevWanBind) notifyLanDeviceWanBind{};
        decltype(&fnet_getTokenByPassword) getTokenByPassword{};
        decltype(&fnet_freeToken) freeToken{};
        decltype(&fnet_getUserProfile) getUserProfile{};
        decltype(&fnet_freeUserProfile) freeUserProfile{};
        decltype(&fnet_bindWanDev) bindWanDevice{};
        decltype(&fnet_freeBindData) freeBindData{};
        decltype(&fnet_getWanDevList) getWanDeviceList{};
        decltype(&fnet_freeWanDevList) freeWanDeviceList{};
        decltype(&fnet_getWanDevProductDetail) getWanDeviceProductDetail{};
        decltype(&fnet_wanDevSendGcodeClound) sendWanGcodeCloud{};
        decltype(&fnet_freeCloundGcodeData) freeCloudGcodeData{};
        decltype(&fnet_wanDevAddCloundJob) addWanCloudJob{};
        decltype(&fnet_freeAddCloudJobResults) freeAddCloudJobResults{};
        decltype(&fnet_getMqttConfig) getMqttConfig{};
        decltype(&fnet_freeMqttConfig) freeMqttConfig{};
        decltype(&fnet_createConnection) createConnection{};
        decltype(&fnet_freeConnection) freeConnection{};
        decltype(&fnet_connectionStop) stopConnection{};
        decltype(&fnet_connectionSend) sendConnection{};
        decltype(&fnet_connectionSubscribe) subscribeConnection{};
        decltype(&fnet_connectionSendMulti) sendConnectionMulti{};
        decltype(&fnet_freeWriteMultiResult) freeConnectionMultiResult{};
        decltype(&fnet_freeJobInfo) freeJobInfo{};
        decltype(&fnet_freeSyncLoginInfo) freeSyncLoginInfo{};
        decltype(&fnet_freeSyncBindInfo) freeSyncBindInfo{};
        decltype(&fnet_freeSyncOnlineInfo) freeSyncOnlineInfo{};
        decltype(&fnet_freeSliceState) freeSliceState{};
        decltype(&fnet_allocString) allocString{};
        decltype(&fnet_bindAccountRelp) bindAccountRelation{};
        decltype(&fnet_freeBindAccountRelpResult) freeBindAccountResult{};
        decltype(&fnet_signOut) signOut{};
        decltype(&fnet_freeString) freeString{};
    } api;

    template <typename T>
    bool resolve(T& target, const char* name, QString& error) {
        target = reinterpret_cast<T>(library->resolve(name));
        if (!target) {
            error = QStringLiteral("Missing FlashNetwork symbol: %1")
                        .arg(QString::fromLatin1(name));
            return false;
        }
        return true;
    }

    bool resolveRequired(QString& error) {
        return resolve(api.initialize, "fnet_initlize", error)
            && resolve(api.uninitialize, "fnet_uninitlize", error)
            && resolve(api.getVersion, "fnet_getVersion", error)
            && resolve(api.setUserAgent, "fnet_setUserAgent", error)
            && resolve(api.getLanDeviceList, "fnet_getLanDevList", error)
            && resolve(api.freeLanDeviceInfos, "fnet_freeLanDevInfos", error)
            && resolve(api.getLanDeviceProduct, "fnet_getLanDevProduct", error)
            && resolve(api.freeDeviceProduct, "fnet_freeDevProduct", error)
            && resolve(api.getLanDeviceDetail, "fnet_getLanDevDetail", error)
            && resolve(api.freeDeviceDetail, "fnet_freeDevDetail", error)
            && resolve(api.sendLanGcode, "fnet_lanDevSendGcode", error)
            && resolve(api.controlLanJob, "fnet_ctrlLanDevJob", error)
            && resolve(api.notifyLanDeviceWanBind, "fnet_notifyLanDevWanBind", error)
            && resolve(api.getTokenByPassword, "fnet_getTokenByPassword", error)
            && resolve(api.freeToken, "fnet_freeToken", error)
            && resolve(api.getUserProfile, "fnet_getUserProfile", error)
            && resolve(api.freeUserProfile, "fnet_freeUserProfile", error)
            && resolve(api.bindWanDevice, "fnet_bindWanDev", error)
            && resolve(api.freeBindData, "fnet_freeBindData", error)
            && resolve(api.getWanDeviceList, "fnet_getWanDevList", error)
            && resolve(api.freeWanDeviceList, "fnet_freeWanDevList", error)
            && resolve(api.getWanDeviceProductDetail, "fnet_getWanDevProductDetail", error)
            && resolve(api.sendWanGcodeCloud, "fnet_wanDevSendGcodeClound", error)
            && resolve(api.freeCloudGcodeData, "fnet_freeCloundGcodeData", error)
            && resolve(api.addWanCloudJob, "fnet_wanDevAddCloundJob", error)
            && resolve(api.freeAddCloudJobResults, "fnet_freeAddCloudJobResults", error)
            && resolve(api.getMqttConfig, "fnet_getMqttConfig", error)
            && resolve(api.freeMqttConfig, "fnet_freeMqttConfig", error)
            && resolve(api.createConnection, "fnet_createConnection", error)
            && resolve(api.freeConnection, "fnet_freeConnection", error)
            && resolve(api.stopConnection, "fnet_connectionStop", error)
            && resolve(api.sendConnection, "fnet_connectionSend", error)
            && resolve(api.subscribeConnection, "fnet_connectionSubscribe", error)
            && resolve(api.sendConnectionMulti, "fnet_connectionSendMulti", error)
            && resolve(api.freeConnectionMultiResult, "fnet_freeWriteMultiResult", error)
            && resolve(api.freeJobInfo, "fnet_freeJobInfo", error)
            && resolve(api.freeSyncLoginInfo, "fnet_freeSyncLoginInfo", error)
            && resolve(api.freeSyncBindInfo, "fnet_freeSyncBindInfo", error)
            && resolve(api.freeSyncOnlineInfo, "fnet_freeSyncOnlineInfo", error)
            && resolve(api.freeSliceState, "fnet_freeSliceState", error)
            && resolve(api.allocString, "fnet_allocString", error)
            && resolve(api.bindAccountRelation, "fnet_bindAccountRelp", error)
            && resolve(api.freeBindAccountResult, "fnet_freeBindAccountRelpResult", error)
            && resolve(api.signOut, "fnet_signOut", error)
            && resolve(api.freeString, "fnet_freeString", error);
    }

    bool ready(QString& error) const {
        if (!initialized) {
            error = QStringLiteral("FlashNetwork is not initialized");
            return false;
        }
        return true;
    }

    struct UploadCallback {
        std::weak_ptr<Impl> owner;
        quint64 generation;
        std::uint64_t requestId;
        std::string deviceId;
        std::uint64_t sessionId;
    };

    // FlashNetwork treats callbackData as opaque. Never hand it an address of
    // request storage: callbacks may arrive after the SDK operation returns.
    // IDs are never reused, and retired IDs cancel without dereferencing memory.
    struct UploadRegistry {
        std::mutex mutex;
        std::uintptr_t nextId{1};
        std::map<std::uintptr_t, std::shared_ptr<const UploadCallback>> callbacks;
    };

    static UploadRegistry& uploads() {
        // One fixed lifetime anchor must outlive detached vendor callbacks even
        // during process teardown. Per-request entries are always reclaimed.
        static auto* registry = new UploadRegistry();
        return *registry;
    }

    struct UploadRegistration {
        explicit UploadRegistration(UploadCallback callback) {
            auto& registry = uploads();
            std::lock_guard<std::mutex> lock(registry.mutex);
            if (registry.nextId == std::numeric_limits<std::uintptr_t>::max())
                throw std::overflow_error("Flashforge upload callback IDs exhausted");
            id = registry.nextId++;
            registry.callbacks.emplace(id, std::make_shared<const UploadCallback>(std::move(callback)));
        }
        UploadRegistration(const UploadRegistration&) = delete;
        UploadRegistration& operator=(const UploadRegistration&) = delete;
        ~UploadRegistration() { reset(); }
        void reset() {
            if (!id) return;
            auto& registry = uploads();
            std::lock_guard<std::mutex> lock(registry.mutex);
            registry.callbacks.erase(id);
            id = 0;
        }
        void* data() const { return reinterpret_cast<void*>(id); }
        std::uintptr_t id{0};
    };

    static int progress(long long now, long long total, void* data) {
        std::shared_ptr<const UploadCallback> upload;
        {
            auto& registry = uploads();
            std::lock_guard<std::mutex> lock(registry.mutex);
            const auto found = registry.callbacks.find(reinterpret_cast<std::uintptr_t>(data));
            if (found == registry.callbacks.end()) return 1;
            upload = found->second;
        }
        auto self = upload->owner.lock();
        if (!self || self->stopping) return 1;
        Event event;
        event.type = EventType::TransferProgress;
        event.requestId = upload->requestId;
        event.deviceId = upload->deviceId;
        event.sessionId = upload->sessionId;
        event.progress = total > 0 ? static_cast<double>(now) / total : 0.0;
        self->post([weak = upload->owner, generation = upload->generation, event] {
            if (auto owner = weak.lock(); owner && generation == owner->sessionGeneration.load())
                owner->publish(event);
        });
        return self->stopping ? 1 : 0;
    }

    static void connectionStatus(fnet_conn_status_t status, void* data) {
        static_cast<SdkState*>(data)->wanConnected.store(status == FNET_CONN_STATUS_CONNECTED);
    }

    static int updateClientId(const char** clientId, void* data) {
        auto* self = static_cast<SdkState*>(data);
        *clientId = self->api.allocString(self->clientId.constData(), self->clientId.size());
        return *clientId ? FNET_OK : FNET_ERROR;
    }

    static void readConnection(fnet_conn_read_data_t* read, void* data) {
        auto* self = static_cast<SdkState*>(data);
        if (!read) {
            return;
        }
        if ((read->type == FNET_CONN_READ_SYS_NOTIFY
             || read->type == FNET_CONN_READ_UPDATE_NOTIFY
             || read->type == FNET_CONN_READ_DEVICE_KEEP_ALIVE) && read->data) {
            self->api.freeString(static_cast<char*>(read->data));
        } else if (read->type == FNET_CONN_READ_SYNC_LOGIN && read->data) {
            self->api.freeSyncLoginInfo(static_cast<fnet_sync_login_info_t*>(read->data));
        } else if ((read->type == FNET_CONN_READ_SYNC_BIND_DEVICE
                    || read->type == FNET_CONN_READ_SYNC_UNBIND_DEVICE) && read->data) {
            self->api.freeSyncBindInfo(static_cast<fnet_sync_bind_info_t*>(read->data));
        } else if ((read->type == FNET_CONN_READ_SYNC_ONLINE
                    || read->type == FNET_CONN_READ_SYNC_OFFLINE) && read->data) {
            self->api.freeSyncOnlineInfo(static_cast<fnet_sync_online_info_t*>(read->data));
        } else if (read->type == FNET_CONN_READ_DEVICE_DETAIL && read->data) {
            self->api.freeDeviceDetail(static_cast<fnet_dev_detail_t*>(read->data));
        } else if (read->type == FNET_CONN_READ_SLICE_STATE && read->data) {
            self->api.freeSliceState(static_cast<fnet_slice_state_t*>(read->data));
        } else if ((read->type == FNET_CONN_READ_JOB_DOWNLOAD
                    || read->type == FNET_CONN_READ_JOB_UNZIP) && read->data) {
            self->api.freeJobInfo(static_cast<fnet_job_info_t*>(read->data));
        }
        if (read->topic) {
            self->api.freeString(read->topic);
        }
    }

    void closeWanConnection() {
        void* connection = std::exchange(wanConnection, nullptr);
        wanConnected.store(false);
        if (connection && api.stopConnection && api.freeConnection) {
            api.stopConnection(connection);
            api.freeConnection(connection);
        }
    }

    bool ensureWanConnection(QString& error) {
        if (wanConnection && wanConnected.load()) {
            return true;
        }
        closeWanConnection();
        if (accessToken.isEmpty()) {
            error = QStringLiteral("Flashforge account login is required");
            return false;
        }

        fnet_mqtt_config_t* config = nullptr;
        const int configResult = api.getMqttConfig(
            clientId.constData(), accessToken.constData(), &config, 15000);
        if (configResult != FNET_OK || !config) {
            if (config) {
                api.freeMqttConfig(config);
            }
            error = resultError(configResult,
                configResult == FNET_OK
                    ? QStringLiteral("Flashforge returned an empty MQTT configuration")
                    : QString());
            return false;
        }
        const QByteArray userTopic = config->userTopic
            ? QByteArray(config->userTopic) : QByteArray();
        QList<QByteArray> commonTopics;
        for (int i = 0; i < config->commonTopicCnt; ++i) {
            if (config->commonTopics[i]) {
                commonTopics.push_back(QByteArray(config->commonTopics[i]));
            }
        }
        api.freeMqttConfig(config);

        const fnet_conn_settings_t settings{
            clientId.constData(), &SdkState::connectionStatus, this,
            &SdkState::updateClientId, this, &SdkState::readConnection, this,
        };
        void* connection = nullptr;
        const int createResult = api.createConnection(&connection, &settings);
        if (createResult != FNET_OK || !connection) {
            wanConnected.store(false);
            error = resultError(createResult,
                QStringLiteral("Unable to create the Flashforge WAN connection"));
            return false;
        }
        wanConnection = connection;
        for (int i = 0; i < 50 && !wanConnected.load() && !stopping.load(); ++i) {
            QThread::msleep(100);
        }
        if (!wanConnected.load()) {
            error = QStringLiteral("Flashforge WAN connection timed out");
            closeWanConnection();
            return false;
        }

        QList<QByteArray> allTopics = commonTopics;
        if (!userTopic.isEmpty()) {
            allTopics.prepend(userTopic);
        }
        QList<const char*> pointers;
        pointers.reserve(allTopics.size());
        for (const QByteArray& topic : allTopics) {
            pointers.push_back(topic.constData());
        }
        if (!pointers.isEmpty()) {
            const fnet_conn_subscribe_data_t subscribe{
                pointers.data(), static_cast<int>(pointers.size())};
            const int subscribeResult = api.subscribeConnection(wanConnection, &subscribe);
            if (subscribeResult != FNET_OK) {
                error = resultError(subscribeResult);
                closeWanConnection();
                return false;
            }
        }
        if (!userTopic.isEmpty()) {
            const fnet_conn_write_data_t syncLogin{
                FNET_CONN_WRITE_SYNC_LOGIN, clientId.constData(), userTopic.constData(), 1,
            };
            const int syncResult = api.sendConnection(wanConnection, &syncLogin);
            if (syncResult != FNET_OK) {
                error = resultError(syncResult);
                closeWanConnection();
                return false;
            }
        }
        return true;
    }

    bool updateAndSubscribeDevices(const std::vector<FlashforgeDevice>& devices,
                                   QString& error) {
        if (devices.empty()) {
            return true;
        }
        if (!ensureWanConnection(error)) {
            return false;
        }
        QList<QByteArray> topicStorage;
        for (const auto& device : devices) {
            if (!device.wanTopic.empty()) {
                topicStorage.push_back(QByteArray::fromStdString(device.wanTopic));
            }
        }
        if (topicStorage.empty()) {
            return true;
        }
        QList<const char*> topics;
        topics.reserve(topicStorage.size());
        for (const QByteArray& topic : topicStorage) {
            topics.push_back(topic.constData());
        }
        const void* empty = nullptr;
        const fnet_conn_write_multi_data_t update{
            FNET_CONN_WRITE_UPDATE_DETAIL, &empty, topics.data(), 1,
            static_cast<int>(topics.size()), 1,
        };
        fnet_conn_write_multi_result_t* updateResult = nullptr;
        int result = api.sendConnectionMulti(wanConnection, &update, &updateResult);
        if (result == FNET_OK && updateResult && updateResult->failedCnt > 0) {
            result = FNET_CONN_SEND_ERROR;
        }
        if (updateResult) {
            api.freeConnectionMultiResult(updateResult);
        }
        if (result != FNET_OK) {
            error = resultError(result);
            return false;
        }
        const fnet_conn_subscribe_data_t subscribe{
            topics.data(), static_cast<int>(topics.size())};
        result = api.subscribeConnection(wanConnection, &subscribe);
        if (result != FNET_OK) {
            error = resultError(result);
            return false;
        }
        return true;
    }

    std::unique_ptr<QLibrary> library;
    QByteArray accessToken;
    QByteArray clientId;
    void* wanConnection{nullptr};
    std::atomic_bool wanConnected{false};
    std::atomic_bool stopping{false};
    bool initialized{false};
};

void FlashforgePrinterAgent::Impl::sdkCreateState() {
    sdkState = new SdkState();
}

FlashforgeAvailability FlashforgePrinterAgent::Impl::sdkInitialize(
    const QString& libraryPath,
    const QString& settingsPath,
    const QString& logDirectory) {
    if (sdkState->initialized) {
        return {true, stringValue(sdkState->api.getVersion()), {}};
    }
    if (!QFileInfo::exists(libraryPath)) {
        return {false, {}, QStringLiteral("%1 was not found next to the application executable")
            .arg(QFileInfo(libraryPath).fileName())};
    }
    if (!QFileInfo::exists(settingsPath)) {
        return {false, {}, QStringLiteral("%1 was not found next to the application executable")
            .arg(QFileInfo(settingsPath).fileName())};
    }
    if (stopping) return {false, {}, QStringLiteral("Printer agent is stopping")};
    sdkState->library = std::make_unique<QLibrary>(libraryPath);
    sdkState->library->setLoadHints(QLibrary::PreventUnloadHint);
    if (!sdkState->library->load()) {
        return {false, {}, sdkState->library->errorString()};
    }
    QString error;
    if (!sdkState->resolveRequired(error)) {
        sdkState->library->unload();
        sdkState->library.reset();
        return {false, {}, error};
    }
    QDir().mkpath(logDirectory);
    const QByteArray settings = QFile::encodeName(settingsPath);
    const QByteArray logs = QFile::encodeName(logDirectory);
    const fnet_log_settings_t logSettings{logs.constData(), 72, FNET_LOG_LEVEL_OFF};
    const int result = sdkState->api.initialize(settings.constData(), &logSettings);
    if (result != FNET_OK) {
        sdkState->library->unload();
        sdkState->library.reset();
        return {false, {}, resultError(result, QStringLiteral("fnet_initlize failed"))};
    }
    sdkState->initialized = true;
    sdkState->api.setUserAgent("GPlatform/1.0 (Flashforge)");
    sdkState->clientId = QStringLiteral("pc_gplatform_%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128)).toUtf8();
    return {true, stringValue(sdkState->api.getVersion()), {}};
}

void FlashforgePrinterAgent::Impl::sdkShutdown() {
    sdkState->stopping.store(true);
    sdkState->closeWanConnection();
    sdkState->accessToken.clear();
    // The vendor MQTT implementation detaches its reconnect thread. Its
    // callbacks retain the supplied context even after freeConnection()
    // returns, so unloading the dylib or deleting SdkState here is unsafe. Both
    // intentionally remain process-lifetime resources and are reclaimed by
    // the operating system at process exit.
}

void FlashforgePrinterAgent::Impl::sdkRequestStop() {
    sdkState->stopping.store(true);
}

FlashforgeDeviceListResult FlashforgePrinterAgent::Impl::sdkScanLan(int timeoutMs) {
    FlashforgeDeviceListResult result;
    if (!sdkState->ready(result.error)) {
        return result;
    }
    fnet_lan_dev_info_t* infos = nullptr;
    int count = 0;
    const int code = sdkState->api.getLanDeviceList(&infos, &count, timeoutMs);
    if (code == FNET_OK) {
        result.value.reserve(count);
        for (int i = 0; i < count; ++i) {
            result.value.push_back(lanDevice(infos[i]));
        }
        result.success = true;
    } else {
        result.error = resultError(code);
    }
    if (infos) {
        sdkState->api.freeLanDeviceInfos(infos);
    }
    return result;
}

FlashforgeStringResult FlashforgePrinterAgent::Impl::sdkLogin(
    const QString& username,
    const QString& password,
    const QString& language) {
    FlashforgeStringResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    sdkState->closeWanConnection();
    sdkState->accessToken.clear();
    const QByteArray user = username.toUtf8();
    const QByteArray secret = password.toUtf8();
    const QByteArray locale = language.toUtf8();
    fnet_token_data_t* token = nullptr;
    char* message = nullptr;
    const int code = sdkState->api.getTokenByPassword(
        user.constData(), secret.constData(), locale.constData(), &token, &message, 10000);
    const QString serverMessage = stringValue(message);
    if (message) {
        sdkState->api.freeString(message);
    }
    if (code != FNET_OK || !token || !token->accessToken) {
        if (token) {
            sdkState->api.freeToken(token);
        }
        output.error = resultError(code, serverMessage);
        return output;
    }
    sdkState->accessToken = QByteArray(token->accessToken);
    sdkState->api.freeToken(token);

    fnet_user_profile_t* profile = nullptr;
    const int profileCode = sdkState->api.getUserProfile(
        sdkState->accessToken.constData(), &profile, 10000);
    QString accountName = username;
    QByteArray accountEmail = user;
    if (profileCode == FNET_OK && profile) {
        accountName = stringValue(profile->nickname);
        if (accountName.isEmpty()) {
            accountName = stringValue(profile->email);
        }
        if (profile->email) {
            accountEmail = QByteArray(profile->email);
        }
    }
    if (profile) {
        sdkState->api.freeUserProfile(profile);
    }

    fnet_bind_account_relp_result_t* bindResult = nullptr;
    const int bindCode = sdkState->api.bindAccountRelation(
        sdkState->clientId.constData(), sdkState->accessToken.constData(),
        accountEmail.constData(), &bindResult, 10000);
    if (bindResult) {
        sdkState->api.freeBindAccountResult(bindResult);
    }
    if (bindCode != FNET_OK) {
        sdkState->accessToken.clear();
        output.error = resultError(bindCode);
        return output;
    }
    QString connectionError;
    if (!sdkState->ensureWanConnection(connectionError)) {
        sdkState->accessToken.clear();
        output.error = connectionError;
        return output;
    }
    output.success = true;
    output.value = accountName;
    return output;
}

FlashforgeVoidResult FlashforgePrinterAgent::Impl::sdkLogout() {
    FlashforgeVoidResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    sdkState->closeWanConnection();
    int code = FNET_OK;
    if (!sdkState->accessToken.isEmpty()) {
        code = sdkState->api.signOut(sdkState->accessToken.constData(), 7000);
    }
    sdkState->accessToken.clear();
    output.success = code == FNET_OK;
    output.error = resultError(code);
    return output;
}

FlashforgeDeviceListResult FlashforgePrinterAgent::Impl::sdkWanDevices() {
    FlashforgeDeviceListResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    if (sdkState->accessToken.isEmpty()) {
        output.error = QStringLiteral("Flashforge account login is required");
        return output;
    }
    fnet_wan_dev_info_t* infos = nullptr;
    int count = 0;
    const int code = sdkState->api.getWanDeviceList(
        sdkState->clientId.constData(), sdkState->accessToken.constData(),
        31, 15, &infos, &count, 15000);
    if (code == FNET_OK) {
        output.value.reserve(count);
        for (int i = 0; i < count; ++i) {
            output.value.push_back(wanDevice(infos[i]));
        }
        output.success = true;
    } else {
        output.error = resultError(code);
    }
    if (infos) {
        sdkState->api.freeWanDeviceList(infos, count);
    }
    if (output.success) {
        QString subscribeError;
        if (!sdkState->updateAndSubscribeDevices(output.value, subscribeError)) {
            output.success = false;
            output.error = subscribeError;
        }
    }
    return output;
}

FlashforgeStringResult FlashforgePrinterAgent::Impl::sdkBindDevice(const FlashforgeDevice& device) {
    FlashforgeStringResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    if (sdkState->accessToken.isEmpty()) {
        output.error = QStringLiteral("Flashforge account login is required before binding a printer");
        return output;
    }
    const QByteArray serial = QByteArray::fromStdString(device.deviceId);
    const QByteArray name = QByteArray::fromStdString(device.name);
    fnet_wan_dev_bind_data_t* bindData = nullptr;
    const int code = sdkState->api.bindWanDevice(
        sdkState->clientId.constData(), sdkState->accessToken.constData(), serial.constData(),
        device.pid, name.constData(), device.bindType, &bindData, 15000);
    if (code != FNET_OK || !bindData || !bindData->devId) {
        if (bindData) {
            sdkState->api.freeBindData(bindData);
        }
        output.error = resultError(code, code == FNET_OK
            ? QStringLiteral("Flashforge returned an empty binding result") : QString());
        return output;
    }
    output.value = stringValue(bindData->devId);
    sdkState->api.freeBindData(bindData);
    const QByteArray ip = QByteArray::fromStdString(device.ip);
    const int notifyCode = sdkState->api.notifyLanDeviceWanBind(
        ip.constData(), device.port, serial.constData(), 5000);
    if (notifyCode != FNET_OK) {
        output.error = resultError(notifyCode,
            QStringLiteral("The account binding succeeded, but the printer could not be notified; rescan the device"));
        return output;
    }
    output.success = true;
    return output;
}

FlashforgeVoidResult FlashforgePrinterAgent::Impl::sdkVerifyLanProduct(
    const FlashforgeDevice& device, const QString& checkCode) {
    FlashforgeVoidResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    const QByteArray ip = QByteArray::fromStdString(device.ip);
    const QByteArray serial = QByteArray::fromStdString(device.deviceId);
    const QByteArray check = checkCode.toUtf8();
    fnet_dev_product_t* product = nullptr;
    const int code = sdkState->api.getLanDeviceProduct(
        ip.constData(), device.port, serial.constData(), check.constData(), &product, 5000);
    output.success = code == FNET_OK && product;
    output.accessRequired = code == FNET_VERIFY_LAN_DEV_FAILED;
    output.error = output.success ? QString() : resultError(code,
        code == FNET_OK ? QStringLiteral("The printer product response was empty") : QString());
    if (product) {
        sdkState->api.freeDeviceProduct(product);
    }
    return output;
}

FlashforgeStatusResult FlashforgePrinterAgent::Impl::sdkLanStatus(
    const FlashforgeDevice& device, const QString& checkCode) {
    FlashforgeStatusResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    const QByteArray ip = QByteArray::fromStdString(device.ip);
    const QByteArray serial = QByteArray::fromStdString(device.deviceId);
    const QByteArray check = checkCode.toUtf8();
    fnet_dev_detail_t* detail = nullptr;
    const int code = sdkState->api.getLanDeviceDetail(
        ip.constData(), device.port, serial.constData(), check.constData(), &detail, 5000);
    if (code == FNET_OK && detail) {
        output.value = statusFromDetail(*detail, device);
        output.success = true;
    } else {
        output.accessRequired = code == FNET_VERIFY_LAN_DEV_FAILED;
        output.error = resultError(code, code == FNET_OK
            ? QStringLiteral("The printer detail response was empty") : QString());
    }
    if (detail) {
        sdkState->api.freeDeviceDetail(detail);
    }
    return output;
}

FlashforgeWanStatusResult FlashforgePrinterAgent::Impl::sdkWanStatus(const FlashforgeDevice& device) {
    FlashforgeWanStatusResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    if (sdkState->accessToken.isEmpty()) {
        output.error = QStringLiteral("Flashforge account login is required for WAN devices");
        return output;
    }
    QByteArray wanId = QByteArray::fromStdString(device.wanDeviceId);
    if (wanId.isEmpty()) {
        const auto list = sdkWanDevices();
        if (!list.success) {
            output.error = list.error;
            return output;
        }
        for (const auto& value : list.value) {
            if (value.deviceId == device.deviceId) {
                wanId = QByteArray::fromStdString(value.wanDeviceId);
                break;
            }
        }
    }
    if (wanId.isEmpty()) {
        output.error = QStringLiteral("The printer is not present in the signed-in Flashforge account");
        return output;
    }
    fnet_dev_product_t* product = nullptr;
    fnet_dev_detail_t* detail = nullptr;
    const int code = sdkState->api.getWanDeviceProductDetail(
        sdkState->clientId.constData(), sdkState->accessToken.constData(), wanId.constData(),
        &product, &detail, 15000);
    if (code == FNET_OK && detail) {
        output.value.status = statusFromDetail(*detail, device);
        output.value.wanDeviceId = QString::fromUtf8(wanId);
        output.success = true;
    } else {
        output.error = resultError(code, code == FNET_OK
            ? QStringLiteral("The WAN printer detail response was empty") : QString());
    }
    if (product) {
        sdkState->api.freeDeviceProduct(product);
    }
    if (detail) {
        sdkState->api.freeDeviceDetail(detail);
    }
    return output;
}

FlashforgeStringResult FlashforgePrinterAgent::Impl::sdkSendJob(
    const FlashforgeDevice& device, const QString& checkCode,
    const PrinterCore::PrintJob& job, quint64 generation) {
    FlashforgeStringResult output;
    if (stopping || generation != sessionGeneration.load()) {
        output.error = QStringLiteral("The printer selection changed or the agent is stopping");
        return output;
    }
    if (!sdkState->ready(output.error)) {
        return output;
    }
    const QString packagePath = QString::fromStdString(job.filePath);
    const QFileInfo packageInfo(packagePath);
    if (!packageInfo.isFile()) {
        output.error = QStringLiteral("The print file does not exist");
        return output;
    }
    QByteArray destination = QByteArray::fromStdString(job.destinationName);
    if (destination.isEmpty()) {
        destination = packageInfo.fileName().toUtf8();
    }
    const QByteArray filePath = QFile::encodeName(packagePath);

    if (device.connectMode == 0) {
        const QByteArray ip = QByteArray::fromStdString(device.ip);
        const QByteArray serial = QByteArray::fromStdString(device.deviceId);
        const QByteArray check = checkCode.toUtf8();
        SdkState::UploadRegistration callback({
            shared_from_this(), generation, job.requestId, device.deviceId, device.sessionId});
        const fnet_send_gcode_data_t data{
            filePath.constData(), nullptr, destination.constData(), job.printNow,
            job.levelingBeforePrint, job.flowCalibration, job.firstLayerInspection,
            job.timeLapseVideo, job.useMaterialStation, 0, nullptr,
            &SdkState::progress, callback.data(),
        };
        const int code = sdkState->api.sendLanGcode(
            ip.constData(), device.port, serial.constData(), check.constData(), &data, 15000);
        output.success = code == FNET_OK;
        output.outcomeUnknown = !output.success;
        output.error = resultError(code);
        return output;
    }

    if (sdkState->accessToken.isEmpty()) {
        output.error = QStringLiteral("Flashforge account login is required for WAN printing");
        return output;
    }
    const QByteArray wanId = QByteArray::fromStdString(device.wanDeviceId);
    const QByteArray serial = QByteArray::fromStdString(device.deviceId);
    const QByteArray topic = QByteArray::fromStdString(device.wanTopic);
    if (wanId.isEmpty() || topic.isEmpty()) {
        output.error = QStringLiteral("The WAN printer identity is incomplete; refresh the account device list");
        return output;
    }
    QString connectionError;
    if (!sdkState->ensureWanConnection(connectionError)) {
        output.error = connectionError;
        return output;
    }

    QTemporaryFile thumbnail(QDir::tempPath() + QStringLiteral("/gplatform-print-XXXXXX.png"));
    thumbnail.setAutoRemove(true);
    if (!extractThumbnail(packagePath, thumbnail)) {
        output.error = QStringLiteral("The sliced 3MF does not contain a usable print thumbnail");
        return output;
    }
    if (stopping || generation != sessionGeneration.load()) {
        output.error = QStringLiteral("Printer agent is stopping");
        return output;
    }
    const QByteArray thumbPath = QFile::encodeName(thumbnail.fileName());
    SdkState::UploadRegistration callback({
        shared_from_this(), generation, job.requestId, device.deviceId, device.sessionId});
    const fnet_send_gcode_data_t upload{
        filePath.constData(), thumbPath.constData(), destination.constData(), job.printNow,
        job.levelingBeforePrint, job.flowCalibration, job.firstLayerInspection,
        job.timeLapseVideo, job.useMaterialStation, 0, nullptr,
        &SdkState::progress, callback.data(),
    };
    fnet_clound_gcode_data_t* cloud = nullptr;
    output.outcomeUnknown = true;
    const int uploadCode = sdkState->api.sendWanGcodeCloud(
        sdkState->clientId.constData(), sdkState->accessToken.constData(),
        &upload, &cloud, 60000);
    callback.reset();
    if (uploadCode != FNET_OK || !cloud) {
        if (cloud) {
            sdkState->api.freeCloudGcodeData(cloud);
        }
        output.error = resultError(uploadCode, uploadCode == FNET_OK
            ? QStringLiteral("Flashforge returned an empty cloud upload result") : QString());
        return output;
    }

    if (stopping || generation != sessionGeneration.load()) {
        sdkState->api.freeCloudGcodeData(cloud);
        output.error = QStringLiteral("Upload interrupted during shutdown; remote outcome is unknown");
        return output;
    }
    const QByteArray gcodeMd5 = fileMd5(packagePath);
    const QByteArray thumbMd5 = fileMd5(thumbnail.fileName());
    const QByteArray gcodeType = packageInfo.suffix().toLower().toUtf8();
    const QByteArray thumbType("png");
    // Orca sends the destination print name here even though thumbType is png.
    const QByteArray thumbName = destination;
    const QByteArray bucket = cloud->bucketName ? QByteArray(cloud->bucketName) : QByteArray();
    const QByteArray endpoint = cloud->endpoint ? QByteArray(cloud->endpoint) : QByteArray();
    const QByteArray gcodeKey = cloud->gcodeStorageKey ? QByteArray(cloud->gcodeStorageKey) : QByteArray();
    const QByteArray gcodeUrl = cloud->gcodeStorageUrl ? QByteArray(cloud->gcodeStorageUrl) : QByteArray();
    const QByteArray thumbKey = cloud->thumbStorageKey ? QByteArray(cloud->thumbStorageKey) : QByteArray();
    const QByteArray thumbUrl = cloud->thumbStorageUrl ? QByteArray(cloud->thumbStorageUrl) : QByteArray();
    sdkState->api.freeCloudGcodeData(cloud);

    const char* devIds[] = {wanId.constData()};
    fnet_clound_job_data_t jobData{
        devIds, nullptr, nullptr, 1,
        destination.constData(), gcodeType.constData(), gcodeMd5.constData(), packageInfo.size(),
        thumbName.constData(), thumbType.constData(), thumbMd5.constData(),
        QFileInfo(thumbnail.fileName()).size(), bucket.constData(), endpoint.constData(),
        gcodeKey.constData(), gcodeUrl.constData(), thumbKey.constData(), thumbUrl.constData(),
        "", 0, upload.printNow, upload.levelingBeforePrint, upload.flowCalibration,
        upload.firstLayerInspection, upload.timeLapseVideo, upload.useMatlStation,
        0, nullptr,
    };
    fnet_add_clound_job_result_t* results = nullptr;
    int resultCount = 0;
    if (stopping || generation != sessionGeneration.load()) {
        output.error = QStringLiteral("Upload interrupted during shutdown; remote outcome is unknown");
        return output;
    }
    const int addCode = sdkState->api.addWanCloudJob(
        sdkState->clientId.constData(), sdkState->accessToken.constData(),
        &jobData, &results, &resultCount, 30000);
    if (addCode != FNET_OK || !results || resultCount != 1
        || results[0].error != FNET_ADD_CLOUND_JOB_OK || !results[0].jobId) {
        if (addCode == FNET_OK && results && resultCount == 1) {
            switch (results[0].error) {
            case FNET_ADD_CLOUND_JOB_DEVICE_BUSY:
                output.error = QStringLiteral("The Flashforge printer is busy or offline");
                break;
            case FNET_ADD_CLOUND_JOB_DEVICE_NOT_FOUND:
                output.error = QStringLiteral("The Flashforge printer was not found");
                break;
            case FNET_ADD_CLOUND_JOB_SERVER_INTERNAL_ERROR:
                output.error = QStringLiteral("The Flashforge cloud service rejected the print job");
                break;
            default:
                output.error = QStringLiteral("Flashforge rejected the cloud job (%1)")
                    .arg(results[0].error);
                break;
            }
        } else {
            output.error = resultError(addCode, addCode == FNET_OK
                ? QStringLiteral("Flashforge returned an invalid cloud job result") : QString());
        }
        if (results) {
            sdkState->api.freeAddCloudJobResults(results, resultCount);
        }
        return output;
    }

    const QByteArray returnedDevId = results[0].devId
        ? QByteArray(results[0].devId) : QByteArray();
    const QByteArray jobId(results[0].jobId);
    if (returnedDevId.isEmpty() || returnedDevId != wanId) {
        sdkState->api.freeAddCloudJobResults(results, resultCount);
        output.error = QStringLiteral(
            "Flashforge returned a cloud job for an unexpected printer");
        return output;
    }
    if (stopping || generation != sessionGeneration.load()) {
        sdkState->api.freeAddCloudJobResults(results, resultCount);
        output.error = QStringLiteral("Cloud job created during shutdown; remote outcome is unknown");
        return output;
    }
    const char* returnedDevIds[] = {returnedDevId.constData()};
    const char* serials[] = {serial.constData()};
    const char* jobIds[] = {jobId.constData()};
    jobData.devIds = returnedDevIds;
    jobData.devSerialNumbers = serials;
    jobData.jobIds = jobIds;
    const void* jobPointers[] = {&jobData};
    const char* topics[] = {topic.constData()};
    const fnet_conn_write_multi_data_t start{
        FNET_CONN_WRITE_START_CLOUND_JOB, jobPointers, topics, 1, 1, 1,
    };
    fnet_conn_write_multi_result_t* writeResult = nullptr;
    int sendCode = sdkState->api.sendConnectionMulti(
        sdkState->wanConnection, &start, &writeResult);
    if (sendCode == FNET_OK && writeResult && writeResult->failedCnt > 0) {
        sendCode = FNET_CONN_SEND_ERROR;
    }
    if (writeResult) {
        sdkState->api.freeConnectionMultiResult(writeResult);
    }
    sdkState->api.freeAddCloudJobResults(results, resultCount);
    if (sendCode != FNET_OK) {
        output.value = QString::fromUtf8(jobId);
        output.error = resultError(sendCode);
        return output;
    }

    bool acknowledged = false;
    for (int attempt = 0; attempt < 15 && !acknowledged && !sdkState->stopping.load()
         && generation == sessionGeneration.load(); ++attempt) {
        QThread::msleep(2000);
        if (stopping || generation != sessionGeneration.load()) break;
        fnet_dev_product_t* product = nullptr;
        fnet_dev_detail_t* detail = nullptr;
        const int detailCode = sdkState->api.getWanDeviceProductDetail(
            sdkState->clientId.constData(), sdkState->accessToken.constData(), wanId.constData(),
            &product, &detail, 10000);
        if (detailCode == FNET_OK && detail) {
            acknowledged = stringValue(detail->jobId) == QString::fromUtf8(jobId)
                || stringValue(detail->printFileName) == QString::fromUtf8(destination);
        }
        if (product) {
            sdkState->api.freeDeviceProduct(product);
        }
        if (detail) {
            sdkState->api.freeDeviceDetail(detail);
        }
    }
    output.value = QString::fromUtf8(jobId);
    output.success = acknowledged;
    output.outcomeUnknown = !acknowledged;
    if (!acknowledged) {
        output.error = QStringLiteral("The cloud job was created, but the printer did not acknowledge it");
    }
    return output;
}

FlashforgeVoidResult FlashforgePrinterAgent::Impl::sdkControlLanJob(
    const FlashforgeDevice& device, const QString& checkCode,
    const QString& jobId, const QString& action) {
    FlashforgeVoidResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    const QByteArray ip = QByteArray::fromStdString(device.ip);
    const QByteArray serial = QByteArray::fromStdString(device.deviceId);
    const QByteArray check = checkCode.toUtf8();
    const QByteArray id = jobId.toUtf8();
    const QByteArray command = action.toUtf8();
    const fnet_job_ctrl_t control{id.constData(), command.constData()};
    const int code = sdkState->api.controlLanJob(
        ip.constData(), device.port, serial.constData(), check.constData(), &control, 5000);
    output.success = code == FNET_OK;
    output.error = resultError(code);
    return output;
}

FlashforgeVoidResult FlashforgePrinterAgent::Impl::sdkControlWanJob(
    const FlashforgeDevice& device, const QString& jobId, const QString& action,
    quint64 generation) {
    FlashforgeVoidResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    const QByteArray topic = QByteArray::fromStdString(device.wanTopic);
    if (topic.isEmpty()) {
        output.error = QStringLiteral(
            "The WAN printer topic is missing; refresh the account device list");
        return output;
    }
    QString connectionError;
    if (!sdkState->ensureWanConnection(connectionError)) {
        output.error = connectionError;
        return output;
    }
    const QByteArray id = jobId.toUtf8();
    const QByteArray command = action.toUtf8();
    const fnet_job_ctrl_t control{id.constData(), command.constData()};
    const fnet_conn_write_data_t write{
        FNET_CONN_WRITE_JOB_CTRL, &control, topic.constData(), 1,
    };
    // Connection setup above can block. Revalidate immediately before sending
    // the physical command, not just when it was dequeued.
    if (stopping || generation != sessionGeneration.load()) {
        output.error = QStringLiteral("The printer selection changed or the agent is stopping");
        return output;
    }
    const int code = sdkState->api.sendConnection(sdkState->wanConnection, &write);
    output.success = code == FNET_OK;
    output.error = resultError(code);
    return output;
}

FlashforgeVoidResult FlashforgePrinterAgent::Impl::sdkControlWanCameraStream(
    const FlashforgeDevice& device, bool enabled, quint64 generation) {
    FlashforgeVoidResult output;
    if (!sdkState->ready(output.error)) {
        return output;
    }
    const QByteArray topic = QByteArray::fromStdString(device.wanTopic);
    if (topic.isEmpty()) {
        output.error = QStringLiteral(
            "The WAN printer topic is missing; refresh the account device list");
        return output;
    }
    QString connectionError;
    if (!sdkState->ensureWanConnection(connectionError)) {
        output.error = connectionError;
        return output;
    }

    const QByteArray action = enabled ? QByteArrayLiteral("open")
                                      : QByteArrayLiteral("close");
    const fnet_camera_stream_ctrl_t control{action.constData()};
    const fnet_conn_write_data_t write{
        FNET_CONN_WRITE_CAMERA_STREAM_CTRL, &control, topic.constData(), 1,
    };
    // Connection setup above can block. Revalidate immediately before sending
    // the physical command, not just when it was dequeued.
    if (stopping || generation != sessionGeneration.load()) {
        output.error = QStringLiteral("The printer selection changed or the agent is stopping");
        return output;
    }
    const int code = sdkState->api.sendConnection(sdkState->wanConnection, &write);
    output.success = code == FNET_OK;
    output.error = resultError(code);
    return output;
}


FlashforgePrinterAgent::FlashforgePrinterAgent()
    : m_impl(std::make_shared<Impl>()) {}

FlashforgePrinterAgent::~FlashforgePrinterAgent() {
    shutdown();
}

void FlashforgePrinterAgent::setEventSink(EventSink sink) {
    m_impl->sink = std::move(sink);
}

std::vector<std::string> FlashforgePrinterAgent::supportedPrintFormats() const {
    return {"gcode-3mf"};
}

void FlashforgePrinterAgent::initialize() { m_impl->initialize(); }

void FlashforgePrinterAgent::shutdown() { m_impl->shutdown(); }

void FlashforgePrinterAgent::scan() { m_impl->scan(); }

void FlashforgePrinterAgent::login(const std::string& username,
                                     const std::string& password) {
    m_impl->login(QString::fromStdString(username), QString::fromStdString(password));
}

void FlashforgePrinterAgent::logout() { m_impl->logout(); }

void FlashforgePrinterAgent::selectDevice(const Device& device) {
    const auto selected = m_impl->nativeDevice(device.deviceId);
    m_impl->resetConnectionSession();
    m_impl->device = {};
    m_impl->device.deviceId = device.deviceId;
    m_impl->device.sessionId = device.sessionId;
    if (!selected || !selected->online) {
        m_impl->publishError(EventType::ConnectionChanged,
                             QStringLiteral("The selected printer is offline"));
        return;
    }
    m_impl->device = *selected;
    m_impl->device.sessionId = device.sessionId;
    m_impl->connectSelected({});
}

void FlashforgePrinterAgent::provideDeviceAccess(const std::string& credential) {
    if (m_impl->device.deviceId.empty()) {
        m_impl->publishError(EventType::ConnectionChanged,
                             QStringLiteral("No printer is selected"));
        return;
    }
    m_impl->connectSelected(credential);
}

void FlashforgePrinterAgent::releaseDevice() {
    const auto released = m_impl->device;
    m_impl->resetConnectionSession();
    m_impl->device = {};
    m_impl->checkCode.clear();
    m_impl->wanDeviceId.clear();
    Event event;
    event.type = EventType::ConnectionChanged;
    event.deviceId = released.deviceId;
    event.sessionId = released.sessionId;
    m_impl->publish(event);
}

void FlashforgePrinterAgent::submitJob(const PrintJob& job) {
    m_impl->submitJob(job);
}

void FlashforgePrinterAgent::controlJob(const std::string& action, std::uint64_t requestId) {
    m_impl->controlJob(action, requestId);
}

void FlashforgePrinterAgent::setCameraStreamEnabled(bool enabled) {
    m_impl->setCameraStreamEnabled(enabled);
}

} // namespace GPlatform::Printer
