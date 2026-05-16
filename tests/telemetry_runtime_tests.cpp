#include <chrono>
#include <gtest/gtest.h>

#include "config/config.h"
#include "telemetry/impl/collector.h"
#include "telemetry/impl/collector_real.h"
#include "telemetry/telemetry.h"
#include "util/file_path.h"
#include "util/lightweight_mutex.h"

std::unique_ptr<TelemetryCollector> CreateRealTelemetryCollector(Trace& trace) {
    (void)trace;
    return nullptr;
}

namespace {

class TelemetryRuntimeTest : public testing::Test, public TelemetryUpdateSink {
protected:
    ~TelemetryRuntimeTest() override {
        if (callbackEvent_ != nullptr) {
            CloseHandle(callbackEvent_);
        }
    }

    void OnTelemetryUpdate(const TelemetryUpdate& update) override {
        {
            const LightweightMutexLock lock(mutex_);
            latest_ = update;
            ++callbackCount_;
        }
        SetEvent(callbackEvent_);
    }

    std::unique_ptr<TelemetryRuntime> CreateRuntime() {
        TelemetryCollectorOptions options;
        options.fake = true;
        return CreateTelemetryRuntime(options, CurrentDirectoryPath(), ExtractTelemetrySettings(config_), trace_, this);
    }

    bool WaitForCallbackCount(int count) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (;;) {
            {
                const LightweightMutexLock lock(mutex_);
                if (callbackCount_ >= count) {
                    return true;
                }
                ResetEvent(callbackEvent_);
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }
            const auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (WaitForSingleObject(callbackEvent_, waitMs > 0 ? static_cast<DWORD>(waitMs) : 0) != WAIT_OBJECT_0) {
                return false;
            }
        }
    }

    int CallbackCount() const {
        const LightweightMutexLock lock(mutex_);
        return callbackCount_;
    }

    TelemetryUpdate LatestCallbackUpdate() const {
        const LightweightMutexLock lock(mutex_);
        return latest_;
    }

    AppConfig config_{};
    Trace trace_;
    mutable LightweightMutex mutex_;
    HANDLE callbackEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    TelemetryUpdate latest_{};
    int callbackCount_ = 0;
};

const RetainedHistorySeries* FindRetainedHistory(const SystemSnapshot& snapshot, RetainedHistoryKey key) {
    const std::string_view seriesRef = RetainedHistorySeriesRef(key);
    for (const auto& history : snapshot.retainedHistories) {
        if (history.seriesRef == seriesRef) {
            return &history;
        }
    }
    return nullptr;
}

}  // namespace

TEST_F(TelemetryRuntimeTest, PublishesInitialAndWorkerUpdates) {
    std::unique_ptr<TelemetryRuntime> runtime = CreateRuntime();
    ASSERT_NE(runtime, nullptr);
    EXPECT_TRUE(WaitForCallbackCount(1));
    EXPECT_GE(runtime->Latest().dump.snapshot.revision, 1u);
    EXPECT_EQ(runtime->Latest().dump.snapshot.gpu.fpsAppName, "fluxsim");
    EXPECT_TRUE(WaitForCallbackCount(2));
    EXPECT_GE(LatestCallbackUpdate().dump.snapshot.revision, 1u);
    runtime->Shutdown();
}

TEST_F(TelemetryRuntimeTest, ShutdownPreventsLaterCallbacks) {
    std::unique_ptr<TelemetryRuntime> runtime = CreateRuntime();
    ASSERT_NE(runtime, nullptr);
    EXPECT_TRUE(WaitForCallbackCount(1));
    runtime->Shutdown();
    const int countAfterShutdown = CallbackCount();
    Sleep(700);
    EXPECT_EQ(CallbackCount(), countAfterShutdown);
}

