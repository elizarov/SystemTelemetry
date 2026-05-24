#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/telemetry_settings.h"
#include "telemetry/board/msi/impl/hdi_msi_center.h"
#include "telemetry/gpu/nvidia/impl/hdi_nvapi.h"
#include "telemetry/gpu/nvidia/impl/hdi_nvml.h"
#include "telemetry/impl/hdi.h"
#include "telemetry/impl/hdi_board_discovery.h"
#include "telemetry/impl/hdi_gpu_discovery.h"
#include "telemetry/impl/hdi_gpu_performance.h"
#include "telemetry/telemetry.h"
#include "util/file_path.h"

namespace {

constexpr char kMsiManufacturer[] = "Micro-Star International Co., Ltd.";
constexpr char kMsiProduct[] = "MPG Z690 CARBON WIFI (MS-7D30)";
constexpr char kNvidiaAdapterName[] = "NVIDIA GeForce RTX 3080";
constexpr wchar_t kMsiCpuTemperatureSensor[] = L"CPU";   // Managed bridge boundary has no A API.
constexpr wchar_t kMsiCpuFanSensor[] = L"CPU Fan 1";     // Managed bridge boundary has no A API.
constexpr wchar_t kMsiSystemFanSensor[] = L"SYS Fan 1";  // Managed bridge boundary has no A API.

struct HdiCallLog {
    int gpuDiscoveryCreates = 0;
    int gpuDiscoveryEnumerations = 0;
    int gpuPerformanceCreates = 0;
    int gpuPerformanceInitializes = 0;
    int gpuPerformanceLoadSamples = 0;
    int gpuPerformanceMemorySamples = 0;
    int boardDiscoveryCreates = 0;
    int boardDiscoveryReads = 0;
    int nvmlCreates = 0;
    int nvmlLoads = 0;
    int nvmlInitializes = 0;
    int nvmlDeviceCounts = 0;
    int nvmlDeviceHandles = 0;
    int nvmlDeviceNames = 0;
    int nvmlPciInfos = 0;
    int nvmlMemoryInfos = 0;
    int nvmlTemperatures = 0;
    int nvmlFanRpms = 0;
    int nvapiCreates = 0;
    int nvapiLoads = 0;
    int nvapiInitializes = 0;
    int nvapiEnumerations = 0;
    int nvapiNames = 0;
    int nvapiGraphicsClocks = 0;
    int msiCenterCreates = 0;
    int msiCenterDirectoryReads = 0;
    int msiCenterCaptures = 0;
};

GpuAdapterInfo MsiMachineGpuAdapter() {
    GpuAdapterInfo adapter;
    adapter.vendorId = 0x10DE;
    adapter.adapterName = kNvidiaAdapterName;
    adapter.adapterIndex = 0;
    adapter.dedicatedVideoMemoryBytes = 10ull * 1024ull * 1024ull * 1024ull;
    adapter.deviceId = 0x2216;
    adapter.subSysId = 0x389B1462;
    adapter.revision = 0xA1;
    adapter.hasAdapterLuid = true;
    adapter.adapterLuidHighPart = 0x12345678;
    adapter.adapterLuidLowPart = 0x9ABCDEF0;
    adapter.hasPciAddress = true;
    adapter.pciBus = 8;
    return adapter;
}

TelemetrySettings MsiMachineTelemetrySettings() {
    TelemetrySettings settings;
    settings.board.requestedTemperatureNames = {"cpu"};
    settings.board.requestedFanNames = {"cpu", "system"};
    settings.board.temperatureSensorNames["cpu"] = "CPU";
    settings.board.fanSensorNames["cpu"] = "CPU Fan 1";
    settings.board.fanSensorNames["system"] = "SYS Fan 1";
    settings.collectPresentedFps = false;
    return settings;
}

void CopyAscii(char* buffer, unsigned int bufferSize, const char* text) {
    ASSERT_NE(buffer, nullptr);
    ASSERT_GT(bufferSize, 0u);
    unsigned int index = 0;
    while (index + 1 < bufferSize && text[index] != '\0') {
        buffer[index] = text[index];
        ++index;
    }
    buffer[index] = '\0';
}

class MsiGpuDiscoveryHdi final : public GpuDiscoveryHdi {
public:
    explicit MsiGpuDiscoveryHdi(HdiCallLog& log) : log_(log) {}

