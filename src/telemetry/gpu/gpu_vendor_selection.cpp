#include "telemetry/gpu/gpu_vendor_selection.h"

#include "util/strings.h"

namespace {

constexpr unsigned int kNvidiaVendorId = 0x10de;
constexpr unsigned int kAmdVendorId = 0x1002;
constexpr unsigned int kIntelVendorId = 0x8086;
constexpr unsigned int kMaxPciBus = 0xff;
constexpr unsigned int kMaxPciDevice = 31;
constexpr unsigned int kMaxPciFunction = 7;

bool SameAdapterDescriptor(const GpuAdapterInfo& lhs, const GpuAdapterInfo& rhs) {
    return lhs.vendorId == rhs.vendorId && lhs.deviceId == rhs.deviceId && lhs.subSysId == rhs.subSysId &&
           lhs.revision == rhs.revision && lhs.dedicatedVideoMemoryBytes == rhs.dedicatedVideoMemoryBytes &&
           EqualsInsensitive(lhs.adapterName, rhs.adapterName);
}

bool SameUsablePciAddress(const GpuAdapterInfo& lhs, const GpuAdapterInfo& rhs) {
    return lhs.pciDomain == rhs.pciDomain && lhs.pciBus == rhs.pciBus && lhs.pciDevice == rhs.pciDevice &&
           lhs.pciFunction == rhs.pciFunction;
}

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

bool HasUsableGpuPciAddress(const GpuAdapterInfo& info) {
    return info.hasPciAddress && info.pciBus <= kMaxPciBus && info.pciDevice <= kMaxPciDevice &&
           info.pciFunction <= kMaxPciFunction;
}

bool GpuAdapterViewsReferToSameHardware(const GpuAdapterInfo& lhs, const GpuAdapterInfo& rhs) {
    const bool lhsHasPciAddress = HasUsableGpuPciAddress(lhs);
    const bool rhsHasPciAddress = HasUsableGpuPciAddress(rhs);
    if (lhsHasPciAddress && rhsHasPciAddress) {
        return SameUsablePciAddress(lhs, rhs) && SameAdapterDescriptor(lhs, rhs);
    }
    return SameAdapterDescriptor(lhs, rhs);
}
