#pragma once

#include <string>

enum class GpuVendor {
    Unknown,
    Amd,
    Nvidia,
};

struct GpuVendorInfo {
    unsigned int vendorId = 0;
    std::string adapterName;
};

const char* GpuVendorName(GpuVendor vendor);
GpuVendor SelectGpuVendor(const GpuVendorInfo& info);
