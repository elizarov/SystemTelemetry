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

bool WriteTelemetryDumpFile(const FilePath& path, const TelemetryDump& dump) {
    std::FILE* output = nullptr;
    if (fopen_s(&output, path.string().c_str(), kWriteBinaryMode) != 0 || output == nullptr) {
        return false;
    }
    const bool written = WriteTelemetryDump(output, dump);
    fclose(output);
    return written;
}

bool SaveBlankWallpaperImage(
    const FilePath& path, const TelemetryDump& dump, const AppConfig& config, double targetScale, Trace& trace) {
    std::string screenshotError;
    return SaveDumpScreenshot(path,
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
}

bool SetConfiguredMonitorWallpaper(const AppConfig& config,
    const std::wstring& wideWallpaperPath,
    const std::string& pathText,
    const char* action,
    Trace& trace) {
    if (config.display.monitorName.empty()) {
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("%s_skipped_missing_monitor wallpaper=\"%s\""),
            action,
            config.display.wallpaper.c_str());
        return false;
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("%s_monitor_unresolved monitor=\"%s\" wallpaper=\"%s\""),
            action,
            config.display.monitorName.c_str(),
            config.display.wallpaper.c_str());
        return false;
    }

    const HRESULT initStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE) {
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("%s_coinitialize_failed hr=0x%08lX"),
            action,
            static_cast<unsigned long>(initStatus));
        return false;
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    const HRESULT createStatus =
        CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktopWallpaper));
    if (FAILED(createStatus) || desktopWallpaper == nullptr) {
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("%s_create_failed hr=0x%08lX"),
            action,
            static_cast<unsigned long>(createStatus));
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
            RES_STR("%s_monitor_count_failed hr=0x%08lX"),
            action,
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
                const HRESULT setStatus = desktopWallpaper->SetWallpaper(monitorId, wideWallpaperPath.c_str());
                applied = SUCCEEDED(setStatus);
                trace.WriteFmt(TracePrefix::Wallpaper,
                    RES_STR("%s_%s monitor=\"%s\" path=\"%s\" hr=0x%08lX"),
                    action,
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
        trace.WriteFmt(TracePrefix::Wallpaper,
            RES_STR("%s_target_not_found monitor=\"%s\" path=\"%s\""),
            action,
            config.display.monitorName.c_str(),
            pathText.c_str());
    }

    desktopWallpaper->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return applied;
}

}  // namespace

bool ApplyConfiguredWallpaper(const AppConfig& config, Trace& trace) {
    if (!ResolveCommittedDisplayWallpaperOwner(config).has_value()) {
        return true;
    }

    const FilePath wallpaperPath = ResolveExecutableRelativePath(FilePath(config.display.wallpaper));
    if (wallpaperPath.empty()) {
        trace.WriteFmt(
            TracePrefix::Wallpaper, RES_STR("path_empty monitor=\"%s\""), config.display.monitorName.c_str());
        return false;
    }

    return SetConfiguredMonitorWallpaper(
        config, wallpaperPath.WideForNativeApi(), wallpaperPath.string(), "apply", trace);
}

bool ClearConfiguredWallpaper(const AppConfig& config, Trace& trace) {
    if (config.display.wallpaper.empty()) {
        return true;
    }
    const std::wstring emptyWallpaperPath;
    return SetConfiguredMonitorWallpaper(config, emptyWallpaperPath, std::string{}, "clear", trace);
}

