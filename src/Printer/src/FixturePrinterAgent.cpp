#include "FixturePrinterAgent.hpp"

#include <stdexcept>
#include <utility>

namespace GPlatform::Printer {

namespace {

std::vector<PrinterCore::Device> fixtureDevices() {
    using PrinterCore::Device;
    using PrinterCore::DeviceRoute;
    return {
        Device{"ff-online-ready", "Adventurer 5M Pro", "AD5M Pro", "ready",
               "192.168.1.128", true, DeviceRoute::LocalNetwork},
        Device{"ff-online-printing", "Guider 3 Ultra", "Guider 3 Ultra", "printing",
               "192.168.1.136", true, DeviceRoute::Cloud},
        Device{"ff-offline-office", "Creator 4 - Office", "Creator 4", "offline",
               "192.168.1.152", false, DeviceRoute::Cloud},
        Device{"ff-offline-lab", "Adventurer 4 - Lab", "AD4", "offline",
               "192.168.1.164", false, DeviceRoute::LocalNetwork},
    };
}

} // namespace

FixturePrinterAgent::FixturePrinterAgent(std::string profile)
    : m_profile(std::move(profile)) {
    if (m_profile != "discovered" && m_profile != "connected"
        && m_profile != "printing") {
        throw std::invalid_argument("Unsupported printer fixture profile: " + m_profile);
    }
}

void FixturePrinterAgent::setEventSink(EventSink sink) {
    m_sink = std::move(sink);
}

void FixturePrinterAgent::initialize() {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::Availability;
    event.success = true;
    event.available = true;
    event.version = "fixture";
    publish(event);
}

void FixturePrinterAgent::shutdown() {}

void FixturePrinterAgent::scan() {
    PrinterCore::Event started;
    started.type = PrinterCore::EventType::ScanStarted;
    started.success = true;
    publish(started);

    PrinterCore::Event finished;
    finished.type = PrinterCore::EventType::ScanFinished;
    finished.success = true;
    finished.devices = fixtureDevices();
    publish(finished);
}

void FixturePrinterAgent::login(const std::string& username,
                                const std::string& password) {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::LoginFinished;
    event.success = !username.empty() && !password.empty();
    event.accountName = event.success ? username : std::string{};
    if (!event.success) {
        event.error = "Fixture credentials must not be empty";
    }
    publish(event);
}

void FixturePrinterAgent::logout() {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::LogoutFinished;
    event.success = true;
    publish(event);
}

void FixturePrinterAgent::selectDevice(const PrinterCore::Device& device) {
    m_selectedDevice = device;

    PrinterCore::Event connection;
    connection.type = PrinterCore::EventType::ConnectionChanged;
    connection.success = device.online;
    if (!device.online) {
        connection.error = "Fixture device is offline";
    }
    publish(connection);
    if (!device.online) {
        return;
    }

    m_status = {};
    m_status.deviceId = device.deviceId;
    m_status.name = device.name;
    m_status.ip = device.ip;
    m_status.state = m_profile == "printing" ? "printing" : "ready";
    m_status.nozzleTemperature = m_profile == "printing" ? 214.0 : 24.0;
    m_status.targetNozzleTemperature = m_profile == "printing" ? 220.0 : 0.0;
    m_status.bedTemperature = m_profile == "printing" ? 58.0 : 23.0;
    m_status.targetBedTemperature = m_profile == "printing" ? 60.0 : 0.0;
    m_status.cameraAvailable = true;
    m_status.cameraStreamUrl = "fixture://camera";
    if (m_profile == "printing") {
        m_status.jobId = "fixture-popup-job";
        m_status.fileName = "Calibration_Cube.gcode.3mf";
        m_status.progress = 68.0;
        m_status.estimatedTimeSeconds = 5400.0;
        m_status.printDurationSeconds = 2880.0;
    }
    publishStatus();
}

void FixturePrinterAgent::provideDeviceAccess(const std::string&) {
    if (!m_selectedDevice.deviceId.empty()) {
        selectDevice(m_selectedDevice);
    }
}

void FixturePrinterAgent::releaseDevice() {
    m_status = {};
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::ConnectionChanged;
    event.success = false;
    publish(event);
    m_selectedDevice = {};
}

void FixturePrinterAgent::submitJob(const PrinterCore::PrintJob& job) {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::JobCommandFinished;
    event.success = !m_selectedDevice.deviceId.empty() && !job.filePath.empty();
    event.command = "send";
    event.requestId = job.requestId;
    if (!event.success) {
        event.error = "Fixture job requires a connected device and file";
    }
    publish(event);
    if (event.success && job.printNow) {
        m_status.state = "printing";
        m_status.jobId = "fixture-job-" + std::to_string(job.requestId);
        m_status.fileName = job.destinationName;
        publishStatus();
    }
}

void FixturePrinterAgent::controlJob(const std::string& action, uint64_t requestId) {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::JobCommandFinished;
    event.success = !m_selectedDevice.deviceId.empty();
    event.command = action;
    event.requestId = requestId;
    if (!event.success) {
        event.error = "Fixture job requires a connected device";
    }
    publish(event);
    if (!event.success) {
        return;
    }
    if (action == "pause") {
        m_status.state = "pause";
    } else if (action == "continue") {
        m_status.state = "printing";
    } else if (action == "cancel") {
        m_status.state = "cancel";
    }
    publishStatus();
}

void FixturePrinterAgent::setCameraStreamEnabled(bool enabled) {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::CameraStreamCommandFinished;
    event.success = true;
    event.command = enabled ? "open" : "close";
    publish(event);
    if (enabled && !m_status.deviceId.empty()) {
        publishStatus();
    }
}

void FixturePrinterAgent::publish(const PrinterCore::Event& event) const {
    if (m_sink) {
        auto tagged = event;
        tagged.deviceId = m_selectedDevice.deviceId;
        tagged.sessionId = m_selectedDevice.sessionId;
        m_sink(tagged);
    }
}

void FixturePrinterAgent::publishStatus() {
    PrinterCore::Event event;
    event.type = PrinterCore::EventType::StatusChanged;
    event.success = true;
    event.status = m_status;
    publish(event);
}

} // namespace GPlatform::Printer
