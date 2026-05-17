#include "telemetry/gpu/intel/gpu_intel_level_zero.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using ZeResult = int;
using ZeBool = std::uint8_t;
using ZesDriver = void*;
using ZesDevice = void*;
using ZesEngine = void*;
using ZesFan = void*;
using ZesFrequency = void*;
using ZesMemory = void*;
using ZesTemperature = void*;

constexpr ZeResult kZeResultSuccess = 0;
constexpr unsigned int kIntelVendorId = 0x8086;
constexpr int kZeDeviceTypeGpu = 1;
constexpr int kZeStructureTypeDeviceProperties = 0x3;
constexpr int kZesStructureTypeDeviceProperties = 0x1;
constexpr int kZesStructureTypeEngineProperties = 0x5;
constexpr int kZesStructureTypeFanProperties = 0x7;
constexpr int kZesStructureTypeFrequencyProperties = 0x9;
constexpr int kZesStructureTypeMemoryProperties = 0xb;
constexpr int kZesStructureTypeTemperatureProperties = 0x14;
constexpr int kZesStructureTypeFrequencyState = 0x1b;
constexpr int kZesStructureTypeMemoryState = 0x1e;
constexpr int kZesEngineGroupAll = 0;
constexpr int kZesEngineGroupComputeAll = 1;
constexpr int kZesEngineGroupRenderComputeAll = 11;
constexpr int kZesEngineGroupRenderAll = 12;
constexpr int kZesFrequencyDomainGpu = 0;
constexpr int kZesMemoryLocationDevice = 1;
constexpr int kZesFanSpeedUnitsRpm = 0;
constexpr int kZesTemperatureGlobal = 0;
constexpr int kZesTemperatureGpu = 1;
constexpr int kZesTemperatureMemory = 2;
constexpr int kZesTemperatureGlobalMin = 3;
constexpr int kZesTemperatureGpuMin = 4;
constexpr int kZesTemperatureMemoryMin = 5;
constexpr int kZesTemperatureGpuBoard = 6;
constexpr int kZesTemperatureGpuBoardMin = 7;
constexpr wchar_t kLevelZeroLibraryName[] = L"ze_loader.dll";  // LoadLibraryW requires a UTF-16 DLL name.

struct ZeDeviceUuid {
    std::uint8_t id[16] = {};
};

struct ZeDeviceProperties {
    int stype = kZeStructureTypeDeviceProperties;
    void* pNext = nullptr;
    int type = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t flags = 0;
    std::uint32_t subdeviceId = 0;
    std::uint32_t coreClockRate = 0;
    std::uint64_t maxMemAllocSize = 0;
    std::uint32_t maxHardwareContexts = 0;
    std::uint32_t maxCommandQueuePriority = 0;
    std::uint32_t numThreadsPerEU = 0;
    std::uint32_t physicalEUSimdWidth = 0;
    std::uint32_t numEUsPerSubslice = 0;
    std::uint32_t numSubslicesPerSlice = 0;
    std::uint32_t numSlices = 0;
    std::uint64_t timerResolution = 0;
    std::uint32_t timestampValidBits = 0;
    std::uint32_t kernelTimestampValidBits = 0;
    ZeDeviceUuid uuid;
    char name[256] = {};
};

struct ZesDeviceProperties {
    int stype = kZesStructureTypeDeviceProperties;
    void* pNext = nullptr;
    ZeDeviceProperties core;
    std::uint32_t numSubdevices = 0;
    char serialNumber[64] = {};
    char boardNumber[64] = {};
    char brandName[64] = {};
    char modelName[64] = {};
    char vendorName[64] = {};
    char driverVersion[64] = {};
};

struct ZesEngineProperties {
    int stype = kZesStructureTypeEngineProperties;
    void* pNext = nullptr;
    int type = 0;
    ZeBool onSubdevice = 0;
    std::uint32_t subdeviceId = 0;
};

struct ZesEngineStats {
    std::uint64_t activeTime = 0;
    std::uint64_t timestamp = 0;
};

struct ZesFanProperties {
    int stype = kZesStructureTypeFanProperties;
    void* pNext = nullptr;
    ZeBool onSubdevice = 0;
    std::uint32_t subdeviceId = 0;
    ZeBool canControl = 0;
    std::uint32_t supportedModes = 0;
    std::uint32_t supportedUnits = 0;
    std::int32_t maxRpm = -1;
    std::int32_t maxPoints = -1;
};

struct ZesFrequencyProperties {
    int stype = kZesStructureTypeFrequencyProperties;
    void* pNext = nullptr;
    int type = 0;
    ZeBool onSubdevice = 0;
    std::uint32_t subdeviceId = 0;
    ZeBool canControl = 0;
    ZeBool isThrottleEventSupported = 0;
    double min = 0.0;
    double max = 0.0;
};

struct ZesFrequencyState {
    int stype = kZesStructureTypeFrequencyState;
    const void* pNext = nullptr;
    double currentVoltage = -1.0;
    double request = -1.0;
    double tdp = -1.0;
    double efficient = -1.0;
    double actual = -1.0;
    std::uint32_t throttleReasons = 0;
};

