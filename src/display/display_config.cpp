#include "display/display_config.h"

#include <cstdio>
#include <shobjidl.h>

#include "config/config_io.h"
#include "config/config_parser.h"
#include "config/config_writer.h"
#include "diagnostics/diagnostics.h"
#include "display/constants.h"
#include "display/monitor.h"
#include "telemetry/metrics.h"
#include "util/command_line.h"
#include "util/elevated_process.h"
#include "util/paths.h"
#include "util/temp_file.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

constexpr char kWriteBinaryMode[] = "wb";

std::string ReadBinaryFile(const FilePath& path) {
    return ReadFileBinary(path).value_or(std::string{});
}

}  // namespace

bool ApplyConfiguredWallpaper(const AppConfig& config, Trace& trace) {
    if (config.display.wallpaper.empty()) {
        return true;
    }
    if (config.display.monitorName.empty()) {
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("skipped_missing_monitor wallpaper=\"%s\""),
            config.display.wallpaper.c_str());
        return false;
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("monitor_unresolved monitor=\"%s\" wallpaper=\"%s\""),
            config.display.monitorName.c_str(),
            config.display.wallpaper.c_str());
        return false;
    }

    const FilePath wallpaperPath = ResolveExecutableRelativePath(FilePath(config.display.wallpaper));
    if (wallpaperPath.empty()) {
        trace.WriteFmt(
            TracePrefix::Wallpaper, RES_STR("path_empty monitor=\"%s\""), config.display.monitorName.c_str());
        return false;
    }

    const HRESULT initStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE) {
        trace.WriteFmt(
            TracePrefix::Wallpaper, RES_STR("coinitialize_failed hr=0x%08lX"), static_cast<unsigned long>(initStatus));
        return false;
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    const HRESULT createStatus =
        CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktopWallpaper));
    if (FAILED(createStatus) || desktopWallpaper == nullptr) {
        trace.WriteFmt(
            TracePrefix::Wallpaper, RES_STR("create_failed hr=0x%08lX"), static_cast<unsigned long>(createStatus));
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
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("monitor_count_failed hr=0x%08lX"),
            static_cast<unsigned long>(countStatus));
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
                const std::wstring wideWallpaperPath = wallpaperPath.WideForNativeApi();
                const HRESULT setStatus = desktopWallpaper->SetWallpaper(monitorId, wideWallpaperPath.c_str());
                applied = SUCCEEDED(setStatus);
                const std::string pathText = wallpaperPath.string();
                trace.WriteFmt(TracePrefix::Wallpaper,
                    RES_STR("apply_%s monitor=\"%s\" path=\"%s\" hr=0x%08lX"),
                    applied ? "done" : "failed",
                    config.display.monitorName.c_str(),
                    pathText.c_str(),
                    static_cast<unsigned long>(setStatus));
                CoTaskMemFree(monitorId);
                break;
            }
            CoTaskMemFree(monitorId);
        }
    }

    if (!targetFound) {
        const std::string pathText = wallpaperPath.string();
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("target_not_found monitor=\"%s\" path=\"%s\""),
            config.display.monitorName.c_str(),
            pathText.c_str());
    }

    desktopWallpaper->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return applied;
}

bool ConfigureDisplay(
    const AppConfig& config, const TelemetryDump& dump, double targetScale, Trace& trace, HWND owner) {
    const FilePath configPath = GetRuntimeConfigPath();
    const FilePath imagePath = GetExecutableDirectory() / kDefaultBlankWallpaperFileName;

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
            trace,
            false,
            RenderPoint{},
            &screenshotError);
        return imageSaved && SaveConfig(configPath, config, ConfigParseContext{TelemetryMetricCatalog()}) &&
               ApplyConfiguredWallpaper(config, trace);
    }

    const FilePath tempConfigPath = CreateTempFilePath("CaseDashConfigureDisplayConfig");
    const FilePath tempDumpPath = CreateTempFilePath("CaseDashConfigureDisplayDump");
    if (tempConfigPath.empty() || tempDumpPath.empty()) {
        return false;
    }

    bool prepared = SaveConfig(tempConfigPath, config, ConfigParseContext{TelemetryMetricCatalog()});
    if (prepared) {
        std::FILE* output = nullptr;
        prepared = fopen_s(&output, tempDumpPath.string().c_str(), kWriteBinaryMode) == 0 && output != nullptr;
        if (prepared) {
            prepared = WriteTelemetryDump(output, dump);
            fclose(output);
        }
    }
    if (!prepared) {
        RemoveFileIfExists(tempConfigPath);
        RemoveFileIfExists(tempDumpPath);
        return false;
    }

    const std::string parameters = FormatText("/configure-display %s /configure-display-target %s "
                                              "/configure-display-dump %s /configure-display-image-target %s",
        QuoteCommandLineArgument(tempConfigPath.string()).c_str(),
        QuoteCommandLineArgument(configPath.string()).c_str(),
        QuoteCommandLineArgument(tempDumpPath.string()).c_str(),
        QuoteCommandLineArgument(imagePath.string()).c_str());

    DWORD exitCode = 1;
    const bool launched = RunElevatedSelfAndWait(owner, parameters, {}, SW_HIDE, &exitCode);
    RemoveFileIfExists(tempConfigPath);
    RemoveFileIfExists(tempDumpPath);
    return launched && exitCode == 0;
}

int RunElevatedConfigureDisplayMode(const FilePath& sourceConfigPath,
    const FilePath& sourceDumpPath,
    const FilePath& targetConfigPath,
    const FilePath& targetImagePath) {
    if (sourceConfigPath.empty() || sourceDumpPath.empty() || targetConfigPath.empty() || targetImagePath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourceConfigPath, true, ConfigParseContext{TelemetryMetricCatalog()});
    TelemetryDump dump;
    {
        const std::string input = ReadBinaryFile(sourceDumpPath);
        std::string error;
        if (input.empty() || !LoadTelemetryDump(input, dump, &error)) {
            return 1;
        }
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        return 1;
    }

    std::string screenshotError;
    Trace trace;
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
        trace,
        false,
        RenderPoint{},
        &screenshotError);
    const bool configSaved =
        imageSaved && SaveConfig(targetConfigPath, config, ConfigParseContext{TelemetryMetricCatalog()});
    const bool wallpaperApplied = configSaved && ApplyConfiguredWallpaper(config, trace);
    RemoveFileIfExists(sourceConfigPath);
    RemoveFileIfExists(sourceDumpPath);
    return wallpaperApplied ? 0 : 1;
}
