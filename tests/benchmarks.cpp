#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <Wbemidl.h>

#include "config/color_resolver.h"
#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "config/config_telemetry.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "dashboard_renderer/impl/dashboard_renderer_benchmark.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_trace_session.h"
#include "layout_edit/layout_edit_tree.h"
#include "layout_edit_dialog/theme_preview.h"
#include "layout_guide_sheet/layout_guide_sheet.h"
#include "layout_model/layout_edit_service.h"
#include "telemetry/board/lenovo/board_lenovo_vantage.h"
#include "telemetry/board/lenovo/board_lenovo_vantage_bridge.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "telemetry/impl/collector.h"
#include "telemetry/metrics.h"
#include "telemetry/telemetry.h"
#include "util/enum_string.h"
#include "util/file_path.h"
#include "util/lightweight_mutex.h"
#include "util/text_encoding.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/win32_format.h"

#define CASEDASH_BENCHMARK_ITEMS(X)                                                                                    \
    X(Animation, "animation")                                                                                          \
    X(EditLayout, "edit-layout")                                                                                       \
    X(LayoutGuideSheet, "layout-guide-sheet")                                                                          \
    X(LayoutSwitch, "layout-switch")                                                                                   \
    X(LenovoGameZone, "lenovo-gamezone")                                                                               \
    X(LenovoLdePhases, "lenovo-lde-phases")                                                                            \
    X(LenovoHardwareScan, "lenovo-hardware-scan")                                                                      \
    X(MouseHover, "mouse-hover")                                                                                       \
    X(SnapshotHandoff, "snapshot-handoff")                                                                             \
    X(TelemetryInit, "telemetry-init")                                                                                 \
    X(TemperatureSources, "temperature-sources")                                                                       \
    X(ThemeChange, "theme-change")                                                                                     \
    X(UpdateTelemetry, "update-telemetry")

ENUM_STRING_DECLARE(Benchmark, CASEDASH_BENCHMARK_ITEMS);

#undef CASEDASH_BENCHMARK_ITEMS

namespace {

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

constexpr size_t kAnimationBenchmarkActiveTransitionChunkFrames = 120;
constexpr auto kSnapshotHandoffBenchmarkCadence = kTelemetryRefreshInterval - std::chrono::milliseconds(50);

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
    std::optional<FilePath> configPath;
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
    if (commandLine.benchmark == Benchmark::LenovoGameZone) {
        commandLine.iterations = 5;
    } else if (commandLine.benchmark == Benchmark::LenovoLdePhases) {
        commandLine.iterations = 1;
    } else if (commandLine.benchmark == Benchmark::LenovoHardwareScan) {
        commandLine.iterations = 3;
    } else if (commandLine.benchmark == Benchmark::TemperatureSources) {
        commandLine.iterations = 1;
    } else if (commandLine.benchmark == Benchmark::TelemetryInit) {
        commandLine.iterations = 2;
    }
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
        ++nextArgument;
    }
    if (argc > nextArgument) {
        commandLine.configPath = FilePath(argv[nextArgument]);
        ++nextArgument;
    }
    if (argc > nextArgument) {
        std::cerr << "too many benchmark arguments\n";
        return std::nullopt;
    }
    return commandLine;
}

std::unique_ptr<TelemetryCollector> CreateBenchmarkTelemetryCollector(const AppConfig& config, Trace& trace) {
    TelemetryCollectorOptions options;
    // The update-telemetry benchmark intentionally uses the package-private synchronous collector. It measures provider
    // collection CPU directly; the production TelemetryRuntime thread would hide that cost behind scheduling waits.
    options.synchronousProviderSamples = true;
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
    options.liveFake = true;
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

void PumpBenchmarkMessagesUntil(Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        MSG message{};
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        const auto now = Clock::now();
        if (now >= deadline) {
            break;
        }
        const auto remaining = deadline - now;
        const auto maxSleep = std::chrono::milliseconds(5);
        const auto sleepDuration = remaining < maxSleep ? remaining : maxSleep;
        const auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(sleepDuration).count();
        Sleep(sleepMs > 0 ? static_cast<DWORD>(sleepMs) : 1);
    }
}

