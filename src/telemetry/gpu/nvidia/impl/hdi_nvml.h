#pragma once

#include <memory>
#include <optional>
#include <string>

class Trace;

using HdiNvmlDevice = void*;
using HdiNvmlReturn = int;

constexpr HdiNvmlReturn kHdiNvmlSuccess = 0;

struct HdiNvmlMemory {
    unsigned long long total = 0;
    unsigned long long free = 0;
    unsigned long long used = 0;
};

struct HdiNvmlPciInfo {
    unsigned int domain = 0;
    unsigned int bus = 0;
    unsigned int device = 0;
    unsigned int pciDeviceId = 0;
    unsigned int pciSubSystemId = 0;
};

class NvidiaNvmlHdi {
public:
    virtual ~NvidiaNvmlHdi() = default;

    virtual bool Load(std::string& diagnostics) = 0;
    virtual HdiNvmlReturn Initialize() = 0;
    virtual std::string ResultText(HdiNvmlReturn result) const = 0;
    virtual HdiNvmlReturn DeviceCount(unsigned int& count) const = 0;
    virtual HdiNvmlReturn DeviceHandleByIndex(unsigned int index, HdiNvmlDevice& device) const = 0;
    virtual HdiNvmlReturn DeviceName(HdiNvmlDevice device, char* buffer, unsigned int bufferSize) const = 0;
    virtual HdiNvmlReturn Temperature(HdiNvmlDevice device, unsigned int& temperatureC) const = 0;
    virtual HdiNvmlReturn MemoryInfo(HdiNvmlDevice device, HdiNvmlMemory& memory) const = 0;
    virtual std::optional<HdiNvmlReturn> FanSpeedRpm(HdiNvmlDevice device, unsigned int& fanRpm) const = 0;
    virtual std::optional<HdiNvmlReturn> PciInfo(HdiNvmlDevice device, HdiNvmlPciInfo& pci) const = 0;
};

std::unique_ptr<NvidiaNvmlHdi> CreateProductionNvidiaNvmlHdi(Trace& trace);
