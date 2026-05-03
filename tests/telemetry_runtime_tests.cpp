#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>

#include "config/config.h"
#include "config/config_resolution.h"
#include "telemetry/impl/collector_real.h"
#include "telemetry/telemetry.h"
#include "util/paths.h"

std::unique_ptr<TelemetryCollector> CreateRealTelemetryCollector(Trace& trace) {
    (void)trace;
    return nullptr;
}

namespace {

class TelemetryRuntimeTest : public testing::Test {
protected:
    std::unique_ptr<TelemetryRuntime> CreateRuntime() {
        TelemetryCollectorOptions options;
        options.fake = true;
        return CreateTelemetryRuntime(options,
            CurrentDirectoryPath(),
            ExtractTelemetrySettings(config_),
            trace_,
            [&](const TelemetryUpdate& update) {
                {
                    const std::lock_guard lock(mutex_);
                    latest_ = update;
                    ++callbackCount_;
                }
                cv_.notify_all();
            });
    }

    bool WaitForCallbackCount(int count) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(2), [&] { return callbackCount_ >= count; });
    }

    int CallbackCount() const {
        const std::lock_guard lock(mutex_);
        return callbackCount_;
    }

    TelemetryUpdate LatestCallbackUpdate() const {
        const std::lock_guard lock(mutex_);
        return latest_;
    }

    AppConfig config_{};
    Trace trace_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    TelemetryUpdate latest_{};
    int callbackCount_ = 0;
};

}  // namespace

TEST_F(TelemetryRuntimeTest, PublishesInitialAndWorkerUpdates) {
    std::unique_ptr<TelemetryRuntime> runtime = CreateRuntime();
    ASSERT_NE(runtime, nullptr);
    EXPECT_TRUE(WaitForCallbackCount(1));
    EXPECT_GE(runtime->Latest().dump.snapshot.revision, 1u);
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