HWND CreateBenchmarkWindow(int width, int height, std::string_view title) {
    const std::string windowTitle(title);
    return CreateWindowExA(WS_EX_TOOLWINDOW,
        "STATIC",
        windowTitle.c_str(),
        WS_POPUP,
        0,
        0,
        width,
        height,
        nullptr,
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr);
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
        hwnd_ = CreateBenchmarkWindow(renderer_.WindowWidth(), renderer_.WindowHeight(), "CaseDashBenchmarkHost");
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

struct AnimationBenchTotals {
    BenchResult animationLoop;
    PhaseStats frame;
    std::string errorText;
    size_t snapshotAnimations = 0;
    size_t overlayAnimations = 0;
    bool succeeded = true;
};

struct SnapshotHandoffBenchTotals {
    BenchResult handoffLoop;
    PhaseStats frameBuild;
    PhaseStats framePublish;
    std::string errorText;
    bool succeeded = true;
};

struct TelemetryInitBenchTotals {
    BenchResult initLoop;
    PhaseStats collectorCreate;
    PhaseStats collectorInitialize;
    PhaseStats collectorDestroy;
    ResolvedTelemetrySelections lastResolvedSelections;
    std::string errorText;
    uint64_t lastRevision = 0;
    size_t lastGpuAdapterCandidates = 0;
    size_t lastNetworkAdapterCandidates = 0;
    size_t lastStorageDriveCandidates = 0;
    bool succeeded = true;
};

struct LenovoGameZoneBenchTotals {
    BenchResult loop;
    PhaseStats sample;
    BoardVendorTelemetrySample lastSample;
};

struct LenovoHardwareScanReading {
    std::string title;
    double celsius = 0.0;
};

struct LenovoHardwareScanIterationResult {
    std::chrono::nanoseconds elapsed{};
    std::vector<LenovoHardwareScanReading> readings;
    std::vector<std::string> executionResults;
    std::vector<std::string> timings;
    std::string diagnostics;
    std::string moduleLoadResult;
    bool captured = false;
};

struct LenovoHardwareScanBenchTotals {
    BenchResult loop;
    PhaseStats sample;
    PhaseStats warmSample;
    std::chrono::nanoseconds coldSample{};
    FilePath addinDirectory;
    LenovoHardwareScanIterationResult cold;
    LenovoHardwareScanIterationResult last;
    std::string errorText;
    bool succeeded = true;
};

struct LenovoLdePhaseResult {
    const char* name = "";
    LenovoHardwareScanLdeProbeMode mode = LenovoHardwareScanLdeProbeMode::ManualCpuThermalToolExecution;
    LenovoHardwareScanIterationResult result;
};

struct LenovoLdePhaseBenchTotals {
    BenchResult loop;
    FilePath addinDirectory;
    std::vector<LenovoLdePhaseResult> phases;
    std::string errorText;
    bool succeeded = true;
};

struct TemperatureSourceResult {
    std::string source;
    std::chrono::nanoseconds elapsed{};
    std::optional<double> temperatureC;
    std::string diagnostics;
    bool available = false;
};

struct TemperatureSourceStats {
    std::string source;
    PhaseStats timing;
    TemperatureSourceResult last;
};

struct TemperatureSourcesBenchTotals {
    BenchResult loop;
    std::vector<TemperatureSourceStats> sources;
};

class AnimationBenchmarkRenderWorker {
public:
    explicit AnimationBenchmarkRenderWorker(HWND hwnd)
        : hwnd_(hwnd), requestEvent_(CreateEventA(nullptr, TRUE, FALSE, nullptr)),
          responseEvent_(CreateEventA(nullptr, TRUE, FALSE, nullptr)) {}

    ~AnimationBenchmarkRenderWorker() {
        Shutdown();
        if (requestEvent_ != nullptr) {
            CloseHandle(requestEvent_);
        }
        if (responseEvent_ != nullptr) {
            CloseHandle(responseEvent_);
        }
    }

    bool Start(DashboardPresentationFrame frame) {
        if (thread_ != nullptr || requestEvent_ == nullptr || responseEvent_ == nullptr) {
            return false;
        }
        thread_ = CreateThread(nullptr, 0, &AnimationBenchmarkRenderWorker::ThreadProc, this, 0, nullptr);
        if (thread_ == nullptr) {
            return false;
        }
        return Send(RequestKind::Initialize, std::move(frame));
    }

    bool ResetTimeline() {
        return Send(RequestKind::ResetTimeline);
    }

    bool PresentStoredFrame() {
        return Send(RequestKind::PresentStoredFrame);
    }

    void Shutdown() {
        if (thread_ == nullptr) {
            return;
        }
        Send(RequestKind::Shutdown);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }

    const std::string& LastError() const {
        return lastError_;
    }

private:
    enum class RequestKind {
        Initialize,
        ResetTimeline,
        PresentStoredFrame,
        Shutdown,
    };

    bool Send(RequestKind kind) {
        return Send(kind, std::nullopt);
    }

    bool Send(RequestKind kind, std::optional<DashboardPresentationFrame> frame) {
        {
            const LightweightMutexLock lock(mutex_);
            if (requestPending_ && !responseReady_) {
                return false;
            }
            requestKind_ = kind;
            requestFrame_ = std::move(frame);
            requestPending_ = true;
            responseReady_ = false;
            ResetEvent(responseEvent_);
        }
        SetEvent(requestEvent_);

        for (;;) {
            WaitForSingleObject(responseEvent_, INFINITE);
            const LightweightMutexLock lock(mutex_);
            if (responseReady_) {
                const bool ok = responseOk_;
                lastError_ = responseError_;
                requestPending_ = false;
                ResetEvent(requestEvent_);
                return ok;
            }
        }
    }

    void CompleteRequest(bool ok, std::string error) {
        {
            const LightweightMutexLock lock(mutex_);
            responseOk_ = ok;
            responseError_ = std::move(error);
            responseReady_ = true;
        }
        SetEvent(responseEvent_);
    }

    void ThreadMain() {
        DashboardRenderThread presenter;
        presenter.Configure(hwnd_, false, true);

        for (;;) {
            RequestKind kind = RequestKind::Shutdown;
            std::optional<DashboardPresentationFrame> frame;
            for (;;) {
                {
                    const LightweightMutexLock lock(mutex_);
                    if (requestPending_ && !responseReady_) {
                        kind = requestKind_;
                        frame = std::move(requestFrame_);
                        ResetEvent(requestEvent_);
                        break;
                    }
                }
                WaitForSingleObject(requestEvent_, INFINITE);
            }

            bool ok = true;
            std::string error;
            switch (kind) {
                case RequestKind::Initialize:
                    if (!frame.has_value()) {
                        ok = false;
                        error = "animation benchmark worker missing initial frame";
                    } else {
                        ok = presenter.PresentFrameSynchronously(std::move(*frame));
                        error = presenter.LastError();
                    }
                    break;
                case RequestKind::ResetTimeline:
                    presenter.ResetTimeline();
                    break;
                case RequestKind::PresentStoredFrame:
                    ok = presenter.PresentStoredFrameSynchronously();
                    error = presenter.LastError();
                    break;
                case RequestKind::Shutdown:
                    presenter.Shutdown();
                    CompleteRequest(true, {});
                    return;
            }
            CompleteRequest(ok, std::move(error));
        }
    }

    static DWORD WINAPI ThreadProc(void* context) {
        static_cast<AnimationBenchmarkRenderWorker*>(context)->ThreadMain();
        return 0;
    }

    HWND hwnd_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE requestEvent_ = nullptr;
    HANDLE responseEvent_ = nullptr;
    mutable LightweightMutex mutex_;
    RequestKind requestKind_ = RequestKind::Shutdown;
    std::optional<DashboardPresentationFrame> requestFrame_;
    bool requestPending_ = false;
    bool responseReady_ = false;
    bool responseOk_ = false;
    std::string responseError_;
    std::string lastError_;
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

AnimationBenchTotals RunAnimationFrameBenchmark(DashboardPresentationFrame frame, HWND hwnd, size_t iterations) {
    AnimationBenchTotals totals{};
    totals.snapshotAnimations = frame.snapshotAnimations.size();
    totals.overlayAnimations = frame.overlayAnimations.size();
    if (iterations == 0) {
        return totals;
    }

    AnimationBenchmarkRenderWorker presenter(hwnd);
    if (!presenter.Start(std::move(frame))) {
        totals.succeeded = false;
        totals.errorText = "initial animation frame present failed: " + presenter.LastError();
        return totals;
    }

    size_t remainingFrames = iterations;
    while (remainingFrames > 0) {
        if (!presenter.ResetTimeline() || !presenter.PresentStoredFrame()) {
            totals.succeeded = false;
            totals.errorText = "animation frame seed failed: " + presenter.LastError();
            break;
        }

        const size_t chunkFrames = std::min(kAnimationBenchmarkActiveTransitionChunkFrames, remainingFrames);
        for (size_t iteration = 0; iteration < chunkFrames; ++iteration) {
            const auto frameStart = Clock::now();
            if (!presenter.PresentStoredFrame()) {
                totals.succeeded = false;
                totals.errorText = "animation frame present failed: " + presenter.LastError();
                break;
            }
            RecordPhase(totals.frame, Clock::now() - frameStart);
        }
        if (!totals.succeeded) {
            break;
        }
        remainingFrames -= chunkFrames;
    }
    totals.animationLoop.total = totals.frame.total;
    if (totals.frame.samples > 0) {
        totals.animationLoop.perIteration = totals.animationLoop.total / static_cast<double>(totals.frame.samples);
    }
    return totals;
}

SnapshotHandoffBenchTotals RunSnapshotHandoffBenchmark(
    DashboardRenderer& renderer, TelemetryCollector& telemetry, size_t iterations) {
    SnapshotHandoffBenchTotals totals{};
    if (iterations == 0) {
        return totals;
    }

    telemetry.UpdateSnapshot();
    DashboardPresentationFrame warmupFrame;
    if (!DashboardRendererBenchmarkAccess::BuildSnapshotHandoffFrame(renderer, telemetry.Snapshot(), warmupFrame) ||
        !DashboardRendererBenchmarkAccess::PublishSnapshotHandoffFrame(renderer, std::move(warmupFrame))) {
        totals.succeeded = false;
        totals.errorText = "snapshot handoff warmup failed: " + renderer.LastError();
        return totals;
    }
    PumpBenchmarkMessagesUntil(Clock::now() + kSnapshotHandoffBenchmarkCadence);

    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto iterationStart = Clock::now();
        telemetry.UpdateSnapshot();

        DashboardPresentationFrame frame;
        const auto buildStart = Clock::now();
        if (!DashboardRendererBenchmarkAccess::BuildSnapshotHandoffFrame(renderer, telemetry.Snapshot(), frame)) {
            totals.succeeded = false;
            totals.errorText = "snapshot frame build failed: " + renderer.LastError();
            return totals;
        }
        RecordPhase(totals.frameBuild, Clock::now() - buildStart);

        const auto publishStart = Clock::now();
        if (!DashboardRendererBenchmarkAccess::PublishSnapshotHandoffFrame(renderer, std::move(frame))) {
            totals.succeeded = false;
            totals.errorText = "snapshot frame publish failed: " + renderer.LastError();
            return totals;
        }
        RecordPhase(totals.framePublish, Clock::now() - publishStart);
        PumpBenchmarkMessagesUntil(iterationStart + kSnapshotHandoffBenchmarkCadence);
    }
    totals.handoffLoop.total = Duration(totals.frameBuild.total + totals.framePublish.total);
    totals.handoffLoop.perIteration = totals.handoffLoop.total / static_cast<double>(iterations);
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

void PrintPhaseResult(const char* name, const PhaseStats& stats);

TelemetryInitBenchTotals RunTelemetryInitBenchmark(const TelemetrySettings& settings, size_t iterations, Trace& trace) {
    TelemetryInitBenchTotals totals{};
    if (iterations == 0) {
        return totals;
    }

    TelemetryCollectorOptions options;
    options.synchronousProviderSamples = true;
    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto createStart = Clock::now();
        std::unique_ptr<TelemetryCollector> telemetry =
            CreateTelemetryCollector(options, CurrentDirectoryPath(), trace);
        RecordPhase(totals.collectorCreate, Clock::now() - createStart);
        if (telemetry == nullptr) {
            totals.succeeded = false;
            totals.errorText = "telemetry collector creation failed";
            break;
        }

        std::string errorText;
        const auto initializeStart = Clock::now();
        const bool initialized = telemetry->Initialize(settings, &errorText);
        RecordPhase(totals.collectorInitialize, Clock::now() - initializeStart);
        if (!initialized) {
            totals.succeeded = false;
            totals.errorText = errorText.empty() ? "telemetry initialize failed" : errorText;
            break;
        }

        totals.lastRevision = telemetry->Snapshot().revision;
        totals.lastResolvedSelections = telemetry->ResolvedSelections();
        totals.lastGpuAdapterCandidates = telemetry->GpuAdapterCandidates().size();
        totals.lastNetworkAdapterCandidates = telemetry->NetworkAdapterCandidates().size();
        totals.lastStorageDriveCandidates = telemetry->StorageDriveCandidates().size();

        const auto destroyStart = Clock::now();
        telemetry.reset();
        RecordPhase(totals.collectorDestroy, Clock::now() - destroyStart);
    }
    totals.initLoop.total = Clock::now() - start;
    totals.initLoop.perIteration = totals.initLoop.total / static_cast<double>(iterations);
    return totals;
}

void PrintTelemetryInitBenchResult(const TelemetryInitBenchTotals& totals) {
    PrintBenchLoopResult("iteration_loop", totals.initLoop);
    PrintPhaseResult("collector_create", totals.collectorCreate);
    PrintPhaseResult("collector_initialize", totals.collectorInitialize);
    PrintPhaseResult("collector_destroy", totals.collectorDestroy);
}

LenovoGameZoneBenchTotals RunLenovoGameZoneBenchmark(size_t iterations, Trace& trace) {
    LenovoGameZoneBenchTotals totals{};
    if (iterations == 0) {
        return totals;
    }

    const BoardVendorInfo info = ExtractBoardVendorInfo();
    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto sampleStart = Clock::now();
        totals.lastSample = CaptureLenovoGameZoneWmiSensorSample(trace, info);
        RecordPhase(totals.sample, Clock::now() - sampleStart);
    }
    totals.loop.total = Clock::now() - start;
    totals.loop.perIteration = totals.loop.total / static_cast<double>(iterations);
    return totals;
}

