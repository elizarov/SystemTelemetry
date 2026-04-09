#include "app_display_config.h"

#include <fstream>

#include <shobjidl.h>

#include "app_diagnostics.h"
#include "app_monitor.h"
#include "app_paths.h"
#include "app_strings.h"
#include "config_parser.h"
#include "config_writer.h"

bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) {
    if (config.display.wallpaper.empty()) {
        return true;
    }
    if (config.display.monitorName.empty()) {
        WriteOptionalTrace(traceStream, "wallpaper:skipped_missing_monitor wallpaper=\"" + config.display.wallpaper + "\"");
        return false;
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        WriteOptionalTrace(traceStream, "wallpaper:monitor_unresolved monitor=\"" + config.display.monitorName +
            "\" wallpaper=\"" + config.display.wallpaper + "\"");
        return false;
    }

    const std::filesystem::path wallpaperPath = ResolveExecutableRelativePath(std::filesystem::path(WideFromUtf8(config.display.wallpaper)));
    if (wallpaperPath.empty()) {
        WriteOptionalTrace(traceStream, "wallpaper:path_empty monitor=\"" + config.display.monitorName + "\"");
        return false;
    }

    const HRESULT initStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE) {
        WriteOptionalTrace(traceStream, "wallpaper:coinitialize_failed hr=" + FormatHresult(initStatus));
        return false;
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    const HRESULT createStatus = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&desktopWallpaper));
    if (FAILED(createStatus) || desktopWallpaper == nullptr) {
        WriteOptionalTrace(traceStream, "wallpaper:create_failed hr=" + FormatHresult(createStatus));
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
        WriteOptionalTrace(traceStream, "wallpaper:monitor_count_failed hr=" + FormatHresult(countStatus));
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
                WriteOptionalTrace(traceStream,
                    std::string("wallpaper:apply_") + (applied ? "done" : "failed") +
                    " monitor=\"" + config.display.monitorName +
                    "\" path=\"" + Utf8FromWide(wallpaperPath.wstring()) +
                    "\" hr=" + FormatHresult(setStatus));
                CoTaskMemFree(monitorId);
                break;
            }
            CoTaskMemFree(monitorId);
        }
    }

    if (!targetFound) {
        WriteOptionalTrace(traceStream, "wallpaper:target_not_found monitor=\"" + config.display.monitorName +
            "\" path=\"" + Utf8FromWide(wallpaperPath.wstring()) + "\"");
    }

    desktopWallpaper->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return applied;
}

int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath, const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath, const std::filesystem::path& targetImagePath) {
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
    const bool imageSaved = SaveDumpScreenshot(
        targetImagePath,
        dump.snapshot,
        config,
        ScaleFromDpi(targetMonitor->dpi),
        DashboardRenderer::RenderMode::Blank,
        false,
        DashboardRenderer::SimilarityIndicatorMode::ActiveGuide,
        std::string{},
        nullptr,
        &screenshotError);
    const bool configSaved = imageSaved && SaveConfig(targetConfigPath, config);
    const bool wallpaperApplied = configSaved && ApplyConfiguredWallpaper(config, nullptr);

    std::error_code ignored;
    std::filesystem::remove(sourceConfigPath, ignored);
    std::filesystem::remove(sourceDumpPath, ignored);
    return wallpaperApplied ? 0 : 1;
}
