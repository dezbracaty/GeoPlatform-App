#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GPlatform::PrinterCore {

enum class DeviceRoute {
    Unknown,
    LocalNetwork,
    Cloud,
};

struct Device {
    std::string deviceId;
    std::string name;
    std::string model;
    std::string state;
    std::string ip;
    bool online{true};
    DeviceRoute route{DeviceRoute::Unknown};
    uint64_t sessionId{0}; // Assigned by the facade on each connection attempt.
};

struct Status {
    std::string deviceId;
    std::string name;
    std::string ip;
    std::string state;
    std::string jobId;
    std::string fileName;
    std::string errorCode;
    std::string cameraStreamUrl;
    double progress{0.0};
    double nozzleTemperature{0.0};
    double targetNozzleTemperature{0.0};
    double bedTemperature{0.0};
    double targetBedTemperature{0.0};
    double estimatedTimeSeconds{0.0};
    double printDurationSeconds{0.0};
    int currentLayer{0};
    int totalLayers{0};
    bool cameraAvailable{false};
};

struct PrintJob {
    uint64_t requestId{0};
    std::string formatId;
    std::string filePath;
    std::string destinationName;
    bool printNow{false};
    bool levelingBeforePrint{false};
    bool flowCalibration{false};
    bool firstLayerInspection{false};
    bool timeLapseVideo{false};
    bool useMaterialStation{false};
};

struct Capabilities {
    bool discovery{false};
    bool account{false};
    bool upload{false};
    bool start{false};
    bool pauseResume{false};
    bool cancel{false};
    bool camera{false};
    bool vendorOptions{false};
    bool requiresJobId{false};
};

enum class EventType {
    CapabilitiesChanged,
    Availability,
    ScanStarted,
    ScanFinished,
    LoginFinished,
    LogoutFinished,
    ConnectionChanged,
    StatusChanged,
    TransferProgress,
    JobCommandFinished,
    CameraStreamCommandFinished,
};

struct Event {
    std::string deviceId;
    uint64_t sessionId{0};
    uint64_t requestId{0};
    bool outcomeUnknown{false};
    EventType type{EventType::Availability};
    bool success{false};
    bool available{false};
    std::string version;
    std::string accountName;
    std::string error;
    std::string command;
    bool accessRequired{false};
    std::vector<Device> devices;
    Status status;
    double progress{0.0};
};

} // namespace GPlatform::PrinterCore