void PrintNamedScalarMetrics(const char* label, const std::vector<NamedScalarMetric>& metrics) {
    std::cout << std::left << std::setw(18) << label << " count=" << metrics.size() << "\n";
    for (const NamedScalarMetric& metric : metrics) {
        std::cout << std::left << std::setw(18) << label << " name=\"" << metric.name << "\" value=";
        if (metric.metric.value.has_value()) {
            std::cout << std::fixed << std::setprecision(2) << *metric.metric.value;
        } else {
            std::cout << "N/A";
        }
        std::cout << " unit=\"" << EnumToString(metric.metric.unit) << "\"\n";
    }
}

void PrintLenovoGameZoneBenchResult(const LenovoGameZoneBenchTotals& totals) {
    PrintBenchLoopResult("gamezone_loop", totals.loop);
    PrintPhaseResult("gamezone_sample", totals.sample);
    const BoardVendorTelemetrySample& sample = totals.lastSample;
    std::cout << std::left << std::setw(18) << "gamezone_result" << " available=" << (sample.available ? "yes" : "no")
              << " provider=\"" << sample.providerName << "\" driver=\"" << sample.driverLibrary << "\" diagnostics=\""
              << sample.diagnostics << "\"\n";
    PrintNamedScalarMetrics("temperature", sample.temperatures);
    PrintNamedScalarMetrics("fan", sample.fans);
}

class BenchmarkBstr {
public:
    explicit BenchmarkBstr(const wchar_t* value) : value_(SysAllocString(value)) {}

    explicit BenchmarkBstr(const std::wstring& value) : value_(SysAllocString(value.c_str())) {}

    ~BenchmarkBstr() {
        SysFreeString(value_);
    }

    BenchmarkBstr(const BenchmarkBstr&) = delete;
    BenchmarkBstr& operator=(const BenchmarkBstr&) = delete;

    BSTR Get() const {
        return value_;
    }

    bool Valid() const {
        return value_ != nullptr;
    }

private:
    BSTR value_ = nullptr;
};

template <typename T> class BenchmarkComObject {
public:
    BenchmarkComObject() = default;

    ~BenchmarkComObject() {
        Reset();
    }

    BenchmarkComObject(const BenchmarkComObject&) = delete;
    BenchmarkComObject& operator=(const BenchmarkComObject&) = delete;

    T* Get() const {
        return value_;
    }

    T** Out() {
        Reset();
        return &value_;
    }

    void Reset() {
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T* value_ = nullptr;
};

class BenchmarkComApartment {
public:
    BenchmarkComApartment() : status_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        uninitialize_ = SUCCEEDED(status_);
    }

    ~BenchmarkComApartment() {
        if (uninitialize_) {
            CoUninitialize();
        }
    }

    bool Ready() const {
        return SUCCEEDED(status_) || status_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Status() const {
        return status_;
    }

private:
    HRESULT status_ = E_FAIL;
    bool uninitialize_ = false;
};

std::string FormatBenchmarkHresult(HRESULT value) {
    std::string text;
    AppendHresult(text, value);
    return text;
}

bool IsSaneTemperatureC(double value) {
    return std::isfinite(value) && value > 0.0 && value <= 125.0;
}

bool StartsWithWide(const wchar_t* text, const wchar_t* prefix) {
    return text != nullptr && prefix != nullptr && std::wcsncmp(text, prefix, std::wcslen(prefix)) == 0;
}

bool ContainsWide(const wchar_t* text, const wchar_t* needle) {
    return text != nullptr && needle != nullptr && std::wcsstr(text, needle) != nullptr;
}

std::string EscapeBenchmarkText(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        if (ch == '"' || ch == '\r' || ch == '\n' || ch == '\t') {
            escaped.push_back(' ');
        } else {
            escaped.push_back(ch);
        }
        if (escaped.size() >= 1400) {
            escaped += "...";
            break;
        }
    }
    return escaped;
}

std::string TextFromNullableWide(const wchar_t* text) {
    return text != nullptr ? TextFromWide(std::wstring_view(text)) : std::string();
}

bool DirectoryExists(const FilePath& path) {
    if (path.Empty()) {
        return false;
    }
    const DWORD attributes = GetFileAttributesA(path.string().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<FilePath> ProgramDataDirectory() {
    const DWORD required = GetEnvironmentVariableA("ProgramData", nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }
    std::string value(required, '\0');
    const DWORD written = GetEnvironmentVariableA("ProgramData", value.data(), required);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    value.resize(written);
    return FilePath(std::move(value));
}

std::optional<std::array<int, 4>> ParseVersionParts(std::string_view text) {
    std::array<int, 4> parts{};
    size_t partIndex = 0;
    size_t offset = 0;
    while (partIndex < parts.size() && offset <= text.size()) {
        const size_t dot = text.find('.', offset);
        const size_t endOffset = dot == std::string_view::npos ? text.size() : dot;
        if (endOffset == offset) {
            return std::nullopt;
        }

        const std::string part(text.substr(offset, endOffset - offset));
        char* parseEnd = nullptr;
        const long parsed = std::strtol(part.c_str(), &parseEnd, 10);
        if (parseEnd == part.c_str() || *parseEnd != '\0' || parsed < 0) {
            return std::nullopt;
        }
        parts[partIndex] = static_cast<int>(parsed);
        ++partIndex;

        if (dot == std::string_view::npos) {
            break;
        }
        offset = dot + 1;
    }
    return partIndex > 0 ? std::optional<std::array<int, 4>>(parts) : std::nullopt;
}

bool VersionPartsLess(const std::array<int, 4>& lhs, const std::array<int, 4>& rhs) {
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return lhs[index] < rhs[index];
        }
    }
    return false;
}

bool IsBenchmarkLenovoHardwareScanDirectory(const FilePath& path) {
    return DirectoryExists(path) && FileExists(path / "LdeApi.Core.dll") && FileExists(path / "LdeApi.Client.dll") &&
           FileExists(path / "Lenovo.Vantage.RpcClient.dll") && FileExists(path / "thermal_monitor_tool.dll") &&
           FileExists(path / "lde_module_cpu.dll");
}

