#include "telemetry/impl/hdi_gpu_discovery.h"

#include <windows.h>

#include <cstdint>
#include <dxgi.h>
#include <memory>
#include <string>
#include <vector>

#include "telemetry/gpu/gpu_vendor_selection.h"
#include "util/resource_strings.h"
#include "util/text_encoding.h"
#include "util/trace.h"

namespace {

using NtStatus = LONG;
using D3DkmtHandle = UINT;

constexpr int kD3DkmtQueryAdapterAddress = 6;
constexpr char kGdi32LibraryName[] = "gdi32.dll";

struct D3DkmtOpenAdapterFromLuid {
    LUID adapterLuid;
    D3DkmtHandle adapter = 0;
};

struct D3DkmtAdapterAddress {
    UINT busNumber = 0;
    UINT deviceNumber = 0;
    UINT functionNumber = 0;
};

struct D3DkmtQueryAdapterInfo {
    D3DkmtHandle adapter = 0;
    int type = 0;
    void* privateDriverData = nullptr;
    UINT privateDriverDataSize = 0;
};

struct D3DkmtCloseAdapter {
    D3DkmtHandle adapter = 0;
};

using D3DkmtOpenAdapterFromLuidFn = NtStatus(WINAPI*)(D3DkmtOpenAdapterFromLuid*);
using D3DkmtQueryAdapterInfoFn = NtStatus(WINAPI*)(const D3DkmtQueryAdapterInfo*);
using D3DkmtCloseAdapterFn = NtStatus(WINAPI*)(const D3DkmtCloseAdapter*);

void PopulateAdapterPciAddress(GpuAdapterInfo& info, LUID adapterLuid) {
    HMODULE gdi = GetModuleHandleA(kGdi32LibraryName);
    if (gdi == nullptr) {
        gdi = LoadLibraryA(kGdi32LibraryName);
    }
    if (gdi == nullptr) {
        return;
    }

    const auto openAdapter =
        reinterpret_cast<D3DkmtOpenAdapterFromLuidFn>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid"));
    const auto queryAdapter = reinterpret_cast<D3DkmtQueryAdapterInfoFn>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
    const auto closeAdapter = reinterpret_cast<D3DkmtCloseAdapterFn>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
    if (openAdapter == nullptr || queryAdapter == nullptr || closeAdapter == nullptr) {
        return;
    }

    D3DkmtOpenAdapterFromLuid open{};
    open.adapterLuid = adapterLuid;
    if (openAdapter(&open) < 0) {
        return;
    }

    D3DkmtAdapterAddress address{};
    D3DkmtQueryAdapterInfo query{};
    query.adapter = open.adapter;
    query.type = kD3DkmtQueryAdapterAddress;
    query.privateDriverData = &address;
    query.privateDriverDataSize = sizeof(address);
    if (queryAdapter(&query) >= 0) {
        info.pciBus = address.busNumber;
        info.pciDevice = address.deviceNumber;
        info.pciFunction = address.functionNumber;
        info.hasPciAddress = true;
        info.hasPciAddress = HasUsableGpuPciAddress(info);
    }

    D3DkmtCloseAdapter close{};
    close.adapter = open.adapter;
    closeAdapter(&close);
}

class ProductionGpuDiscoveryHdi final : public GpuDiscoveryHdi {
public:
    explicit ProductionGpuDiscoveryHdi(Trace& trace) : trace_(trace) {}

    std::vector<GpuAdapterInfo> EnumerateAdapters() override {
        std::vector<GpuAdapterInfo> adapters;
        IDXGIFactory1* factory = nullptr;
        const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
        if (FAILED(factoryHr) || factory == nullptr) {
            trace_.WriteFmt(
                TracePrefix::GpuVendor, RES_STR("adapter_factory hr=0x%08X"), static_cast<unsigned int>(factoryHr));
            return adapters;
        }

        for (UINT adapterIndex = 0;; ++adapterIndex) {
            IDXGIAdapter1* adapter = nullptr;
            const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
            if (enumHr == DXGI_ERROR_NOT_FOUND) {
                trace_.Write(TracePrefix::GpuVendor, RES_STR("adapter_enum done"));
                break;
            }
            if (FAILED(enumHr) || adapter == nullptr) {
                trace_.WriteFmt(TracePrefix::GpuVendor,
                    RES_STR("adapter_enum index=%u hr=0x%08X"),
                    adapterIndex,
                    static_cast<unsigned int>(enumHr));
                break;
            }

            DXGI_ADAPTER_DESC1 desc{};
            const HRESULT descHr = adapter->GetDesc1(&desc);
            const bool software = SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            const std::string adapterName = SUCCEEDED(descHr) ? TextFromWide(desc.Description) : std::string();
            if (SUCCEEDED(descHr) && !software) {
                GpuAdapterInfo info;
                info.vendorId = desc.VendorId;
                info.adapterName = adapterName;
                info.adapterIndex = adapterIndex;
                info.dedicatedVideoMemoryBytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory);
                info.deviceId = desc.DeviceId;
                info.subSysId = desc.SubSysId;
                info.revision = desc.Revision;
                info.hasAdapterLuid = true;
                info.adapterLuidHighPart = static_cast<std::uint32_t>(desc.AdapterLuid.HighPart);
                info.adapterLuidLowPart = static_cast<std::uint32_t>(desc.AdapterLuid.LowPart);
                PopulateAdapterPciAddress(info, desc.AdapterLuid);
                adapters.push_back(info);
            } else {
                trace_.WriteFmt(TracePrefix::GpuVendor,
                    RES_STR("adapter_skip index=%u hr=0x%08X software=%s name=\"%s\""),
                    adapterIndex,
                    static_cast<unsigned int>(descHr),
                    Trace::BoolText(software),
                    adapterName.c_str());
            }
            adapter->Release();
        }

        factory->Release();
        return adapters;
    }

private:
    Trace& trace_;
};

}  // namespace

std::unique_ptr<GpuDiscoveryHdi> CreateProductionGpuDiscoveryHdi(Trace& trace) {
    return std::make_unique<ProductionGpuDiscoveryHdi>(trace);
}
