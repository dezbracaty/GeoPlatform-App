#pragma once
#include "Printer/IPrinterAgent.hpp"
#include <memory>

namespace GPlatform::Printer {
struct MoonrakerConfig {
    std::string endpoint;
    std::string apiKey;
    int requestTimeoutMs{15000};
    int pollIntervalMs{2000};
    int uploadTimeoutMs{600000}; // Independent total deadline for large G-code transfers.
};

// Construct, call, and destroy on one owner thread with a running Qt event loop.
// Events are delivered on that thread. Configuration is memory-only. No discovery
// broadcasts, automatic reconnects, mutation retries, or persistent credentials.
// Qt remains a private implementation dependency, not part of this contract.
class MoonrakerPrinterAgent final : public PrinterCore::IPrinterAgent {
public:
    explicit MoonrakerPrinterAgent(MoonrakerConfig config);
    ~MoonrakerPrinterAgent() override;
    static bool validConfig(const MoonrakerConfig& config);
    void setEventSink(EventSink sink) override;
    void initialize() override;
    void shutdown() override;
    void scan() override;
    void login(const std::string&, const std::string&) override;
    void logout() override;
    void selectDevice(const PrinterCore::Device&) override;
    void provideDeviceAccess(const std::string&) override;
    void releaseDevice() override;
    std::vector<std::string> supportedPrintFormats() const override;
    PrinterCore::Capabilities capabilities() const override;
    void acknowledgeUnknownOutcome() override;
    void submitJob(const PrinterCore::PrintJob&) override;
    void controlJob(const std::string&, uint64_t) override;
    void setCameraStreamEnabled(bool) override;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace GPlatform::Printer
