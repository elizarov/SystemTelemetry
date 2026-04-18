#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config_parser.h"
#include "dashboard_renderer.h"
#include "layout_edit_controller.h"
#include "layout_edit_parameter.h"
#include "layout_edit_service.h"
#include "layout_edit_trace_session.h"

namespace {

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

enum class BenchPhase {
    TelemetryUpdate = 0,
    Snap,
    Apply,
    PaintTotal,
    PaintDraw,
    Count,
};

constexpr size_t kBenchPhaseCount = static_cast<size_t>(BenchPhase::Count);

struct PhaseStats {
    std::chrono::nanoseconds total{};
    size_t samples = 0;
};

struct BenchResult {
    Duration total{};
    Duration perIteration{};
};

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
}

size_t PhaseIndex(BenchPhase phase) {
    return static_cast<size_t>(phase);
}

const char* PhaseName(BenchPhase phase) {
    switch (phase) {
        case BenchPhase::TelemetryUpdate:
            return "telemetry_update";
        case BenchPhase::Snap:
            return "snap";
        case BenchPhase::Apply:
            return "apply";
        case BenchPhase::PaintTotal:
            return "paint_total";
        case BenchPhase::PaintDraw:
            return "paint_draw";
        case BenchPhase::Count:
            break;
    }
    return "unknown";
}

double DurationMilliseconds(std::chrono::nanoseconds value) {
    return std::chrono::duration<double, std::milli>(value).count();
}

size_t PhaseIndex(LayoutEditHost::TracePhase phase) {
    switch (phase) {
        case LayoutEditHost::TracePhase::Snap:
            return PhaseIndex(BenchPhase::Snap);
        case LayoutEditHost::TracePhase::Apply:
            return PhaseIndex(BenchPhase::Apply);
        case LayoutEditHost::TracePhase::PaintTotal:
            return PhaseIndex(BenchPhase::PaintTotal);
        case LayoutEditHost::TracePhase::PaintDraw:
            return PhaseIndex(BenchPhase::PaintDraw);
    }
    return 0;
}

const char* PhaseName(LayoutEditHost::TracePhase phase) {
    switch (phase) {
        case LayoutEditHost::TracePhase::Snap:
            return "snap";
        case LayoutEditHost::TracePhase::Apply:
            return "apply";
        case LayoutEditHost::TracePhase::PaintTotal:
            return "paint_total";
        case LayoutEditHost::TracePhase::PaintDraw:
            return "paint_draw";
    }
    return "unknown";
}

bool IsEditLayoutBenchmarkName(const std::string& name) {
    return name == "edit-layout" || name == "eidt-layout";
}

bool IsUpdateTelemetryBenchmarkName(const std::string& name) {
    return name == "update-telemetry";
}

bool IsKnownBenchmarkName(const std::string& name) {
    return IsEditLayoutBenchmarkName(name) || IsUpdateTelemetryBenchmarkName(name);
}

