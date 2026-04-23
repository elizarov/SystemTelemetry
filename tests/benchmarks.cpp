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
#include <string_view>
#include <vector>

#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_parameter_metadata.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_trace_session.h"
#include "layout_edit/layout_edit_tree.h"
#include "telemetry/metrics.h"
#include "telemetry/telemetry.h"
#include "util/enum_string.h"
#include "util/trace.h"

#define SYSTEM_TELEMETRY_BENCHMARK_ITEMS(X)                                                                            \
    X(EditLayout, "edit-layout")                                                                                       \
    X(LayoutSwitch, "layout-switch")                                                                                   \
    X(MouseHover, "mouse-hover")                                                                                       \
    X(UpdateTelemetry, "update-telemetry")

ENUM_STRING_DECLARE(Benchmark, SYSTEM_TELEMETRY_BENCHMARK_ITEMS);

#undef SYSTEM_TELEMETRY_BENCHMARK_ITEMS

namespace {

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

enum class BenchPhase {
    TelemetryUpdate = 0,
    HoverHitTest,
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

struct BenchmarkCommandLine {
    Benchmark benchmark;
    size_t iterations = 240;
    double renderScale = 2.0;
};

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
}

ConfigParseContext BenchmarkConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

size_t PhaseIndex(BenchPhase phase) {
    return static_cast<size_t>(phase);
}

const char* PhaseName(BenchPhase phase) {
    switch (phase) {
        case BenchPhase::TelemetryUpdate:
            return "telemetry_update";
        case BenchPhase::HoverHitTest:
            return "hover_hit_test";
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

std::optional<Benchmark> ParseBenchmarkName(std::string_view name) {
    return EnumFromString<Benchmark>(name);
}

std::string SupportedBenchmarkNames() {
    std::ostringstream names;
    bool first = true;
    for (const std::string_view name : EnumStringTraits<Benchmark>::names) {
        if (!first) {
            names << ", ";
        }
        names << name;
        first = false;
    }
    return names.str();
}

bool TryParsePositiveSize(const char* text, size_t& value) {
    char* end = nullptr;
    const long long parsed = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0) {
        return false;
    }
    value = static_cast<size_t>(parsed);
    return true;
}

bool TryParsePositiveDouble(const char* text, double& value) {
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(parsed) || parsed <= 0.0) {
        return false;
    }
    value = parsed;
    return true;
}

std::optional<BenchmarkCommandLine> ParseBenchmarkCommandLine(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "supported benchmarks: " << SupportedBenchmarkNames() << "\n";
        return std::nullopt;
    }

    const std::string_view firstArg = argv[1];
    const std::optional<Benchmark> parsedBenchmark = ParseBenchmarkName(firstArg);
    if (!parsedBenchmark.has_value()) {
        std::cerr << "unknown benchmark \"" << firstArg << "\"; supported benchmarks: " << SupportedBenchmarkNames()
                  << "\n";
        return std::nullopt;
    }

    BenchmarkCommandLine commandLine{*parsedBenchmark};
    int nextArgument = 2;
    if (argc > nextArgument) {
        size_t parsedIterations = commandLine.iterations;
        if (TryParsePositiveSize(argv[nextArgument], parsedIterations)) {
            commandLine.iterations = parsedIterations;
        }
        ++nextArgument;
    }
    if (argc > nextArgument) {
        double parsedRenderScale = commandLine.renderScale;
        if (TryParsePositiveDouble(argv[nextArgument], parsedRenderScale)) {
            commandLine.renderScale = parsedRenderScale;
        }
    }
    return commandLine;
}

std::unique_ptr<TelemetryCollector> CreateBenchmarkTelemetryCollector(const AppConfig& config, Trace& trace) {
    TelemetryCollectorOptions options;
    std::unique_ptr<TelemetryCollector> telemetry =
        CreateTelemetryCollector(options, std::filesystem::current_path(), trace);
    if (telemetry == nullptr) {
        return nullptr;
    }
    if (!telemetry->Initialize(ExtractTelemetrySettings(config))) {
        return nullptr;
    }
    return telemetry;
}

