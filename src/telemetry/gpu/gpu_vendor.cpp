#include "telemetry/gpu/gpu_vendor.h"

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
#include "util/trace.h"
#include "util/utf8.h"

namespace {

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
        char buffer[256];
        sprintf_s(buffer,
            "initialize vendor_id=0x%04X name=\"%s\" fps=\"%s\"",
            vendorId,
            adapterName.c_str(),
            fpsDiagnostics_.c_str());
        trace_.Write(TracePrefix::UnsupportedGpu, buffer);
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
            trace_.WriteLazy(TracePrefix::UnsupportedGpu, [&] {
                return std::string("get_presented_fps available=") + Trace::BoolText(fpsSample.fps.has_value()) +
                       " value=" +
                       (fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1)
                                                  : std::string("fps=N/A")) +
                       " process=\"" + fpsSample.processName + "\" diagnostics=\"" + fpsSample.diagnostics + "\"";
            });
        }

        sample.diagnostics += " fps=" + fpsDiagnostics_;
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
        return CreateNvidiaGpuTelemetryProvider(trace);
    }
    if (vendor == GpuVendor::Amd) {
        return CreateAmdGpuTelemetryProvider(trace);
    }
    if (vendor == GpuVendor::Intel) {
        return CreateIntelGpuTelemetryProvider(trace, adapter.has_value() ? adapter->adapterName : std::string());
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

}  // namespace

GpuAdapterSelection ResolveGpuAdapterSelection(Trace& trace, std::string_view preferredAdapterName) {
    GpuAdapterSelection selection;
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        char buffer[128];
        sprintf_s(buffer, "adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        trace.Write(TracePrefix::GpuVendor, buffer);
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
            char buffer[128];
            sprintf_s(buffer, "adapter_enum index=%u hr=0x%08X", adapterIndex, static_cast<unsigned int>(enumHr));
            trace.Write(TracePrefix::GpuVendor, buffer);
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        const bool software = SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
        if (SUCCEEDED(descHr) && !software) {
            GpuVendorInfo info{
                desc.VendorId, adapterName, adapterIndex, static_cast<std::uint64_t>(desc.DedicatedVideoMemory)};
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

            char buffer[320];
            sprintf_s(buffer,
                "adapter_candidate index=%u vendor_id=0x%04X vendor=%s match_rank=%d dedicated_gb=%.2f name=\"%s\"",
                adapterIndex,
                desc.VendorId,
                GpuVendorName(vendor),
                matchRank,
                DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes),
                adapterName.c_str());
            trace.Write(TracePrefix::GpuVendor, buffer);
            adapter->Release();
            continue;
        }

        char buffer[256];
        sprintf_s(buffer,
            "adapter_skip index=%u hr=0x%08X software=%s name=\"%s\"",
            adapterIndex,
            static_cast<unsigned int>(descHr),
            Trace::BoolText(software),
            adapterName.c_str());
        trace.Write(TracePrefix::GpuVendor, buffer);
        adapter->Release();
    }

    factory->Release();
    if (selection.selectedAdapter.has_value()) {
        const GpuVendor vendor = SelectGpuVendor(*selection.selectedAdapter);
        char buffer[320];
        sprintf_s(buffer,
            "adapter_selected index=%u vendor_id=0x%04X vendor=%s preferred=\"%s\" name=\"%s\"",
            selection.selectedAdapter->adapterIndex,
            selection.selectedAdapter->vendorId,
            GpuVendorName(vendor),
            std::string(preferredAdapterName).c_str(),
            selection.selectedAdapter->adapterName.c_str());
        trace.Write(TracePrefix::GpuVendor, buffer);
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
    trace.Write(TracePrefix::GpuVendor,
        std::string("create vendor=") + GpuVendorName(vendor) + " adapter=\"" +
            (adapter.has_value() ? adapter->adapterName : std::string()) + "\"");
    return CreateGpuVendorProviderForVendor(trace, vendor, adapter);
}