std::optional<FilePath> FindBenchmarkLenovoHardwareScanDirectory() {
    const std::optional<FilePath> programData = ProgramDataDirectory();
    if (!programData.has_value()) {
        return std::nullopt;
    }
    const FilePath root = *programData / "Lenovo" / "Vantage" / "Addins" / "LenovoHardwareScanAddin";
    if (!DirectoryExists(root)) {
        return std::nullopt;
    }

    WIN32_FIND_DATAA data{};
    const FilePath pattern = root / "*";
    HANDLE find = FindFirstFileA(pattern.string().c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::optional<FilePath> bestPath;
    std::optional<std::array<int, 4>> bestVersion;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || std::strcmp(data.cFileName, ".") == 0 ||
            std::strcmp(data.cFileName, "..") == 0) {
            continue;
        }
        const FilePath candidate = root / data.cFileName;
        if (!IsBenchmarkLenovoHardwareScanDirectory(candidate)) {
            continue;
        }
        const std::optional<std::array<int, 4>> version = ParseVersionParts(data.cFileName);
        if (!bestPath.has_value() ||
            (version.has_value() && (!bestVersion.has_value() || VersionPartsLess(*bestVersion, *version)))) {
            bestPath = candidate;
            bestVersion = version;
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);

    return bestPath;
}

class BenchmarkLenovoHardwareScanSink final : public LenovoHardwareScanCaptureSink {
public:
    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        readings_.push_back({TextFromNullableWide(title), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        diagnostics_ = TextFromNullableWide(diagnostics);
    }

    void TraceAssemblyLoaded(const wchar_t* path) override {
        assemblyPath_ = TextFromNullableWide(path);
    }

    void TraceExecutionResult(const wchar_t* result) override {
        executionResults_.push_back(TextFromNullableWide(result));
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        diagnostics_ = TextFromNullableWide(diagnostics);
    }

    void TraceModuleLoadResult(const wchar_t* result) override {
        moduleLoadResult_ = TextFromNullableWide(result);
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        diagnostics_ = TextFromNullableWide(diagnostics);
    }

    bool TraceTimingEnabled() const override {
        return true;
    }

    void TraceTiming(const wchar_t* timing) override {
        timings_.push_back(TextFromNullableWide(timing));
    }

    LenovoHardwareScanIterationResult Result(std::chrono::nanoseconds elapsed, bool captured) const {
        LenovoHardwareScanIterationResult result;
        result.elapsed = elapsed;
        result.readings = readings_;
        result.executionResults = executionResults_;
        result.timings = timings_;
        result.diagnostics = diagnostics_.empty() ? assemblyPath_ : diagnostics_;
        result.moduleLoadResult = moduleLoadResult_;
        result.captured = captured;
        return result;
    }

private:
    std::vector<LenovoHardwareScanReading> readings_;
    std::vector<std::string> executionResults_;
    std::vector<std::string> timings_;
    std::string diagnostics_;
    std::string assemblyPath_;
    std::string moduleLoadResult_;
};

LenovoHardwareScanBenchTotals RunLenovoHardwareScanBenchmark(size_t iterations, Trace&) {
    LenovoHardwareScanBenchTotals totals{};
    if (iterations == 0) {
        return totals;
    }

    const std::optional<FilePath> addinDirectory = FindBenchmarkLenovoHardwareScanDirectory();
    if (!addinDirectory.has_value()) {
        totals.succeeded = false;
        totals.errorText = "Lenovo Hardware Scan addin directory was not found";
        return totals;
    }
    totals.addinDirectory = *addinDirectory;

    LenovoHardwareScanCaptureOptions options;
    options.includeCpuTemperature = true;
    options.includeGpuTemperature = false;
    options.includeStorageTemperature = false;
    options.includeMotherboardTemperature = false;
    options.includeBatteryTemperature = false;

    LenovoHardwareScanRuntime runtime;
    const std::wstring wideAddinDirectory = totals.addinDirectory.WideForNativeApi();
    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        BenchmarkLenovoHardwareScanSink sink;
        const auto sampleStart = Clock::now();
        const bool captured = runtime.Capture(wideAddinDirectory.c_str(), options, sink);
        const std::chrono::nanoseconds elapsed = Clock::now() - sampleStart;
        RecordPhase(totals.sample, elapsed);
        if (iteration == 0) {
            totals.coldSample = elapsed;
            totals.cold = sink.Result(elapsed, captured);
        } else {
            RecordPhase(totals.warmSample, elapsed);
        }
        totals.last = sink.Result(elapsed, captured);
    }
    totals.loop.total = Clock::now() - start;
    totals.loop.perIteration = totals.loop.total / static_cast<double>(iterations);
    return totals;
}

LenovoLdePhaseBenchTotals RunLenovoLdePhaseBenchmark(Trace&) {
    LenovoLdePhaseBenchTotals totals{};
    const std::optional<FilePath> addinDirectory = FindBenchmarkLenovoHardwareScanDirectory();
    if (!addinDirectory.has_value()) {
        totals.succeeded = false;
        totals.errorText = "Lenovo Hardware Scan addin directory was not found";
        return totals;
    }
    totals.addinDirectory = *addinDirectory;

    totals.phases = {
        {"manual-thermal-tool", LenovoHardwareScanLdeProbeMode::ManualCpuThermalToolExecution, {}},
        {"available-modules-then-manual",
            LenovoHardwareScanLdeProbeMode::AvailableModulesThenManualCpuThermalToolExecution,
            {}},
        {"load-modules-then-execution", LenovoHardwareScanLdeProbeMode::LoadModulesThenCpuThermalToolExecution, {}},
    };

    const std::wstring wideAddinDirectory = totals.addinDirectory.WideForNativeApi();
    const auto start = Clock::now();
    for (LenovoLdePhaseResult& phase : totals.phases) {
        BenchmarkLenovoHardwareScanSink sink;
        const auto phaseStart = Clock::now();
        const bool captured = ProbeLenovoHardwareScanCpuThermalTool(wideAddinDirectory.c_str(), phase.mode, sink);
        const std::chrono::nanoseconds elapsed = Clock::now() - phaseStart;
        phase.result = sink.Result(elapsed, captured);
    }
    totals.loop.total = Clock::now() - start;
    totals.loop.perIteration = totals.loop.total / static_cast<double>(totals.phases.size());
    return totals;
}

void PrintLenovoHardwareScanBenchResult(const LenovoHardwareScanBenchTotals& totals) {
    PrintBenchLoopResult("scan_loop", totals.loop);
    PrintPhaseResult("scan_sample", totals.sample);
    PrintPhaseResult("scan_warm_sample", totals.warmSample);
    std::cout << std::left << std::setw(18) << "scan_cold" << " elapsed_ms=" << std::fixed << std::setprecision(2)
              << DurationMilliseconds(totals.coldSample) << "\n";
    std::cout << std::left << std::setw(18) << "scan_addin" << " path=\"" << totals.addinDirectory.string() << "\"\n";

    const LenovoHardwareScanIterationResult& last = totals.last;
    std::cout << std::left << std::setw(18) << "scan_last" << " captured=" << (last.captured ? "yes" : "no")
              << " elapsed_ms=" << std::fixed << std::setprecision(2) << DurationMilliseconds(last.elapsed)
              << " temperatures=" << last.readings.size() << " module_load=\""
              << EscapeBenchmarkText(last.moduleLoadResult) << "\" diagnostics=\""
              << EscapeBenchmarkText(last.diagnostics) << "\"\n";
    for (const LenovoHardwareScanReading& reading : last.readings) {
        std::cout << std::left << std::setw(18) << "scan_temperature" << " name=\"" << reading.title
                  << "\" value_c=" << std::fixed << std::setprecision(2) << reading.celsius << "\n";
    }
    size_t index = 0;
    for (const std::string& timing : totals.cold.timings) {
        std::cout << std::left << std::setw(18) << "scan_cold_timing" << " index=" << index++ << " "
                  << EscapeBenchmarkText(timing) << "\n";
    }
    index = 0;
    for (const std::string& result : last.executionResults) {
        std::cout << std::left << std::setw(18) << "scan_execution" << " index=" << index++ << " result=\""
                  << EscapeBenchmarkText(result) << "\"\n";
    }
    index = 0;
    for (const std::string& timing : last.timings) {
        std::cout << std::left << std::setw(18) << "scan_timing" << " index=" << index++ << " "
                  << EscapeBenchmarkText(timing) << "\n";
    }
}

