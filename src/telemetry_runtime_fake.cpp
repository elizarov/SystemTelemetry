#include "telemetry_runtime.h"

#include <chrono>
#include <fstream>
#include <utility>

#include "snapshot_dump.h"
#include "trace.h"
#include "utf8.h"

namespace {

class FakeTelemetryRuntime : public TelemetryRuntime {
public:
    FakeTelemetryRuntime(std::filesystem::path fakePath, bool showDialogs)
        : fakePath_(std::move(fakePath)), showDialogs_(showDialogs) {}

    bool Initialize(const AppConfig& config, std::ostream* traceStream) override {
        config_ = config;
        trace_.SetOutput(traceStream);
        trace_.Write("fake:initialize_begin path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
        if (!ReloadFakeDump(true)) {
            trace_.Write("fake:initialize_failed");
            return false;
        }
        trace_.Write("fake:initialize_done");
        return true;
    }

    const SystemSnapshot& Snapshot() const override {
        return dump_.snapshot;
    }

    TelemetryDump Dump() const override {
        return dump_;
    }

    AppConfig EffectiveConfig() const override {
        return config_;
    }

    void UpdateSnapshot() override {
        const auto now = std::chrono::steady_clock::now();
        if (lastReload_.time_since_epoch().count() == 0 || now - lastReload_ >= std::chrono::seconds(1)) {
            ReloadFakeDump(false);
        }
    }

private:
    bool ReloadFakeDump(bool required) {
        std::ifstream input(fakePath_, std::ios::binary);
        if (!input.is_open()) {
            trace_.Write("fake:load_failed reason=open path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
            if (required && showDialogs_) {
                const std::wstring message =
                    WideFromUtf8("Failed to open fake telemetry file:\n" + Utf8FromWide(fakePath_.wstring()));
                MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
            }
            return false;
        }

        TelemetryDump loaded;
        std::string error;
        if (!LoadTelemetryDump(input, loaded, &error)) {
            trace_.Write("fake:load_failed reason=parse error=\"" + error + "\"");
            if (required && showDialogs_) {
                const std::wstring message =
                    WideFromUtf8("Failed to parse fake telemetry file:\n" + error);
                MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
            }
            return false;
        }

        dump_ = std::move(loaded);
        lastReload_ = std::chrono::steady_clock::now();
        trace_.Write("fake:load_done path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
        return true;
    }

    std::filesystem::path fakePath_;
    bool showDialogs_ = true;
    AppConfig config_;
    TelemetryDump dump_{};
    tracing::Trace trace_;
    std::chrono::steady_clock::time_point lastReload_{};
};

}  // namespace

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime();

std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(
    const DiagnosticsOptions& options,
    const std::filesystem::path& executableDirectory) {
    if (options.fake) {
        return std::make_unique<FakeTelemetryRuntime>(
            executableDirectory / L"telemetry_fake.txt",
            ShouldShowRuntimeDialogs(options));
    }
    return CreateRealTelemetryRuntime();
}
