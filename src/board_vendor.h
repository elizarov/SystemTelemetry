#pragma once

#include <memory>
#include <optional>
#include <string>

#include "config.h"

namespace tracing {
class Trace;
}

struct BoardVendorTelemetrySample {
    std::optional<double> fanRpm;
    std::optional<double> cpuTemperatureC;
    std::optional<int> requestedFanChannel;
    std::optional<int> selectedFanChannel;
    std::optional<uint16_t> probePort;
    std::optional<uint16_t> chipId;
    std::optional<uint32_t> monitorBaseAddress;
    std::optional<uint16_t> rawFanCounter;
    std::optional<uint8_t> ecMmioRegisterValue;
    std::string boardManufacturer;
    std::string boardProduct;
    std::string chipName;
    std::string controllerType;
    std::string driverLibrary;
    std::string selectedCpuTemperatureSensor;
    std::string providerName = "None";
    std::string diagnostics;
    bool fan16BitMode = false;
    bool available = false;
};

class BoardVendorTelemetryProvider {
public:
    virtual ~BoardVendorTelemetryProvider() = default;
    virtual bool Initialize(const AppConfig& config) = 0;
    virtual BoardVendorTelemetrySample Sample() = 0;
};

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(tracing::Trace* trace);
