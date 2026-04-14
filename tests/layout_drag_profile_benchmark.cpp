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

#include <windows.h>

#include "config_parser.h"
#include "dashboard_metrics.h"
#include "dashboard_renderer.h"
#include "layout_edit_controller.h"
#include "layout_edit_service.h"
#include "layout_edit_trace_session.h"

namespace {

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct PhaseStats {
    std::chrono::nanoseconds total{};
    size_t samples = 0;
};

struct BenchResult {
    Duration total{};
    Duration perIteration{};
};

struct PaintBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HBITMAP previousBitmap = nullptr;
    int width = 0;
    int height = 0;

    void Reset() {
        if (dc != nullptr && previousBitmap != nullptr) {
            SelectObject(dc, previousBitmap);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (dc != nullptr) {
            DeleteDC(dc);
        }
        dc = nullptr;
        bitmap = nullptr;
        previousBitmap = nullptr;
        width = 0;
        height = 0;
    }

    ~PaintBuffer() {
        Reset();
    }

    bool Ensure(HDC compatibleWith, int targetWidth, int targetHeight) {
        if (dc != nullptr && width == targetWidth && height == targetHeight) {
            return true;
        }

        Reset();
        dc = CreateCompatibleDC(compatibleWith);
        if (dc == nullptr) {
            return false;
        }
        bitmap = CreateCompatibleBitmap(compatibleWith, targetWidth, targetHeight);
        if (bitmap == nullptr) {
            Reset();
            return false;
        }
        previousBitmap = static_cast<HBITMAP>(SelectObject(dc, bitmap));
        width = targetWidth;
        height = targetHeight;
        return true;
    }
};

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
}

double DurationMilliseconds(std::chrono::nanoseconds value) {
    return std::chrono::duration<double, std::milli>(value).count();
}

size_t PhaseIndex(LayoutEditHost::TracePhase phase) {
    switch (phase) {
        case LayoutEditHost::TracePhase::Snap:
            return 0;
        case LayoutEditHost::TracePhase::Apply:
            return 1;
        case LayoutEditHost::TracePhase::PaintTotal:
            return 2;
        case LayoutEditHost::TracePhase::PaintDraw:
            return 3;
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

SystemSnapshot BuildSyntheticSnapshot(const AppConfig& config) {
    SystemSnapshot snapshot;
    snapshot.cpu.name = "AMD Ryzen 7";
    snapshot.cpu.loadPercent = 63.0;
    snapshot.cpu.clock = ScalarMetric{4.85, "GHz"};
    snapshot.cpu.memory = MemoryMetric{18.5, 32.0};
    snapshot.gpu.name = "Radeon RX";
    snapshot.gpu.loadPercent = 58.0;
    snapshot.gpu.temperature = ScalarMetric{67.0, "C"};
    snapshot.gpu.clock = ScalarMetric{2480.0, "MHz"};
    snapshot.gpu.fan = ScalarMetric{1580.0, "RPM"};
    snapshot.gpu.vram = MemoryMetric{8.4, 16.0};
    snapshot.network.adapterName = "Ethernet";
    snapshot.network.ipAddress = "192.168.1.20";
    snapshot.network.uploadMbps = 78.0;
    snapshot.network.downloadMbps = 312.0;
    snapshot.storage.readMbps = 420.0;
    snapshot.storage.writeMbps = 155.0;
    GetLocalTime(&snapshot.now);

    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{74.0, "C"}});
    snapshot.boardTemperatures.push_back({"vrm", ScalarMetric{61.0, "C"}});
    snapshot.boardFans.push_back({"system", ScalarMetric{890.0, "RPM"}});

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

std::optional<DashboardRenderer::LayoutEditGuide> FindTopLevelGuide(const DashboardRenderer& renderer) {
    const auto& guides = renderer.LayoutEditGuides();
    const auto it = std::find_if(guides.begin(), guides.end(), [](const auto& guide) {
        return guide.editCardId.empty() && guide.nodePath.size() <= 1 && guide.childExtents.size() >= 2;
    });
    return it != guides.end() ? std::optional<DashboardRenderer::LayoutEditGuide>(*it) : std::nullopt;
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

POINT GuideDragStartPoint(const DashboardRenderer::LayoutEditGuide& guide) {
    return POINT{(guide.hitRect.left + guide.hitRect.right) / 2, (guide.hitRect.top + guide.hitRect.bottom) / 2};
}

POINT DragPointForWeights(const DashboardRenderer::LayoutEditGuide& guide,
    const std::vector<int>& initialWeights,
    const std::vector<int>& targetWeights) {
    POINT point = GuideDragStartPoint(guide);
    if (guide.separatorIndex < initialWeights.size() && guide.separatorIndex < targetWeights.size()) {
        const int delta = targetWeights[guide.separatorIndex] - initialWeights[guide.separatorIndex];
        if (guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical) {
            point.x += delta;
        } else {
            point.y += delta;
        }
    }
    return point;
}

class BenchmarkDragHost : private LayoutEditHost {
public:
    BenchmarkDragHost(const AppConfig& config, const SystemSnapshot& snapshot, double renderScale)
        : config_(config), snapshot_(snapshot), renderScale_(renderScale), layoutEditController_(*this) {
        renderer_.SetConfig(config_);
        renderer_.SetRenderScale(renderScale_);
        overlayState_.showLayoutEditGuides = true;
    }

    ~BenchmarkDragHost() {
        paintBuffer_.Reset();
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
        renderer_.Shutdown();
    }

    bool Initialize() {
        if (!renderer_.Initialize()) {
            return false;
        }
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
        return hwnd_ != nullptr;
    }

    LayoutEditController& Controller() {
        return layoutEditController_;
    }

    DashboardRenderer& LayoutRenderer() {
        return renderer_;
    }

    const std::array<PhaseStats, 4>& PhaseTotals() const {
        return phaseTotals_;
    }

    void FlushPaintIfDirty() {
        if (!dirty_) {
            return;
        }

        dirty_ = false;
        const auto paintStart = Clock::now();
        HDC hdc = GetDC(hwnd_);
        RECT client{0, 0, renderer_.WindowWidth(), renderer_.WindowHeight()};
        if (hdc == nullptr || !paintBuffer_.Ensure(hdc, client.right, client.bottom)) {
            if (hdc != nullptr) {
                ReleaseDC(hwnd_, hdc);
            }
            return;
        }
        HBRUSH background = CreateSolidBrush(renderer_.BackgroundColor());
        FillRect(paintBuffer_.dc, &client, background);
        DeleteObject(background);
        SetBkMode(paintBuffer_.dc, TRANSPARENT);

        const auto drawStart = Clock::now();
        renderer_.Draw(paintBuffer_.dc, snapshot_, overlayState_);
        const auto drawEnd = Clock::now();

        BitBlt(hdc, 0, 0, client.right, client.bottom, paintBuffer_.dc, 0, 0, SRCCOPY);
        ReleaseDC(hwnd_, hdc);

        const auto paintEnd = Clock::now();
        RecordLayoutEditTracePhase(TracePhase::PaintDraw, drawEnd - drawStart);
        RecordLayoutEditTracePhase(TracePhase::PaintTotal, paintEnd - paintStart);
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
        const bool applied = layout_edit::ApplyGuideWeights(config_, target, weights);
        if (applied) {
            renderer_.SetConfig(config_);
            dirty_ = true;
        }
        RecordLayoutEditTracePhase(TracePhase::Apply, Clock::now() - start);
        return applied;
    }

    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutTarget& target,
        const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget,
        DashboardRenderer::LayoutGuideAxis axis) override {
        return layout_edit::EvaluateWidgetExtentForGuideWeights(renderer_, config_, target, weights, widget, axis);
    }

    bool ApplyLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter, double value) override {
        const auto start = Clock::now();
        const bool applied = layout_edit::ApplyValue(config_, parameter, value);
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
        PhaseStats& stats = phaseTotals_[PhaseIndex(phase)];
        stats.total += elapsed;
        ++stats.samples;
    }

    void EndLayoutEditTraceSession(const std::string& reason) override {
        traceSession_.End(&traceStream_, reason);
    }

    HWND hwnd_ = nullptr;
    AppConfig config_{};
    DashboardRenderer renderer_{};
    DashboardRenderer::EditOverlayState overlayState_{};
    const SystemSnapshot& snapshot_;
    double renderScale_ = 1.0;
    bool dirty_ = false;
    PaintBuffer paintBuffer_{};
    LayoutEditController layoutEditController_;
    LayoutEditTraceSession traceSession_{};
    std::ostringstream traceStream_{};
    std::array<PhaseStats, 4> phaseTotals_{};
};

