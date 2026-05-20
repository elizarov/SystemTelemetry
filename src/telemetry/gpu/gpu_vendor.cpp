#include "telemetry/gpu/gpu_vendor.h"

#include <windows.h>

#include <cstdint>
#include <dxgi.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/amd/gpu_amd_adl.h"
#include "telemetry/gpu/gpu_vendor_selection.h"
#include "telemetry/gpu/intel/gpu_intel_level_zero.h"
#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"
#include "util/resource_strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using NtStatus = LONG;
using D3DkmtHandle = UINT;

constexpr int kD3DkmtQueryAdapterAddress = 6;
constexpr wchar_t kGdi32LibraryName[] = L"gdi32.dll";  // Win32 module loading requires a UTF-16 DLL name.

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

struct EnumeratedGpuAdapter {
    GpuAdapterInfo info;
    GpuVendor vendor = GpuVendor::Unknown;
};

using D3DkmtOpenAdapterFromLuidFn = NtStatus(WINAPI*)(D3DkmtOpenAdapterFromLuid*);
using D3DkmtQueryAdapterInfoFn = NtStatus(WINAPI*)(const D3DkmtQueryAdapterInfo*);
using D3DkmtCloseAdapterFn = NtStatus(WINAPI*)(const D3DkmtCloseAdapter*);

class UnsupportedGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    UnsupportedGpuTelemetryProvider(Trace& trace, std::optional<GpuAdapterInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    bool Initialize() override {
        sample_.providerName = "Unsupported GPU";
        sample_.available = false;
        sample_.diagnostics =
            ResourceStringText(RES_STR("No supported GPU telemetry provider matches the selected adapter vendor."));
        fpsProvider_ = CreatePresentedFpsProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider active."));
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ = fpsSample.diagnostics.empty()
                                  ? ResourceStringText(RES_STR("Presented FPS ETW provider unavailable."))
                                  : fpsSample.diagnostics;
        }

        const std::string adapterName = adapter_.has_value() ? adapter_->adapterName : std::string();
        const unsigned int vendorId = adapter_.has_value() ? adapter_->vendorId : 0;
        trace_.WriteFmt(TracePrefix::UnsupportedGpu,
            RES_STR("initialize vendor_id=0x%04X name=\"%s\" fps=\"%s\""),
            vendorId,
            adapterName.c_str(),
            fpsDiagnostics_.c_str());
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        GpuVendorTelemetrySample sample = sample_;
        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            sample.fpsPermissionRequired = fpsSample.permissionRequired;
            sample.fpsAppName = fpsSample.processName;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                sample.available = true;
            }
            if (trace_.Enabled(TracePrefix::UnsupportedGpu)) {
                const std::string fpsText =
                    fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1) : "fps=N/A";
                trace_.WriteFmt(TracePrefix::UnsupportedGpu,
                    RES_STR("get_presented_fps available=%s value=%s process=\"%s\" diagnostics=\"%s\""),
                    Trace::BoolText(fpsSample.fps.has_value()),
                    fpsText.c_str(),
                    fpsSample.processName.c_str(),
                    fpsSample.diagnostics.c_str());
            }
        }

        AppendFormat(sample.diagnostics, RES_STR(" fps=%s"), fpsDiagnostics_.c_str());
        return sample;
    }

private:
    Trace& trace_;
    std::optional<GpuAdapterInfo> adapter_;
    GpuVendorTelemetrySample sample_;
    std::string fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider not initialized."));
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
};

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorProviderForVendor(
    Trace& trace, GpuVendor vendor, std::optional<GpuAdapterInfo> adapter) {
    if (vendor == GpuVendor::Nvidia) {
        return CreateNvidiaGpuTelemetryProvider(trace, adapter);
    }
    if (vendor == GpuVendor::Amd) {
        return CreateAmdGpuTelemetryProvider(trace, adapter);
    }
    if (vendor == GpuVendor::Intel) {
        return CreateIntelGpuTelemetryProvider(trace, adapter);
    }
    return std::make_unique<UnsupportedGpuTelemetryProvider>(trace, std::move(adapter));
}