bool ConfigureDisplay(const AppConfig& config,
    const TelemetryDump& dump,
    double targetScale,
    bool writeWallpaper,
    const AppConfig* previousWallpaperConfig,
    Trace& trace,
    HWND owner) {
    const FilePath configPath = GetRuntimeConfigPath();
    const FilePath imagePath = GetExecutableDirectory() / kDefaultBlankWallpaperFileName;

    if (CanWriteRuntimeConfig(configPath) && (!writeWallpaper || CanWriteRuntimeConfig(imagePath))) {
        if (writeWallpaper && !SaveBlankWallpaperImage(imagePath, dump, config, targetScale, trace)) {
            return false;
        }
        const bool configSaved = SaveConfig(configPath, config, ConfigParseContext{TelemetryMetricCatalog()});
        const bool wallpaperApplied = configSaved && (!writeWallpaper || ApplyConfiguredWallpaper(config, trace));
        const bool previousWallpaperCleared =
            wallpaperApplied &&
            (previousWallpaperConfig == nullptr || ClearConfiguredWallpaper(*previousWallpaperConfig, trace));
        return previousWallpaperCleared;
    }

    const FilePath tempConfigPath = CreateTempFilePath("CaseDashConfigureDisplayConfig");
    const FilePath tempDumpPath = writeWallpaper ? CreateTempFilePath("CaseDashConfigureDisplayDump") : FilePath{};
    if (tempConfigPath.empty() || (writeWallpaper && tempDumpPath.empty())) {
        return false;
    }

    bool prepared = SaveConfig(tempConfigPath, config, ConfigParseContext{TelemetryMetricCatalog()});
    if (prepared && writeWallpaper) {
        prepared = WriteTelemetryDumpFile(tempDumpPath, dump);
    }
    if (!prepared) {
        RemoveFileIfExists(tempConfigPath);
        RemoveFileIfExists(tempDumpPath);
        return false;
    }

    std::string parameters =
        FormatText("/configure-display %s", QuoteCommandLineArgument(tempConfigPath.string()).c_str());
    if (writeWallpaper) {
        AppendFormat(parameters,
            " /configure-display-write-wallpaper /configure-display-dump %s",
            QuoteCommandLineArgument(tempDumpPath.string()).c_str());
    }
    DWORD exitCode = 1;
    const bool launched = RunElevatedSelfAndWait(owner, parameters, {}, SW_HIDE, &exitCode);
    RemoveFileIfExists(tempConfigPath);
    RemoveFileIfExists(tempDumpPath);
    const bool elevatedConfigured = launched && exitCode == 0;
    return elevatedConfigured &&
           (previousWallpaperConfig == nullptr || ClearConfiguredWallpaper(*previousWallpaperConfig, trace));
}

int RunElevatedConfigureDisplayMode(
    const FilePath& configPayloadPath, const FilePath& dumpPayloadPath, bool writeWallpaper) {
    if (configPayloadPath.empty() || (writeWallpaper && dumpPayloadPath.empty())) {
        return 2;
    }

    const AppConfig config = LoadConfig(configPayloadPath, true, ConfigParseContext{TelemetryMetricCatalog()});
    TelemetryDump dump;
    if (writeWallpaper) {
        const std::string input = ReadBinaryFile(dumpPayloadPath);
        std::string error;
        if (input.empty() || !LoadTelemetryDump(input, dump, &error)) {
            return 1;
        }
    }

    Trace trace;
    const FilePath runtimeConfigPath = GetRuntimeConfigPath();
    const FilePath imagePath = GetExecutableDirectory() / kDefaultBlankWallpaperFileName;
    bool imageSaved = true;
    if (writeWallpaper) {
        const double targetScale = HasExplicitDisplayScale(config.display.scale) ? config.display.scale : 0.0;
        if (targetScale <= 0.0) {
            return 1;
        }
        imageSaved = SaveBlankWallpaperImage(imagePath, dump, config, targetScale, trace);
    }

    const bool configSaved =
        imageSaved && SaveConfig(runtimeConfigPath, config, ConfigParseContext{TelemetryMetricCatalog()});
    const bool wallpaperApplied = configSaved && (!writeWallpaper || ApplyConfiguredWallpaper(config, trace));
    RemoveFileIfExists(configPayloadPath);
    RemoveFileIfExists(dumpPayloadPath);
    return wallpaperApplied ? 0 : 1;
}
