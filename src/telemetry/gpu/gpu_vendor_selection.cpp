#include "telemetry/gpu/gpu_vendor_selection.h"

#include "util/strings.h"

namespace {

constexpr unsigned int kNvidiaVendorId = 0x10de;
constexpr unsigned int kAmdVendorId = 0x1002;
constexpr unsigned int kIntelVendorId = 0x8086;

}  // namespace

const char* GpuVendorName(GpuVendor vendor) {
    switch (vendor) {
        case GpuVendor::Amd:
            return "AMD";
        case GpuVendor::Intel:
            return "Intel";
        case GpuVendor::Nvidia:
            return "NVIDIA";
        case GpuVendor::Unknown:
        default:
            return "Unknown";
    }
}

GpuVendor SelectGpuVendor(const GpuVendorInfo& info) {
    if (info.vendorId == kNvidiaVendorId || ContainsInsensitive(info.adapterName, "nvidia")) {
        return GpuVendor::Nvidia;
    }
    if (info.vendorId == kAmdVendorId || ContainsInsensitive(info.adapterName, "amd") ||
        ContainsInsensitive(info.adapterName, "radeon")) {
        return GpuVendor::Amd;
    }
    if (info.vendorId == kIntelVendorId || ContainsInsensitive(info.adapterName, "intel")) {
        return GpuVendor::Intel;
    }
    return GpuVendor::Unknown;
}
