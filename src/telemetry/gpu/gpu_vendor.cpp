#include "telemetry/gpu/gpu_vendor.h"

#include <dxgi.h>
#include <memory>
#include <optional>
#include <string>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/amd/gpu_amd_adl.h"
#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

constexpr UINT kNvidiaVendorId = 0x10de;
constexpr UINT kAmdVendorId = 0x1002;

enum class GpuVendor {
    Unknown,
    Amd,
    Nvidia,
};

struct PrimaryGpuAdapter {
    std::string name;
    UINT vendorId = 0;
    GpuVendor vendor = GpuVendor::Unknown;
};

std::string VendorName(GpuVendor vendor) {
    switch (vendor) {
        case GpuVendor::Amd:
            return "AMD";
        case GpuVendor::Nvidia:
            return "NVIDIA";
        case GpuVendor::Unknown:
        default:
            return "Unknown";
    }
}

GpuVendor ClassifyGpuVendor(UINT vendorId, const std::string& adapterName) {
    if (vendorId == kNvidiaVendorId || ContainsInsensitive(adapterName, "nvidia")) {
        return GpuVendor::Nvidia;
    }
    if (vendorId == kAmdVendorId || ContainsInsensitive(adapterName, "amd") ||
        ContainsInsensitive(adapterName, "radeon")) {
        return GpuVendor::Amd;
    }
    return GpuVendor::Unknown;
}

std::optional<PrimaryGpuAdapter> QueryPrimaryGpuAdapter(Trace& trace) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        char buffer[128];
        sprintf_s(buffer, "gpu_vendor:adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        trace.Write(buffer);
        return std::nullopt;
    }

    std::optional<PrimaryGpuAdapter> selected;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            trace.Write("gpu_vendor:adapter_enum done");
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            char buffer[128];
            sprintf_s(
                buffer, "gpu_vendor:adapter_enum index=%u hr=0x%08X", adapterIndex, static_cast<unsigned int>(enumHr));
            trace.Write(buffer);
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        const bool software = SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
        if (SUCCEEDED(descHr) && !software) {
            selected = PrimaryGpuAdapter{adapterName, desc.VendorId, ClassifyGpuVendor(desc.VendorId, adapterName)};
            char buffer[256];
            sprintf_s(buffer,
                "gpu_vendor:adapter_selected index=%u vendor_id=0x%04X vendor=%s name=\"%s\"",
                adapterIndex,
                desc.VendorId,
                VendorName(selected->vendor).c_str(),
                adapterName.c_str());
            trace.Write(buffer);
            adapter->Release();
            break;
        }

        char buffer[256];
        sprintf_s(buffer,
            "gpu_vendor:adapter_skip index=%u hr=0x%08X software=%s name=\"%s\"",
            adapterIndex,
            static_cast<unsigned int>(descHr),
            Trace::BoolText(software).c_str(),
            adapterName.c_str());
        trace.Write(buffer);
        adapter->Release();
    }

    factory->Release();
    if (!selected.has_value()) {
        trace.Write("gpu_vendor:adapter_selected none");
    }
    return selected;
}

class UnsupportedGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    UnsupportedGpuTelemetryProvider(Trace& trace, std::optional<PrimaryGpuAdapter> adapter)
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

        const std::string adapterName = adapter_.has_value() ? adapter_->name : std::string();
        const UINT vendorId = adapter_.has_value() ? adapter_->vendorId : 0;
        char buffer[256];
        sprintf_s(buffer,
            "unsupported_gpu:initialize vendor_id=0x%04X name=\"%s\" fps=\"%s\"",
            vendorId,
            adapterName.c_str(),
            fpsDiagnostics_.c_str());
        trace_.Write(buffer);
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        GpuVendorTelemetrySample sample = sample_;
        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            sample.fpsPermissionRequired = fpsSample.permissionRequired;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                sample.fpsAppName = fpsSample.processName;
                sample.available = true;
            }
            trace_.WriteLazy([&] {
                return "unsupported_gpu:get_presented_fps available=" + Trace::BoolText(fpsSample.fps.has_value()) +
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
    std::optional<PrimaryGpuAdapter> adapter_;
    GpuVendorTelemetrySample sample_;
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(Trace& trace) {
    std::optional<PrimaryGpuAdapter> adapter = QueryPrimaryGpuAdapter(trace);
    const GpuVendor vendor = adapter.has_value() ? adapter->vendor : GpuVendor::Unknown;
    trace.Write("gpu_vendor:create vendor=" + VendorName(vendor));
    if (vendor == GpuVendor::Nvidia) {
        return CreateNvidiaGpuTelemetryProvider(trace);
    }
    if (vendor == GpuVendor::Amd) {
        return CreateAmdGpuTelemetryProvider(trace);
    }
    return std::make_unique<UnsupportedGpuTelemetryProvider>(trace, std::move(adapter));
}
