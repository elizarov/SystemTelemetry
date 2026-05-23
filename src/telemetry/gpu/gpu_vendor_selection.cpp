#include "telemetry/gpu/gpu_vendor_selection.h"

#include <algorithm>
#include <string>
#include <tuple>

#include "util/strings.h"
#include "util/text_format.h"

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

bool GpuAdapterStableOrderLess(const GpuAdapterInfo& lhs, const GpuAdapterInfo& rhs) {
    const bool lhsHasPciAddress = HasUsableGpuPciAddress(lhs);
    const bool rhsHasPciAddress = HasUsableGpuPciAddress(rhs);
    if (lhsHasPciAddress != rhsHasPciAddress) {
        return lhsHasPciAddress;
    }
    const auto descriptorOrder = [](const GpuAdapterInfo& left, const GpuAdapterInfo& right) {
        return std::tie(left.vendorId,
                   left.deviceId,
                   left.subSysId,
                   left.revision,
                   left.dedicatedVideoMemoryBytes,
                   left.adapterIndex,
                   left.adapterName) < std::tie(right.vendorId,
                                           right.deviceId,
                                           right.subSysId,
                                           right.revision,
                                           right.dedicatedVideoMemoryBytes,
                                           right.adapterIndex,
                                           right.adapterName);
    };
    if (lhsHasPciAddress && rhsHasPciAddress) {
        const auto lhsPci = std::tie(lhs.pciDomain, lhs.pciBus, lhs.pciDevice, lhs.pciFunction);
        const auto rhsPci = std::tie(rhs.pciDomain, rhs.pciBus, rhs.pciDevice, rhs.pciFunction);
        if (lhsPci != rhsPci) {
            return lhsPci < rhsPci;
        }
    }
    return descriptorOrder(lhs, rhs);
}

std::string NumberedGpuAdapterSelectionName(const std::string& adapterName, size_t number) {
    return adapterName + " #" + std::to_string(number);
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

std::optional<std::string> GpuAdapterPdhLuidToken(const GpuAdapterInfo& info) {
    if (!info.hasAdapterLuid) {
        return std::nullopt;
    }
    return FormatText("luid_0x%08x_0x%08x",
        static_cast<unsigned int>(info.adapterLuidHighPart),
        static_cast<unsigned int>(info.adapterLuidLowPart));
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

std::string GpuAdapterSelectionName(const GpuAdapterInfo& info) {
    return info.selectionName.empty() ? info.adapterName : info.selectionName;
}

std::vector<std::string> BuildGpuAdapterSelectionNames(const std::vector<GpuAdapterInfo>& adapters) {
    std::vector<std::string> names(adapters.size());
    std::vector<bool> visited(adapters.size(), false);
    for (size_t i = 0; i < adapters.size(); ++i) {
        if (visited[i]) {
            continue;
        }

        std::vector<size_t> sameNameIndices;
        for (size_t j = i; j < adapters.size(); ++j) {
            if (!visited[j] && EqualsInsensitive(adapters[i].adapterName, adapters[j].adapterName)) {
                sameNameIndices.push_back(j);
                visited[j] = true;
            }
        }

        if (sameNameIndices.size() == 1) {
            names[sameNameIndices.front()] = adapters[sameNameIndices.front()].adapterName;
            continue;
        }

        std::stable_sort(sameNameIndices.begin(), sameNameIndices.end(), [&](size_t lhs, size_t rhs) {
            return GpuAdapterStableOrderLess(adapters[lhs], adapters[rhs]);
        });
        for (size_t rank = 0; rank < sameNameIndices.size(); ++rank) {
            const size_t adapterIndex = sameNameIndices[rank];
            names[adapterIndex] = NumberedGpuAdapterSelectionName(adapters[adapterIndex].adapterName, rank + 1);
        }
    }
    return names;
}

int GpuAdapterSelectionMatchRank(const GpuAdapterInfo& info, std::string_view preferredAdapterName) {
    if (preferredAdapterName.empty()) {
        return 0;
    }

    const std::string preferredText(preferredAdapterName);
    const std::string selectionName = GpuAdapterSelectionName(info);
    if (EqualsInsensitive(selectionName, preferredText)) {
        return 4;
    }
    if (EqualsInsensitive(info.adapterName, preferredText)) {
        return 3;
    }
    if (ContainsInsensitive(selectionName, preferredText)) {
        return 2;
    }
    return ContainsInsensitive(info.adapterName, preferredText) ? 1 : 0;
}
