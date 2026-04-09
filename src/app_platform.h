#pragma once

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "app_constants.h"
#include "app_types.h"
#include "config.h"
#include "telemetry_runtime.h"
#include "utf8.h"

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
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);
bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream);
int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath, const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath, const std::filesystem::path& targetImagePath);
int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath);
int RunElevatedAutoStartMode(bool enabled);

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

bool CanWriteRuntimeConfig(const std::filesystem::path& path);
std::filesystem::path CreateTempFilePath(const wchar_t* prefix);
std::filesystem::path CreateElevatedSaveConfigTempPath();

std::string FormatSpeed(double mbps);
double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory);
UINT GetMonitorDpi(HMONITOR monitor);
double ScaleFromDpi(UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);

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
