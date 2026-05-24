#pragma once

#include <memory>
#include <string>
#include <vector>

class Trace;

using HdiNvapiPhysicalGpuHandle = void*;
using HdiNvapiStatus = int;

constexpr HdiNvapiStatus kHdiNvapiOk = 0;
constexpr HdiNvapiStatus kHdiNvapiGpuNotPowered = -220;
constexpr int kHdiNvapiShortStringSize = 64;

struct HdiNvapiClockFrequencies {
    HdiNvapiStatus status = 0;
    bool graphicsPresent = false;
    unsigned int graphicsFrequencyKhz = 0;
};

class NvidiaNvapiHdi {
public:
    virtual ~NvidiaNvapiHdi() = default;

    virtual bool Load(std::string& diagnostics) = 0;
    virtual HdiNvapiStatus Initialize() = 0;
    virtual HdiNvapiStatus EnumPhysicalGpus(std::vector<HdiNvapiPhysicalGpuHandle>& handles) = 0;
    virtual HdiNvapiStatus GpuFullName(HdiNvapiPhysicalGpuHandle handle, char* buffer, unsigned int bufferSize) = 0;
    virtual HdiNvapiClockFrequencies GraphicsClock(HdiNvapiPhysicalGpuHandle handle) const = 0;
    virtual std::string ResultText(HdiNvapiStatus status) const = 0;
};

std::unique_ptr<NvidiaNvapiHdi> CreateProductionNvidiaNvapiHdi(Trace& trace);
