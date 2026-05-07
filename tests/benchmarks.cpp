#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "config/color_resolver.h"
#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_trace_session.h"
#include "layout_edit/layout_edit_tree.h"
#include "layout_edit_dialog/theme_preview.h"
#include "layout_guide_sheet/layout_guide_sheet.h"
#include "layout_model/layout_edit_service.h"
#include "telemetry/impl/collector.h"
#include "telemetry/metrics.h"
#include "telemetry/telemetry.h"
#include "util/enum_string.h"
#include "util/file_path.h"
#include "util/trace.h"
#include "util/utf8.h"

#define CASEDASH_BENCHMARK_ITEMS(X)                                                                                    \
    X(EditLayout, "edit-layout")                                                                                       \
    X(LayoutGuideSheet, "layout-guide-sheet")                                                                          \
    X(LayoutSwitch, "layout-switch")                                                                                   \
    X(MouseHover, "mouse-hover")                                                                                       \
    X(ThemeChange, "theme-change")                                                                                     \
    X(UpdateTelemetry, "update-telemetry")

ENUM_STRING_DECLARE(Benchmark, CASEDASH_BENCHMARK_ITEMS);

#undef CASEDASH_BENCHMARK_ITEMS

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
    LayoutGuideActiveRegions,
    LayoutGuidePlan,
    LayoutGuideMeasure,
    LayoutGuidePlacement,
    LayoutGuideDraw,
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

FilePath SourceConfigPath() {
    return FilePath(CASEDASH_SOURCE_DIR) / "resources" / "config.ini";
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
        case BenchPhase::LayoutGuideActiveRegions:
            return "active_regions";
        case BenchPhase::LayoutGuidePlan:
            return "sheet_plan";
        case BenchPhase::LayoutGuideMeasure:
            return "sheet_measure";
        case BenchPhase::LayoutGuidePlacement:
            return "sheet_place";
        case BenchPhase::LayoutGuideDraw:
            return "sheet_draw";
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
    // The update-telemetry benchmark intentionally uses the package-private synchronous collector. It measures provider
    // collection CPU directly; the production TelemetryRuntime thread would hide that cost behind scheduling waits.
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, CurrentDirectoryPath(), trace);
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
    std::unique_ptr<TelemetryCollector> telemetry = CreateTelemetryCollector(options, CurrentDirectoryPath(), trace);
    if (telemetry == nullptr) {
        return nullptr;
    }
    if (!telemetry->Initialize(ExtractTelemetrySettings(config))) {
        return nullptr;
    }
    return telemetry;
}

