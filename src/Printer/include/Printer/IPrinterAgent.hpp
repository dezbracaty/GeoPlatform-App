#pragma once

#include "PrinterCoreTypes.hpp"

#include <functional>
#include <string>

namespace GPlatform::PrinterCore {

// Vendor-neutral printer networking boundary. Implementations translate these
// operations to a vendor DLL or native protocol; callers never depend on that
// implementation detail.
class IPrinterAgent {
public:
    using EventSink = std::function<void(const Event&)>;

    virtual ~IPrinterAgent() = default;

    // The facade marshals events to its owner thread. Events must retain the
    // originating device/session/request identity, including late callbacks.
    // Clearing the sink stops new deliveries; callbacks already copied may finish.
    virtual void setEventSink(EventSink sink) = 0;
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    virtual void scan() = 0;
    virtual void login(const std::string& username, const std::string& password) = 0;
    virtual void logout() = 0;
    virtual void selectDevice(const Device& device) = 0;
    virtual void provideDeviceAccess(const std::string& credential) = 0;
    virtual void releaseDevice() = 0;
    virtual std::vector<std::string> supportedPrintFormats() const = 0;
    virtual Capabilities capabilities() const { return {}; }
    // Explicit user acknowledgement after physically checking an unknown outcome.
    virtual void acknowledgeUnknownOutcome() {}
    virtual void submitJob(const PrintJob& job) = 0;
    virtual void controlJob(const std::string& action, uint64_t requestId) = 0;
    virtual void setCameraStreamEnabled(bool enabled) = 0;
};

} // namespace GPlatform::PrinterCore