struct ZesMemoryProperties {
    int stype = kZesStructureTypeMemoryProperties;
    void* pNext = nullptr;
    int type = 0;
    ZeBool onSubdevice = 0;
    std::uint32_t subdeviceId = 0;
    int location = 0;
    std::uint64_t physicalSize = 0;
    std::int32_t busWidth = -1;
    std::int32_t numChannels = -1;
};

struct ZesMemoryState {
    int stype = kZesStructureTypeMemoryState;
    const void* pNext = nullptr;
    int health = 0;
    std::uint64_t free = 0;
    std::uint64_t size = 0;
};

struct ZesTemperatureProperties {
    int stype = kZesStructureTypeTemperatureProperties;
    void* pNext = nullptr;
    int type = 0;
    ZeBool onSubdevice = 0;
    std::uint32_t subdeviceId = 0;
    double maxTemperature = 0.0;
    ZeBool isCriticalTempSupported = 0;
    ZeBool isThreshold1Supported = 0;
    ZeBool isThreshold2Supported = 0;
};

using ZesInitFn = ZeResult(__cdecl*)(std::uint32_t);
using ZesDriverGetFn = ZeResult(__cdecl*)(std::uint32_t*, ZesDriver*);
using ZesDeviceGetFn = ZeResult(__cdecl*)(ZesDriver, std::uint32_t*, ZesDevice*);
using ZesDeviceGetPropertiesFn = ZeResult(__cdecl*)(ZesDevice, ZesDeviceProperties*);
using ZesDeviceEnumEngineGroupsFn = ZeResult(__cdecl*)(ZesDevice, std::uint32_t*, ZesEngine*);
using ZesEngineGetPropertiesFn = ZeResult(__cdecl*)(ZesEngine, ZesEngineProperties*);
using ZesEngineGetActivityFn = ZeResult(__cdecl*)(ZesEngine, ZesEngineStats*);
using ZesDeviceEnumFansFn = ZeResult(__cdecl*)(ZesDevice, std::uint32_t*, ZesFan*);
using ZesFanGetPropertiesFn = ZeResult(__cdecl*)(ZesFan, ZesFanProperties*);
using ZesFanGetStateFn = ZeResult(__cdecl*)(ZesFan, int, std::int32_t*);
using ZesDeviceEnumFrequencyDomainsFn = ZeResult(__cdecl*)(ZesDevice, std::uint32_t*, ZesFrequency*);
using ZesFrequencyGetPropertiesFn = ZeResult(__cdecl*)(ZesFrequency, ZesFrequencyProperties*);
using ZesFrequencyGetStateFn = ZeResult(__cdecl*)(ZesFrequency, ZesFrequencyState*);
using ZesDeviceEnumMemoryModulesFn = ZeResult(__cdecl*)(ZesDevice, std::uint32_t*, ZesMemory*);
using ZesMemoryGetPropertiesFn = ZeResult(__cdecl*)(ZesMemory, ZesMemoryProperties*);
using ZesMemoryGetStateFn = ZeResult(__cdecl*)(ZesMemory, ZesMemoryState*);
using ZesDeviceEnumTemperatureSensorsFn = ZeResult(__cdecl*)(ZesDevice, std::uint32_t*, ZesTemperature*);
using ZesTemperatureGetPropertiesFn = ZeResult(__cdecl*)(ZesTemperature, ZesTemperatureProperties*);
using ZesTemperatureGetStateFn = ZeResult(__cdecl*)(ZesTemperature, double*);

template <typename Function> bool LoadFunction(HMODULE module, const char* name, Function& function) {
    function = reinterpret_cast<Function>(GetProcAddress(module, name));
    return function != nullptr;
}

std::string ResultCodeString(ZeResult result) {
    switch (result) {
        case 0:
            return "success";
        case 0x70000001:
            return "device lost";
        case 0x70000002:
            return "out of host memory";
        case 0x70000003:
            return "out of device memory";
        case 0x70000006:
            return "device requires reset";
        case 0x70000007:
            return "device in low power state";
        case 0x70010000:
            return "insufficient permissions";
        case 0x70010001:
            return "not available";
        case 0x70020000:
            return "dependency unavailable";
        case 0x78000001:
            return "uninitialized";
        case 0x78000003:
            return "unsupported feature";
        case 0x78000004:
            return "invalid argument";
        case 0x78000005:
            return "invalid null handle";
        case 0x78000007:
            return "invalid null pointer";
        case 0x7800000c:
            return "invalid enumeration";
        case 0x7800000d:
            return "unsupported enumeration";
        case 0x7ffffffe:
            return "unknown";
        default:
            break;
    }
    return FormatText("0x%08X", static_cast<unsigned int>(result));
}

std::string KnownAnsiString(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    std::string value = Utf8FromAnsi(text);
    return value.empty() || EqualsInsensitive(value, "unknown") ? std::string{} : value;
}