    std::vector<GpuAdapterInfo> EnumerateAdapters() override {
        ++log_.gpuDiscoveryEnumerations;
        return {MsiMachineGpuAdapter()};
    }

private:
    HdiCallLog& log_;
};

class MsiBoardDiscoveryHdi final : public BoardDiscoveryHdi {
public:
    explicit MsiBoardDiscoveryHdi(HdiCallLog& log) : log_(log) {}

    BoardVendorInfo ReadBoardVendorInfo() override {
        ++log_.boardDiscoveryReads;
        return BoardVendorInfo{kMsiManufacturer, kMsiProduct};
    }

private:
    HdiCallLog& log_;
};

class MsiGpuPerformanceHdi final : public GpuPerformanceHdi {
public:
    explicit MsiGpuPerformanceHdi(HdiCallLog& log) : log_(log) {}

    void Initialize() override {
        ++log_.gpuPerformanceInitializes;
    }

    GpuPerformanceLoadSample SampleLoad(std::string_view instanceFilter, const char*) override {
        ++log_.gpuPerformanceLoadSamples;
        EXPECT_FALSE(instanceFilter.empty());
        return GpuPerformanceLoadSample{37.5, 32.0, 2};
    }

    double SampleDedicatedMemoryBytes(std::string_view, const char*) override {
        ++log_.gpuPerformanceMemorySamples;
        return 2.0 * 1024.0 * 1024.0 * 1024.0;
    }

private:
    HdiCallLog& log_;
};

class MockNvidiaNvmlHdi final : public NvidiaNvmlHdi {
public:
    explicit MockNvidiaNvmlHdi(HdiCallLog& log) : log_(log) {}

    bool Load(std::string&) override {
        ++log_.nvmlLoads;
        return true;
    }

    HdiNvmlReturn Initialize() override {
        ++log_.nvmlInitializes;
        return kHdiNvmlSuccess;
    }

    std::string ResultText(HdiNvmlReturn result) const override {
        return result == kHdiNvmlSuccess ? "OK" : std::to_string(result);
    }

    HdiNvmlReturn DeviceCount(unsigned int& count) const override {
        ++log_.nvmlDeviceCounts;
        count = 1;
        return kHdiNvmlSuccess;
    }

    HdiNvmlReturn DeviceHandleByIndex(unsigned int index, HdiNvmlDevice& device) const override {
        ++log_.nvmlDeviceHandles;
        EXPECT_EQ(index, 0u);
        device = kDevice;
        return kHdiNvmlSuccess;
    }

    HdiNvmlReturn DeviceName(HdiNvmlDevice device, char* buffer, unsigned int bufferSize) const override {
        ++log_.nvmlDeviceNames;
        EXPECT_EQ(device, kDevice);
        CopyAscii(buffer, bufferSize, kNvidiaAdapterName);
        return kHdiNvmlSuccess;
    }

    HdiNvmlReturn Temperature(HdiNvmlDevice device, unsigned int& temperatureC) const override {
        ++log_.nvmlTemperatures;
        EXPECT_EQ(device, kDevice);
        temperatureC = 63;
        return kHdiNvmlSuccess;
    }

    HdiNvmlReturn MemoryInfo(HdiNvmlDevice device, HdiNvmlMemory& memory) const override {
        ++log_.nvmlMemoryInfos;
        EXPECT_EQ(device, kDevice);
        memory.total = 10ull * 1024ull * 1024ull * 1024ull;
        memory.used = 4ull * 1024ull * 1024ull * 1024ull;
        return kHdiNvmlSuccess;
    }

    std::optional<HdiNvmlReturn> FanSpeedRpm(HdiNvmlDevice device, unsigned int& fanRpm) const override {
        ++log_.nvmlFanRpms;
        EXPECT_EQ(device, kDevice);
        fanRpm = 1420;
        return kHdiNvmlSuccess;
    }

