#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "config_parser.h"
#include "dashboard_metrics.h"
#include "dashboard_renderer.h"
#include "layout_edit_service.h"

namespace {

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct BenchResult {
    std::string name;
    Duration total{};
    Duration perIteration{};
};

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
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
        series.samples.reserve(120);
        for (int i = 0; i < 120; ++i) {
            const double phase = static_cast<double>(i) / period;
            const double wave = std::sin(phase) * amplitude;
            series.samples.push_back((std::max)(0.0, base + wave));
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

    const std::vector<std::string>& drives = config.storage.drives;
    for (size_t i = 0; i < drives.size(); ++i) {
        DriveInfo drive;
        drive.label = drives[i] + ":";
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
    if (it == guides.end()) {
        return std::nullopt;
    }
    return *it;
}

std::vector<std::vector<int>> BuildWeightSequence(const std::vector<int>& seedWeights, size_t iterations) {
    std::vector<std::vector<int>> sequence;
    if (seedWeights.size() < 2 || iterations == 0) {
        return sequence;
    }

    std::vector<int> current = seedWeights;
    sequence.reserve(iterations);
    size_t sourceIndex = 0;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const size_t left = sourceIndex % (current.size() - 1);
        const size_t right = left + 1;
        if (current[right] > 1) {
            ++current[left];
            --current[right];
        } else if (current[left] > 1) {
            --current[left];
            ++current[right];
        }
        sequence.push_back(current);
        sourceIndex = (sourceIndex + 1) % (current.size() - 1);
    }
    return sequence;
}

HBITMAP CreateBenchmarkBitmap(HDC referenceDc, int width, int height, void** pixels) {
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(referenceDc, &bitmapInfo, DIB_RGB_COLORS, pixels, nullptr, 0);
}

BenchResult TimeRelayout(DashboardRenderer& renderer,
    AppConfig config,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<std::vector<int>>& weightSequence) {
    renderer.SetLayoutGuideDragActive(true);
    const auto start = Clock::now();
    for (const auto& weights : weightSequence) {
        layout_edit::ApplyGuideWeights(config, target, weights);
        renderer.SetConfig(config);
    }
    const Duration total = Clock::now() - start;
    renderer.SetLayoutGuideDragActive(false);
    renderer.RebuildEditArtifacts();
    return {"relayout", total, total / static_cast<double>(weightSequence.size())};
}

BenchResult TimeDraw(DashboardRenderer& renderer,
    const SystemSnapshot& snapshot,
    const DashboardRenderer::EditOverlayState& overlayState,
    size_t iterations) {
    HDC screenDc = GetDC(nullptr);
    HDC memDc = screenDc != nullptr ? CreateCompatibleDC(screenDc) : nullptr;
    void* pixels = nullptr;
    HBITMAP bitmap = memDc != nullptr
                         ? CreateBenchmarkBitmap(screenDc, renderer.WindowWidth(), renderer.WindowHeight(), &pixels)
                         : nullptr;
    HGDIOBJ oldBitmap = bitmap != nullptr ? SelectObject(memDc, bitmap) : nullptr;

    const auto start = Clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        RECT client{0, 0, renderer.WindowWidth(), renderer.WindowHeight()};
        HBRUSH background = CreateSolidBrush(renderer.BackgroundColor());
        FillRect(memDc, &client, background);
        DeleteObject(background);
        renderer.Draw(memDc, snapshot, overlayState);
    }
    const Duration total = Clock::now() - start;

    if (oldBitmap != nullptr) {
        SelectObject(memDc, oldBitmap);
    }
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    return {"draw", total, total / static_cast<double>(iterations)};
}

