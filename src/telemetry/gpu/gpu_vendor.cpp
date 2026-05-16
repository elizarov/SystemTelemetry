#include "telemetry/gpu/gpu_vendor.h"

#include <dxgi.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/amd/gpu_amd_adl.h"
#include "telemetry/gpu/gpu_vendor_selection.h"
#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"
#include "util/text_format.h"
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
        sample_.diagnostics = "No supported GPU telemetry provider matches the primary adapter vendor.";
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
        return CreateNvidiaGpuTelemetryProvider(trace);
    }
    if (vendor == GpuVendor::Amd) {
        return CreateAmdGpuTelemetryProvider(trace);
    }
    return std::make_unique<UnsupportedGpuTelemetryProvider>(trace, std::move(adapter));
}

}  // namespace

std::optional<GpuVendorInfo> ExtractPrimaryGpuVendorInfo(Trace& trace) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        trace.WriteFmt(TracePrefix::GpuVendor, "adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        return std::nullopt;
    }

    std::optional<GpuVendorInfo> selected;
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
            selected = GpuVendorInfo{desc.VendorId, adapterName};
            const GpuVendor vendor = SelectGpuVendor(*selected);
            trace.WriteFmt(TracePrefix::GpuVendor,
                "adapter_selected index=%u vendor_id=0x%04X vendor=%s name=\"%s\"",
                adapterIndex,
                desc.VendorId,
                GpuVendorName(vendor),
                adapterName.c_str());
            adapter->Release();
            break;
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
    if (!selected.has_value()) {
        trace.Write(TracePrefix::GpuVendor, "adapter_selected none");
    }
    return selected;
}

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(Trace& trace) {
    std::optional<GpuVendorInfo> adapter = ExtractPrimaryGpuVendorInfo(trace);
    const GpuVendor vendor = adapter.has_value() ? SelectGpuVendor(*adapter) : GpuVendor::Unknown;
    trace.WriteFmt(TracePrefix::GpuVendor, "create vendor=%s", GpuVendorName(vendor));
    return CreateGpuVendorProviderForVendor(trace, vendor, std::move(adapter));
}