BenchResult RunDragBenchmark(BenchmarkDragHost& host,
    const DashboardRenderer::LayoutEditGuide& guide,
    const std::vector<int>& initialWeights,
    const std::vector<std::vector<int>>& weightSequence) {
    LayoutEditController& controller = host.Controller();
    controller.StartSession();

    const POINT startPoint = GuideDragStartPoint(guide);
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
    size_t iterations = 240;
    double renderScale = 2.0;
    if (argc >= 2) {
        const long long parsed = std::atoll(argv[1]);
        if (parsed > 0) {
            iterations = static_cast<size_t>(parsed);
        }
    }
    if (argc >= 3) {
        const double parsed = std::atof(argv[2]);
        if (std::isfinite(parsed) && parsed > 0.0) {
            renderScale = parsed;
        }
    }

    const AppConfig config = LoadConfig(SourceConfigPath(), false);
    const SystemSnapshot snapshot = BuildSyntheticSnapshot(config);
    BenchmarkDragHost host(config, snapshot, renderScale);
    if (!host.Initialize()) {
        std::cerr << "renderer init failed: " << host.LayoutRenderer().LastError() << "\n";
        return 1;
    }

    const std::optional<DashboardRenderer::LayoutEditGuide> guide = FindTopLevelGuide(host.LayoutRenderer());
    if (!guide.has_value()) {
        std::cerr << "no top-level layout guide found\n";
        return 1;
    }

    const LayoutEditHost::LayoutTarget target = LayoutEditHost::LayoutTarget::ForGuide(*guide);
    const LayoutNodeConfig* node = layout_edit::FindGuideNode(config, target);
    const std::vector<int> initialWeights = layout_edit::SeedGuideWeights(*guide, node);
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
    PrintPhaseResult(PhaseName(LayoutEditHost::TracePhase::Snap), phases[PhaseIndex(LayoutEditHost::TracePhase::Snap)]);
    PrintPhaseResult(
        PhaseName(LayoutEditHost::TracePhase::Apply), phases[PhaseIndex(LayoutEditHost::TracePhase::Apply)]);
    PrintPhaseResult(
        PhaseName(LayoutEditHost::TracePhase::PaintTotal), phases[PhaseIndex(LayoutEditHost::TracePhase::PaintTotal)]);
    PrintPhaseResult(
        PhaseName(LayoutEditHost::TracePhase::PaintDraw), phases[PhaseIndex(LayoutEditHost::TracePhase::PaintDraw)]);
    return 0;
}