    std::optional<HdiNvmlReturn> PciInfo(HdiNvmlDevice device, HdiNvmlPciInfo& pci) const override {
        ++log_.nvmlPciInfos;
        EXPECT_EQ(device, kDevice);
        pci.bus = 8;
        pci.pciDeviceId = (0x2216u << 16) | 0x10DEu;
        pci.pciSubSystemId = 0x389B1462;
        return kHdiNvmlSuccess;
    }

private:
    static inline HdiNvmlDevice kDevice = reinterpret_cast<HdiNvmlDevice>(0x3080);

    HdiCallLog& log_;
};

class MockNvidiaNvapiHdi final : public NvidiaNvapiHdi {
public:
    explicit MockNvidiaNvapiHdi(HdiCallLog& log) : log_(log) {}

    bool Load(std::string&) override {
        ++log_.nvapiLoads;
        return true;
    }

    HdiNvapiStatus Initialize() override {
        ++log_.nvapiInitializes;
        return kHdiNvapiOk;
    }

    HdiNvapiStatus EnumPhysicalGpus(std::vector<HdiNvapiPhysicalGpuHandle>& handles) override {
        ++log_.nvapiEnumerations;
        handles = {kGpu};
        return kHdiNvapiOk;
    }

    HdiNvapiStatus GpuFullName(HdiNvapiPhysicalGpuHandle handle, char* buffer, unsigned int bufferSize) override {
        ++log_.nvapiNames;
        EXPECT_EQ(handle, kGpu);
        CopyAscii(buffer, bufferSize, kNvidiaAdapterName);
        return kHdiNvapiOk;
    }

    HdiNvapiClockFrequencies GraphicsClock(HdiNvapiPhysicalGpuHandle handle) const override {
        ++log_.nvapiGraphicsClocks;
        EXPECT_EQ(handle, kGpu);
        HdiNvapiClockFrequencies clock;
        clock.status = kHdiNvapiOk;
        clock.graphicsPresent = true;
        clock.graphicsFrequencyKhz = 1860000;
        return clock;
    }

    std::string ResultText(HdiNvapiStatus status) const override {
        return status == kHdiNvapiOk ? "OK" : std::to_string(status);
    }

private:
    static inline HdiNvapiPhysicalGpuHandle kGpu = reinterpret_cast<HdiNvapiPhysicalGpuHandle>(0x1000);

    HdiCallLog& log_;
};

class MockMsiCenterHdi final : public MsiCenterHdi {
public:
    explicit MockMsiCenterHdi(HdiCallLog& log) : log_(log) {}

    std::optional<FilePath> FindInstalledDirectory() override {
        ++log_.msiCenterDirectoryReads;
        return FilePath("C:\\Program Files\\MSI\\MSI Center");
    }

    bool Capture(const char*, MsiCenterCaptureSink& sink) override {
        ++log_.msiCenterCaptures;
        sink.AddTemperatureReading(kMsiCpuTemperatureSensor, 44.0);
        sink.AddFanReading(kMsiCpuFanSensor, 1280.0);
        sink.AddFanReading(kMsiSystemFanSensor, 910.0);
        sink.TraceQuerySuccess(2, 1);
        return true;
    }

private:
    HdiCallLog& log_;
};

class MsiMachineHdiFactory final : public HdiFactory {
public:
    explicit MsiMachineHdiFactory(HdiCallLog& log) : log_(log) {}

    std::unique_ptr<GpuDiscoveryHdi> CreateGpuDiscoveryHdi(Trace&) override {
        ++log_.gpuDiscoveryCreates;
        return std::make_unique<MsiGpuDiscoveryHdi>(log_);
    }

    std::unique_ptr<GpuPerformanceHdi> CreateGpuPerformanceHdi(Trace&) override {
        ++log_.gpuPerformanceCreates;
        return std::make_unique<MsiGpuPerformanceHdi>(log_);
    }

    std::unique_ptr<BoardDiscoveryHdi> CreateBoardDiscoveryHdi() override {
        ++log_.boardDiscoveryCreates;
        return std::make_unique<MsiBoardDiscoveryHdi>(log_);
    }