void PrintLenovoLdePhaseBenchResult(const LenovoLdePhaseBenchTotals& totals) {
    PrintBenchLoopResult("lde_phase_loop", totals.loop);
    std::cout << std::left << std::setw(18) << "lde_phase_addin" << " path=\"" << totals.addinDirectory.string()
              << "\"\n";

    for (const LenovoLdePhaseResult& phase : totals.phases) {
        const LenovoHardwareScanIterationResult& result = phase.result;
        std::cout << std::left << std::setw(18) << "lde_phase" << " name=\"" << phase.name
                  << "\" captured=" << (result.captured ? "yes" : "no") << " elapsed_ms=" << std::fixed
                  << std::setprecision(2) << DurationMilliseconds(result.elapsed)
                  << " temperatures=" << result.readings.size() << " module_load=\""
                  << EscapeBenchmarkText(result.moduleLoadResult) << "\" diagnostics=\""
                  << EscapeBenchmarkText(result.diagnostics) << "\"\n";

        for (const LenovoHardwareScanReading& reading : result.readings) {
            std::cout << std::left << std::setw(18) << "lde_temperature" << " phase=\"" << phase.name << "\" name=\""
                      << reading.title << "\" value_c=" << std::fixed << std::setprecision(2) << reading.celsius
                      << "\n";
        }

        size_t index = 0;
        for (const std::string& execution : result.executionResults) {
            std::cout << std::left << std::setw(18) << "lde_execution" << " phase=\"" << phase.name
                      << "\" index=" << index++ << " result=\"" << EscapeBenchmarkText(execution) << "\"\n";
        }

        index = 0;
        for (const std::string& timing : result.timings) {
            std::cout << std::left << std::setw(18) << "lde_timing" << " phase=\"" << phase.name
                      << "\" index=" << index++ << " " << EscapeBenchmarkText(timing) << "\n";
        }
    }
}