SystemSnapshot BuildSyntheticSnapshot(const AppConfig& config) {
    SystemSnapshot snapshot;
    snapshot.cpu.name = "AMD Ryzen 7";
    snapshot.cpu.loadPercent = 63.0;
    snapshot.cpu.clock = ScalarMetric{4.85, ScalarMetricUnit::Gigahertz};
    snapshot.cpu.memory = MemoryMetric{18.5, 32.0};
    snapshot.gpu.name = "Radeon RX";
    snapshot.gpu.loadPercent = 58.0;
    snapshot.gpu.temperature = ScalarMetric{67.0, ScalarMetricUnit::Celsius};
    snapshot.gpu.clock = ScalarMetric{2480.0, ScalarMetricUnit::Megahertz};
    snapshot.gpu.fan = ScalarMetric{1580.0, ScalarMetricUnit::Rpm};
    snapshot.gpu.vram = MemoryMetric{8.4, 16.0};
    snapshot.network.adapterName = "Ethernet";
    snapshot.network.ipAddress = "192.168.1.20";
    snapshot.network.uploadMbps = 78.0;
    snapshot.network.downloadMbps = 312.0;
    snapshot.storage.readMbps = 420.0;
    snapshot.storage.writeMbps = 155.0;
    GetLocalTime(&snapshot.now);

    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{74.0, ScalarMetricUnit::Celsius}});
    snapshot.boardTemperatures.push_back({"vrm", ScalarMetric{61.0, ScalarMetricUnit::Celsius}});
    snapshot.boardFans.push_back({"system", ScalarMetric{890.0, ScalarMetricUnit::Rpm}});

    const auto addHistory = [&](const std::string& ref, double base, double amplitude, double period) {
        RetainedHistorySeries series;
        series.seriesRef = ref;
        for (int i = 0; i < 120; ++i) {
            const double phase = static_cast<double>(i) / period;
            series.samples.push_back((std::max)(0.0, base + std::sin(phase) * amplitude));
        }
        snapshot.retainedHistoryIndexByRef.emplace(series.seriesRef, snapshot.retainedHistories.size());
        snapshot.retainedHistories.push_back(std::move(series));
    };
    addHistory("cpu.load", 55.0, 18.0, 5.5);
    addHistory("gpu.load", 48.0, 22.0, 6.5);
    addHistory("network.upload", 55.0, 25.0, 4.0);
    addHistory("network.download", 220.0, 95.0, 4.5);
    addHistory("storage.read", 300.0, 140.0, 3.8);
    addHistory("storage.write", 120.0, 45.0, 3.2);

    for (size_t i = 0; i < config.storage.drives.size(); ++i) {
        DriveInfo drive;
        drive.label = config.storage.drives[i] + ":";
        drive.volumeLabel = "Data";
        drive.totalGb = 1907.0;
        drive.usedPercent = 42.0 + static_cast<double>((i * 11) % 37);
        drive.freeGb = drive.totalGb * (100.0 - drive.usedPercent) / 100.0;
        drive.readMbps = 35.0 + static_cast<double>(i * 9);
        drive.writeMbps = 18.0 + static_cast<double>(i * 7);
        drive.driveType = DRIVE_FIXED;
        snapshot.drives.push_back(std::move(drive));
    }
    return snapshot;
}

size_t FindRetainedHistoryIndex(const SystemSnapshot& snapshot, std::string_view seriesRef) {
    const auto it = snapshot.retainedHistoryIndexByRef.find(std::string(seriesRef));
    if (it == snapshot.retainedHistoryIndexByRef.end() || it->second >= snapshot.retainedHistories.size()) {
        return snapshot.retainedHistories.size();
    }
    return it->second;
}

void PushRetainedHistorySample(SystemSnapshot& snapshot, std::string_view seriesRef, double value) {
    const size_t index = FindRetainedHistoryIndex(snapshot, seriesRef);
    if (index >= snapshot.retainedHistories.size()) {
        return;
    }

    std::vector<double>& samples = snapshot.retainedHistories[index].samples;
    if (samples.empty()) {
        return;
    }

    samples.erase(samples.begin());
    samples.push_back(value);
}