    std::unique_ptr<NvidiaNvmlHdi> CreateNvidiaNvmlHdi(Trace&) override {
        ++log_.nvmlCreates;
        return std::make_unique<MockNvidiaNvmlHdi>(log_);
    }

    std::unique_ptr<NvidiaNvapiHdi> CreateNvidiaNvapiHdi(Trace&) override {
        ++log_.nvapiCreates;
        return std::make_unique<MockNvidiaNvapiHdi>(log_);
    }

    std::unique_ptr<MsiCenterHdi> CreateMsiCenterHdi(Trace&) override {
        ++log_.msiCenterCreates;
        return std::make_unique<MockMsiCenterHdi>(log_);
    }

private:
    HdiCallLog& log_;
};

class FailingDefaultHdiFactory final : public HdiFactory {
public:
    std::unique_ptr<GpuDiscoveryHdi> CreateGpuDiscoveryHdi(Trace&) override {
        ADD_FAILURE() << "HDI tests must inject a factory.";
        return nullptr;
    }

    std::unique_ptr<GpuPerformanceHdi> CreateGpuPerformanceHdi(Trace&) override {
        ADD_FAILURE() << "HDI tests must inject a factory.";
        return nullptr;
    }

    std::unique_ptr<BoardDiscoveryHdi> CreateBoardDiscoveryHdi() override {
        ADD_FAILURE() << "HDI tests must inject a factory.";
        return nullptr;
    }

    std::unique_ptr<NvidiaNvmlHdi> CreateNvidiaNvmlHdi(Trace&) override {
        ADD_FAILURE() << "HDI tests must inject a factory.";
        return nullptr;
    }

    std::unique_ptr<NvidiaNvapiHdi> CreateNvidiaNvapiHdi(Trace&) override {
        ADD_FAILURE() << "HDI tests must inject a factory.";
        return nullptr;
    }

    std::unique_ptr<MsiCenterHdi> CreateMsiCenterHdi(Trace&) override {
        ADD_FAILURE() << "HDI tests must inject a factory.";
        return nullptr;
    }
};

}  // namespace

HdiFactory& DefaultHdiFactory() {
    static FailingDefaultHdiFactory factory;
    return factory;
}