std::optional<double> NumericVariantValue(const VARIANT& value) {
    if ((value.vt & VT_BYREF) != 0) {
        return std::nullopt;
    }
    switch (value.vt) {
        case VT_I1:
            return static_cast<double>(value.cVal);
        case VT_UI1:
            return static_cast<double>(value.bVal);
        case VT_I2:
            return static_cast<double>(value.iVal);
        case VT_UI2:
            return static_cast<double>(value.uiVal);
        case VT_I4:
        case VT_INT:
            return static_cast<double>(value.lVal);
        case VT_UI4:
        case VT_UINT:
            return static_cast<double>(value.ulVal);
        case VT_I8:
            return static_cast<double>(value.llVal);
        case VT_UI8:
            return static_cast<double>(value.ullVal);
        case VT_R4:
            return static_cast<double>(value.fltVal);
        case VT_R8:
            return value.dblVal;
        case VT_BSTR:
            if (value.bstrVal != nullptr) {
                wchar_t* end = nullptr;
                const double parsed = std::wcstod(value.bstrVal, &end);
                if (end != value.bstrVal && std::isfinite(parsed)) {
                    return parsed;
                }
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

std::string FormatVariantBrief(const VARIANT& value) {
    if (value.vt == VT_EMPTY) {
        return "empty";
    }
    if (value.vt == VT_NULL) {
        return "null";
    }
    if (value.vt == VT_BOOL) {
        return value.boolVal == VARIANT_TRUE ? "true" : "false";
    }
    if (value.vt == VT_BSTR) {
        return value.bstrVal != nullptr ? TextFromWide(value.bstrVal) : std::string();
    }
    const std::optional<double> numeric = NumericVariantValue(value);
    if (numeric.has_value()) {
        std::ostringstream text;
        text << std::fixed << std::setprecision(2) << *numeric;
        return text.str();
    }
    return FormatText("vt=%u", static_cast<unsigned int>(value.vt));
}

std::optional<double> InterpretPossibleTemperature(double raw) {
    if (IsSaneTemperatureC(raw)) {
        return raw;
    }
    const double acpiCelsius = (raw / 10.0) - 273.15;
    if (raw > 1000.0 && IsSaneTemperatureC(acpiCelsius)) {
        return acpiCelsius;
    }
    return std::nullopt;
}

std::optional<double> ReadNumericProperty(IWbemClassObject* object, const wchar_t* propertyName, std::string& text) {
    VARIANT value;
    VariantInit(&value);
    const HRESULT hr = object != nullptr ? object->Get(propertyName, 0, &value, nullptr, nullptr) : E_POINTER;
    if (FAILED(hr)) {
        text = FormatText("%s=%s", TextFromWide(propertyName).c_str(), FormatBenchmarkHresult(hr).c_str());
        return std::nullopt;
    }
    text = FormatText("%s=%s", TextFromWide(propertyName).c_str(), FormatVariantBrief(value).c_str());
    const std::optional<double> numeric = NumericVariantValue(value);
    VariantClear(&value);
    return numeric;
}

bool ConnectWmiServices(const wchar_t* namespacePath, BenchmarkComObject<IWbemServices>& services, std::string& error) {
    HRESULT securityHr = CoInitializeSecurity(nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);
    if (FAILED(securityHr) && securityHr != RPC_E_TOO_LATE) {
        error = FormatText("CoInitializeSecurity failed: %s", FormatBenchmarkHresult(securityHr).c_str());
        return false;
    }

    BenchmarkComObject<IWbemLocator> locator;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.Out()));
    if (FAILED(hr) || locator.Get() == nullptr) {
        error = FormatText("WbemLocator creation failed: %s", FormatBenchmarkHresult(hr).c_str());
        return false;
    }

    BenchmarkBstr namespaceBstr(namespacePath);
    if (!namespaceBstr.Valid()) {
        error = "WMI namespace allocation failed";
        return false;
    }
    hr = locator.Get()->ConnectServer(
        namespaceBstr.Get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.Out());
    if (FAILED(hr) || services.Get() == nullptr) {
        error = FormatText("WMI connection failed: %s", FormatBenchmarkHresult(hr).c_str());
        return false;
    }

    hr = CoSetProxyBlanket(services.Get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    if (FAILED(hr)) {
        error = FormatText("WMI proxy security failed: %s", FormatBenchmarkHresult(hr).c_str());
        return false;
    }
    return true;
}

TemperatureSourceResult MakeTemperatureSourceResult(
    const std::string& source, const Clock::time_point& start, std::string diagnostics) {
    TemperatureSourceResult result;
    result.source = source;
    result.elapsed = Clock::now() - start;
    result.diagnostics = std::move(diagnostics);
    return result;
}

TemperatureSourceResult ProbeWmiTemperatureQuery(const std::string& source,
    const wchar_t* namespacePath,
    const wchar_t* queryText,
    const std::vector<const wchar_t*>& propertyNames) {
    const auto start = Clock::now();
    BenchmarkComApartment apartment;
    if (!apartment.Ready()) {
        return MakeTemperatureSourceResult(source,
            start,
            FormatText("COM initialization failed: %s", FormatBenchmarkHresult(apartment.Status()).c_str()));
    }

    BenchmarkComObject<IWbemServices> services;
    std::string error;
    if (!ConnectWmiServices(namespacePath, services, error)) {
        return MakeTemperatureSourceResult(source, start, error);
    }

    BenchmarkBstr language(L"WQL");
    BenchmarkBstr query(queryText);
    BenchmarkComObject<IEnumWbemClassObject> enumerator;
    HRESULT hr = services.Get()->ExecQuery(
        language.Get(), query.Get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.Out());
    if (FAILED(hr) || enumerator.Get() == nullptr) {
        return MakeTemperatureSourceResult(
            source, start, FormatText("ExecQuery failed: %s", FormatBenchmarkHresult(hr).c_str()));
    }

    TemperatureSourceResult result;
    result.source = source;
    std::ostringstream details;
    size_t rowCount = 0;
    while (rowCount < 8) {
        ULONG returned = 0;
        BenchmarkComObject<IWbemClassObject> object;
        hr = enumerator.Get()->Next(1500, 1, object.Out(), &returned);
        if (hr == WBEM_S_FALSE || returned == 0) {
            break;
        }
        if (FAILED(hr)) {
            details << " read_failed=" << FormatBenchmarkHresult(hr);
            break;
        }
        ++rowCount;
        details << " row" << rowCount << "{";
        for (const wchar_t* propertyName : propertyNames) {
            std::string propertyText;
            const std::optional<double> raw = ReadNumericProperty(object.Get(), propertyName, propertyText);
            details << propertyText << ";";
            if (!result.temperatureC.has_value() && raw.has_value()) {
                result.temperatureC = InterpretPossibleTemperature(*raw);
            }
        }
        details << "}";
    }
    result.available = result.temperatureC.has_value();
    result.elapsed = Clock::now() - start;
    result.diagnostics =
        FormatText("namespace=%s rows=%zu %s", TextFromWide(namespacePath).c_str(), rowCount, details.str().c_str());
    return result;
}

std::string FormatWmiOutputProperties(IWbemClassObject* object, std::optional<double>& possibleTemperature) {
    if (object == nullptr) {
        return "no_output";
    }

    SAFEARRAY* names = nullptr;
    HRESULT hr = object->GetNames(nullptr, WBEM_FLAG_NONSYSTEM_ONLY, nullptr, &names);
    if (FAILED(hr) || names == nullptr) {
        return FormatText("GetNames failed: %s", FormatBenchmarkHresult(hr).c_str());
    }

    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(names, 1, &lower);
    SafeArrayGetUBound(names, 1, &upper);
    std::ostringstream text;
    for (LONG index = lower; index <= upper; ++index) {
        BSTR name = nullptr;
        if (FAILED(SafeArrayGetElement(names, &index, &name)) || name == nullptr) {
            continue;
        }
        std::string propertyText;
        const std::optional<double> raw = ReadNumericProperty(object, name, propertyText);
        if (!possibleTemperature.has_value() && raw.has_value()) {
            possibleTemperature = InterpretPossibleTemperature(*raw);
        }
        text << propertyText << ";";
        SysFreeString(name);
    }
    SafeArrayDestroy(names);
    return text.str();
}

size_t WmiInputPropertyCount(IWbemClassObject* inParams) {
    if (inParams == nullptr) {
        return 0;
    }
    SAFEARRAY* names = nullptr;
    const HRESULT hr = inParams->GetNames(nullptr, WBEM_FLAG_NONSYSTEM_ONLY, nullptr, &names);
    if (FAILED(hr) || names == nullptr) {
        return 0;
    }
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(names, 1, &lower);
    SafeArrayGetUBound(names, 1, &upper);
    SafeArrayDestroy(names);
    return upper >= lower ? static_cast<size_t>(upper - lower + 1) : 0;
}

std::optional<std::wstring> FirstWmiRelPath(
    IWbemServices* services, const wchar_t* className, std::string& diagnostics) {
    BenchmarkBstr language(L"WQL");
    const std::wstring queryText = std::wstring(L"SELECT __RELPATH FROM ") + className;
    BenchmarkBstr query(queryText);
    BenchmarkComObject<IEnumWbemClassObject> enumerator;
    HRESULT hr = services->ExecQuery(
        language.Get(), query.Get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.Out());
    if (FAILED(hr) || enumerator.Get() == nullptr) {
        diagnostics = FormatText("instance query failed: %s", FormatBenchmarkHresult(hr).c_str());
        return std::nullopt;
    }

    ULONG returned = 0;
    BenchmarkComObject<IWbemClassObject> object;
    hr = enumerator.Get()->Next(1500, 1, object.Out(), &returned);
    if (hr == WBEM_S_FALSE || returned == 0) {
        diagnostics = "no instances";
        return std::nullopt;
    }
    if (FAILED(hr)) {
        diagnostics = FormatText("instance read failed: %s", FormatBenchmarkHresult(hr).c_str());
        return std::nullopt;
    }

    VARIANT value;
    VariantInit(&value);
    hr = object.Get()->Get(L"__RELPATH", 0, &value, nullptr, nullptr);
    if (FAILED(hr) || value.vt != VT_BSTR || value.bstrVal == nullptr) {
        VariantClear(&value);
        diagnostics = FormatText("instance path missing: %s", FormatBenchmarkHresult(hr).c_str());
        return std::nullopt;
    }
    std::wstring path(value.bstrVal);
    VariantClear(&value);
    return path;
}

bool ShouldInvokeLenovoGetter(const wchar_t* methodName) {
    return StartsWithWide(methodName, L"Get") &&
           (ContainsWide(methodName, L"Temp") || ContainsWide(methodName, L"Thermal") ||
               ContainsWide(methodName, L"Sensor") || ContainsWide(methodName, L"Fan") ||
               ContainsWide(methodName, L"Data"));
}

TemperatureSourceResult ProbeLenovoWmiGetterMethods() {
    const auto start = Clock::now();
    static constexpr const wchar_t* kClasses[] = {
        L"LENOVO_GAMEZONE_DATA",
        L"LENOVO_GAMEZONE_CPU_OC_DATA",
        L"LENOVO_GAMEZONE_GPU_OC_DATA",
        L"LENOVO_UTILITY_DATA",
        L"LENOVO_SUPERKEY_DATA",
        L"BatteryTemperature",
    };

    BenchmarkComApartment apartment;
    if (!apartment.Ready()) {
        return MakeTemperatureSourceResult("lenovo_wmi_getters",
            start,
            FormatText("COM initialization failed: %s", FormatBenchmarkHresult(apartment.Status()).c_str()));
    }

    BenchmarkComObject<IWbemServices> services;
    std::string error;
    if (!ConnectWmiServices(L"ROOT\\WMI", services, error)) {
        return MakeTemperatureSourceResult("lenovo_wmi_getters", start, error);
    }

    TemperatureSourceResult result;
    result.source = "lenovo_wmi_getters";
    std::ostringstream details;
    size_t invoked = 0;
    for (const wchar_t* className : kClasses) {
        BenchmarkBstr classBstr(className);
        BenchmarkComObject<IWbemClassObject> classObject;
        HRESULT hr = services.Get()->GetObject(classBstr.Get(), 0, nullptr, classObject.Out(), nullptr);
        if (FAILED(hr) || classObject.Get() == nullptr) {
            details << TextFromWide(className) << "{class=" << FormatBenchmarkHresult(hr) << "} ";
            continue;
        }

        std::string instanceDiagnostics;
        const std::optional<std::wstring> objectPath = FirstWmiRelPath(services.Get(), className, instanceDiagnostics);
        details << TextFromWide(className) << "{";
        if (!objectPath.has_value()) {
            details << instanceDiagnostics << "} ";
            continue;
        }

        hr = classObject.Get()->BeginMethodEnumeration(0);
        if (FAILED(hr)) {
            details << "method_enum=" << FormatBenchmarkHresult(hr) << "} ";
            continue;
        }

        while (invoked < 80) {
            BSTR methodName = nullptr;
            BenchmarkComObject<IWbemClassObject> inParams;
            BenchmarkComObject<IWbemClassObject> outParams;
            hr = classObject.Get()->NextMethod(0, &methodName, inParams.Out(), outParams.Out());
            if (hr == WBEM_S_NO_MORE_DATA) {
                break;
            }
            if (FAILED(hr) || methodName == nullptr) {
                details << "next_method=" << FormatBenchmarkHresult(hr) << ";";
                break;
            }

            const bool invoke = ShouldInvokeLenovoGetter(methodName) && WmiInputPropertyCount(inParams.Get()) == 0;
            if (invoke) {
                BenchmarkBstr pathBstr(*objectPath);
                BenchmarkBstr methodBstr(methodName);
                BenchmarkComObject<IWbemClassObject> output;
                const HRESULT methodHr = services.Get()->ExecMethod(
                    pathBstr.Get(), methodBstr.Get(), 0, nullptr, nullptr, output.Out(), nullptr);
                std::optional<double> possibleTemperature;
                details << TextFromWide(methodName) << "=";
                if (SUCCEEDED(methodHr)) {
                    details << FormatWmiOutputProperties(output.Get(), possibleTemperature);
                    if (!result.temperatureC.has_value() && possibleTemperature.has_value() &&
                        ContainsWide(methodName, L"Temp")) {
                        result.temperatureC = possibleTemperature;
                    }
                } else {
                    details << FormatBenchmarkHresult(methodHr);
                }
                details << ";";
                ++invoked;
            }
            SysFreeString(methodName);
        }
        classObject.Get()->EndMethodEnumeration();
        details << "} ";
    }

    result.available = result.temperatureC.has_value();
    result.elapsed = Clock::now() - start;
    result.diagnostics = FormatText("invoked_getters=%zu %s", invoked, details.str().c_str());
    return result;
}

TemperatureSourceResult ProbeLenovoGameZoneSource(Trace& trace) {
    const auto start = Clock::now();
    const BoardVendorTelemetrySample sample = CaptureLenovoGameZoneWmiSensorSample(trace, ExtractBoardVendorInfo());
    TemperatureSourceResult result;
    result.source = "lenovo_gamezone_wmi";
    result.elapsed = Clock::now() - start;
    result.available = false;
    for (const NamedScalarMetric& metric : sample.temperatures) {
        if (metric.metric.value.has_value() && IsSaneTemperatureC(*metric.metric.value)) {
            result.temperatureC = *metric.metric.value;
            result.available = true;
            break;
        }
    }
    result.diagnostics = FormatText("provider=%s driver=%s available=%s temperatures=%zu fans=%zu diagnostics=%s",
        sample.providerName.c_str(),
        sample.driverLibrary.c_str(),
        sample.available ? "yes" : "no",
        sample.temperatures.size(),
        sample.fans.size(),
        sample.diagnostics.c_str());
    return result;
}

TemperatureSourceResult ProbeGpuProviderTemperature(
    const std::string& source, std::string_view preferredAdapter, Trace& trace) {
    const auto start = Clock::now();
    GpuAdapterSelection selection = ResolveGpuAdapterSelection(trace, preferredAdapter);
    if (!selection.selectedAdapter.has_value()) {
        return MakeTemperatureSourceResult(source,
            start,
            FormatText("no selected adapter for preferred adapter \"%s\"", std::string(preferredAdapter).c_str()));
    }

    const auto initStart = Clock::now();
    std::unique_ptr<GpuVendorTelemetryProvider> provider =
        CreateGpuVendorTelemetryProvider(trace, selection.selectedAdapter, false);
    const bool initialized = provider != nullptr && provider->Initialize();
    const Duration initDuration = Clock::now() - initStart;
    if (!initialized || provider == nullptr) {
        return MakeTemperatureSourceResult(source,
            start,
            FormatText("provider initialization failed adapter=\"%s\" init_ms=%.2f",
                selection.selectedAdapter->adapterName.c_str(),
                initDuration.count()));
    }

    const auto sampleStart = Clock::now();
    const GpuVendorTelemetrySample sample = provider->Sample();
    const Duration sampleDuration = Clock::now() - sampleStart;

    TemperatureSourceResult result;
    result.source = source;
    result.elapsed = Clock::now() - start;
    result.temperatureC = sample.temperatureC;
    result.available = sample.temperatureC.has_value() && IsSaneTemperatureC(*sample.temperatureC);
    result.diagnostics =
        FormatText("adapter=\"%s\" provider=\"%s\" init_ms=%.2f sample_ms=%.2f available=%s diagnostics=%s",
            selection.selectedAdapter->adapterName.c_str(),
            sample.providerName.c_str(),
            initDuration.count(),
            sampleDuration.count(),
            sample.available ? "yes" : "no",
            sample.diagnostics.c_str());
    return result;
}

std::vector<TemperatureSourceResult> ProbeTemperatureSources(Trace& trace) {
    std::vector<TemperatureSourceResult> results;
    results.push_back(ProbeLenovoGameZoneSource(trace));
    results.push_back(ProbeLenovoWmiGetterMethods());
    results.push_back(ProbeWmiTemperatureQuery("windows_acpi_thermal_zone",
        L"ROOT\\WMI",
        L"SELECT InstanceName, CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
        {L"InstanceName", L"CurrentTemperature"}));
    results.push_back(ProbeWmiTemperatureQuery("windows_perf_thermal_zone",
        L"ROOT\\CIMV2",
        L"SELECT Name, Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation",
        {L"Name", L"Temperature"}));
    results.push_back(ProbeWmiTemperatureQuery("windows_storage_temperature",
        L"ROOT\\Microsoft\\Windows\\Storage",
        L"SELECT FriendlyName, Temperature FROM MSFT_PhysicalDisk",
        {L"FriendlyName", L"Temperature"}));
    results.push_back(ProbeWmiTemperatureQuery("librehardwaremonitor_wmi",
        L"ROOT\\LibreHardwareMonitor",
        L"SELECT Name, Value FROM Sensor WHERE SensorType='Temperature'",
        {L"Name", L"Value"}));
    results.push_back(ProbeWmiTemperatureQuery("openhardwaremonitor_wmi",
        L"ROOT\\OpenHardwareMonitor",
        L"SELECT Name, Value FROM Sensor WHERE SensorType='Temperature'",
        {L"Name", L"Value"}));
    results.push_back(ProbeGpuProviderTemperature("intel_level_zero_gpu_temp", "Intel", trace));
    results.push_back(ProbeGpuProviderTemperature("nvidia_nvml_gpu_temp", "NVIDIA", trace));
    return results;
}

void RecordTemperatureSourceResult(TemperatureSourcesBenchTotals& totals, const TemperatureSourceResult& result) {
    auto it = std::find_if(totals.sources.begin(), totals.sources.end(), [&](const TemperatureSourceStats& stats) {
        return stats.source == result.source;
    });
    if (it == totals.sources.end()) {
        TemperatureSourceStats stats;
        stats.source = result.source;
        stats.last = result;
        RecordPhase(stats.timing, result.elapsed);
        totals.sources.push_back(std::move(stats));
        return;
    }
    it->last = result;
    RecordPhase(it->timing, result.elapsed);
}

TemperatureSourcesBenchTotals RunTemperatureSourcesBenchmark(size_t iterations, Trace& trace) {
    TemperatureSourcesBenchTotals totals{};
    if (iterations == 0) {
        return totals;
    }

    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::vector<TemperatureSourceResult> results = ProbeTemperatureSources(trace);
        for (const TemperatureSourceResult& result : results) {
            RecordTemperatureSourceResult(totals, result);
        }
    }
    totals.loop.total = Clock::now() - start;
    totals.loop.perIteration = totals.loop.total / static_cast<double>(iterations);
    return totals;
}

void PrintTemperatureSourcesBenchResult(const TemperatureSourcesBenchTotals& totals) {
    PrintBenchLoopResult("source_loop", totals.loop);
    for (const TemperatureSourceStats& stats : totals.sources) {
        const double totalMs = Duration(stats.timing.total).count();
        const double avgMs = stats.timing.samples > 0 ? totalMs / static_cast<double>(stats.timing.samples) : 0.0;
        std::cout << std::left << std::setw(22) << "temperature_source" << " source=\"" << stats.source
                  << "\" avg_ms=" << std::fixed << std::setprecision(2) << avgMs << " samples=" << stats.timing.samples
                  << " available=" << (stats.last.available ? "yes" : "no") << " value_c=";
        if (stats.last.temperatureC.has_value()) {
            std::cout << std::fixed << std::setprecision(2) << *stats.last.temperatureC;
        } else {
            std::cout << "N/A";
        }
        std::cout << " diagnostics=\"" << EscapeBenchmarkText(stats.last.diagnostics) << "\"\n";
    }
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

int RunAnimationBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    DashboardRenderer renderer(trace);
    renderer.SetConfig(runtimeConfig);
    renderer.SetRenderScale(renderScale);
    renderer.SetRenderMode(DashboardRenderer::RenderMode::Normal);
    renderer.SetLiveAnimationEnabled(true);
    if (!renderer.Initialize()) {
        std::cerr << "renderer init failed: " << renderer.LastError() << "\n";
        return 1;
    }

    DashboardPresentationFrame frame;
    if (!DashboardRendererBenchmarkAccess::BuildAnimationFrame(renderer, telemetry->Snapshot(), frame)) {
        std::cerr << "animation frame build failed: " << renderer.LastError() << "\n";
        renderer.Shutdown();
        return 1;
    }

    HWND hwnd = CreateBenchmarkWindow(frame.width, frame.height, "CaseDashAnimationBenchmark");
    if (hwnd == nullptr) {
        std::cerr << "benchmark window creation failed\n";
        renderer.Shutdown();
        return 1;
    }

    std::cout << "animation_benchmark iterations=" << iterations << " render_scale=" << renderScale
              << " window=" << frame.width << "x" << frame.height
              << " snapshot_animations=" << frame.snapshotAnimations.size()
              << " overlay_animations=" << frame.overlayAnimations.size()
              << " active_chunk_frames=" << kAnimationBenchmarkActiveTransitionChunkFrames << "\n";
    const AnimationBenchTotals totals = RunAnimationFrameBenchmark(std::move(frame), hwnd, iterations);
    DestroyWindow(hwnd);
    renderer.Shutdown();
    if (!totals.succeeded) {
        std::cerr << totals.errorText << "\n";
        return 1;
    }

    PrintBenchLoopResult("animation_loop", totals.animationLoop);
    PrintPhaseResult("animation_frame", totals.frame);
    return 0;
}

int RunSnapshotHandoffBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    const AppConfig config = LoadConfig(SourceConfigPath(), false, BenchmarkConfigParseContext());
    std::unique_ptr<TelemetryCollector> telemetry = CreateBenchmarkFakeTelemetryCollector(config, trace);
    if (telemetry == nullptr) {
        std::cerr << "fake telemetry init failed\n";
        return 1;
    }

    const AppConfig runtimeConfig = BuildEffectiveRuntimeConfig(config, telemetry->ResolvedSelections());
    DashboardRenderer renderer(trace);
    renderer.SetConfig(runtimeConfig);
    renderer.SetRenderScale(renderScale);
    renderer.SetRenderMode(DashboardRenderer::RenderMode::Normal);
    renderer.SetLiveAnimationEnabled(true);

    HWND hwnd =
        CreateBenchmarkWindow(renderer.WindowWidth(), renderer.WindowHeight(), "CaseDashSnapshotHandoffBenchmark");
    if (hwnd == nullptr) {
        std::cerr << "benchmark window creation failed\n";
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    if (!renderer.Initialize(hwnd)) {
        std::cerr << "renderer init failed: " << renderer.LastError() << "\n";
        DestroyWindow(hwnd);
        return 1;
    }

    std::cout << "snapshot_handoff_benchmark mode=threaded_vsync telemetry_cadence_ms="
              << kSnapshotHandoffBenchmarkCadence.count() << " iterations=" << iterations
              << " render_scale=" << renderScale << " window=" << renderer.WindowWidth() << "x"
              << renderer.WindowHeight() << "\n";
    const SnapshotHandoffBenchTotals totals = RunSnapshotHandoffBenchmark(renderer, *telemetry, iterations);
    renderer.Shutdown();
    DestroyWindow(hwnd);
    if (!totals.succeeded) {
        std::cerr << totals.errorText << "\n";
        return 1;
    }

    PrintBenchLoopResult("snapshot_loop", totals.handoffLoop);
    PrintPhaseResult("presentation_frame_build", totals.frameBuild);
    PrintPhaseResult("presentation_frame_publish", totals.framePublish);
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

int RunLenovoGameZoneBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    std::cout << "lenovo_gamezone_benchmark iterations=" << iterations << " render_scale_ignored=" << renderScale
              << "\n";
    const LenovoGameZoneBenchTotals totals = RunLenovoGameZoneBenchmark(iterations, trace);
    PrintLenovoGameZoneBenchResult(totals);
    return 0;
}

int RunLenovoHardwareScanBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    std::cout << "lenovo_hardware_scan_benchmark modules=cpu_temperature_only iterations=" << iterations
              << " render_scale_ignored=" << renderScale << "\n";
    const LenovoHardwareScanBenchTotals totals = RunLenovoHardwareScanBenchmark(iterations, trace);
    if (!totals.succeeded) {
        std::cerr << totals.errorText << "\n";
        return 1;
    }
    PrintLenovoHardwareScanBenchResult(totals);
    return 0;
}

int RunLenovoLdePhaseBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    std::cout << "lenovo_lde_phases_benchmark mode=cold_private_paths requested_iterations=" << iterations
              << " render_scale_ignored=" << renderScale << "\n";
    const LenovoLdePhaseBenchTotals totals = RunLenovoLdePhaseBenchmark(trace);
    if (!totals.succeeded) {
        std::cerr << totals.errorText << "\n";
        return 1;
    }
    PrintLenovoLdePhaseBenchResult(totals);
    return 0;
}

int RunTemperatureSourcesBenchmarkCommand(size_t iterations, double renderScale, Trace& trace) {
    std::cout << "temperature_sources_benchmark iterations=" << iterations << " render_scale_ignored=" << renderScale
              << "\n";
    const TemperatureSourcesBenchTotals totals = RunTemperatureSourcesBenchmark(iterations, trace);
    PrintTemperatureSourcesBenchResult(totals);
    return 0;
}

int RunUpdateTelemetryBenchmarkCommand(
    size_t iterations, double renderScale, const std::optional<FilePath>& configPath, Trace& trace) {
    const FilePath resolvedConfigPath = configPath.value_or(SourceConfigPath());
    const bool includeOverlay = configPath.has_value();
    const AppConfig config = LoadConfig(resolvedConfigPath, includeOverlay, BenchmarkConfigParseContext());
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

    std::cout << "update_telemetry_benchmark mode=sync_collector sync_provider_samples=yes iterations=" << iterations
              << " render_scale=" << renderScale << " config=\"" << resolvedConfigPath.string()
              << "\" include_overlay=" << (includeOverlay ? "yes" : "no") << "\n";
    const BenchResult result = RunTelemetryUpdateBenchmark(host, *telemetry, iterations);
    PrintTelemetryBenchResult(result);

    const auto& phases = host.PhaseTotals();
    PrintPhaseResult(PhaseName(BenchPhase::TelemetryUpdate), phases[PhaseIndex(BenchPhase::TelemetryUpdate)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintTotal), phases[PhaseIndex(BenchPhase::PaintTotal)]);
    PrintPhaseResult(PhaseName(BenchPhase::PaintDraw), phases[PhaseIndex(BenchPhase::PaintDraw)]);
    return 0;
}

