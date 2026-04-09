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
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "../resources/resource.h"
#include "config.h"
#include "dashboard_renderer.h"
#include "layout_snap_solver.h"
#include "snapshot_dump.h"
#include "telemetry.h"
#include "telemetry_runtime.h"
#include "trace.h"
#include "utf8.h"

inline constexpr UINT_PTR kRefreshTimerId = 1;
inline constexpr UINT_PTR kMoveTimerId = 2;
inline constexpr UINT_PTR kPlacementTimerId = 3;
inline constexpr UINT kRefreshTimerMs = 500;
inline constexpr UINT kMoveTimerMs = 16;
inline constexpr UINT kPlacementTimerMs = 2000;
inline constexpr UINT kTrayMessage = WM_APP + 1;
inline constexpr UINT kCommandMove = 1001;
inline constexpr UINT kCommandBringOnTop = 1002;
inline constexpr UINT kCommandReloadConfig = 1003;
inline constexpr UINT kCommandSaveConfig = 1004;
inline constexpr UINT kCommandExit = 1005;
inline constexpr UINT kCommandSaveDumpAs = 1006;
inline constexpr UINT kCommandSaveScreenshotAs = 1007;
inline constexpr UINT kCommandAutoStart = 1008;
inline constexpr UINT kCommandSaveFullConfigAs = 1009;
inline constexpr UINT kCommandEditLayout = 1010;
inline constexpr UINT kCommandLayoutBase = 1100;
inline constexpr UINT kCommandLayoutMax = 1199;
inline constexpr UINT kCommandNetworkAdapterBase = 1200;
inline constexpr UINT kCommandNetworkAdapterMax = 1299;
inline constexpr UINT kCommandStorageDriveBase = 1300;
inline constexpr UINT kCommandStorageDriveMax = 1399;
inline constexpr UINT kCommandConfigureDisplayBase = 2000;
inline constexpr UINT kCommandConfigureDisplayMax = 2099;
inline constexpr wchar_t kWindowClassName[] = L"SystemTelemetryDashboard";
inline constexpr wchar_t kDefaultTraceFileName[] = L"telemetry_trace.txt";
inline constexpr wchar_t kDefaultDumpFileName[] = L"telemetry_dump.txt";
inline constexpr wchar_t kDefaultScreenshotFileName[] = L"telemetry_screenshot.png";
inline constexpr wchar_t kDefaultSavedConfigFileName[] = L"telemetry_config.ini";
inline constexpr wchar_t kDefaultSavedFullConfigFileName[] = L"telemetry_full_config.ini";
inline constexpr wchar_t kDefaultBlankWallpaperFileName[] = L"telemetry_blank.png";
inline constexpr wchar_t kAutoStartRunSubKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kAutoStartValueName[] = L"SystemTelemetry";
inline constexpr UINT kDefaultDpi = USER_DEFAULT_SCREEN_DPI;

COLORREF ToColorRef(unsigned int color);
std::string ToLower(std::string value);
std::string Trim(std::string value);
SIZE MeasureTextSize(HDC hdc, HFONT font, const std::string& text);
int MeasureFontHeight(HDC hdc, HFONT font);
int MeasureWrappedTextHeight(HDC hdc, HFONT font, const std::string& text, int width);
bool ContainsInsensitive(const std::string& value, const std::string& needle);
bool EqualsInsensitive(const std::string& left, const std::string& right);
bool EqualsInsensitive(const std::wstring& left, const std::wstring& right);
std::string JoinNames(const std::vector<std::string>& names);
bool RectsEqual(const RECT& lhs, const RECT& rhs);
void WriteOptionalTrace(std::ostream* traceStream, const std::string& text);
std::string FormatHresult(HRESULT value);
std::filesystem::path GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options);
bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const AppConfig& config,
    double scale, DashboardRenderer::RenderMode renderMode, bool showLayoutEditGuides,
    DashboardRenderer::SimilarityIndicatorMode similarityIndicatorMode, const std::string& editLayoutWidgetName,
    std::ostream* traceStream = nullptr,
    std::string* errorText = nullptr);
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);

DiagnosticsOptions GetDiagnosticsOptions();
bool ValidateDiagnosticsOptions(const DiagnosticsOptions& options);
DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options);
DashboardRenderer::SimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options);
bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream);
int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath, const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath, const std::filesystem::path& targetImagePath);
int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath);
int RunElevatedAutoStartMode(bool enabled);
int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions);

class DiagnosticsSession {
public:
    explicit DiagnosticsSession(const DiagnosticsOptions& options);

    bool Initialize();
    bool ShouldShowDialogs() const;
    std::ostream* TraceStream();
    void WriteTraceMarker(const std::string& text);
    bool WriteOutputs(const TelemetryDump& dump, const AppConfig& config);

private:
    void ReportError(const std::string& traceText, const std::wstring& message);
    void ShowFileOpenError(const char* label, const std::filesystem::path& path);