BenchResult TimeRelayoutAndDraw(DashboardRenderer& renderer,
    AppConfig config,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<std::vector<int>>& weightSequence,
    const SystemSnapshot& snapshot,
    DashboardRenderer::EditOverlayState overlayState) {
    HDC screenDc = GetDC(nullptr);
    HDC memDc = screenDc != nullptr ? CreateCompatibleDC(screenDc) : nullptr;
    void* pixels = nullptr;
    HBITMAP bitmap = memDc != nullptr
                         ? CreateBenchmarkBitmap(screenDc, renderer.WindowWidth(), renderer.WindowHeight(), &pixels)
                         : nullptr;
    HGDIOBJ oldBitmap = bitmap != nullptr ? SelectObject(memDc, bitmap) : nullptr;

    renderer.SetLayoutGuideDragActive(true);
    const auto start = Clock::now();
    for (const auto& weights : weightSequence) {
        layout_edit::ApplyGuideWeights(config, target, weights);
        renderer.SetConfig(config);
        RECT client{0, 0, renderer.WindowWidth(), renderer.WindowHeight()};
        HBRUSH background = CreateSolidBrush(renderer.BackgroundColor());
        FillRect(memDc, &client, background);
        DeleteObject(background);
        renderer.Draw(memDc, snapshot, overlayState);
    }
    const Duration total = Clock::now() - start;
    renderer.SetLayoutGuideDragActive(false);
    renderer.RebuildEditArtifacts();

    if (oldBitmap != nullptr) {
        SelectObject(memDc, oldBitmap);
    }
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    return {"relayout+draw", total, total / static_cast<double>(weightSequence.size())};
}

std::optional<BenchResult> TimeSnapEvaluation(DashboardRenderer& renderer,
    const AppConfig& config,
    const DashboardRenderer::LayoutEditGuide& guide,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<std::vector<int>>& weightSequence) {
    const auto candidates = renderer.CollectLayoutGuideSnapCandidates(guide);
    if (candidates.empty()) {
        return std::nullopt;
    }

    renderer.SetLayoutGuideDragActive(true);
    const auto start = Clock::now();
    size_t completed = 0;
    for (const auto& weights : weightSequence) {
        const auto extent = layout_edit::EvaluateWidgetExtentForGuideWeights(
            renderer, config, target, weights, candidates.front().widget, guide.axis);
        if (!extent.has_value()) {
            break;
        }
        ++completed;
    }
    if (completed == 0) {
        renderer.SetLayoutGuideDragActive(false);
        renderer.RebuildEditArtifacts();
        return std::nullopt;
    }

    const Duration total = Clock::now() - start;
    renderer.SetLayoutGuideDragActive(false);
    renderer.RebuildEditArtifacts();
    return BenchResult{"snap_eval", total, total / static_cast<double>(completed)};
}

void PrintBenchResult(const BenchResult& result) {
    std::cout << std::left << std::setw(14) << result.name << " total_ms=" << std::fixed << std::setprecision(2)
              << result.total.count() << " per_iter_ms=" << result.perIteration.count() << "\n";
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
    SystemSnapshot snapshot = BuildSyntheticSnapshot(config);

    DashboardRenderer renderer;
    renderer.SetConfig(config);
    renderer.SetRenderScale(renderScale);
    if (!renderer.Initialize()) {
        std::cerr << "renderer init failed: " << renderer.LastError() << "\n";
        return 1;
    }

    const std::optional<DashboardRenderer::LayoutEditGuide> guide = FindTopLevelGuide(renderer);
    if (!guide.has_value()) {
        std::cerr << "no top-level layout guide found\n";
        return 1;
    }

    const LayoutEditHost::LayoutTarget target = LayoutEditHost::LayoutTarget::ForGuide(*guide);
    const LayoutNodeConfig* node = layout_edit::FindGuideNode(config, target);
    const std::vector<int> seedWeights = layout_edit::SeedGuideWeights(*guide, node);
    const std::vector<std::vector<int>> weightSequence = BuildWeightSequence(seedWeights, iterations);
    if (weightSequence.empty()) {
        std::cerr << "weight sequence generation failed\n";
        return 1;
    }

    DashboardRenderer::EditOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;
    overlayState.activeLayoutEditGuide = *guide;

    std::cout << "layout_edit_benchmark guide_children=" << seedWeights.size()
              << " separator_index=" << guide->separatorIndex << " iterations=" << weightSequence.size()
              << " render_scale=" << renderScale << "\n";

    const BenchResult relayout = TimeRelayout(renderer, config, target, weightSequence);
    PrintBenchResult(relayout);

    renderer.SetConfig(config);
    const BenchResult draw = TimeDraw(renderer, snapshot, overlayState, weightSequence.size());
    PrintBenchResult(draw);

    renderer.SetConfig(config);
    const BenchResult combined = TimeRelayoutAndDraw(renderer, config, target, weightSequence, snapshot, overlayState);
    PrintBenchResult(combined);

    renderer.SetConfig(config);
    if (const auto snapEval = TimeSnapEvaluation(renderer, config, *guide, target, weightSequence);
        snapEval.has_value()) {
        PrintBenchResult(*snapEval);
    }

    renderer.Shutdown();
    return 0;
}