int RunTelemetryInitBenchmarkCommand(
    size_t iterations, double renderScale, const std::optional<FilePath>& configPath, Trace& trace) {
    const FilePath resolvedConfigPath = configPath.value_or(SourceConfigPath());
    const bool includeOverlay = configPath.has_value();
    const AppConfig config = LoadConfig(resolvedConfigPath, includeOverlay, BenchmarkConfigParseContext());
    const TelemetrySettings settings = ExtractTelemetrySettings(config);

    std::cout << "telemetry_init_benchmark mode=sync_collector sync_provider_samples=yes iterations=" << iterations
              << " render_scale_ignored=" << renderScale << " config=\"" << resolvedConfigPath.string()
              << "\" include_overlay=" << (includeOverlay ? "yes" : "no") << "\n";
    const TelemetryInitBenchTotals totals = RunTelemetryInitBenchmark(settings, iterations, trace);
    if (!totals.succeeded) {
        std::cerr << totals.errorText << "\n";
        return 1;
    }

    PrintTelemetryInitBenchResult(totals);
    std::cout << std::left << std::setw(14) << "init_result" << " revision=" << totals.lastRevision << " gpu_adapter=\""
              << totals.lastResolvedSelections.gpuAdapterName << "\" network_adapter=\""
              << totals.lastResolvedSelections.adapterName
              << "\" drives=" << totals.lastResolvedSelections.drives.size()
              << " gpu_candidates=" << totals.lastGpuAdapterCandidates
              << " network_candidates=" << totals.lastNetworkAdapterCandidates
              << " storage_candidates=" << totals.lastStorageDriveCandidates << "\n";
    return 0;
}

int RunBenchmarkCommand(const BenchmarkCommandLine& commandLine, Trace& trace) {
    switch (commandLine.benchmark) {
        case Benchmark::Animation:
            return RunAnimationBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::EditLayout:
            return RunEditLayoutBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LayoutGuideSheet:
            return RunLayoutGuideSheetBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LayoutSwitch:
            return RunLayoutSwitchBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LenovoGameZone:
            return RunLenovoGameZoneBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LenovoLdePhases:
            return RunLenovoLdePhaseBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::LenovoHardwareScan:
            return RunLenovoHardwareScanBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::MouseHover:
            return RunMouseHoverBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::SnapshotHandoff:
            return RunSnapshotHandoffBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::TelemetryInit:
            return RunTelemetryInitBenchmarkCommand(
                commandLine.iterations, commandLine.renderScale, commandLine.configPath, trace);
        case Benchmark::TemperatureSources:
            return RunTemperatureSourcesBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::ThemeChange:
            return RunThemeChangeBenchmarkCommand(commandLine.iterations, commandLine.renderScale, trace);
        case Benchmark::UpdateTelemetry:
            return RunUpdateTelemetryBenchmarkCommand(
                commandLine.iterations, commandLine.renderScale, commandLine.configPath, trace);
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