int AdapterNameMatchRank(const ZesDeviceProperties& properties, const std::string& adapterName) {
    if (adapterName.empty()) {
        return 0;
    }

    const std::string names[] = {
        KnownAnsiString(properties.modelName),
        KnownAnsiString(properties.brandName),
        KnownAnsiString(properties.core.name),
    };
    int bestRank = 0;
    for (const std::string& name : names) {
        if (name.empty()) {
            continue;
        }
        if (EqualsInsensitive(name, adapterName)) {
            return 2;
        }
        if (ContainsInsensitive(name, adapterName) || ContainsInsensitive(adapterName, name)) {
            bestRank = 1;
        }
    }
    return bestRank;
}

bool IsKnownMetric(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool IsPreferredTemperatureType(int type) {
    return type == kZesTemperatureGpu || type == kZesTemperatureGlobal || type == kZesTemperatureGpuBoard;
}

bool IsFallbackTemperatureType(int type) {
    return type == kZesTemperatureMemory ||
           (type >= 0 && type != kZesTemperatureGlobalMin && type != kZesTemperatureGpuMin &&
               type != kZesTemperatureMemoryMin && type != kZesTemperatureGpuBoardMin);
}

std::string FormatOptionalMetric(const char* label, std::optional<double> value, int precision) {
    return value.has_value() ? Trace::FormatValueDouble(label, *value, precision) : FormatText("%s=N/A", label);
}

template <typename Handle, typename Enumerator>
ZeResult EnumerateHandles(Enumerator&& enumerate, std::vector<Handle>& handles) {
    handles.clear();
    std::uint32_t count = 0;
    ZeResult result = enumerate(&count, nullptr);
    if (result != kZeResultSuccess || count == 0) {
        return result;
    }

    handles.resize(count);
    result = enumerate(&count, handles.data());
    if (result != kZeResultSuccess) {
        handles.clear();
        return result;
    }
    if (count < handles.size()) {
        handles.resize(count);
    }
    return result;
}

class LevelZeroLibrary {
public:
    ~LevelZeroLibrary() {
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    bool Load(std::string& diagnostics) {
        module_ = LoadLibraryW(kLevelZeroLibraryName);
        if (module_ == nullptr) {
            diagnostics = "Level Zero loader not found.";
            return false;
        }

        bool loaded = true;
        loaded = LoadFunction(module_, "zesInit", sysmanInit_) && loaded;
        loaded = LoadFunction(module_, "zesDriverGet", driverGet_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceGet", deviceGet_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceGetProperties", deviceGetProperties_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceEnumEngineGroups", deviceEnumEngineGroups_) && loaded;
        loaded = LoadFunction(module_, "zesEngineGetProperties", engineGetProperties_) && loaded;
        loaded = LoadFunction(module_, "zesEngineGetActivity", engineGetActivity_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceEnumFans", deviceEnumFans_) && loaded;
        loaded = LoadFunction(module_, "zesFanGetProperties", fanGetProperties_) && loaded;
        loaded = LoadFunction(module_, "zesFanGetState", fanGetState_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceEnumFrequencyDomains", deviceEnumFrequencyDomains_) && loaded;
        loaded = LoadFunction(module_, "zesFrequencyGetProperties", frequencyGetProperties_) && loaded;
        loaded = LoadFunction(module_, "zesFrequencyGetState", frequencyGetState_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceEnumMemoryModules", deviceEnumMemoryModules_) && loaded;
        loaded = LoadFunction(module_, "zesMemoryGetProperties", memoryGetProperties_) && loaded;
        loaded = LoadFunction(module_, "zesMemoryGetState", memoryGetState_) && loaded;
        loaded = LoadFunction(module_, "zesDeviceEnumTemperatureSensors", deviceEnumTemperatureSensors_) && loaded;
        loaded = LoadFunction(module_, "zesTemperatureGetProperties", temperatureGetProperties_) && loaded;
        loaded = LoadFunction(module_, "zesTemperatureGetState", temperatureGetState_) && loaded;

        if (!loaded) {
            diagnostics = "Level Zero loader is missing required Sysman entry points.";
            return false;
        }
        return true;
    }

    ZeResult InitializeSysman() const {
        return sysmanInit_(0);
    }

    ZeResult Drivers(std::vector<ZesDriver>& drivers) const {
        return EnumerateHandles<ZesDriver>(
            [&](std::uint32_t* count, ZesDriver* values) { return driverGet_(count, values); }, drivers);
    }

    ZeResult Devices(ZesDriver driver, std::vector<ZesDevice>& devices) const {
        return EnumerateHandles<ZesDevice>(
            [&](std::uint32_t* count, ZesDevice* values) { return deviceGet_(driver, count, values); }, devices);
    }

    ZeResult DeviceProperties(ZesDevice device, ZesDeviceProperties& properties) const {
        properties = ZesDeviceProperties{};
        properties.stype = kZesStructureTypeDeviceProperties;
        return deviceGetProperties_(device, &properties);
    }

    ZeResult EngineGroups(ZesDevice device, std::vector<ZesEngine>& engines) const {
        return EnumerateHandles<ZesEngine>(
            [&](std::uint32_t* count, ZesEngine* values) { return deviceEnumEngineGroups_(device, count, values); },
            engines);
    }

    ZeResult EngineProperties(ZesEngine engine, ZesEngineProperties& properties) const {
        properties = ZesEngineProperties{};
        properties.stype = kZesStructureTypeEngineProperties;
        return engineGetProperties_(engine, &properties);
    }

    ZeResult EngineActivity(ZesEngine engine, ZesEngineStats& stats) const {
        return engineGetActivity_(engine, &stats);
    }

    ZeResult Fans(ZesDevice device, std::vector<ZesFan>& fans) const {
        return EnumerateHandles<ZesFan>(
            [&](std::uint32_t* count, ZesFan* values) { return deviceEnumFans_(device, count, values); }, fans);
    }

    ZeResult FanProperties(ZesFan fan, ZesFanProperties& properties) const {
        properties = ZesFanProperties{};
        properties.stype = kZesStructureTypeFanProperties;
        return fanGetProperties_(fan, &properties);
    }

    ZeResult FanStateRpm(ZesFan fan, std::int32_t& speed) const {
        return fanGetState_(fan, kZesFanSpeedUnitsRpm, &speed);
    }

    ZeResult FrequencyDomains(ZesDevice device, std::vector<ZesFrequency>& frequencies) const {
        return EnumerateHandles<ZesFrequency>(
            [&](std::uint32_t* count, ZesFrequency* values) {
                return deviceEnumFrequencyDomains_(device, count, values);
            },
            frequencies);
    }

    ZeResult FrequencyProperties(ZesFrequency frequency, ZesFrequencyProperties& properties) const {
        properties = ZesFrequencyProperties{};
        properties.stype = kZesStructureTypeFrequencyProperties;
        return frequencyGetProperties_(frequency, &properties);
    }

    ZeResult FrequencyState(ZesFrequency frequency, ZesFrequencyState& state) const {
        state = ZesFrequencyState{};
        state.stype = kZesStructureTypeFrequencyState;
        return frequencyGetState_(frequency, &state);
    }

    ZeResult MemoryModules(ZesDevice device, std::vector<ZesMemory>& memoryModules) const {
        return EnumerateHandles<ZesMemory>(
            [&](std::uint32_t* count, ZesMemory* values) { return deviceEnumMemoryModules_(device, count, values); },
            memoryModules);
    }

    ZeResult MemoryProperties(ZesMemory memory, ZesMemoryProperties& properties) const {
        properties = ZesMemoryProperties{};
        properties.stype = kZesStructureTypeMemoryProperties;
        return memoryGetProperties_(memory, &properties);
    }

    ZeResult MemoryState(ZesMemory memory, ZesMemoryState& state) const {
        state = ZesMemoryState{};
        state.stype = kZesStructureTypeMemoryState;
        return memoryGetState_(memory, &state);
    }

    ZeResult TemperatureSensors(ZesDevice device, std::vector<ZesTemperature>& temperatures) const {
        return EnumerateHandles<ZesTemperature>(
            [&](std::uint32_t* count, ZesTemperature* values) {
                return deviceEnumTemperatureSensors_(device, count, values);
            },
            temperatures);
    }

    ZeResult TemperatureProperties(ZesTemperature temperature, ZesTemperatureProperties& properties) const {
        properties = ZesTemperatureProperties{};
        properties.stype = kZesStructureTypeTemperatureProperties;
        return temperatureGetProperties_(temperature, &properties);
    }

    ZeResult TemperatureState(ZesTemperature temperature, double& value) const {
        return temperatureGetState_(temperature, &value);
    }

private:
    HMODULE module_ = nullptr;
    ZesInitFn sysmanInit_ = nullptr;
    ZesDriverGetFn driverGet_ = nullptr;
    ZesDeviceGetFn deviceGet_ = nullptr;
    ZesDeviceGetPropertiesFn deviceGetProperties_ = nullptr;
    ZesDeviceEnumEngineGroupsFn deviceEnumEngineGroups_ = nullptr;
    ZesEngineGetPropertiesFn engineGetProperties_ = nullptr;
    ZesEngineGetActivityFn engineGetActivity_ = nullptr;
    ZesDeviceEnumFansFn deviceEnumFans_ = nullptr;
    ZesFanGetPropertiesFn fanGetProperties_ = nullptr;
    ZesFanGetStateFn fanGetState_ = nullptr;
    ZesDeviceEnumFrequencyDomainsFn deviceEnumFrequencyDomains_ = nullptr;
    ZesFrequencyGetPropertiesFn frequencyGetProperties_ = nullptr;
    ZesFrequencyGetStateFn frequencyGetState_ = nullptr;
    ZesDeviceEnumMemoryModulesFn deviceEnumMemoryModules_ = nullptr;
    ZesMemoryGetPropertiesFn memoryGetProperties_ = nullptr;
    ZesMemoryGetStateFn memoryGetState_ = nullptr;
    ZesDeviceEnumTemperatureSensorsFn deviceEnumTemperatureSensors_ = nullptr;
    ZesTemperatureGetPropertiesFn temperatureGetProperties_ = nullptr;
    ZesTemperatureGetStateFn temperatureGetState_ = nullptr;
};

struct EngineProbe {
    ZesEngine handle = nullptr;
    int type = kZesEngineGroupAll;
    std::optional<ZesEngineStats> previous;
};

struct FanProbe {
    ZesFan handle = nullptr;
    bool rpmSupported = false;
};

struct FrequencyProbe {
    ZesFrequency handle = nullptr;
    int type = kZesFrequencyDomainGpu;
};

struct MemoryProbe {
    ZesMemory handle = nullptr;
    bool deviceLocal = false;
};

struct TemperatureProbe {
    ZesTemperature handle = nullptr;
    int type = kZesTemperatureGlobal;
};

struct MemorySample {
    double usedGb = 0.0;
    double totalGb = 0.0;
};

class IntelLevelZeroGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    IntelLevelZeroGpuTelemetryProvider(Trace& trace, std::optional<GpuVendorInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    bool Initialize() override {
        trace_.Write(TracePrefix::IntelLevelZero, "initialize_begin");
        if (!levelZero_.Load(diagnostics_)) {
            trace_.WriteFmt(TracePrefix::IntelLevelZero, "load_failed diagnostics=\"%s\"", diagnostics_.c_str());
            return false;
        }

        const ZeResult initResult = levelZero_.InitializeSysman();
        trace_.WriteFmt(TracePrefix::IntelLevelZero, "sysman_init result=\"%s\"", ResultCodeString(initResult).c_str());
        if (initResult != kZeResultSuccess) {
            diagnostics_ =
                FormatText("Level Zero Sysman initialization failed: %s", ResultCodeString(initResult).c_str());
            return false;
        }

        if (!SelectIntelGpuDevice()) {
            return false;
        }

        EnumerateMetricHandles();
        CaptureEngineBaselines();

        diagnostics_ = FormatText("Level Zero GPU=%s display_name=%s engine_groups=%zu temperature_sensors=%zu "
                                  "frequency_domains=%zu memory_modules=%zu device_memory_modules=%zu "
                                  "fan_rpm_supported=%s native_fps_supported=no",
            sysmanGpuName_.c_str(),
            gpuName_.c_str(),
            engines_.size(),
            temperatures_.size(),
            frequencies_.size(),
            memoryModules_.size(),
            deviceMemoryModuleCount_,
            Trace::BoolText(HasFanSpeedRpm()));

        fpsProvider_ = CreatePresentedFpsProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = "Presented FPS ETW provider active.";
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ =
                fpsSample.diagnostics.empty() ? "Presented FPS ETW provider unavailable." : fpsSample.diagnostics;
        }

        initialized_ = true;
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "initialize_done diagnostics=\"%s\" fps=\"%s\"",
            diagnostics_.c_str(),
            fpsDiagnostics_.c_str());
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace_.Write(TracePrefix::IntelLevelZero, "sample_begin");
        GpuVendorTelemetrySample sample;
        sample.providerName = "Intel Level Zero";
        sample.name = gpuName_;
        sample.diagnostics = diagnostics_;

        if (!initialized_ || device_ == nullptr) {
            sample.available = false;
            return sample;
        }

        bool hasAnyMetric = false;

        const std::optional<double> loadPercent = QueryLoadPercent();
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "get_engine_load %s engines=%zu",
            FormatOptionalMetric("value", loadPercent, 2).c_str(),
            engines_.size());
        if (loadPercent.has_value()) {
            sample.loadPercent = *loadPercent;
            hasAnyMetric = true;
        }

        const std::optional<double> temperatureC = QueryTemperatureC();
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "get_temperature %s sensors=%zu",
            FormatOptionalMetric("value", temperatureC, 1).c_str(),
            temperatures_.size());
        if (temperatureC.has_value()) {
            sample.temperatureC = *temperatureC;
            hasAnyMetric = true;
        }

        const std::optional<double> clockMhz = QueryClockMhz();
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "get_clock %s domains=%zu",
            FormatOptionalMetric("value", clockMhz, 1).c_str(),
            frequencies_.size());
        if (clockMhz.has_value()) {
            sample.coreClockMhz = *clockMhz;
            hasAnyMetric = true;
        }

        const std::optional<MemorySample> memory = QueryMemory();
        if (memory.has_value()) {
            trace_.WriteFmt(TracePrefix::IntelLevelZero,
                "get_memory used_gb=value=%.2f total_gb=value=%.2f",
                memory->usedGb,
                memory->totalGb);
        } else {
            trace_.Write(TracePrefix::IntelLevelZero, "get_memory used_gb=N/A total_gb=N/A");
        }
        if (memory.has_value()) {
            sample.usedVramGb = memory->usedGb;
            sample.totalVramGb = memory->totalGb;
            hasAnyMetric = true;
        }

        const std::optional<double> fanRpm = QueryFanRpm();
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "get_fan_rpm %s fans=%zu",
            FormatOptionalMetric("value", fanRpm, 0).c_str(),
            fans_.size());
        if (fanRpm.has_value()) {
            sample.fanRpm = *fanRpm;
            hasAnyMetric = true;
        }

        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            sample.fpsPermissionRequired = fpsSample.permissionRequired;
            sample.fpsAppName = fpsSample.processName;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                hasAnyMetric = true;
            }
            trace_.WriteFmt(TracePrefix::IntelLevelZero,
                "get_presented_fps available=%s value=%s process=\"%s\" diagnostics=\"%s\"",
                Trace::BoolText(fpsSample.fps.has_value()),
                FormatOptionalMetric("fps", fpsSample.fps, 1).c_str(),
                fpsSample.processName.c_str(),
                fpsSample.diagnostics.c_str());
        }

        sample.available = hasAnyMetric;
        AppendFormat(sample.diagnostics, " fps=%s", fpsDiagnostics_.c_str());
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "sample_done available=%s diagnostics=\"%s\"",
            Trace::BoolText(sample.available),
            sample.diagnostics.c_str());
        return sample;
    }

