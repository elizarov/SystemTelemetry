#include "telemetry/telemetry.h"

#include <chrono>
#include <mutex>

#include "telemetry/impl/collector.h"

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kTelemetryRefreshInterval = std::chrono::milliseconds(500);
constexpr const char* kRetainedHistorySeriesRefs[] = {
    "cpu.ram",
    "cpu.load",
    "cpu.clock",
    "gpu.load",
    "gpu.temp",
    "gpu.clock",
    "gpu.fan",
    "gpu.fps",
    "gpu.vram",
    "network.upload",
    "network.download",
    "storage.read",
    "storage.write",
};
static_assert(sizeof(kRetainedHistorySeriesRefs) / sizeof(kRetainedHistorySeriesRefs[0]) == kRetainedHistoryKeyCount);

size_t RetainedHistoryKeyIndex(RetainedHistoryKey key) {
    return static_cast<size_t>(key);
}

TelemetryUpdate CaptureTelemetryUpdate(const TelemetryCollector& collector) {
    TelemetryUpdate update;
    update.dump = collector.Dump();
    update.resolvedSelections = collector.ResolvedSelections();
    update.networkAdapterCandidates = collector.NetworkAdapterCandidates();
    update.storageDriveCandidates = collector.StorageDriveCandidates();
    return update;
}

class ThreadedTelemetryRuntime final : public TelemetryRuntime {
public:
    ThreadedTelemetryRuntime(std::unique_ptr<TelemetryCollector> collector, TelemetryUpdateSink* callback)
        : collector_(std::move(collector)), callback_(callback) {}

    ~ThreadedTelemetryRuntime() override {
        Shutdown();
        if (wakeEvent_ != nullptr) {
            CloseHandle(wakeEvent_);
        }
    }

    bool Initialize(const TelemetrySettings& settings, std::string* errorText) {
        if (!collector_->Initialize(settings, errorText)) {
            return false;
        }
        wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (wakeEvent_ == nullptr) {
            if (errorText != nullptr) {
                *errorText = "Failed to create telemetry wake event.";
            }
            return false;
        }
        PublishLocked();
        workerThread_ = CreateThread(nullptr, 0, &ThreadedTelemetryRuntime::WorkerThread, this, 0, nullptr);
        if (workerThread_ == nullptr) {
            if (errorText != nullptr) {
                *errorText = "Failed to create telemetry worker thread.";
            }
            return false;
        }
        return true;
    }

    void Shutdown() override {
        {
            const std::lock_guard lock(commandMutex_);
            if (stopped_) {
                return;
            }
            stopped_ = true;
        }
        if (wakeEvent_ != nullptr) {
            SetEvent(wakeEvent_);
        }
        if (workerThread_ != nullptr) {
            WaitForSingleObject(workerThread_, INFINITE);
            CloseHandle(workerThread_);
            workerThread_ = nullptr;
        }
    }

    void Reconfigure(const TelemetrySettings& settings) override {
        RunSynchronized([&] {
            collector_->ApplySettings(settings);
            collector_->UpdateSnapshot();
        });
    }

    void SetPreferredNetworkAdapterName(std::string adapterName) override {
        RunSynchronized([&] {
            collector_->SetPreferredNetworkAdapterName(std::move(adapterName));
            collector_->UpdateSnapshot();
        });
    }

    void SetSelectedStorageDrives(std::vector<std::string> driveLetters) override {
        RunSynchronized([&] {
            collector_->SetSelectedStorageDrives(std::move(driveLetters));
            collector_->UpdateSnapshot();
        });
    }

    void RefreshSelections() override {
        RunSynchronized([&] { collector_->RefreshSelectionsAndSnapshot(); });
    }

    TelemetryUpdate Latest() const override {
        const std::lock_guard lock(latestMutex_);
        return latest_;
    }

private:
    template <typename Action> void RunSynchronized(Action&& action) {
        std::lock_guard lock(commandMutex_);
        action();
        PublishLocked();
    }

    void RunWorker() {
        auto nextCollection = Clock::now() + kTelemetryRefreshInterval;
        for (;;) {
            const auto now = Clock::now();
            while (nextCollection <= now) {
                nextCollection += kTelemetryRefreshInterval;
            }
            const auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(nextCollection - now).count();
            const DWORD timeoutMs = waitMs > 0 ? static_cast<DWORD>(waitMs) : 0;
            if (WaitForSingleObject(wakeEvent_, timeoutMs) == WAIT_OBJECT_0) {
                const std::lock_guard lock(commandMutex_);
                if (stopped_) {
                    return;
                }
                continue;
            }
            const std::lock_guard lock(commandMutex_);
            if (stopped_) {
                return;
            }
            nextCollection += kTelemetryRefreshInterval;
            collector_->UpdateSnapshot();
            PublishLocked();
        }
    }

    static DWORD WINAPI WorkerThread(void* context) {
        static_cast<ThreadedTelemetryRuntime*>(context)->RunWorker();
        return 0;
    }

    void PublishLocked() {
        TelemetryUpdate update = CaptureTelemetryUpdate(*collector_);
        {
            const std::lock_guard latestLock(latestMutex_);
            latest_ = update;
        }
        if (callback_ != nullptr) {
            callback_->OnTelemetryUpdate(update);
        }
    }

    std::unique_ptr<TelemetryCollector> collector_;
    TelemetryUpdateSink* callback_ = nullptr;
    mutable std::mutex latestMutex_;
    TelemetryUpdate latest_;
    std::mutex commandMutex_;
    HANDLE wakeEvent_ = nullptr;
    HANDLE workerThread_ = nullptr;
    bool stopped_ = false;
};

}  // namespace

const char* RetainedHistorySeriesRef(RetainedHistoryKey key) {
    const size_t index = RetainedHistoryKeyIndex(key);
    return index < kRetainedHistoryKeyCount ? kRetainedHistorySeriesRefs[index] : "";
}

bool TryRetainedHistoryKey(std::string_view seriesRef, RetainedHistoryKey& key) {
    for (size_t i = 0; i < kRetainedHistoryKeyCount; ++i) {
        if (seriesRef == kRetainedHistorySeriesRefs[i]) {
            key = static_cast<RetainedHistoryKey>(i);
            return true;
        }
    }
    return false;
}

std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(const TelemetryCollectorOptions& options,
    const FilePath& workingDirectory,
    const TelemetrySettings& settings,
    Trace& trace,
    TelemetryUpdateSink* callback,
    std::string* errorText) {
    if (errorText != nullptr) {
        errorText->clear();
    }
    std::unique_ptr<TelemetryCollector> collector = CreateTelemetryCollector(options, workingDirectory, trace);
    if (collector == nullptr) {
        return nullptr;
    }
    auto runtime = std::make_unique<ThreadedTelemetryRuntime>(std::move(collector), callback);
    if (!runtime->Initialize(settings, errorText)) {
        return nullptr;
    }
    return runtime;
}
