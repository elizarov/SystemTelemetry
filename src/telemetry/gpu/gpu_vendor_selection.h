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
    unsigned int deviceId = 0;
    unsigned int subSysId = 0;
    unsigned int revision = 0;
    bool hasPciAddress = false;
    unsigned int pciDomain = 0;
    unsigned int pciBus = 0;
    unsigned int pciDevice = 0;
    unsigned int pciFunction = 0;
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
