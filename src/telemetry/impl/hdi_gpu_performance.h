#pragma once

#include <memory>
#include <string_view>

class Trace;

struct GpuPerformanceLoadSample {
    double total = 0.0;
    double total3d = 0.0;
    unsigned long matchedCount = 0;
};

class GpuPerformanceHdi {
public:
    virtual ~GpuPerformanceHdi() = default;

    virtual void Initialize() = 0;
    virtual GpuPerformanceLoadSample SampleLoad(std::string_view instanceFilter, const char* filterLabel) = 0;
    virtual double SampleDedicatedMemoryBytes(std::string_view instanceFilter, const char* filterLabel) = 0;
};

std::unique_ptr<GpuPerformanceHdi> CreateProductionGpuPerformanceHdi(Trace& trace);
