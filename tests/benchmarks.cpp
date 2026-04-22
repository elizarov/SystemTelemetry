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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "diagnostics/diagnostics_options.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_tree.h"
#include "layout_edit/layout_edit_parameter.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_trace_session.h"
#include "telemetry/metrics.h"
#include "telemetry/telemetry.h"

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

bool IsLayoutSwitchBenchmarkName(const std::string& name) {
    return name == "layout-switch";
}

bool IsKnownBenchmarkName(const std::string& name) {
    return IsEditLayoutBenchmarkName(name) || IsUpdateTelemetryBenchmarkName(name) || IsLayoutSwitchBenchmarkName(name);
}

DiagnosticsOptions BenchmarkDiagnosticsOptions() {
    DiagnosticsOptions options;
    options.trace = true;
    return options;
}

std::unique_ptr<TelemetryCollector> CreateBenchmarkTelemetryCollector(const AppConfig& config) {
    const DiagnosticsOptions options = BenchmarkDiagnosticsOptions();
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, std::filesystem::current_path());
    if (telemetry == nullptr) {
        return nullptr;
    }
    if (!telemetry->Initialize(ExtractTelemetrySettings(config), nullptr)) {
        return nullptr;
    }
    return telemetry;
}

std::unique_ptr<TelemetryCollector> CreateBenchmarkFakeTelemetryCollector(const AppConfig& config) {
    DiagnosticsOptions options;
    options.fake = true;
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, std::filesystem::current_path());
    if (telemetry == nullptr) {
        return nullptr;
    }
    if (!telemetry->Initialize(ExtractTelemetrySettings(config), nullptr)) {
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

class BenchmarkHost : private LayoutEditHost {
public:
    BenchmarkHost(const AppConfig& config, double renderScale)
        : config_(config), renderScale_(renderScale), layoutEditController_(*this) {
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
    std::ostringstream traceStream_{};
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

void PrintBenchResult(const BenchResult& result) {
    std::cout << std::left << std::setw(14) << "drag_loop" << " total_ms=" << std::fixed << std::setprecision(2)
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

    if (benchmarkName == "edit-layout") {
        const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
        std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config);
        if (telemetry == nullptr) {
            std::cerr << "fake telemetry init failed\n";
            return 1;
        }

        const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
        BenchmarkHost host(runtimeConfig, renderScale);
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

    if (benchmarkName == "layout-switch") {
        const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
        std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config);
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

        BenchmarkHost host(runtimeConfig, renderScale);
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

    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkTelemetryCollector(config);
    if (telemetry == nullptr) {
        std::cerr << "telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    BenchmarkHost host(runtimeConfig, renderScale);
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