double DedicatedVideoMemoryGb(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

std::optional<size_t> FindDuplicateGpuAdapterIndex(
    const std::vector<EnumeratedGpuAdapter>& adapters, const GpuAdapterInfo& info) {
    for (size_t i = 0; i < adapters.size(); ++i) {
        if (GpuAdapterViewsReferToSameHardware(adapters[i].info, info)) {
            return i;
        }
    }
    return std::nullopt;
}

GpuAdapterCandidate MakeGpuAdapterCandidate(const EnumeratedGpuAdapter& adapter, bool selected) {
    GpuAdapterCandidate candidate;
    candidate.adapterName = GpuAdapterSelectionName(adapter.info);
    candidate.vendorName = GpuVendorName(adapter.vendor);
    candidate.vendorId = adapter.info.vendorId;
    candidate.dedicatedVramGb = DedicatedVideoMemoryGb(adapter.info.dedicatedVideoMemoryBytes);
    candidate.selected = selected;
    return candidate;
}

void PopulateAdapterPciAddress(GpuAdapterInfo& info, LUID adapterLuid) {
    HMODULE gdi = GetModuleHandleW(kGdi32LibraryName);
    if (gdi == nullptr) {
        gdi = LoadLibraryW(kGdi32LibraryName);
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

}  // namespace

GpuAdapterSelection ResolveGpuAdapterSelection(Trace& trace, std::string_view preferredAdapterName) {
    GpuAdapterSelection selection;
    std::vector<EnumeratedGpuAdapter> adapters;
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        trace.WriteFmt(
            TracePrefix::GpuVendor, RES_STR("adapter_factory hr=0x%08X"), static_cast<unsigned int>(factoryHr));
        return selection;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            trace.Write(TracePrefix::GpuVendor, RES_STR("adapter_enum done"));
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            trace.WriteFmt(TracePrefix::GpuVendor,
                RES_STR("adapter_enum index=%u hr=0x%08X"),
                adapterIndex,
                static_cast<unsigned int>(enumHr));
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        const bool software = SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
        if (SUCCEEDED(descHr) && !software) {
            GpuAdapterInfo info;
            info.vendorId = desc.VendorId;
            info.adapterName = adapterName;
            info.adapterIndex = adapterIndex;
            info.dedicatedVideoMemoryBytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory);
            info.deviceId = desc.DeviceId;
            info.subSysId = desc.SubSysId;
            info.revision = desc.Revision;
            PopulateAdapterPciAddress(info, desc.AdapterLuid);
            const GpuVendor vendor = SelectGpuVendor(info);
            const std::optional<size_t> duplicateIndex = FindDuplicateGpuAdapterIndex(adapters, info);
            if (duplicateIndex.has_value()) {
                const bool replace =
                    !HasUsableGpuPciAddress(adapters[*duplicateIndex].info) && HasUsableGpuPciAddress(info);
                trace.WriteFmt(TracePrefix::GpuVendor,
                    RES_STR("adapter_duplicate index=%u duplicate_of=%u replace=%s vendor_id=0x%04X "
                            "device_id=0x%04X subsystem_id=0x%08X revision=0x%02X pci=%04X:%02X:%02X.%u "
                            "vendor=%s match_rank=%d dedicated_gb=%.2f name=\"%s\""),
                    adapterIndex,
                    adapters[*duplicateIndex].info.adapterIndex,
                    Trace::BoolText(replace),
                    desc.VendorId,
                    desc.DeviceId,
                    desc.SubSysId,
                    desc.Revision,
                    info.pciDomain,
                    info.pciBus,
                    info.pciDevice,
                    info.pciFunction,
                    GpuVendorName(vendor),
                    GpuAdapterSelectionMatchRank(info, preferredAdapterName),
                    DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes),
                    adapterName.c_str());
                if (replace) {
                    adapters[*duplicateIndex] = EnumeratedGpuAdapter{info, vendor};
                }
                adapter->Release();
                continue;
            }

            adapters.push_back(EnumeratedGpuAdapter{info, vendor});

            trace.WriteFmt(TracePrefix::GpuVendor,
                RES_STR(
                    "adapter_candidate index=%u vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X revision=0x%02X "
                    "pci=%04X:%02X:%02X.%u vendor=%s match_rank=%d dedicated_gb=%.2f name=\"%s\""),
                adapterIndex,
                desc.VendorId,
                desc.DeviceId,
                desc.SubSysId,
                desc.Revision,
                info.pciDomain,
                info.pciBus,
                info.pciDevice,
                info.pciFunction,
                GpuVendorName(vendor),
                GpuAdapterSelectionMatchRank(info, preferredAdapterName),
                DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes),
                adapterName.c_str());
            adapter->Release();
            continue;
        }

        trace.WriteFmt(TracePrefix::GpuVendor,
            RES_STR("adapter_skip index=%u hr=0x%08X software=%s name=\"%s\""),
            adapterIndex,
            static_cast<unsigned int>(descHr),
            Trace::BoolText(software),
            adapterName.c_str());
        adapter->Release();
    }

    factory->Release();
    std::vector<GpuAdapterInfo> adapterInfos;
    adapterInfos.reserve(adapters.size());
    for (const EnumeratedGpuAdapter& adapter : adapters) {
        adapterInfos.push_back(adapter.info);
    }
    const std::vector<std::string> selectionNames = BuildGpuAdapterSelectionNames(adapterInfos);
    for (size_t i = 0; i < adapters.size(); ++i) {
        adapters[i].info.selectionName = selectionNames[i];
    }

    selection.candidates.reserve(adapters.size());
    std::optional<size_t> selectedCandidateIndex;
    int selectedMatchRank = 0;
    for (size_t i = 0; i < adapters.size(); ++i) {
        const int matchRank = GpuAdapterSelectionMatchRank(adapters[i].info, preferredAdapterName);
        const bool select =
            !selection.selectedAdapter.has_value() || (!preferredAdapterName.empty() && matchRank > selectedMatchRank);
        if (select) {
            selection.selectedAdapter = adapters[i].info;
            selectedMatchRank = matchRank;
            selectedCandidateIndex = i;
        }
    }
    for (size_t i = 0; i < adapters.size(); ++i) {
        selection.candidates.push_back(
            MakeGpuAdapterCandidate(adapters[i], selectedCandidateIndex.has_value() && i == *selectedCandidateIndex));
    }

    if (selection.selectedAdapter.has_value()) {
        const GpuVendor vendor = SelectGpuVendor(*selection.selectedAdapter);
        trace.WriteFmt(TracePrefix::GpuVendor,
            RES_STR("adapter_selected index=%u vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X revision=0x%02X "
                    "pci=%04X:%02X:%02X.%u vendor=%s preferred=\"%s\" name=\"%s\""),
            selection.selectedAdapter->adapterIndex,
            selection.selectedAdapter->vendorId,
            selection.selectedAdapter->deviceId,
            selection.selectedAdapter->subSysId,
            selection.selectedAdapter->revision,
            selection.selectedAdapter->pciDomain,
            selection.selectedAdapter->pciBus,
            selection.selectedAdapter->pciDevice,
            selection.selectedAdapter->pciFunction,
            GpuVendorName(vendor),
            std::string(preferredAdapterName).c_str(),
            selection.selectedAdapter->adapterName.c_str());
    } else {
        trace.Write(TracePrefix::GpuVendor, RES_STR("adapter_selected none"));
    }
    return selection;
}

std::optional<GpuAdapterInfo> ExtractPrimaryGpuAdapterInfo(Trace& trace) {
    return ResolveGpuAdapterSelection(trace, {}).selectedAdapter;
}

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(
    Trace& trace, const std::optional<GpuAdapterInfo>& adapter) {
    const GpuVendor vendor = adapter.has_value() ? SelectGpuVendor(*adapter) : GpuVendor::Unknown;
    trace.WriteFmt(TracePrefix::GpuVendor,
        RES_STR("create vendor=%s adapter=\"%s\""),
        GpuVendorName(vendor),
        adapter.has_value() ? adapter->adapterName.c_str() : "");
    return CreateGpuVendorProviderForVendor(trace, vendor, adapter);
}