    DiagnosticsOptions options_;
    std::filesystem::path tracePath_;
    std::filesystem::path dumpPath_;
    std::filesystem::path screenshotPath_;
    std::filesystem::path saveConfigPath_;
    std::filesystem::path saveFullConfigPath_;
    std::ofstream traceStream_;
};

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetWorkingDirectory();
std::filesystem::path ResolveExecutableRelativePath(const std::filesystem::path& configuredPath);
std::optional<std::wstring> GetExecutablePath();
std::wstring TrimWhitespace(std::wstring value);
std::wstring StripOuterQuotes(std::wstring value);
std::wstring NormalizeWindowsPath(std::wstring value);
std::wstring QuoteCommandLineArgument(const std::wstring& value);
std::optional<std::wstring> ReadAutoStartCommand();
bool IsAutoStartEnabledForCurrentExecutable();
LSTATUS WriteAutoStartRegistryValue(bool enabled);
bool UpdateAutoStartElevated(bool enabled, HWND owner);
bool UpdateAutoStartRegistration(bool enabled, HWND owner);

std::filesystem::path ResolveDiagnosticsOutputPath(
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredPath,
    const wchar_t* defaultFileName);
std::optional<std::filesystem::path> PromptSavePath(
    HWND owner,
    const std::filesystem::path& initialDirectory,
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension);

std::vector<std::wstring> GetCommandLineArguments();
bool HasSwitch(const std::string& target);
std::optional<std::wstring> GetSwitchValue(const std::wstring& target);
std::optional<std::wstring> GetColonSwitchValue(const std::wstring& target);
std::optional<double> TryParseScaleValue(const std::wstring& text);
std::optional<double> GetScaleSwitchValue();
std::optional<std::string> GetLayoutSwitchValue();
bool ApplyDiagnosticsLayoutOverride(AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics = nullptr);

bool CanWriteRuntimeConfig(const std::filesystem::path& path);
std::filesystem::path CreateTempFilePath(const wchar_t* prefix);
std::filesystem::path CreateElevatedSaveConfigTempPath();

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(
    const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    std::ostream* traceStream);
bool ReloadTelemetryRuntimeFromDisk(
    const std::filesystem::path& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    DiagnosticsSession* diagnostics);

std::string FormatSpeed(double mbps);
double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory);
UINT GetMonitorDpi(HMONITOR monitor);
double ScaleFromDpi(UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);

struct MonitorPlacementInfo {
    std::string deviceName;
    std::string monitorName = "Unknown";
    std::string configMonitorName;
    RECT monitorRect{};
    POINT relativePosition{};
    UINT dpi = kDefaultDpi;
};

struct MonitorIdentity {
    std::string displayName;
    std::string configName;
};

struct TargetMonitorInfo {
    RECT rect{};
    UINT dpi = kDefaultDpi;
};

struct DisplayMenuOption {
    UINT commandId = 0;
    std::string displayName;
    std::string configMonitorName;
    RECT rect{};
    UINT dpi = kDefaultDpi;
    bool layoutFits = false;
};

struct LayoutMenuOption {
    UINT commandId = 0;
    std::string name;
};

struct NetworkMenuOption {
    UINT commandId = 0;
    std::string adapterName;
    std::string ipAddress;
    bool selected = false;
};

struct StorageDriveMenuOption {
    UINT commandId = 0;
    std::string driveLetter;
    std::string volumeLabel;
    double totalGb = 0.0;
    bool selected = false;
};

struct LayoutDragState {
    DashboardRenderer::LayoutEditGuide guide;
    std::vector<int> initialWeights;
    std::vector<DashboardRenderer::LayoutGuideSnapCandidate> snapCandidates;
    int dragStartCoordinate = 0;
};

struct WidgetEditDragState {
    DashboardRenderer::WidgetEditGuide guide;
    int initialValue = 0;
    int dragStartCoordinate = 0;
};

struct TextEditDragState {
    DashboardRenderer::EditableTextKey key;
    int initialValue = 0;
    int dragStartCoordinate = 0;
};

NamedLayoutSectionConfig* FindNamedLayoutByName(AppConfig& config, const std::string& name);
LayoutCardConfig* FindCardLayoutById(LayoutConfig& layout, const std::string& cardId);
LayoutNodeConfig* FindLayoutNodeByPath(LayoutNodeConfig& root, const std::vector<size_t>& path);
const LayoutNodeConfig* FindLayoutNodeByPath(const LayoutNodeConfig& root, const std::vector<size_t>& path);
const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const DashboardRenderer::LayoutEditGuide& guide);
std::vector<int> SeedLayoutGuideWeights(const DashboardRenderer::LayoutEditGuide& guide, const LayoutNodeConfig* node);
bool ApplyLayoutGuideWeightsToConfig(AppConfig& config, const DashboardRenderer::LayoutEditGuide& guide, const std::vector<int>& weights);
void SetMenuItemRadioStyle(HMENU menu, UINT commandId);
std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress);
std::string FormatStorageDriveSize(double totalGb);
std::string FormatStorageDriveMenuText(const StorageDriveMenuOption& option);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
MonitorIdentity GetMonitorIdentity(const std::string& deviceName);
std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd);
HFONT CreateUiFont(const UiFontConfig& font);
void ShutdownPreviousInstance();

