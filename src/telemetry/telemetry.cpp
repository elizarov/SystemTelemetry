#include "telemetry/telemetry.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "telemetry/impl/collector.h"

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kTelemetryRefreshInterval = std::chrono::milliseconds(500);

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
    }

    bool Initialize(const TelemetrySettings& settings, std::string* errorText) {
        if (!collector_->Initialize(settings, errorText)) {
            return false;
        }
        PublishLocked();
        worker_ = std::thread([this] { RunWorker(); });
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
        commandCv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
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
        std::unique_lock lock(commandMutex_);
        action();
        PublishLocked();
    }

    void RunWorker() {
        auto nextCollection = Clock::now() + kTelemetryRefreshInterval;
        for (;;) {
            std::unique_lock lock(commandMutex_);
            const auto now = Clock::now();
            while (nextCollection <= now) {
                nextCollection += kTelemetryRefreshInterval;
            }
            if (commandCv_.wait_until(lock, nextCollection, [&] { return stopped_; })) {
                return;
            }
            nextCollection += kTelemetryRefreshInterval;
            collector_->UpdateSnapshot();
            PublishLocked();
        }
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
    std::condition_variable commandCv_;
    bool stopped_ = false;
    std::thread worker_;
};

}  // namespace

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