std::optional<LayoutEditGuide> FindTopLevelGuide(const DashboardRenderer& renderer) {
    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;
    const auto regions = renderer.CollectLayoutEditActiveRegions(overlayState);
    const auto it = std::find_if(regions.begin(), regions.end(), [](const auto& region) {
        if (region.kind != LayoutEditActiveRegionKind::LayoutWeightGuide) {
            return false;
        }
        const auto* guide = LayoutEditActiveRegionPayloadAs<LayoutEditGuide>(region);
        return guide != nullptr && guide->editCardId.empty() && guide->nodePath.size() <= 1 &&
               guide->childExtents.size() >= 2;
    });
    const auto* guide = it != regions.end() ? LayoutEditActiveRegionPayloadAs<LayoutEditGuide>(*it) : nullptr;
    return guide != nullptr ? std::optional<LayoutEditGuide>(*guide) : std::nullopt;
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
        const std::wstring staticClass = WideFromUtf8("STATIC");
        const std::wstring windowTitle = WideFromUtf8("CaseDashBenchmarkHost");
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW,
            staticClass.c_str(),
            windowTitle.c_str(),
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

    void SetRuntimeConfig(const AppConfig& config) {
        config_ = config;
        renderer_.SetConfig(config_);
        dirty_ = true;
    }

private:
    bool FinishLayoutEditConfigMutation() {
        // Keep this aligned with the real layout-edit mutation tail; drag cost includes config dirty tracking.
        renderer_.SetConfig(config_);
        dirty_ = true;
        return true;
    }

    const AppConfig& LayoutEditConfig() const override {
        return config_;
    }

    DashboardOverlayState& LayoutDashboardOverlayState() override {
        return overlayState_;
    }

    LayoutEditActiveRegions CollectLayoutEditActiveRegions() const override {
        return renderer_.CollectLayoutEditActiveRegions(overlayState_);
    }

    LayoutEditHoverResolution ResolveLayoutEditHover(RenderPoint clientPoint) const override {
        return renderer_.ResolveLayoutEditHover(overlayState_, clientPoint);
    }

    double LayoutEditRenderScale() const override {
        return renderer_.RenderScale();
    }

    int LayoutEditSimilarityThreshold() const override {
        return renderer_.LayoutSimilarityThreshold();
    }

    void SetLayoutGuideDragActive(bool active) override {
        renderer_.SetLayoutGuideDragActive(active);
    }

    void SetLayoutEditInteractiveDragTraceActive(bool active) override {
        renderer_.SetInteractiveDragTraceActive(active);
    }

    void RebuildLayoutEditArtifacts() override {
        renderer_.RebuildEditArtifacts();
    }

    bool ApplyLayoutGuideWeights(const LayoutEditLayoutTarget& target, const std::vector<int>& weights) override {
        const auto start = Clock::now();
        const bool applied = ApplyGuideWeights(config_, target, weights);
        if (applied) {
            FinishLayoutEditConfigMutation();
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    bool ApplyMetricListOrder(
        const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) override {
        const auto start = Clock::now();
        const bool applied = ::ApplyMetricListOrder(config_, widget, metricRefs);
        if (applied) {
            FinishLayoutEditConfigMutation();
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    bool ApplyContainerChildOrder(const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) override {
        const auto start = Clock::now();
        const bool applied = ::ApplyContainerChildOrder(config_, key, fromIndex, toIndex);
        if (applied) {
            FinishLayoutEditConfigMutation();
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis) override {
        if (!renderer_.ApplyLayoutGuideWeightsPreview(target.editCardId, target.nodePath, weights)) {
            return std::nullopt;
        }
        return renderer_.FindLayoutWidgetExtent(widget, axis);
    }

    bool ApplyLayoutEditValue(LayoutEditParameter parameter, double value) override {
        const auto start = Clock::now();
        const bool applied = ApplyLayoutEditParameterValue(config_, parameter, value);
        if (applied) {
            FinishLayoutEditConfigMutation();
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    void InvalidateLayoutEdit() override {
        dirty_ = true;
    }

    void BeginLayoutEditTraceSession(const char* kind, const std::string& detail) override {
        phaseTotals_ = {};
        traceSession_.Begin(trace_, kind, detail);
    }

    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override {
        traceSession_.Record(phase, elapsed);
        RecordPhase(static_cast<BenchPhase>(PhaseIndex(phase)), elapsed);
    }

    void EndLayoutEditTraceSession(const char* reason) override {
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

std::vector<std::string> CollectThemeNames(const AppConfig& config) {
    std::vector<std::string> names;
    names.reserve(config.layout.themes.size());
    for (const ThemeConfig& theme : config.layout.themes) {
        if (!theme.name.empty()) {
            names.push_back(theme.name);
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

struct ThemeChangePhaseTotals {
    PhaseStats configCopy;
    PhaseStats colorResolve;
    PhaseStats dashboardReconfigure;
    PhaseStats dialogTreeRebuild;
    PhaseStats previewDraw;
    PhaseStats dashboardPaint;
};

struct ThemeChangeBenchTotals {
    BenchResult changeLoop;
    ThemeChangePhaseTotals phases;
};

struct LayoutGuideSheetBenchTotals {
    BenchResult generationLoop;
    std::array<PhaseStats, kBenchPhaseCount> phases{};
    std::vector<std::string> traceDetails;
    std::string errorText;
    size_t selectedCards = 0;
    size_t callouts = 0;
    bool succeeded = true;
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

ThemeChangeBenchTotals RunThemeChangeBenchmark(
    BenchmarkHost& host, const std::vector<std::string>& themeNames, size_t iterations) {
    ThemeChangeBenchTotals totals{};
    if (themeNames.empty() || iterations == 0) {
        return totals;
    }

    HDC screenDc = GetDC(nullptr);
    HDC previewDc = CreateCompatibleDC(screenDc);
    constexpr int kPreviewWidth = 420;
    constexpr int kPreviewHeight = 172;
    HBITMAP previewBitmap = CreateCompatibleBitmap(screenDc, kPreviewWidth, kPreviewHeight);
    if (previewDc == nullptr || previewBitmap == nullptr) {
        if (previewBitmap != nullptr) {
            DeleteObject(previewBitmap);
        }
        if (previewDc != nullptr) {
            DeleteDC(previewDc);
        }
        ReleaseDC(nullptr, screenDc);
        return totals;
    }
    HGDIOBJ oldPreviewBitmap = SelectObject(previewDc, previewBitmap);
    ReleaseDC(nullptr, screenDc);

    const RECT previewRect{0, 0, kPreviewWidth, kPreviewHeight};
    host.DrawCurrentSnapshot();

    const auto themeLoopStart = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::string& themeName = themeNames[iteration % themeNames.size()];

        const auto configCopyStart = Clock::now();
        AppConfig updatedConfig = host.CurrentConfig();
        updatedConfig.display.theme = themeName;
        const auto configCopyEnd = Clock::now();
        RecordPhase(totals.phases.configCopy, configCopyEnd - configCopyStart);

        const auto colorResolveStart = Clock::now();
        ResolveConfiguredColors(updatedConfig);
        const auto colorResolveEnd = Clock::now();
        RecordPhase(totals.phases.colorResolve, colorResolveEnd - colorResolveStart);

        const auto dashboardReconfigureStart = Clock::now();
        host.SetRuntimeConfig(updatedConfig);
        const auto dashboardReconfigureEnd = Clock::now();
        RecordPhase(totals.phases.dashboardReconfigure, dashboardReconfigureEnd - dashboardReconfigureStart);

        const auto dialogTreeRebuildStart = Clock::now();
        [[maybe_unused]] const LayoutEditTreeModel treeModel = BuildLayoutEditTreeModel(host.CurrentConfig());
        const auto dialogTreeRebuildEnd = Clock::now();
        RecordPhase(totals.phases.dialogTreeRebuild, dialogTreeRebuildEnd - dialogTreeRebuildStart);

        const auto previewDrawStart = Clock::now();
        if (const ThemeConfig* activeTheme = FindActiveThemeConfig(host.CurrentConfig()); activeTheme != nullptr) {
            DrawThemePreviewTriangle(previewDc, previewRect, *activeTheme);
        }
        const auto previewDrawEnd = Clock::now();
        RecordPhase(totals.phases.previewDraw, previewDrawEnd - previewDrawStart);

        const auto paintStart = Clock::now();
        host.FlushPaintIfDirty();
        const auto paintEnd = Clock::now();
        RecordPhase(totals.phases.dashboardPaint, paintEnd - paintStart);
    }
    totals.changeLoop.total = Clock::now() - themeLoopStart;
    totals.changeLoop.perIteration = totals.changeLoop.total / static_cast<double>(iterations);

    SelectObject(previewDc, oldPreviewBitmap);
    DeleteObject(previewBitmap);
    DeleteDC(previewDc);
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
        // Mirrors the app's drag WM_MOUSEMOVE path, which forces a redraw instead of waiting for queued WM_PAINT.
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

LayoutGuideSheetBenchTotals RunLayoutGuideSheetGenerationBenchmark(
    DashboardRenderer& renderer, const SystemSnapshot& snapshot, size_t iterations) {
    LayoutGuideSheetBenchTotals totals{};
    if (iterations == 0) {
        return totals;
    }

    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        std::string errorText;
        LayoutGuideSheetPipelineStats pipelineStats;
        if (!RenderLayoutGuideSheetOffscreen(renderer, snapshot, &errorText, &pipelineStats)) {
            totals.succeeded = false;
            totals.errorText =
                "layout guide offscreen render failed: " + (errorText.empty() ? renderer.LastError() : errorText);
            return totals;
        }
        RecordPhase(totals.phases[PhaseIndex(BenchPhase::LayoutGuideActiveRegions)], pipelineStats.activeRegions);
        RecordPhase(totals.phases[PhaseIndex(BenchPhase::LayoutGuidePlan)], pipelineStats.plan);
        RecordPhase(totals.phases[PhaseIndex(BenchPhase::LayoutGuideMeasure)], pipelineStats.measure);
        RecordPhase(totals.phases[PhaseIndex(BenchPhase::LayoutGuidePlacement)], pipelineStats.placement);
        RecordPhase(totals.phases[PhaseIndex(BenchPhase::LayoutGuideDraw)], pipelineStats.draw);
        totals.traceDetails = std::move(pipelineStats.traceDetails);
        totals.selectedCards = pipelineStats.selectedCards;
        totals.callouts = pipelineStats.callouts;
    }
    totals.generationLoop.total = Clock::now() - start;
    totals.generationLoop.perIteration = totals.generationLoop.total / static_cast<double>(iterations);
    return totals;
}

void PrintLayoutGuideSheetBenchResult(const LayoutGuideSheetBenchTotals& totals) {
    std::cout << std::left << std::setw(14) << "sheet_loop" << " total_ms=" << std::fixed << std::setprecision(2)
              << totals.generationLoop.total.count() << " per_iter_ms=" << totals.generationLoop.perIteration.count()
              << "\n";
    for (const std::string& detail : totals.traceDetails) {
        std::cout << std::left << std::setw(14) << "sheet_trace" << " " << detail << "\n";
    }
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

    const LayoutEditLayoutTarget target = LayoutEditLayoutTarget::ForGuide(*guide);
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

int RunThemeChangeBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    const std::vector<std::string> themeNames = CollectThemeNames(runtimeConfig);
    if (themeNames.size() < 2) {
        std::cerr << "theme-change benchmark requires at least two named themes\n";
        return 1;
    }

    BenchmarkHost host(runtimeConfig, renderScale, trace);
    host.SetSnapshot(telemetry->Snapshot());
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    std::cout << "theme_change_benchmark themes=" << themeNames.size() << " iterations=" << iterations
              << " render_scale=" << renderScale << "\n";
    const ThemeChangeBenchTotals totals = RunThemeChangeBenchmark(host, themeNames, iterations);
    PrintBenchLoopResult("theme_loop", totals.changeLoop);
    PrintPhaseResult("config_copy", totals.phases.configCopy);
    PrintPhaseResult("color_resolve", totals.phases.colorResolve);
    PrintPhaseResult("dashboard_config", totals.phases.dashboardReconfigure);
    PrintPhaseResult("edit_tree", totals.phases.dialogTreeRebuild);
    PrintPhaseResult("theme_preview", totals.phases.previewDraw);
    PrintPhaseResult("theme_paint", totals.phases.dashboardPaint);
    return 0;
}

int RunLayoutGuideSheetBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    DashboardRenderer renderer(trace);
    renderer.SetRenderScale(renderScale);
    renderer.SetConfig(runtimeConfig);
    renderer.SetRenderMode(DashboardRenderer::RenderMode::Normal);
    if (!renderer.Initialize()) {
        std::cerr << "renderer init failed: " << renderer.LastError() << "\n";
        return 1;
    }

    const LayoutGuideSheetBenchTotals totals =
        RunLayoutGuideSheetGenerationBenchmark(renderer, telemetry->Snapshot(), iterations);
    if (!totals.succeeded) {
        std::cerr << totals.errorText << "\n";
        return 1;
    }
    std::cout << "layout_guide_sheet_benchmark iterations=" << iterations << " render_scale=" << renderScale
              << " selected_cards=" << totals.selectedCards << " callouts=" << totals.callouts << "\n";
    PrintLayoutGuideSheetBenchResult(totals);
    PrintPhaseResult(PhaseName(BenchPhase::LayoutGuideActiveRegions),
        totals.phases[PhaseIndex(BenchPhase::LayoutGuideActiveRegions)]);
    PrintPhaseResult(PhaseName(BenchPhase::LayoutGuidePlan), totals.phases[PhaseIndex(BenchPhase::LayoutGuidePlan)]);
    PrintPhaseResult(
        PhaseName(BenchPhase::LayoutGuideMeasure), totals.phases[PhaseIndex(BenchPhase::LayoutGuideMeasure)]);
    PrintPhaseResult(
        PhaseName(BenchPhase::LayoutGuidePlacement), totals.phases[PhaseIndex(BenchPhase::LayoutGuidePlacement)]);
    PrintPhaseResult(PhaseName(BenchPhase::LayoutGuideDraw), totals.phases[PhaseIndex(BenchPhase::LayoutGuideDraw)]);
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

    std::cout << "update_telemetry_benchmark mode=sync_collector iterations=" << iterations
              << " render_scale=" << renderScale << "\n";
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
        case Benchmark::LayoutGuideSheet:
            return RunLayoutGuideSheetBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LayoutSwitch:
            return RunLayoutSwitchBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::MouseHover:
            return RunMouseHoverBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::ThemeChange:
            return RunThemeChangeBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
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
