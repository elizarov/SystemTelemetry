#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <windows.h>

#include "config.h"
#include "dashboard_metrics.h"

namespace Gdiplus {
class Bitmap;
}

class DashboardRenderer {
public:
    DashboardRenderer();
    ~DashboardRenderer();

    void SetConfig(const AppConfig& config);
    void SetRenderScale(double scale);
    double RenderScale() const;
    int WindowWidth() const;
    int WindowHeight() const;

    COLORREF BackgroundColor() const;
    COLORREF ForegroundColor() const;
    COLORREF AccentColor() const;
    COLORREF MutedTextColor() const;
    HFONT LabelFont() const;
    HFONT SmallFont() const;
    void SetTraceOutput(std::ostream* traceOutput);

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    void Draw(HDC hdc, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    const std::string& LastError() const;

private:
    struct WidgetBinding {
        std::string metric;
        std::string param;
    };

    enum class WidgetKind {
        Text,
        Gauge,
        MetricList,
        Throughput,
        NetworkFooter,
        Spacer,
        DriveUsageList,
        ClockTime,
        ClockDate,
        Unknown,
    };

    struct ResolvedWidgetLayout {
        WidgetKind kind = WidgetKind::Unknown;
        RECT rect{};
        WidgetBinding binding;
    };

    struct ResolvedCardLayout {
        std::string id;
        std::string title;
        std::string iconName;
        RECT rect{};
        RECT titleRect{};
        RECT iconRect{};
        RECT contentRect{};
        std::vector<ResolvedWidgetLayout> widgets;
    };

    struct ResolvedDashboardLayout {
        int windowWidth = 800;
        int windowHeight = 480;
        std::vector<ResolvedCardLayout> cards;
    };

    struct FontHeights {
        int title = 0;
        int big = 0;
        int value = 0;
        int label = 0;
        int smallText = 0;
    };

    struct MeasuredWidths {
        int throughputLabel = 0;
        int throughputAxis = 0;
        int driveLabel = 0;
        int drivePercent = 0;
    };

    struct Fonts {
        HFONT title = nullptr;
        HFONT big = nullptr;
        HFONT value = nullptr;
        HFONT label = nullptr;
        HFONT smallFont = nullptr;
    };

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format);
    void DrawPanel(HDC hdc, const ResolvedCardLayout& card);
    void DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect);
    void DrawResolvedWidget(HDC hdc, const ResolvedWidgetLayout& widget, const DashboardMetricSource& metrics);
    void DrawPillBar(HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio);
    void DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label);
    void DrawMetricRow(HDC hdc, const RECT& rect, const DashboardMetricRow& row);
    void DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue, double guideStepMbps,
        double timeMarkerOffsetSamples, double timeMarkerIntervalSamples);
    void DrawThroughputWidget(HDC hdc, const RECT& rect, const DashboardThroughputMetric& metric);
    void DrawDriveUsageWidget(HDC hdc, const RECT& rect, const std::vector<DashboardDriveRow>& rows);

    bool InitializeGdiplus();
    void ShutdownGdiplus();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    bool MeasureFonts();
    bool ResolveLayout();
    void ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets);
    int PreferredNodeHeight(const LayoutNodeConfig& node, int width) const;
    int EffectiveHeaderHeight() const;
    int EffectiveMetricRowHeight() const;
    int EffectiveDriveRowHeight() const;
    int ScaleLogical(int value) const;
    void WriteTrace(const std::string& text) const;

    AppConfig config_;
    HWND hwnd_ = nullptr;
    std::ostream* traceOutput_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<std::pair<std::string, std::unique_ptr<Gdiplus::Bitmap>>> panelIcons_;
    Fonts fonts_{};
    FontHeights fontHeights_{};
    MeasuredWidths measuredWidths_{}; 
    ResolvedDashboardLayout resolvedLayout_{};
    std::string lastError_;
    double renderScale_ = 1.0;
};
