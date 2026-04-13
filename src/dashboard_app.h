#pragma once

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "../resources/resource.h"
#include "app_autostart.h"
#include "app_command_line.h"
#include "app_constants.h"
#include "app_config_io.h"
#include "app_display_config.h"
#include "app_monitor.h"
#include "app_paths.h"
#include "app_strings.h"
#include "app_win32_ui.h"
#include "dashboard_controller.h"
#include "diagnostics_options.h"
#include "layout_edit_controller.h"

class DashboardApp : private LayoutEditHost, public DashboardShellHost {
public:
    explicit DashboardApp(const DiagnosticsOptions& diagnosticsOptions = {});
    bool Initialize(HINSTANCE instance);
    int Run();
    bool InitializeFonts() override;
    void SetRenderConfig(const AppConfig& config);
    void ReleaseFonts() override;
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool WriteDiagnosticsOutputs();
    HWND WindowHandle() const override;
    DashboardRenderer& Renderer() override;
    const DashboardRenderer& Renderer() const override;
    DashboardRenderer::EditOverlayState& RendererEditOverlayState() override;
    const DashboardRenderer::EditOverlayState& RendererEditOverlayState() const override;
    UINT CurrentWindowDpi() const override;
    double CurrentRenderScale() const override;
    void ApplyConfigPlacement() override;
    void InvalidateShell() override;
    MonitorPlacementInfo GetWindowPlacementInfo() const override;
    std::optional<std::filesystem::path> PromptDiagnosticsSavePath(
        const wchar_t* defaultFileName, const wchar_t* filter, const wchar_t* defaultExtension) const override;
    void ShowError(const std::wstring& message) const override;

private:
    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void ShowContextMenu(POINT screenPoint);
    void BringOnTop();
    bool ApplyConfiguredWallpaper();
    bool ApplyWindowDpi(UINT dpi, const RECT* suggestedRect = nullptr);
    void UpdateRendererScale(double scale);
    double ResolveCurrentDisplayScale(UINT dpi) const;
    std::optional<double> PromptCustomScale() const;
    bool IsLayoutEditMode() const;
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditHost::LayoutTarget& target,
        const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget,
        DashboardRenderer::LayoutGuideAxis axis);
    void StartMoveMode();
    void StopMoveMode();
    void UpdateMoveTracking();
    void DrawMoveOverlay(HDC hdc);
    bool CreateLayoutEditTooltip();
    void DestroyLayoutEditTooltip();
    void HideLayoutEditTooltip();
    void UpdateLayoutEditTooltip();
    void UpdateLayoutEditMouseTracking();
    void RelayLayoutEditTooltipMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateTrayIcon();
    void RemoveTrayIcon();
    HICON LoadAppIcon(int width, int height);
    COLORREF BackgroundColor() const;
    COLORREF ForegroundColor() const;
    COLORREF AccentColor() const;
    COLORREF MutedTextColor() const;
    int WindowWidth() const;
    int WindowHeight() const;
    void StartPlacementWatch();
    void StopPlacementWatch();
    void RetryConfigPlacementIfPending();

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);

    const AppConfig& LayoutEditConfig() const override;
    DashboardRenderer& LayoutEditRenderer() override;
    DashboardRenderer::EditOverlayState& LayoutEditOverlayState() override;
    bool ApplyLayoutGuideWeights(const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) override;
    bool ApplyLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter, double value) override;
    void InvalidateLayoutEdit() override;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    DashboardRenderer renderer_;
    DashboardRenderer::EditOverlayState rendererEditOverlayState_{};
    DashboardController controller_{};
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
    DiagnosticsOptions diagnosticsOptions_;
    UINT currentDpi_ = kDefaultDpi;
    LayoutEditController layoutEditController_;
    HWND layoutEditTooltipHwnd_ = nullptr;
    std::wstring layoutEditTooltipText_;
    bool layoutEditTooltipVisible_ = false;
    bool layoutEditMouseTracking_ = false;
    RECT layoutEditTooltipRect_{};
    bool layoutEditTooltipRectValid_ = false;
};
