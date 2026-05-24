#include "telemetry/gpu/gpu_vendor.h"

#include <cstdint>
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
#include "telemetry/impl/hdi.h"
#include "telemetry/impl/hdi_gpu_discovery.h"
#include "util/resource_strings.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

struct EnumeratedGpuAdapter {
    GpuAdapterInfo info;
    GpuVendor vendor = GpuVendor::Unknown;
};

class UnsupportedGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    UnsupportedGpuTelemetryProvider(Trace& trace, std::optional<GpuAdapterInfo> adapter, bool collectPresentedFps)
        : trace_(trace), adapter_(std::move(adapter)), collectPresentedFps_(collectPresentedFps) {}

    bool Initialize() override {
        sample_.providerName = "Unsupported GPU";
        sample_.available = false;
        sample_.diagnostics =
            ResourceStringText(RES_STR("No supported GPU telemetry provider matches the selected adapter vendor."));
        if (collectPresentedFps_) {
            fpsProvider_ = CreatePresentedFpsProvider(trace_, adapter_);
            if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
                fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider active."));
            } else {
                const FpsTelemetrySample fpsSample =
                    fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
                fpsDiagnostics_ = fpsSample.diagnostics.empty()
                                      ? ResourceStringText(RES_STR("Presented FPS ETW provider unavailable."))
                                      : fpsSample.diagnostics;
            }
        } else {
            fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS collection not requested by layout."));
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
    bool collectPresentedFps_ = false;
};

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorProviderForVendor(Trace& trace,
    GpuVendor vendor,
    std::optional<GpuAdapterInfo> adapter,
    bool collectPresentedFps,
    const HardwareDependencyInjection* injection) {
    if (vendor == GpuVendor::Nvidia) {
        return CreateNvidiaGpuTelemetryProvider(trace, adapter, collectPresentedFps, injection);
    }
    if (vendor == GpuVendor::Amd) {
        return CreateAmdGpuTelemetryProvider(trace, adapter, collectPresentedFps);
    }
    if (vendor == GpuVendor::Intel) {
        return CreateIntelGpuTelemetryProvider(trace, adapter, collectPresentedFps);
    }
    return std::make_unique<UnsupportedGpuTelemetryProvider>(trace, std::move(adapter), collectPresentedFps);
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

}  // namespace

GpuAdapterSelection ResolveGpuAdapterSelection(Trace& trace, std::string_view preferredAdapterName) {
    return ResolveGpuAdapterSelection(trace, preferredAdapterName, nullptr);
}

GpuAdapterSelection ResolveGpuAdapterSelection(
    Trace& trace, std::string_view preferredAdapterName, const HardwareDependencyInjection* injection) {
    GpuAdapterSelection selection;
    std::vector<EnumeratedGpuAdapter> adapters;
    std::unique_ptr<GpuDiscoveryHdi> discovery = ResolveHdiFactory(injection).CreateGpuDiscoveryHdi(trace);
    if (discovery == nullptr) {
        trace.Write(TracePrefix::GpuVendor, RES_STR("adapter_discovery result=null"));
        return selection;
    }

    for (GpuAdapterInfo info : discovery->EnumerateAdapters()) {
        const GpuVendor vendor = SelectGpuVendor(info);
        const std::optional<size_t> duplicateIndex = FindDuplicateGpuAdapterIndex(adapters, info);
        if (duplicateIndex.has_value()) {
            const bool replace =
                !HasUsableGpuPciAddress(adapters[*duplicateIndex].info) && HasUsableGpuPciAddress(info);
            trace.WriteFmt(TracePrefix::GpuVendor,
                RES_STR("adapter_duplicate index=%u duplicate_of=%u replace=%s vendor_id=0x%04X "
                        "device_id=0x%04X subsystem_id=0x%08X revision=0x%02X pci=%04X:%02X:%02X.%u "
                        "vendor=%s match_rank=%d dedicated_gb=%.2f name=\"%s\""),
                info.adapterIndex,
                adapters[*duplicateIndex].info.adapterIndex,
                Trace::BoolText(replace),
                info.vendorId,
                info.deviceId,
                info.subSysId,
                info.revision,
                info.pciDomain,
                info.pciBus,
                info.pciDevice,
                info.pciFunction,
                GpuVendorName(vendor),
                GpuAdapterSelectionMatchRank(info, preferredAdapterName),
                DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes),
                info.adapterName.c_str());
            if (replace) {
                adapters[*duplicateIndex] = EnumeratedGpuAdapter{info, vendor};
            }
            continue;
        }

        adapters.push_back(EnumeratedGpuAdapter{info, vendor});

        trace.WriteFmt(TracePrefix::GpuVendor,
            RES_STR("adapter_candidate index=%u vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X revision=0x%02X "
                    "luid=0x%08x:0x%08x pci=%04X:%02X:%02X.%u vendor=%s match_rank=%d dedicated_gb=%.2f "
                    "name=\"%s\""),
            info.adapterIndex,
            info.vendorId,
            info.deviceId,
            info.subSysId,
            info.revision,
            static_cast<unsigned int>(info.adapterLuidHighPart),
            static_cast<unsigned int>(info.adapterLuidLowPart),
            info.pciDomain,
            info.pciBus,
            info.pciDevice,
            info.pciFunction,
            GpuVendorName(vendor),
            GpuAdapterSelectionMatchRank(info, preferredAdapterName),
            DedicatedVideoMemoryGb(info.dedicatedVideoMemoryBytes),
            info.adapterName.c_str());
    }
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
                    "luid=0x%08x:0x%08x pci=%04X:%02X:%02X.%u vendor=%s preferred=\"%s\" name=\"%s\""),
            selection.selectedAdapter->adapterIndex,
            selection.selectedAdapter->vendorId,
            selection.selectedAdapter->deviceId,
            selection.selectedAdapter->subSysId,
            selection.selectedAdapter->revision,
            static_cast<unsigned int>(selection.selectedAdapter->adapterLuidHighPart),
            static_cast<unsigned int>(selection.selectedAdapter->adapterLuidLowPart),
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
    Trace& trace, const std::optional<GpuAdapterInfo>& adapter, bool collectPresentedFps) {
    return CreateGpuVendorTelemetryProvider(trace, adapter, collectPresentedFps, nullptr);
}

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(Trace& trace,
    const std::optional<GpuAdapterInfo>& adapter,
    bool collectPresentedFps,
    const HardwareDependencyInjection* injection) {
    const GpuVendor vendor = adapter.has_value() ? SelectGpuVendor(*adapter) : GpuVendor::Unknown;
    trace.WriteFmt(TracePrefix::GpuVendor,
        RES_STR("create vendor=%s adapter=\"%s\" collect_presented_fps=%s"),
        GpuVendorName(vendor),
        adapter.has_value() ? adapter->adapterName.c_str() : "",
        Trace::BoolText(collectPresentedFps));
    return CreateGpuVendorProviderForVendor(trace, vendor, adapter, collectPresentedFps, injection);
}
