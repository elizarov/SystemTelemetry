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
#include "app_constants.h"
#include "app_diagnostics.h"
#include "app_platform.h"
#include "dashboard_controller.h"
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
    void SaveDumpAs();
    void SaveScreenshotAs();
    void SaveFullConfigAs();
    bool IsAutoStartEnabled() const;
    void ToggleAutoStart();
    HWND WindowHandle() const override;
    DashboardRenderer& Renderer() override;
    const DashboardRenderer& Renderer() const override;
    DashboardRenderer::EditOverlayState& RendererEditOverlayState() override;
    const DashboardRenderer::EditOverlayState& RendererEditOverlayState() const override;
    UINT CurrentWindowDpi() const override;
    void ApplyConfigPlacement() override;
    void InvalidateShell() override;
    MonitorPlacementInfo GetWindowPlacementInfo() const override;
    std::optional<std::filesystem::path> PromptDiagnosticsSavePath(
        const wchar_t* defaultFileName,
        const wchar_t* filter,
        const wchar_t* defaultExtension) const override;
    void ShowError(const std::wstring& message) const override;

private:
    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void ShowContextMenu(POINT screenPoint);
    void BringOnTop();
    bool ReloadConfigFromDisk();
    AppConfig BuildCurrentConfigForSaving() const;
    void UpdateConfigFromCurrentPlacement();
    bool ApplyConfiguredWallpaper();
    bool ConfigureDisplay(const DisplayMenuOption& option);
    bool SwitchLayout(const std::string& layoutName);
    void SelectNetworkAdapter(const NetworkMenuOption& option);
    void ToggleStorageDrive(const StorageDriveMenuOption& option);
    bool ApplyWindowDpi(UINT dpi, const RECT* suggestedRect = nullptr);
    void UpdateRendererScale(double scale);
    bool IsLayoutEditMode() const;
    void StartLayoutEditMode();
    void StopLayoutEditMode();
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditHost::LayoutTarget& target,
        const std::vector<int>& weights, const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis);
    bool ApplyLayoutGuideWeights(const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights);
    void StartMoveMode();
    void StopMoveMode();
    void UpdateMoveTracking();
    void DrawMoveOverlay(HDC hdc);
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

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
        COLORREF color, UINT format);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);

    const AppConfig& LayoutEditConfig() const override;
    DashboardRenderer& LayoutEditRenderer() override;
    DashboardRenderer::EditOverlayState& LayoutEditOverlayState() override;
    bool ApplyLayoutEditValue(const LayoutEditHost::ValueTarget& target, double value) override;
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
};