std::unique_ptr<TelemetryCollector> CreateBenchmarkFakeTelemetryCollector(const AppConfig& config, Trace& trace) {
    TelemetryCollectorOptions options;
    options.fake = true;
    std::unique_ptr<TelemetryCollector> telemetry =
        CreateTelemetryCollector(options, std::filesystem::current_path(), trace);
    if (telemetry == nullptr) {
        return nullptr;
    }
    if (!telemetry->Initialize(ExtractTelemetrySettings(config))) {
        return nullptr;
    }
    return telemetry;
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

std::vector<RenderPoint> BuildMouseHoverPath(int width, int height, size_t iterations) {
    std::vector<RenderPoint> path;
    if (width <= 0 || height <= 0 || iterations == 0) {
        return path;
    }

    path.reserve(iterations);
    const int maxX = (std::max)(0, width - 1);
    const int maxY = (std::max)(0, height - 1);
    const double denominator = static_cast<double>((std::max)(size_t{1}, iterations - 1));
    for (size_t index = 0; index < iterations; ++index) {
        const double t = static_cast<double>(index) / denominator;
        path.push_back(RenderPoint{static_cast<int>(std::lround(t * static_cast<double>(maxX))),
            static_cast<int>(std::lround(t * static_cast<double>(maxY)))});
    }
    return path;
}

class BenchmarkHost : private LayoutEditHost {
public:
    BenchmarkHost(const AppConfig& config, double renderScale, Trace& trace)
        : config_(config), trace_(trace), renderer_(trace_), renderScale_(renderScale), layoutEditController_(*this) {
        renderer_.SetConfig(config_);
        renderer_.SetRenderScale(renderScale_);
        renderer_.SetImmediatePresent(true);
        overlayState_.showLayoutEditGuides = true;
    }

    ~BenchmarkHost() {
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

    void SetSnapshot(const SystemSnapshot& snapshot) {
        snapshot_ = &snapshot;
    }

    void DrawCurrentSnapshot() {
        if (snapshot_ == nullptr) {
            return;
        }
        const auto paintStart = Clock::now();
        const auto drawStart = Clock::now();
        renderer_.DrawWindow(*snapshot_, overlayState_);
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

    void UpdateTelemetry(TelemetryCollector& telemetry) {
        const auto start = Clock::now();
        telemetry.UpdateSnapshot();
        snapshot_ = &telemetry.Snapshot();
        RecordPhase(BenchPhase::TelemetryUpdate, Clock::now() - start);
    }

    void RecordHoverHitTest(std::chrono::nanoseconds elapsed) {
        RecordPhase(BenchPhase::HoverHitTest, elapsed);
    }

    HWND WindowHandle() const {
        return hwnd_;
    }

    const AppConfig& CurrentConfig() const {
        return config_;
    }

    bool SwitchLayout(std::string_view layoutName) {
        if (!SelectLayout(config_, std::string(layoutName))) {
            return false;
        }
        renderer_.SetConfig(config_);
        dirty_ = true;
        return true;
    }

private:
    const AppConfig& LayoutEditConfig() const override {
        return config_;
    }

    DashboardRenderer& LayoutEditRenderer() override {
        return renderer_;
    }

    DashboardOverlayState& LayoutDashboardOverlayState() override {
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
        traceSession_.Begin(trace_, kind, detail);
    }

    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override {
        traceSession_.Record(phase, elapsed);
        RecordPhase(static_cast<BenchPhase>(PhaseIndex(phase)), elapsed);
    }

    void EndLayoutEditTraceSession(const std::string& reason) override {
        traceSession_.End(trace_, reason);
    }

    HWND hwnd_ = nullptr;
    AppConfig config_{};
    Trace& trace_;
    DashboardRenderer renderer_;
    DashboardOverlayState overlayState_{};

    void RecordPhase(BenchPhase phase, std::chrono::nanoseconds elapsed) {
        PhaseStats& stats = phaseTotals_[PhaseIndex(phase)];
        stats.total += elapsed;
        ++stats.samples;
    }

    const SystemSnapshot* snapshot_ = nullptr;
    double renderScale_ = 1.0;
    bool dirty_ = false;
    LayoutEditController layoutEditController_;
    LayoutEditTraceSession traceSession_{};
    std::array<PhaseStats, kBenchPhaseCount> phaseTotals_{};
};

std::vector<std::string> CollectLayoutNames(const AppConfig& config) {
    std::vector<std::string> names;
    names.reserve(config.layout.layouts.size());
    for (const auto& layout : config.layout.layouts) {
        if (!layout.name.empty()) {
            names.push_back(layout.name);
        }
    }
    return names;
}

struct LayoutSwitchPhaseTotals {
    PhaseStats switchApply;
    PhaseStats dialogRefresh;
    PhaseStats paint;
};

struct LayoutSwitchBenchTotals {
    BenchResult switchLoop;
    LayoutSwitchPhaseTotals phases;
};

void RecordPhase(PhaseStats& stats, std::chrono::nanoseconds elapsed) {
    stats.total += elapsed;
    ++stats.samples;
}

void PrintBenchLoopResult(const char* name, const BenchResult& result) {
    std::cout << std::left << std::setw(18) << name << " total_ms=" << std::fixed << std::setprecision(2)
              << result.total.count() << " per_iter_ms=" << result.perIteration.count() << "\n";
}

LayoutSwitchBenchTotals RunLayoutSwitchBenchmark(
    BenchmarkHost& host, const std::vector<std::string>& layoutNames, size_t iterations) {
    LayoutSwitchBenchTotals totals{};
    if (layoutNames.empty() || iterations == 0) {
        return totals;
    }

    host.DrawCurrentSnapshot();
    host.ResetPhaseTotals();

    const auto switchLoopStart = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::string& layoutName = layoutNames[iteration % layoutNames.size()];

        const auto switchApplyStart = Clock::now();
        if (!host.SwitchLayout(layoutName)) {
            return {};
        }
        const auto switchApplyEnd = Clock::now();
        RecordPhase(totals.phases.switchApply, switchApplyEnd - switchApplyStart);

        const auto dialogRefreshStart = Clock::now();
        [[maybe_unused]] const LayoutEditTreeModel treeModel = BuildLayoutEditTreeModel(host.CurrentConfig());
        const auto dialogRefreshEnd = Clock::now();
        RecordPhase(totals.phases.dialogRefresh, dialogRefreshEnd - dialogRefreshStart);

        const auto paintStart = Clock::now();
        host.FlushPaintIfDirty();
        const auto paintEnd = Clock::now();
        RecordPhase(totals.phases.paint, paintEnd - paintStart);
    }
    totals.switchLoop.total = Clock::now() - switchLoopStart;
    totals.switchLoop.perIteration = totals.switchLoop.total / static_cast<double>(iterations);
    return totals;
}

BenchResult RunDragBenchmark(BenchmarkHost& host,
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

BenchResult RunMouseHoverBenchmark(BenchmarkHost& host, const std::vector<RenderPoint>& path) {
    if (path.empty()) {
        return {};
    }

    LayoutEditController& controller = host.Controller();
    controller.StartSession();
    host.DrawCurrentSnapshot();
    host.ResetPhaseTotals();

    const auto start = Clock::now();
    for (const RenderPoint point : path) {
        const auto hitTestStart = Clock::now();
        controller.HandleMouseMove(point);
        host.RecordHoverHitTest(Clock::now() - hitTestStart);
        host.DrawCurrentSnapshot();
    }
    const Duration total = Clock::now() - start;

    controller.HandleMouseLeave();
    controller.StopSession(true);
    return {total, total / static_cast<double>(path.size())};
}

void PrintBenchResult(const BenchResult& result) {
    std::cout << std::left << std::setw(14) << "drag_loop" << " total_ms=" << std::fixed << std::setprecision(2)
              << result.total.count() << " per_iter_ms=" << result.perIteration.count() << "\n";
}

void PrintMouseHoverBenchResult(const BenchResult& result) {
    std::cout << std::left << std::setw(14) << "hover_loop" << " total_ms=" << std::fixed << std::setprecision(2)
              << result.total.count() << " per_iter_ms=" << result.perIteration.count() << "\n";
}

BenchResult RunTelemetryUpdateBenchmark(BenchmarkHost& host, TelemetryCollector& telemetry, size_t iterations) {
    host.DrawCurrentSnapshot();
    host.ResetPhaseTotals();

    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        host.UpdateTelemetry(telemetry);
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

int RunEditLayoutBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    BenchmarkHost host(runtimeConfig, renderScale, trace);
    host.SetSnapshot(telemetry->Snapshot());
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    const std::optional<LayoutEditGuide> guide = FindTopLevelGuide(host.LayoutRenderer());
    if (!guide.has_value()) {
        std::cerr << "no top-level layout guide found\n";
        return 1;
    }

    const LayoutEditHost::LayoutTarget target = LayoutEditHost::LayoutTarget::ForGuide(*guide);
    const LayoutNodeConfig* node = FindGuideNode(runtimeConfig, target);
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

int RunLayoutSwitchBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    const std::vector<std::string> layoutNames = CollectLayoutNames(runtimeConfig);
    if (layoutNames.size() < 2) {
        std::cerr << "layout-switch benchmark requires at least two named layouts\n";
        return 1;
    }

    BenchmarkHost host(runtimeConfig, renderScale, trace);
    host.SetSnapshot(telemetry->Snapshot());
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    std::cout << "layout_switch_benchmark layouts=" << layoutNames.size() << " iterations=" << iterations
              << " render_scale=" << renderScale << "\n";
    const LayoutSwitchBenchTotals totals = RunLayoutSwitchBenchmark(host, layoutNames, iterations);
    PrintBenchLoopResult("switch_loop", totals.switchLoop);
    PrintPhaseResult("switch_apply", totals.phases.switchApply);
    PrintPhaseResult("dialog_refresh", totals.phases.dialogRefresh);
    PrintPhaseResult("switch_paint", totals.phases.paint);
    return 0;
}

int RunMouseHoverBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    BenchmarkHost host(runtimeConfig, renderScale, trace);
    host.SetSnapshot(telemetry->Snapshot());
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    const std::vector<RenderPoint> path =
        BuildMouseHoverPath(host.LayoutRenderer().WindowWidth(), host.LayoutRenderer().WindowHeight(), iterations);
    if (path.empty()) {
        std::cerr << "mouse hover path generation failed\n";
        return 1;
    }

    std::cout << "mouse_hover_benchmark path_points=" << path.size()
              << " window=" << host.LayoutRenderer().WindowWidth() << "x" << host.LayoutRenderer().WindowHeight()
              << " render_scale=" << renderScale << "\n";
    const BenchResult result = RunMouseHoverBenchmark(host, path);
    PrintMouseHoverBenchResult(result);

    const auto& phases = host.PhaseTotals();
    PrintPhaseResult(PhaseName(BenchPhase::HoverHitTest), phases[PhaseIndex(BenchPhase::HoverHitTest)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintTotal), phases[PhaseIndex(BenchPhase::PaintTotal)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintDraw), phases[PhaseIndex(BenchPhase::PaintDraw)]);
    return 0;
}

int RunUpdateTelemetryBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    BenchmarkHost host(runtimeConfig, renderScale, trace);
    host.SetSnapshot(telemetry->Snapshot());
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    std::cout << "update_telemetry_benchmark iterations=" << iterations << " render_scale=" << renderScale << "\n";
    const BenchResult result = RunTelemetryUpdateBenchmark(host, *telemetry, iterations);
    PrintTelemetryBenchResult(result);

    const auto& phases = host.PhaseTotals();
    PrintPhaseResult(PhaseName(BenchPhase::TelemetryUpdate), phases[PhaseIndex(BenchPhase::TelemetryUpdate)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintTotal), phases[PhaseIndex(BenchPhase::PaintTotal)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintDraw), phases[PhaseIndex(BenchPhase::PaintDraw)]);
    return 0;
}

int RunBenchmarkCommand(const BenchmarkCommandLine& commandLine, Trace& trace) {
    switch (commandLine.benchmark) {
        case Benchmark::EditLayout:
            return RunEditLayoutBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LayoutSwitch:
            return RunLayoutSwitchBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::MouseHover:
            return RunMouseHoverBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::UpdateTelemetry:
            return RunUpdateTelemetryBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
    }
    std::cerr << "unknown benchmark \"" << EnumToString(commandLine.benchmark) << "\"\n";
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::optional<BenchmarkCommandLine> commandLine = ParseBenchmarkCommandLine(argc, argv);
    if (!commandLine.has_value()) {
        return 1;
    }

    Trace trace;
    return RunBenchmarkCommand(*commandLine, trace);
}
