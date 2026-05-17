#include "telemetry/gpu/gpu_vendor.h"

#include <windows.h>

#include <cstdint>
#include <dxgi.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/amd/gpu_amd_adl.h"
#include "telemetry/gpu/gpu_vendor_selection.h"
#include "telemetry/gpu/intel/gpu_intel_level_zero.h"
#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"
#include "util/strings.h"
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

using D3DkmtOpenAdapterFromLuidFn = NtStatus(WINAPI*)(D3DkmtOpenAdapterFromLuid*);
using D3DkmtQueryAdapterInfoFn = NtStatus(WINAPI*)(const D3DkmtQueryAdapterInfo*);
using D3DkmtCloseAdapterFn = NtStatus(WINAPI*)(const D3DkmtCloseAdapter*);

class UnsupportedGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    UnsupportedGpuTelemetryProvider(Trace& trace, std::optional<GpuVendorInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    bool Initialize() override {
        sample_.providerName = "Unsupported GPU";
        sample_.available = false;
        sample_.diagnostics = "No supported GPU telemetry provider matches the selected adapter vendor.";
        fpsProvider_ = CreatePresentedFpsProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = "Presented FPS ETW provider active.";
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ =
                fpsSample.diagnostics.empty() ? "Presented FPS ETW provider unavailable." : fpsSample.diagnostics;
        }

        const std::string adapterName = adapter_.has_value() ? adapter_->adapterName : std::string();
        const unsigned int vendorId = adapter_.has_value() ? adapter_->vendorId : 0;
        trace_.WriteFmt(TracePrefix::UnsupportedGpu,
            "initialize vendor_id=0x%04X name=\"%s\" fps=\"%s\"",
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
                    "get_presented_fps available=%s value=%s process=\"%s\" diagnostics=\"%s\"",
                    Trace::BoolText(fpsSample.fps.has_value()),
                    fpsText.c_str(),
                    fpsSample.processName.c_str(),
                    fpsSample.diagnostics.c_str());
            }
        }

        AppendFormat(sample.diagnostics, " fps=%s", fpsDiagnostics_.c_str());
        return sample;
    }

private:
    Trace& trace_;
    std::optional<GpuVendorInfo> adapter_;
    GpuVendorTelemetrySample sample_;
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
};

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorProviderForVendor(
    Trace& trace, GpuVendor vendor, std::optional<GpuVendorInfo> adapter) {
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

int PreferredGpuAdapterMatchRank(const std::string& adapterName, std::string_view preferredAdapterName) {
    if (preferredAdapterName.empty()) {
        return 0;
    }
    const std::string preferredText(preferredAdapterName);
    if (EqualsInsensitive(adapterName, preferredText)) {
        return 2;
    }
    return ContainsInsensitive(adapterName, preferredText) ? 1 : 0;
}

double DedicatedVideoMemoryGb(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

void PopulateAdapterPciAddress(GpuVendorInfo& info, LUID adapterLuid) {
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
        info.hasPciAddress = true;
        info.pciBus = address.busNumber;
        info.pciDevice = address.deviceNumber;
        info.pciFunction = address.functionNumber;
    }

    D3DkmtCloseAdapter close{};
    close.adapter = open.adapter;
    closeAdapter(&close);
}

}  // namespace

GpuAdapterSelection ResolveGpuAdapterSelection(Trace& trace, std::string_view preferredAdapterName) {
    GpuAdapterSelection selection;
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        trace.WriteFmt(TracePrefix::GpuVendor, "adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        return selection;
    }

    std::optional<size_t> selectedCandidateIndex;
    int selectedMatchRank = 0;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            trace.Write(TracePrefix::GpuVendor, "adapter_enum done");
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            trace.WriteFmt(TracePrefix::GpuVendor,
                "adapter_enum index=%u hr=0x%08X",
                adapterIndex,
                static_cast<unsigned int>(enumHr));
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        const bool software = SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
        if (SUCCEEDED(descHr) && !software) {
            GpuVendorInfo info{desc.VendorId,
                adapterName,
                adapterIndex,
                static_cast<std::uint64_t>(desc.DedicatedVideoMemory),
                desc.DeviceId,
                desc.SubSysId,
                desc.Revision};
            PopulateAdapterPciAddress(info, desc.AdapterLuid);
            const GpuVendor vendor = SelectGpuVendor(info);
            const int matchRank = PreferredGpuAdapterMatchRank(adapterName, preferredAdapterName);
            const bool select = !selection.selectedAdapter.has_value() ||
                                (!preferredAdapterName.empty() && matchRank > selectedMatchRank);
            if (select) {
                if (selectedCandidateIndex.has_value()) {
                    selection.candidates[*selectedCandidateIndex].selected = false;
                }
                selection.selectedAdapter = info;
                selectedMatchRank = matchRank;
                selectedCandidateIndex = selection.candidates.size();
            }

            GpuAdapterCandidate candidate;
            candidate.adapterName = adapterName;
            candidate.vendorName = GpuVendorName(vendor);
            candidate.vendorId = desc.VendorId;
            candidate.dedicatedVramGb = DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes);
            candidate.selected = select;
            selection.candidates.push_back(std::move(candidate));

            trace.WriteFmt(TracePrefix::GpuVendor,
                "adapter_candidate index=%u vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X revision=0x%02X "
                "pci=%04X:%02X:%02X.%u vendor=%s match_rank=%d dedicated_gb=%.2f name=\"%s\"",
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
                matchRank,
                DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes),
                adapterName.c_str());
            adapter->Release();
            continue;
        }

        trace.WriteFmt(TracePrefix::GpuVendor,
            "adapter_skip index=%u hr=0x%08X software=%s name=\"%s\"",
            adapterIndex,
            static_cast<unsigned int>(descHr),
            Trace::BoolText(software),
            adapterName.c_str());
        adapter->Release();
    }

    factory->Release();
    if (selection.selectedAdapter.has_value()) {
        const GpuVendor vendor = SelectGpuVendor(*selection.selectedAdapter);
        trace.WriteFmt(TracePrefix::GpuVendor,
            "adapter_selected index=%u vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X revision=0x%02X "
            "pci=%04X:%02X:%02X.%u vendor=%s preferred=\"%s\" name=\"%s\"",
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
        trace.Write(TracePrefix::GpuVendor, "adapter_selected none");
    }
    return selection;
}

std::optional<GpuVendorInfo> ExtractPrimaryGpuVendorInfo(Trace& trace) {
    return ResolveGpuAdapterSelection(trace, {}).selectedAdapter;
}

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(
    Trace& trace, const std::optional<GpuVendorInfo>& adapter) {
    const GpuVendor vendor = adapter.has_value() ? SelectGpuVendor(*adapter) : GpuVendor::Unknown;
    trace.WriteFmt(TracePrefix::GpuVendor,
        "create vendor=%s adapter=\"%s\"",
        GpuVendorName(vendor),
        adapter.has_value() ? adapter->adapterName.c_str() : "");
    return CreateGpuVendorProviderForVendor(trace, vendor, adapter);
}