void AdvanceSyntheticSnapshot(SystemSnapshot& snapshot, size_t iteration) {
    const double step = static_cast<double>(iteration + 1);
    snapshot.cpu.loadPercent = 52.0 + std::sin(step * 0.12) * 18.0;
    snapshot.cpu.clock = ScalarMetric{4.65 + std::sin(step * 0.07) * 0.22, ScalarMetricUnit::Gigahertz};
    snapshot.cpu.memory.usedGb = 17.8 + std::sin(step * 0.03) * 1.1;

    snapshot.gpu.loadPercent = 49.0 + std::sin(step * 0.09 + 0.4) * 24.0;
    snapshot.gpu.temperature = ScalarMetric{64.0 + std::sin(step * 0.05 + 0.8) * 5.0, ScalarMetricUnit::Celsius};
    snapshot.gpu.clock = ScalarMetric{2410.0 + std::sin(step * 0.08 + 0.2) * 105.0, ScalarMetricUnit::Megahertz};
    snapshot.gpu.fan = ScalarMetric{1540.0 + std::sin(step * 0.06 + 0.5) * 140.0, ScalarMetricUnit::Rpm};
    snapshot.gpu.vram.usedGb = 8.1 + std::sin(step * 0.04 + 0.1) * 0.9;

    snapshot.network.uploadMbps = 82.0 + std::sin(step * 0.18) * 32.0;
    snapshot.network.downloadMbps = 305.0 + std::sin(step * 0.14 + 0.6) * 110.0;
    snapshot.storage.readMbps = 410.0 + std::sin(step * 0.15 + 0.3) * 135.0;
    snapshot.storage.writeMbps = 148.0 + std::sin(step * 0.11 + 0.9) * 58.0;

    for (size_t i = 0; i < snapshot.drives.size(); ++i) {
        DriveInfo& drive = snapshot.drives[i];
        const double phase = step * (0.09 + static_cast<double>(i) * 0.01);
        drive.usedPercent = 35.0 + std::fmod(static_cast<double>(i) * 13.0 + step * 0.35, 55.0);
        drive.freeGb = drive.totalGb * (100.0 - drive.usedPercent) / 100.0;
        drive.readMbps = 28.0 + std::sin(phase) * 12.0 + static_cast<double>(i * 7);
        drive.writeMbps = 14.0 + std::sin(phase + 0.8) * 8.0 + static_cast<double>(i * 5);
    }

    if (!snapshot.boardTemperatures.empty()) {
        snapshot.boardTemperatures[0].metric.value = 71.0 + std::sin(step * 0.05) * 4.0;
    }
    if (snapshot.boardTemperatures.size() > 1) {
        snapshot.boardTemperatures[1].metric.value = 60.0 + std::sin(step * 0.04 + 0.7) * 3.0;
    }
    if (!snapshot.boardFans.empty()) {
        snapshot.boardFans[0].metric.value = 900.0 + std::sin(step * 0.08 + 0.5) * 90.0;
    }

    PushRetainedHistorySample(snapshot, "cpu.load", snapshot.cpu.loadPercent);
    PushRetainedHistorySample(snapshot, "gpu.load", snapshot.gpu.loadPercent);
    PushRetainedHistorySample(snapshot, "network.upload", snapshot.network.uploadMbps);
    PushRetainedHistorySample(snapshot, "network.download", snapshot.network.downloadMbps);
    PushRetainedHistorySample(snapshot, "storage.read", snapshot.storage.readMbps);
    PushRetainedHistorySample(snapshot, "storage.write", snapshot.storage.writeMbps);
    if (!snapshot.boardTemperatures.empty()) {
        PushRetainedHistorySample(snapshot, "board.temp.cpu", snapshot.boardTemperatures[0].metric.value.value_or(0.0));
    }
    if (snapshot.boardTemperatures.size() > 1) {
        PushRetainedHistorySample(snapshot, "board.temp.vrm", snapshot.boardTemperatures[1].metric.value.value_or(0.0));
    }
    if (!snapshot.boardFans.empty()) {
        PushRetainedHistorySample(snapshot, "board.fan.system", snapshot.boardFans[0].metric.value.value_or(0.0));
    }

    FILETIME fileTime{};
    SystemTimeToFileTime(&snapshot.now, &fileTime);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    ticks.QuadPart += 10ULL * 1000ULL * 1000ULL;
    fileTime.dwLowDateTime = ticks.LowPart;
    fileTime.dwHighDateTime = ticks.HighPart;
    FileTimeToSystemTime(&fileTime, &snapshot.now);

    ++snapshot.revision;
}

std::optional<LayoutEditGuide> FindTopLevelGuide(const DashboardRenderer& renderer) {
    const auto& guides = renderer.LayoutEditGuides();
    const auto it = std::find_if(guides.begin(), guides.end(), [](const auto& guide) {
        return guide.editCardId.empty() && guide.nodePath.size() <= 1 && guide.childExtents.size() >= 2;
    });
    return it != guides.end() ? std::optional<LayoutEditGuide>(*it) : std::nullopt;
}