TEST(HardwareDependencyInterfaceTest, MsiZ690Rtx3080RuntimeCollectsInitialSnapshotThroughProviderHdis) {
    HdiCallLog log;
    MsiMachineHdiFactory factory(log);
    HardwareDependencyInjection injection{&factory};
    TelemetryCollectorOptions options;
    options.synchronousProviderSamples = true;
    Trace trace;
    std::string error;

    std::unique_ptr<TelemetryRuntime> runtime = CreateTelemetryRuntime(
        options, CurrentDirectoryPath(), MsiMachineTelemetrySettings(), trace, nullptr, &error, &injection);

    ASSERT_NE(runtime, nullptr) << error;
    const TelemetryUpdate update = runtime->Latest();
    runtime->Shutdown();

    EXPECT_GE(update.dump.snapshot.revision, 1u);
    EXPECT_EQ(update.resolvedSelections.gpuAdapterName, kNvidiaAdapterName);
    ASSERT_EQ(update.gpuAdapterCandidates.size(), 1u);
    EXPECT_EQ(update.gpuAdapterCandidates.front().adapterName, kNvidiaAdapterName);
    EXPECT_TRUE(update.gpuAdapterCandidates.front().selected);

    EXPECT_EQ(update.dump.gpuProvider.providerName, "NVIDIA NVML");
    EXPECT_TRUE(update.dump.gpuProvider.available);
    EXPECT_EQ(update.dump.snapshot.gpu.name, kNvidiaAdapterName);
    EXPECT_DOUBLE_EQ(update.dump.snapshot.gpu.loadPercent, 32.0);
    ASSERT_TRUE(update.dump.snapshot.gpu.temperature.value.has_value());
    EXPECT_DOUBLE_EQ(*update.dump.snapshot.gpu.temperature.value, 63.0);
    ASSERT_TRUE(update.dump.snapshot.gpu.clock.value.has_value());
    EXPECT_DOUBLE_EQ(*update.dump.snapshot.gpu.clock.value, 1860.0);
    ASSERT_TRUE(update.dump.snapshot.gpu.fan.value.has_value());
    EXPECT_DOUBLE_EQ(*update.dump.snapshot.gpu.fan.value, 1420.0);
    EXPECT_FALSE(update.dump.snapshot.gpu.fps.value.has_value());
    EXPECT_DOUBLE_EQ(update.dump.snapshot.gpu.vram.usedGb, 4.0);
    EXPECT_DOUBLE_EQ(update.dump.snapshot.gpu.vram.totalGb, 10.0);

    EXPECT_EQ(update.dump.boardProvider.providerName, "MSI");
    EXPECT_EQ(update.dump.boardProvider.boardManufacturer, kMsiManufacturer);
    EXPECT_EQ(update.dump.boardProvider.boardProduct, kMsiProduct);
    EXPECT_TRUE(update.dump.boardProvider.available);
    ASSERT_EQ(update.dump.snapshot.boardTemperatures.size(), 1u);
    EXPECT_EQ(update.dump.snapshot.boardTemperatures.front().name, "cpu");
    ASSERT_TRUE(update.dump.snapshot.boardTemperatures.front().metric.value.has_value());
    EXPECT_DOUBLE_EQ(*update.dump.snapshot.boardTemperatures.front().metric.value, 44.0);
    ASSERT_EQ(update.dump.snapshot.boardFans.size(), 2u);
    EXPECT_EQ(update.dump.snapshot.boardFans[0].name, "cpu");
    ASSERT_TRUE(update.dump.snapshot.boardFans[0].metric.value.has_value());
    EXPECT_DOUBLE_EQ(*update.dump.snapshot.boardFans[0].metric.value, 1280.0);
    EXPECT_EQ(update.dump.snapshot.boardFans[1].name, "system");
    ASSERT_TRUE(update.dump.snapshot.boardFans[1].metric.value.has_value());
    EXPECT_DOUBLE_EQ(*update.dump.snapshot.boardFans[1].metric.value, 910.0);

    EXPECT_EQ(log.gpuDiscoveryCreates, 1);
    EXPECT_EQ(log.gpuDiscoveryEnumerations, 1);
    EXPECT_EQ(log.gpuPerformanceCreates, 1);
    EXPECT_EQ(log.gpuPerformanceInitializes, 1);
    EXPECT_EQ(log.gpuPerformanceLoadSamples, 1);
    EXPECT_EQ(log.gpuPerformanceMemorySamples, 0);
    EXPECT_EQ(log.boardDiscoveryCreates, 1);
    EXPECT_EQ(log.boardDiscoveryReads, 1);
    EXPECT_EQ(log.nvmlCreates, 1);
    EXPECT_EQ(log.nvmlLoads, 1);
    EXPECT_EQ(log.nvmlInitializes, 1);
    EXPECT_EQ(log.nvmlDeviceCounts, 1);
    EXPECT_EQ(log.nvmlDeviceHandles, 1);
    EXPECT_EQ(log.nvmlDeviceNames, 1);
    EXPECT_EQ(log.nvmlPciInfos, 1);
    EXPECT_EQ(log.nvmlMemoryInfos, 3);
    EXPECT_EQ(log.nvmlTemperatures, 2);
    EXPECT_EQ(log.nvmlFanRpms, 3);
    EXPECT_EQ(log.nvapiCreates, 1);
    EXPECT_EQ(log.nvapiLoads, 1);
    EXPECT_EQ(log.nvapiInitializes, 1);
    EXPECT_EQ(log.nvapiEnumerations, 1);
    EXPECT_EQ(log.nvapiNames, 1);
    EXPECT_EQ(log.nvapiGraphicsClocks, 2);
    EXPECT_EQ(log.msiCenterCreates, 1);
    EXPECT_EQ(log.msiCenterDirectoryReads, 1);
    EXPECT_EQ(log.msiCenterCaptures, 2);
}