class DashboardApp {
public:
    explicit DashboardApp(const DiagnosticsOptions& diagnosticsOptions = {});
    bool Initialize(HINSTANCE instance);
    int Run();
    bool InitializeFonts();
    void SetRenderConfig(const AppConfig& config);
    void ReleaseFonts();
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool WriteDiagnosticsOutputs();
    void SaveDumpAs();
    void SaveScreenshotAs();
    void SaveFullConfigAs();
    bool IsAutoStartEnabled() const;
    void ToggleAutoStart();

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
    void ApplyConfigPlacement();
    bool ApplyConfiguredWallpaper();
    bool ConfigureDisplay(const DisplayMenuOption& option);
    bool SwitchLayout(const std::string& layoutName);
    void SelectNetworkAdapter(const NetworkMenuOption& option);
    void ToggleStorageDrive(const StorageDriveMenuOption& option);
    bool ApplyWindowDpi(UINT dpi, const RECT* suggestedRect = nullptr);
    void UpdateRendererScale(double scale);
    UINT CurrentWindowDpi() const;
    bool IsLayoutEditMode() const;
    void StartLayoutEditMode();
    void StopLayoutEditMode();
    void RefreshLayoutEditHover(POINT clientPoint);
    const DashboardRenderer::LayoutEditGuide* HitTestLayoutGuide(POINT clientPoint, size_t* index = nullptr) const;
    const DashboardRenderer::WidgetEditGuide* HitTestWidgetEditGuide(POINT clientPoint, size_t* index = nullptr) const;
    std::optional<DashboardRenderer::LayoutWidgetIdentity> HitTestEditableWidget(POINT clientPoint) const;
    std::optional<DashboardRenderer::EditableTextKey> HitTestEditableText(POINT clientPoint) const;
    std::optional<DashboardRenderer::EditableTextKey> HitTestEditableTextAnchor(POINT clientPoint) const;
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const DashboardRenderer::LayoutEditGuide& guide,
        const std::vector<int>& weights, const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis);
    std::optional<std::vector<int>> FindSnappedLayoutGuideWeights(const LayoutDragState& drag, const std::vector<int>& freeWeights);
    bool ApplyLayoutGuideWeights(const DashboardRenderer::LayoutEditGuide& guide, const std::vector<int>& weights);
    bool UpdateLayoutDrag(POINT clientPoint);
    bool ApplyWidgetEditValue(const DashboardRenderer::WidgetEditGuide& guide, int value);
    bool UpdateWidgetEditDrag(POINT clientPoint);
    bool ApplyTextEditValue(const DashboardRenderer::EditableTextKey& key, int value);
    bool UpdateTextEditDrag(POINT clientPoint);
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
    std::optional<std::filesystem::path> PromptDiagnosticsSavePath(
        const wchar_t* defaultFileName,
        const wchar_t* filter,
        const wchar_t* defaultExtension) const;

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
        COLORREF color, UINT format);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig config_;
    DashboardRenderer renderer_;
    std::unique_ptr<TelemetryRuntime> telemetry_;
    bool isMoving_ = false;
    bool isEditingLayout_ = false;
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
    DiagnosticsOptions diagnosticsOptions_;
    std::unique_ptr<DiagnosticsSession> diagnostics_;
    std::chrono::steady_clock::time_point lastDiagnosticsOutput_{};
    UINT currentDpi_ = kDefaultDpi;
    bool placementWatchActive_ = false;
    std::vector<DisplayMenuOption> configDisplayOptions_;
    std::vector<LayoutMenuOption> layoutMenuOptions_;
    std::vector<NetworkMenuOption> networkMenuOptions_;
    std::vector<StorageDriveMenuOption> storageDriveMenuOptions_;
    std::optional<size_t> hoveredLayoutGuideIndex_;
    std::optional<DashboardRenderer::LayoutWidgetIdentity> hoveredEditableWidget_;
    std::optional<size_t> hoveredWidgetEditGuideIndex_;
    std::optional<DashboardRenderer::EditableTextKey> hoveredEditableText_;
    std::optional<DashboardRenderer::EditableTextKey> hoveredEditableTextAnchor_;
    std::optional<LayoutDragState> activeLayoutDrag_;
    std::optional<WidgetEditDragState> activeWidgetEditDrag_;
    std::optional<TextEditDragState> activeTextEditDrag_;
};
