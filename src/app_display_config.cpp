#include "app_display_config.h"

#include <fstream>
#include <shellapi.h>
#include <shobjidl.h>

#include "app_config_io.h"
#include "util/command_line.h"
#include "app_diagnostics.h"
#include "app_monitor.h"
#include "util/paths.h"
#include "util/strings.h"
#include "config/config_parser.h"
#include "config/config_writer.h"
#include "util/trace.h"

bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) {
    const tracing::Trace trace(traceStream);
    if (config.display.wallpaper.empty()) {
        return true;
    }
    if (config.display.monitorName.empty()) {
        trace.Write("wallpaper:skipped_missing_monitor wallpaper=\"" + config.display.wallpaper + "\"");
        return false;
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        trace.Write("wallpaper:monitor_unresolved monitor=\"" + config.display.monitorName + "\" wallpaper=\"" +
                    config.display.wallpaper + "\"");
        return false;
    }

    const std::filesystem::path wallpaperPath =
        ResolveExecutableRelativePath(std::filesystem::path(WideFromUtf8(config.display.wallpaper)));
    if (wallpaperPath.empty()) {
        trace.Write("wallpaper:path_empty monitor=\"" + config.display.monitorName + "\"");
        return false;
    }

    const HRESULT initStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE) {
        trace.Write("wallpaper:coinitialize_failed hr=" + FormatHresult(initStatus));
        return false;
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    const HRESULT createStatus =
        CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktopWallpaper));
    if (FAILED(createStatus) || desktopWallpaper == nullptr) {
        trace.Write("wallpaper:create_failed hr=" + FormatHresult(createStatus));
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    bool applied = false;
    bool targetFound = false;
    UINT monitorCount = 0;
    const HRESULT countStatus = desktopWallpaper->GetMonitorDevicePathCount(&monitorCount);
    if (FAILED(countStatus)) {
        trace.Write("wallpaper:monitor_count_failed hr=" + FormatHresult(countStatus));
    } else {
        for (UINT index = 0; index < monitorCount; ++index) {
            LPWSTR monitorId = nullptr;
            const HRESULT idStatus = desktopWallpaper->GetMonitorDevicePathAt(index, &monitorId);
            if (FAILED(idStatus) || monitorId == nullptr) {
                continue;
            }

            RECT monitorRect{};
            const HRESULT rectStatus = desktopWallpaper->GetMonitorRECT(monitorId, &monitorRect);
            if (SUCCEEDED(rectStatus) && RectsEqual(monitorRect, targetMonitor->rect)) {
                targetFound = true;
                const HRESULT setStatus = desktopWallpaper->SetWallpaper(monitorId, wallpaperPath.c_str());
                applied = SUCCEEDED(setStatus);
                trace.Write(std::string("wallpaper:apply_") + (applied ? "done" : "failed") + " monitor=\"" +
                            config.display.monitorName + "\" path=\"" + Utf8FromWide(wallpaperPath.wstring()) +
                            "\" hr=" + FormatHresult(setStatus));
                CoTaskMemFree(monitorId);
                break;
            }
            CoTaskMemFree(monitorId);
        }
    }

    if (!targetFound) {
        trace.Write("wallpaper:target_not_found monitor=\"" + config.display.monitorName + "\" path=\"" +
                    Utf8FromWide(wallpaperPath.wstring()) + "\"");
    }

    desktopWallpaper->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return applied;
}

bool ConfigureDisplay(
    const AppConfig& config, const TelemetryDump& dump, double targetScale, std::ostream* traceStream, HWND owner) {
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    const std::filesystem::path imagePath = GetExecutableDirectory() / kDefaultBlankWallpaperFileName;

    if (CanWriteRuntimeConfig(configPath) && CanWriteRuntimeConfig(imagePath)) {
        std::string screenshotError;
        const bool imageSaved = SaveDumpScreenshot(imagePath,
            dump.snapshot,
            config,
            targetScale,
            DashboardRenderer::RenderMode::Blank,
            false,
            LayoutSimilarityIndicatorMode::ActiveGuide,
            std::string{},
            std::nullopt,
            traceStream,
            &screenshotError);
        return imageSaved && SaveConfig(configPath, config) && ApplyConfiguredWallpaper(config, traceStream);
    }

    const std::filesystem::path tempConfigPath = CreateTempFilePath(L"SystemTelemetryConfigureDisplayConfig");
    const std::filesystem::path tempDumpPath = CreateTempFilePath(L"SystemTelemetryConfigureDisplayDump");
    if (tempConfigPath.empty() || tempDumpPath.empty()) {
        return false;
    }

    bool prepared = SaveConfig(tempConfigPath, config);
    if (prepared) {
        std::ofstream output(tempDumpPath, std::ios::binary | std::ios::trunc);
        prepared = output.is_open() && WriteTelemetryDump(output, dump);
    }
    if (!prepared) {
        std::error_code ignored;
        std::filesystem::remove(tempConfigPath, ignored);
        std::filesystem::remove(tempDumpPath, ignored);
        return false;
    }

    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        std::error_code ignored;
        std::filesystem::remove(tempConfigPath, ignored);
        std::filesystem::remove(tempDumpPath, ignored);
        return false;
    }

    std::wstring parameters = L"/configure-display ";
    parameters += QuoteCommandLineArgument(tempConfigPath.wstring());
    parameters += L" /configure-display-target ";
    parameters += QuoteCommandLineArgument(configPath.wstring());
    parameters += L" /configure-display-dump ";
    parameters += QuoteCommandLineArgument(tempDumpPath.wstring());
    parameters += L" /configure-display-image-target ";
    parameters += QuoteCommandLineArgument(imagePath.wstring());

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        std::error_code ignored;
        std::filesystem::remove(tempConfigPath, ignored);
        std::filesystem::remove(tempDumpPath, ignored);
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);

    std::error_code ignored;
    std::filesystem::remove(tempConfigPath, ignored);
    std::filesystem::remove(tempDumpPath, ignored);
    return exitCode == 0;
}

int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath,
    const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath,
    const std::filesystem::path& targetImagePath) {
    if (sourceConfigPath.empty() || sourceDumpPath.empty() || targetConfigPath.empty() || targetImagePath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourceConfigPath);
    TelemetryDump dump;
    {
        std::ifstream input(sourceDumpPath, std::ios::binary);
        std::string error;
        if (!input.is_open() || !LoadTelemetryDump(input, dump, &error)) {
            return 1;
        }
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        return 1;
    }

    std::string screenshotError;
    const double targetScale = HasExplicitDisplayScale(config.display.scale)
                                   ? config.display.scale
                                   : ComputeMonitorFittedScale(config,
                                         targetMonitor->rect.right - targetMonitor->rect.left,
                                         targetMonitor->rect.bottom - targetMonitor->rect.top);
    if (targetScale <= 0.0) {
        return 1;
    }

    const bool imageSaved = SaveDumpScreenshot(targetImagePath,
        dump.snapshot,
        config,
        targetScale,
        DashboardRenderer::RenderMode::Blank,
        false,
        LayoutSimilarityIndicatorMode::ActiveGuide,
        std::string{},
        std::nullopt,
        nullptr,
        &screenshotError);
    const bool configSaved = imageSaved && SaveConfig(targetConfigPath, config);
    const bool wallpaperApplied = configSaved && ApplyConfiguredWallpaper(config, nullptr);

    std::error_code ignored;
    std::filesystem::remove(sourceConfigPath, ignored);
    std::filesystem::remove(sourceDumpPath, ignored);
    return wallpaperApplied ? 0 : 1;
}