std::vector<std::vector<int>> BuildWeightSequence(const std::vector<int>& seedWeights, size_t iterations) {
    std::vector<std::vector<int>> sequence;
    if (seedWeights.size() < 2 || iterations == 0) {
        return sequence;
    }

    std::vector<int> current = seedWeights;
    sequence.reserve(iterations);
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const size_t left = iteration % (current.size() - 1);
        const size_t right = left + 1;
        if (current[right] > 1) {
            ++current[left];
            --current[right];
        } else if (current[left] > 1) {
            --current[left];
            ++current[right];
        }
        sequence.push_back(current);
    }
    return sequence;
}

RenderPoint GuideDragStartPoint(const LayoutEditGuide& guide) {
    return RenderPoint{(guide.hitRect.left + guide.hitRect.right) / 2, (guide.hitRect.top + guide.hitRect.bottom) / 2};
}

RenderPoint DragPointForWeights(
    const LayoutEditGuide& guide, const std::vector<int>& initialWeights, const std::vector<int>& targetWeights) {
    RenderPoint dragPoint = GuideDragStartPoint(guide);
    if (guide.separatorIndex < initialWeights.size() && guide.separatorIndex < targetWeights.size()) {
        const int delta = targetWeights[guide.separatorIndex] - initialWeights[guide.separatorIndex];
        if (guide.axis == LayoutGuideAxis::Vertical) {
            dragPoint.x += delta;
        } else {
            dragPoint.y += delta;
        }
    }
    return dragPoint;
}

class BenchmarkDragHost : private LayoutEditHost {
public:
    BenchmarkDragHost(const AppConfig& config, SystemSnapshot& snapshot, double renderScale)
        : config_(config), snapshot_(snapshot), renderScale_(renderScale), layoutEditController_(*this) {
        renderer_.SetConfig(config_);
        renderer_.SetRenderScale(renderScale_);
        renderer_.SetImmediatePresent(true);
        overlayState_.showLayoutEditGuides = true;
    }

    ~BenchmarkDragHost() {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
        renderer_.Shutdown();
    }

    bool Initialize() {
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW,
            L"STATIC",
            L"SystemTelemetryBenchmarkHost",
            WS_POPUP,
            0,
            0,
            renderer_.WindowWidth(),
            renderer_.WindowHeight(),
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (hwnd_ == nullptr) {
            return false;
        }
        if (!renderer_.Initialize(hwnd_)) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            return false;
        }
        return true;
    }

    LayoutEditController& Controller() {
        return layoutEditController_;
    }

    DashboardRenderer& LayoutRenderer() {
        return renderer_;
    }

    const std::array<PhaseStats, kBenchPhaseCount>& PhaseTotals() const {
        return phaseTotals_;
    }

    void ResetPhaseTotals() {
        phaseTotals_ = {};
    }

    void DrawCurrentSnapshot() {
        const auto paintStart = Clock::now();
        const auto drawStart = Clock::now();
        renderer_.DrawWindow(snapshot_, overlayState_);
        const auto drawEnd = Clock::now();

        const auto paintEnd = Clock::now();
        RecordPhase(BenchPhase::PaintDraw, drawEnd - drawStart);
        RecordPhase(BenchPhase::PaintTotal, paintEnd - paintStart);
    }

    void FlushPaintIfDirty() {
        if (!dirty_) {
            return;
        }

        dirty_ = false;
        DrawCurrentSnapshot();
    }

    void AdvanceTelemetry(size_t iteration) {
        const auto start = Clock::now();
        AdvanceSyntheticSnapshot(snapshot_, iteration);
        RecordPhase(BenchPhase::TelemetryUpdate, Clock::now() - start);
    }

    HWND WindowHandle() const {
        return hwnd_;
    }

