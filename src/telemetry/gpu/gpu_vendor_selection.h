#pragma once

#include <cstdint>
#include <string>

enum class GpuVendor {
    Unknown,
    Amd,
    Intel,
    Nvidia,
};

struct GpuVendorInfo {
    unsigned int vendorId = 0;
    std::string adapterName;
    unsigned int adapterIndex = 0;
    std::uint64_t dedicatedVideoMemoryBytes = 0;
};

struct GpuAdapterCandidate {
    std::string adapterName;
    std::string vendorName;
    unsigned int vendorId = 0;
    double dedicatedVramGb = 0.0;
    bool selected = false;
};

const char* GpuVendorName(GpuVendor vendor);
GpuVendor SelectGpuVendor(const GpuVendorInfo& info);
