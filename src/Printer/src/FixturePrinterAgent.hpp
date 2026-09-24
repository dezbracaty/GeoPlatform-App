#pragma once

#include "Printer/IPrinterAgent.hpp"

#include <string>

namespace GPlatform::Printer {

// Deterministic replacement for the external printer service. It implements
// the same event boundary as the real agent so the facade and QML continue to
// execute their production paths during Journal scenarios.
class FixturePrinterAgent final : public PrinterCore::IPrinterAgent {
public:
    explicit FixturePrinterAgent(std::string profile);

    void setEventSink(EventSink sink) override;
    void initialize() override;
    void shutdown() override;
    void scan() override;
    void login(const std::string& username, const std::string& password) override;
    void logout() override;
    void selectDevice(const PrinterCore::Device& device) override;
    void provideDeviceAccess(const std::string& credential) override;
    void releaseDevice() override;
    GPlatform::PrinterCore::Capabilities capabilities() const override {
        return {true, true, true, true, true, true, true, true, true};
    }
    std::vector<std::string> supportedPrintFormats() const override { return {"gcode-3mf"}; }
    void submitJob(const PrinterCore::PrintJob& job) override;
    void controlJob(const std::string& action, uint64_t requestId) override;
    void setCameraStreamEnabled(bool enabled) override;

private:
    void publish(const PrinterCore::Event& event) const;
    void publishStatus();

    std::string m_profile;
    EventSink m_sink;
    PrinterCore::Device m_selectedDevice;
    PrinterCore::Status m_status;
};

} // namespace GPlatform::Printer