private:
    const AppConfig& LayoutEditConfig() const override {
        return config_;
    }

    DashboardRenderer& LayoutEditRenderer() override {
        return renderer_;
    }

    DashboardRenderer::EditOverlayState& LayoutEditOverlayState() override {
        return overlayState_;
    }

    bool ApplyLayoutGuideWeights(const LayoutTarget& target, const std::vector<int>& weights) override {
        const auto start = Clock::now();
        const bool applied = ApplyGuideWeights(config_, target, weights);
        if (applied) {
            renderer_.SetConfig(config_);
            dirty_ = true;
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    bool ApplyMetricListOrder(
        const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) override {
        const auto start = Clock::now();
        const bool applied = ::ApplyMetricListOrder(config_, widget, metricRefs);
        if (applied) {
            renderer_.SetConfig(config_);
            dirty_ = true;
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis) override {
        return EvaluateWidgetExtentForGuideWeights(renderer_, target, weights, widget, axis);
    }

    bool ApplyLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter, double value) override {
        const auto start = Clock::now();
        const bool applied = ApplyLayoutEditParameterValue(config_, parameter, value);
        if (applied) {
            renderer_.SetConfig(config_);
            dirty_ = true;
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    void InvalidateLayoutEdit() override {
        dirty_ = true;
    }

    void BeginLayoutEditTraceSession(const std::string& kind, const std::string& detail) override {
        phaseTotals_ = {};
        traceStream_.str({});
        traceStream_.clear();
        traceSession_.Begin(&traceStream_, kind, detail);
    }

    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override {
        traceSession_.Record(phase, elapsed);
        RecordPhase(static_cast<BenchPhase>(PhaseIndex(phase)), elapsed);
    }

    void EndLayoutEditTraceSession(const std::string& reason) override {
        traceSession_.End(&traceStream_, reason);
    }

    HWND hwnd_ = nullptr;
    AppConfig config_{};
    DashboardRenderer renderer_{};
    DashboardRenderer::EditOverlayState overlayState_{};
    void RecordPhase(BenchPhase phase, std::chrono::nanoseconds elapsed) {
        PhaseStats& stats = phaseTotals_[PhaseIndex(phase)];
        stats.total += elapsed;
        ++stats.samples;
    }

    SystemSnapshot& snapshot_;
    double renderScale_ = 1.0;
    bool dirty_ = false;
    LayoutEditController layoutEditController_;
    LayoutEditTraceSession traceSession_{};
    std::ostringstream traceStream_{};
    std::array<PhaseStats, kBenchPhaseCount> phaseTotals_{};
};

BenchResult RunDragBenchmark(BenchmarkDragHost& host,
    const LayoutEditGuide& guide,
    const std::vector<int>& initialWeights,
    const std::vector<std::vector<int>>& weightSequence) {
    LayoutEditController& controller = host.Controller();
    controller.StartSession();

    const RenderPoint startPoint = GuideDragStartPoint(guide);
    controller.HandleMouseMove(startPoint);
    host.FlushPaintIfDirty();

    if (!controller.HandleLButtonDown(host.WindowHandle(), startPoint)) {
        return {};
    }

    const auto start = Clock::now();
    for (const auto& weights : weightSequence) {
        controller.HandleMouseMove(DragPointForWeights(guide, initialWeights, weights));
        host.FlushPaintIfDirty();
    }
    const Duration total = Clock::now() - start;

    controller.HandleLButtonUp(DragPointForWeights(guide, initialWeights, weightSequence.back()));
    host.FlushPaintIfDirty();
    controller.StopSession(true);
    return {total, total / static_cast<double>(weightSequence.size())};
}

void PrintBenchResult(const BenchResult& result) {
    std::cout << std::left << std::setw(14) << "drag_loop" << " total_ms=" << std::fixed << std::setprecision(2)
              << result.total.count() << " per_iter_ms=" << result.perIteration.count() << "\n";
}

BenchResult RunTelemetryUpdateBenchmark(BenchmarkDragHost& host, size_t iterations) {
    host.DrawCurrentSnapshot();
    host.ResetPhaseTotals();

    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        host.AdvanceTelemetry(iteration);
        host.DrawCurrentSnapshot();
    }
    const Duration total = Clock::now() - start;
    return {total, total / static_cast<double>(iterations)};
}

void PrintTelemetryBenchResult(const BenchResult& result) {
    std::cout << std::left << std::setw(14) << "update_loop" << " total_ms=" << std::fixed << std::setprecision(2)
              << result.total.count() << " per_iter_ms=" << result.perIteration.count() << "\n";
}

void PrintPhaseResult(const char* name, const PhaseStats& stats) {
    if (stats.samples == 0) {
        return;
    }

    const double totalMs = DurationMilliseconds(stats.total);
    const double averageMs = totalMs / static_cast<double>(stats.samples);
    std::cout << std::left << std::setw(14) << name << " total_ms=" << std::fixed << std::setprecision(2) << totalMs
              << " avg_ms=" << averageMs << " samples=" << stats.samples << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string benchmarkName = "edit-layout";
    size_t iterations = 240;
    double renderScale = 2.0;
    if (argc >= 2) {
        const std::string firstArg = argv[1];
        int nextArgument = 1;
        if (IsKnownBenchmarkName(firstArg)) {
            benchmarkName = IsEditLayoutBenchmarkName(firstArg) ? "edit-layout" : firstArg;
            ++nextArgument;
        }

        if (argc > nextArgument) {
            const long long parsed = std::atoll(argv[nextArgument]);
            if (parsed > 0) {
                iterations = static_cast<size_t>(parsed);
            }
        }
        if (argc > nextArgument + 1) {
            const double parsed = std::atof(argv[nextArgument + 1]);
            if (std::isfinite(parsed) && parsed > 0.0) {
                renderScale = parsed;
            }
        }
    }

    if (!IsKnownBenchmarkName(benchmarkName)) {
        std::cerr << "unknown benchmark \"" << benchmarkName << "\"\n";
        return 1;
    }

    const AppConfig config = LoadConfig(SourceConfigPath(), false);
    SystemSnapshot snapshot = BuildSyntheticSnapshot(config);
    BenchmarkDragHost host(config, snapshot, renderScale);
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    if (benchmarkName == "edit-layout") {
        const std::optional<LayoutEditGuide> guide = FindTopLevelGuide(host.LayoutRenderer());
        if (!guide.has_value()) {
            std::cerr << "no top-level layout guide found\n";
            return 1;
        }

        const LayoutEditHost::LayoutTarget target = LayoutEditHost::LayoutTarget::ForGuide(*guide);
        const LayoutNodeConfig* node = FindGuideNode(config, target);
        const std::vector<int> initialWeights = SeedGuideWeights(*guide, node);
        const std::vector<std::vector<int>> weightSequence = BuildWeightSequence(initialWeights, iterations);
        if (weightSequence.empty()) {
            std::cerr << "weight sequence generation failed\n";
            return 1;
        }

        std::cout << "layout_edit_drag_benchmark guide_children=" << initialWeights.size()
                  << " separator_index=" << guide->separatorIndex << " iterations=" << weightSequence.size()
                  << " render_scale=" << renderScale << "\n";

        const BenchResult result = RunDragBenchmark(host, *guide, initialWeights, weightSequence);
        PrintBenchResult(result);

        const auto& phases = host.PhaseTotals();
        PrintPhaseResult(PhaseName(BenchPhase::Snap), phases[PhaseIndex(BenchPhase::Snap)]);
        PrintPhaseResult(PhaseName(BenchPhase::Apply), phases[PhaseIndex(BenchPhase::Apply)]);
        PrintPhaseResult(PhaseName(BenchPhase::PaintTotal), phases[PhaseIndex(BenchPhase::PaintTotal)]);
        PrintPhaseResult(PhaseName(BenchPhase::PaintDraw), phases[PhaseIndex(BenchPhase::PaintDraw)]);
        return 0;
    }

    std::cout << "update_telemetry_benchmark iterations=" << iterations << " render_scale=" << renderScale << "\n";
    const BenchResult result = RunTelemetryUpdateBenchmark(host, iterations);
    PrintTelemetryBenchResult(result);

    const auto& phases = host.PhaseTotals();
    PrintPhaseResult(PhaseName(BenchPhase::TelemetryUpdate), phases[PhaseIndex(BenchPhase::TelemetryUpdate)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintTotal), phases[PhaseIndex(BenchPhase::PaintTotal)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintDraw), phases[PhaseIndex(BenchPhase::PaintDraw)]);
    return 0;
}
