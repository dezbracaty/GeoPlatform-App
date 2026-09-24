#pragma once

#include "Printer/IPrinterAgent.hpp"

#include <memory>

namespace GPlatform::Printer {

class FlashforgePrinterAgent final : public PrinterCore::IPrinterAgent {
public:
    FlashforgePrinterAgent();
    ~FlashforgePrinterAgent() override;

    void setEventSink(EventSink sink) override;
    GPlatform::PrinterCore::Capabilities capabilities() const override {
        return {true, true, true, true, true, true, true, true, true};
    }
    std::vector<std::string> supportedPrintFormats() const override;
    void initialize() override;
    void shutdown() override;
    void scan() override;
    void login(const std::string& username, const std::string& password) override;
    void logout() override;
    void selectDevice(const PrinterCore::Device& device) override;
    void provideDeviceAccess(const std::string& credential) override;
    void releaseDevice() override;
    void submitJob(const PrinterCore::PrintJob& job) override;
    void controlJob(const std::string& action, std::uint64_t requestId) override;
    void setCameraStreamEnabled(bool enabled) override;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace GPlatform::Printer