private:
    bool SelectIntelGpuDevice() {
        std::vector<ZesDriver> drivers;
        const ZeResult driverResult = levelZero_.Drivers(drivers);
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "get_drivers result=\"%s\" count=%zu",
            ResultCodeString(driverResult).c_str(),
            drivers.size());
        if (driverResult != kZeResultSuccess || drivers.empty()) {
            diagnostics_ = FormatText("Level Zero Sysman found no drivers: %s", ResultCodeString(driverResult).c_str());
            return false;
        }

        ZesDevice fallbackDevice = nullptr;
        ZesDeviceProperties fallbackProperties;
        int fallbackNameRank = -1;
        std::string fallbackMatch = "fallback";

        for (size_t driverIndex = 0; driverIndex < drivers.size(); ++driverIndex) {
            std::vector<ZesDevice> devices;
            const ZeResult deviceResult = levelZero_.Devices(drivers[driverIndex], devices);
            trace_.WriteFmt(TracePrefix::IntelLevelZero,
                "get_devices driver=%zu result=\"%s\" count=%zu",
                driverIndex,
                ResultCodeString(deviceResult).c_str(),
                devices.size());
            if (deviceResult != kZeResultSuccess) {
                continue;
            }

            for (size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex) {
                ZesDeviceProperties properties;
                const ZeResult propertiesResult = levelZero_.DeviceProperties(devices[deviceIndex], properties);
                const bool intelGpu = propertiesResult == kZeResultSuccess &&
                                      properties.core.vendorId == kIntelVendorId &&
                                      properties.core.type == kZeDeviceTypeGpu;
                const bool deviceIdMatch = intelGpu && adapter_.has_value() && adapter_->deviceId != 0 &&
                                           properties.core.deviceId == adapter_->deviceId;
                const int nameMatchRank =
                    intelGpu && adapter_.has_value() ? AdapterNameMatchRank(properties, adapter_->adapterName) : 0;
                trace_.WriteFmt(TracePrefix::IntelLevelZero,
                    "device_properties driver=%zu device=%zu result=\"%s\" vendor_id=0x%04X device_id=0x%04X "
                    "type=%d device_id_match=%s name_match_rank=%d selected=%s",
                    driverIndex,
                    deviceIndex,
                    ResultCodeString(propertiesResult).c_str(),
                    properties.core.vendorId,
                    properties.core.deviceId,
                    properties.core.type,
                    Trace::BoolText(deviceIdMatch),
                    nameMatchRank,
                    Trace::BoolText(intelGpu && (!adapter_.has_value() || deviceIdMatch)));
                if (!intelGpu) {
                    continue;
                }

                if (!adapter_.has_value() || deviceIdMatch) {
                    SelectDevice(devices[deviceIndex], properties, adapter_.has_value() ? "device_id" : "first");
                    return true;
                }
                if (nameMatchRank > fallbackNameRank) {
                    fallbackDevice = devices[deviceIndex];
                    fallbackProperties = properties;
                    fallbackNameRank = nameMatchRank;
                    fallbackMatch = nameMatchRank > 0 ? "name" : "fallback";
                }
            }
        }

        if (fallbackDevice != nullptr) {
            SelectDevice(fallbackDevice, fallbackProperties, fallbackMatch.c_str());
            return true;
        }

        diagnostics_ = "Level Zero Sysman found no Intel GPU devices.";
        return false;
    }

    static std::string ResolveGpuName(const ZesDeviceProperties& properties) {
        std::string name = KnownAnsiString(properties.modelName);
        if (!name.empty()) {
            return name;
        }
        name = KnownAnsiString(properties.brandName);
        if (!name.empty()) {
            return name;
        }
        name = KnownAnsiString(properties.core.name);
        return name.empty() ? std::string("Intel GPU") : name;
    }

    void SelectDevice(ZesDevice device, const ZesDeviceProperties& properties, const char* matchKind) {
        device_ = device;
        sysmanGpuName_ = ResolveGpuName(properties);
        gpuName_ = matchKind != nullptr && std::string_view(matchKind) == "device_id" && adapter_.has_value() &&
                           !adapter_->adapterName.empty()
                       ? adapter_->adapterName
                       : sysmanGpuName_;
        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "device_selected match=\"%s\" sysman_name=\"%s\" display_name=\"%s\" selected_adapter=\"%s\"",
            matchKind != nullptr ? matchKind : "unknown",
            sysmanGpuName_.c_str(),
            gpuName_.c_str(),
            adapter_.has_value() ? adapter_->adapterName.c_str() : "");
    }

    void EnumerateMetricHandles() {
        std::vector<ZesEngine> engineHandles;
        engineEnumResult_ = levelZero_.EngineGroups(device_, engineHandles);
        for (ZesEngine engine : engineHandles) {
            ZesEngineProperties properties;
            if (levelZero_.EngineProperties(engine, properties) == kZeResultSuccess) {
                engines_.push_back(EngineProbe{engine, properties.type, std::nullopt});
            }
        }

        std::vector<ZesFan> fanHandles;
        fanEnumResult_ = levelZero_.Fans(device_, fanHandles);
        for (ZesFan fan : fanHandles) {
            ZesFanProperties properties;
            if (levelZero_.FanProperties(fan, properties) == kZeResultSuccess) {
                const bool rpmSupported = (properties.supportedUnits & (1u << kZesFanSpeedUnitsRpm)) != 0;
                fans_.push_back(FanProbe{fan, rpmSupported});
            }
        }

        std::vector<ZesFrequency> frequencyHandles;
        frequencyEnumResult_ = levelZero_.FrequencyDomains(device_, frequencyHandles);
        for (ZesFrequency frequency : frequencyHandles) {
            ZesFrequencyProperties properties;
            if (levelZero_.FrequencyProperties(frequency, properties) == kZeResultSuccess) {
                frequencies_.push_back(FrequencyProbe{frequency, properties.type});
            }
        }

        std::vector<ZesMemory> memoryHandles;
        memoryEnumResult_ = levelZero_.MemoryModules(device_, memoryHandles);
        for (ZesMemory memory : memoryHandles) {
            ZesMemoryProperties properties;
            if (levelZero_.MemoryProperties(memory, properties) == kZeResultSuccess) {
                const bool deviceLocal = properties.location == kZesMemoryLocationDevice;
                if (deviceLocal) {
                    ++deviceMemoryModuleCount_;
                }
                memoryModules_.push_back(MemoryProbe{memory, deviceLocal});
            }
        }

        std::vector<ZesTemperature> temperatureHandles;
        temperatureEnumResult_ = levelZero_.TemperatureSensors(device_, temperatureHandles);
        for (ZesTemperature temperature : temperatureHandles) {
            ZesTemperatureProperties properties;
            if (levelZero_.TemperatureProperties(temperature, properties) == kZeResultSuccess) {
                temperatures_.push_back(TemperatureProbe{temperature, properties.type});
            }
        }

        trace_.WriteFmt(TracePrefix::IntelLevelZero,
            "enumerate_metrics engines=%zu result=\"%s\" fans=%zu result=\"%s\" frequencies=%zu result=\"%s\" "
            "memory_modules=%zu result=\"%s\" temperature_sensors=%zu result=\"%s\"",
            engines_.size(),
            ResultCodeString(engineEnumResult_).c_str(),
            fans_.size(),
            ResultCodeString(fanEnumResult_).c_str(),
            frequencies_.size(),
            ResultCodeString(frequencyEnumResult_).c_str(),
            memoryModules_.size(),
            ResultCodeString(memoryEnumResult_).c_str(),
            temperatures_.size(),
            ResultCodeString(temperatureEnumResult_).c_str());
    }

    void CaptureEngineBaselines() {
        for (EngineProbe& engine : engines_) {
            ZesEngineStats stats{};
            if (levelZero_.EngineActivity(engine.handle, stats) == kZeResultSuccess) {
                engine.previous = stats;
            }
        }
    }

    bool HasFanSpeedRpm() const {
        return std::any_of(fans_.begin(), fans_.end(), [](const FanProbe& fan) { return fan.rpmSupported; });
    }

    std::optional<double> QueryLoadPercent() {
        struct EngineLoad {
            int type = kZesEngineGroupAll;
            double value = 0.0;
        };

        std::vector<EngineLoad> loads;
        loads.reserve(engines_.size());
        for (EngineProbe& engine : engines_) {
            ZesEngineStats stats{};
            if (levelZero_.EngineActivity(engine.handle, stats) != kZeResultSuccess) {
                continue;
            }

            if (engine.previous.has_value() && stats.timestamp > engine.previous->timestamp &&
                stats.activeTime >= engine.previous->activeTime) {
                const double activeDelta = static_cast<double>(stats.activeTime - engine.previous->activeTime);
                const double timeDelta = static_cast<double>(stats.timestamp - engine.previous->timestamp);
                if (timeDelta > 0.0) {
                    loads.push_back(EngineLoad{engine.type, std::clamp((activeDelta * 100.0) / timeDelta, 0.0, 100.0)});
                }
            }
            engine.previous = stats;
        }

        if (loads.empty()) {
            return std::nullopt;
        }

        const auto findByType = [&](int type) -> std::optional<double> {
            for (const EngineLoad& load : loads) {
                if (load.type == type) {
                    return load.value;
                }
            }
            return std::nullopt;
        };

        if (std::optional<double> value = findByType(kZesEngineGroupAll); value.has_value()) {
            return value;
        }
        if (std::optional<double> value = findByType(kZesEngineGroupRenderAll); value.has_value()) {
            return value;
        }
        if (std::optional<double> value = findByType(kZesEngineGroupRenderComputeAll); value.has_value()) {
            return value;
        }
        if (std::optional<double> value = findByType(kZesEngineGroupComputeAll); value.has_value()) {
            return value;
        }

        double maxLoad = 0.0;
        for (const EngineLoad& load : loads) {
            maxLoad = std::max(maxLoad, load.value);
        }
        return maxLoad;
    }

    std::optional<double> QueryTemperatureC() const {
        std::optional<double> preferred;
        std::optional<double> fallback;
        for (const TemperatureProbe& temperature : temperatures_) {
            double value = 0.0;
            if (levelZero_.TemperatureState(temperature.handle, value) != kZeResultSuccess || !IsKnownMetric(value)) {
                continue;
            }
            if (IsPreferredTemperatureType(temperature.type)) {
                preferred = preferred.has_value() ? std::max(*preferred, value) : value;
            } else if (IsFallbackTemperatureType(temperature.type)) {
                fallback = fallback.has_value() ? std::max(*fallback, value) : value;
            }
        }
        return preferred.has_value() ? preferred : fallback;
    }

    std::optional<double> QueryClockMhz() const {
        std::optional<double> preferred;
        std::optional<double> fallback;
        for (const FrequencyProbe& frequency : frequencies_) {
            ZesFrequencyState state;
            if (levelZero_.FrequencyState(frequency.handle, state) != kZeResultSuccess) {
                continue;
            }
            std::optional<double> value;
            if (IsKnownMetric(state.actual)) {
                value = state.actual;
            } else if (IsKnownMetric(state.request)) {
                value = state.request;
            } else if (IsKnownMetric(state.tdp)) {
                value = state.tdp;
            }
            if (!value.has_value()) {
                continue;
            }
            if (frequency.type == kZesFrequencyDomainGpu) {
                preferred = preferred.has_value() ? std::max(*preferred, *value) : *value;
            } else {
                fallback = fallback.has_value() ? std::max(*fallback, *value) : *value;
            }
        }
        return preferred.has_value() ? preferred : fallback;
    }

    std::optional<MemorySample> QueryMemory() const {
        std::uint64_t totalBytes = 0;
        std::uint64_t usedBytes = 0;
        for (const MemoryProbe& memory : memoryModules_) {
            if (!memory.deviceLocal) {
                continue;
            }

            ZesMemoryProperties properties;
            ZesMemoryState state;
            if (levelZero_.MemoryProperties(memory.handle, properties) != kZeResultSuccess ||
                levelZero_.MemoryState(memory.handle, state) != kZeResultSuccess) {
                continue;
            }

            const std::uint64_t moduleTotal = properties.physicalSize > 0 ? properties.physicalSize : state.size;
            if (moduleTotal == 0 || state.free > moduleTotal) {
                continue;
            }
            totalBytes += moduleTotal;
            usedBytes += moduleTotal - state.free;
        }

        if (totalBytes == 0) {
            return std::nullopt;
        }

        constexpr double bytesPerGb = 1024.0 * 1024.0 * 1024.0;
        return MemorySample{static_cast<double>(usedBytes) / bytesPerGb, static_cast<double>(totalBytes) / bytesPerGb};
    }

    std::optional<double> QueryFanRpm() const {
        for (const FanProbe& fan : fans_) {
            if (!fan.rpmSupported) {
                continue;
            }
            std::int32_t speed = -1;
            if (levelZero_.FanStateRpm(fan.handle, speed) == kZeResultSuccess && speed >= 0) {
                return static_cast<double>(speed);
            }
        }
        return std::nullopt;
    }

    Trace& trace_;
    LevelZeroLibrary levelZero_;
    ZesDevice device_ = nullptr;
    std::optional<GpuVendorInfo> adapter_;
    std::string sysmanGpuName_ = "Intel GPU";
    std::string gpuName_ = "Intel GPU";
    std::string diagnostics_ = "Level Zero provider not initialized.";
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
    std::vector<EngineProbe> engines_;
    std::vector<FanProbe> fans_;
    std::vector<FrequencyProbe> frequencies_;
    std::vector<MemoryProbe> memoryModules_;
    std::vector<TemperatureProbe> temperatures_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    size_t deviceMemoryModuleCount_ = 0;
    ZeResult engineEnumResult_ = kZeResultSuccess;
    ZeResult fanEnumResult_ = kZeResultSuccess;
    ZeResult frequencyEnumResult_ = kZeResultSuccess;
    ZeResult memoryEnumResult_ = kZeResultSuccess;
    ZeResult temperatureEnumResult_ = kZeResultSuccess;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateIntelGpuTelemetryProvider(
    Trace& trace, std::optional<GpuVendorInfo> adapter) {
    return std::make_unique<IntelLevelZeroGpuTelemetryProvider>(trace, std::move(adapter));
}