TEST_F(TelemetryRuntimeTest, SelectionChangesPublishFreshResolvedSelections) {
    std::unique_ptr<TelemetryRuntime> runtime = CreateRuntime();
    ASSERT_NE(runtime, nullptr);
    EXPECT_TRUE(WaitForCallbackCount(1));

    runtime->SetSelectedStorageDrives({"D:"});
    TelemetryUpdate update = runtime->Latest();
    ASSERT_EQ(update.resolvedSelections.drives.size(), 1u);
    EXPECT_EQ(update.resolvedSelections.drives.front(), "D");
    ASSERT_EQ(update.dump.snapshot.drives.size(), 1u);
    EXPECT_EQ(update.dump.snapshot.drives.front().label, "D:");

    runtime->SetPreferredNetworkAdapterName("Ethernet");
    update = runtime->Latest();
    EXPECT_EQ(update.resolvedSelections.adapterName, "Ethernet");
    EXPECT_EQ(update.dump.snapshot.network.adapterName, "Ethernet");
    runtime->Shutdown();
}

TEST(TelemetryCollectorTest, StaticFakeKeepsBuiltInSnapshotStableAcrossUpdates) {
    Trace trace;
    TelemetryCollectorOptions options;
    options.fake = true;
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, CurrentDirectoryPath(), trace);
    ASSERT_NE(telemetry, nullptr);
    ASSERT_TRUE(telemetry->Initialize({}));

    const double initialCpuLoad = telemetry->Snapshot().cpu.loadPercent;
    const WORD initialSecond = telemetry->Snapshot().now.wSecond;
    telemetry->UpdateSnapshot();

    EXPECT_EQ(telemetry->Snapshot().cpu.loadPercent, initialCpuLoad);
    EXPECT_EQ(telemetry->Snapshot().now.wSecond, initialSecond);
}

TEST(TelemetryCollectorTest, LiveFakeAdvancesBuiltInSnapshotOnUpdate) {
    Trace trace;
    TelemetryCollectorOptions options;
    options.fake = true;
    options.liveFake = true;
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, CurrentDirectoryPath(), trace);
    ASSERT_NE(telemetry, nullptr);
    ASSERT_TRUE(telemetry->Initialize({}));

    const double initialCpuLoad = telemetry->Snapshot().cpu.loadPercent;
    const WORD initialSecond = telemetry->Snapshot().now.wSecond;
    const WORD initialMilliseconds = telemetry->Snapshot().now.wMilliseconds;
    telemetry->UpdateSnapshot();

    EXPECT_NE(telemetry->Snapshot().cpu.loadPercent, initialCpuLoad);
    EXPECT_EQ(telemetry->Snapshot().now.wSecond, initialSecond);
    EXPECT_EQ(telemetry->Snapshot().now.wMilliseconds, initialMilliseconds + 250);
}

TEST(TelemetryCollectorTest, LiveFakeThroughputUsesRetainedHistorySmoothing) {
    Trace trace;
    TelemetryCollectorOptions options;
    options.fake = true;
    options.liveFake = true;
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, CurrentDirectoryPath(), trace);
    ASSERT_NE(telemetry, nullptr);
    ASSERT_TRUE(telemetry->Initialize({}));

    const RetainedHistorySeries* initial =
        FindRetainedHistory(telemetry->Snapshot(), RetainedHistoryKey::NetworkUpload);
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(initial->throughputBucketSampleCount, 0u);
    const std::vector<double> initialSamples = initial->samples;
    const std::vector<double> initialLiveSamples = initial->throughputLiveSamples;

    telemetry->UpdateSnapshot();
    const RetainedHistorySeries* afterOne =
        FindRetainedHistory(telemetry->Snapshot(), RetainedHistoryKey::NetworkUpload);
    ASSERT_NE(afterOne, nullptr);
    EXPECT_EQ(afterOne->throughputBucketSampleCount, 1u);
    EXPECT_EQ(afterOne->samples, initialSamples);
    EXPECT_NE(afterOne->throughputLiveSamples, initialLiveSamples);

    telemetry->UpdateSnapshot();
    telemetry->UpdateSnapshot();
    telemetry->UpdateSnapshot();
    const RetainedHistorySeries* afterFour =
        FindRetainedHistory(telemetry->Snapshot(), RetainedHistoryKey::NetworkUpload);
    ASSERT_NE(afterFour, nullptr);
    EXPECT_EQ(afterFour->throughputBucketSampleCount, 0u);
    EXPECT_NE(afterFour->samples, initialSamples);
    EXPECT_NE(afterFour->throughputLiveSamples, initialLiveSamples);
    EXPECT_EQ(telemetry->Snapshot().now.wSecond, 1);
    EXPECT_EQ(telemetry->Snapshot().now.wMilliseconds, 88);
}
