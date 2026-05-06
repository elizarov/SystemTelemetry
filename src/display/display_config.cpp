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
#include "util/trace.h"
#include "util/utf8.h"
#include "util/win32_format.h"

namespace {

std::string ReadBinaryFile(const FilePath& path) {
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
        return {};
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return {};
    }
    fseek(file, 0, SEEK_SET);
    std::string text(static_cast<size_t>(size), '\0');
    const size_t read = text.empty() ? 0 : fread(text.data(), 1, text.size(), file);
    fclose(file);
    return read == text.size() ? text : std::string{};
}

}  // namespace

bool ApplyConfiguredWallpaper(const AppConfig& config, Trace& trace) {
    if (config.display.wallpaper.empty()) {
        return true;
    }
    if (config.display.monitorName.empty()) {
        trace.Write(TracePrefix::Wallpaper, "skipped_missing_monitor wallpaper=\"" + config.display.wallpaper + "\"");
        return false;
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        trace.Write(TracePrefix::Wallpaper,
            "monitor_unresolved monitor=\"" + config.display.monitorName + "\" wallpaper=\"" +
                config.display.wallpaper + "\"");
        return false;
    }

    const FilePath wallpaperPath = ResolveExecutableRelativePath(FilePath(WideFromUtf8(config.display.wallpaper)));
    if (wallpaperPath.empty()) {
        trace.Write(TracePrefix::Wallpaper, "path_empty monitor=\"" + config.display.monitorName + "\"");
        return false;
    }

    const HRESULT initStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE) {
        trace.Write(TracePrefix::Wallpaper, "coinitialize_failed hr=" + FormatHresult(initStatus));
        return false;
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    const HRESULT createStatus =
        CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktopWallpaper));
    if (FAILED(createStatus) || desktopWallpaper == nullptr) {
        trace.Write(TracePrefix::Wallpaper, "create_failed hr=" + FormatHresult(createStatus));
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
        trace.Write(TracePrefix::Wallpaper, "monitor_count_failed hr=" + FormatHresult(countStatus));
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
                trace.Write(TracePrefix::Wallpaper,
                    std::string("apply_") + (applied ? "done" : "failed") + " monitor=\"" + config.display.monitorName +
                        "\" path=\"" + Utf8FromWide(wallpaperPath.wstring()) + "\" hr=" + FormatHresult(setStatus));
                CoTaskMemFree(monitorId);
                break;
            }
            CoTaskMemFree(monitorId);
        }
    }

    if (!targetFound) {
        trace.Write(TracePrefix::Wallpaper,
            "target_not_found monitor=\"" + config.display.monitorName + "\" path=\"" +
                Utf8FromWide(wallpaperPath.wstring()) + "\"");
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

    const FilePath tempConfigPath = CreateTempFilePath(L"CaseDashConfigureDisplayConfig");
    const FilePath tempDumpPath = CreateTempFilePath(L"CaseDashConfigureDisplayDump");
    if (tempConfigPath.empty() || tempDumpPath.empty()) {
        return false;
    }

    bool prepared = SaveConfig(tempConfigPath, config, ConfigParseContext{TelemetryMetricCatalog()});
    if (prepared) {
        std::FILE* output = nullptr;
        prepared = _wfopen_s(&output, tempDumpPath.c_str(), L"wb") == 0 && output != nullptr;
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

    std::string parameters = "/configure-display ";
    parameters += QuoteCommandLineArgument(Utf8FromWide(tempConfigPath.wstring()));
    parameters += " /configure-display-target ";
    parameters += QuoteCommandLineArgument(Utf8FromWide(configPath.wstring()));
    parameters += " /configure-display-dump ";
    parameters += QuoteCommandLineArgument(Utf8FromWide(tempDumpPath.wstring()));
    parameters += " /configure-display-image-target ";
    parameters += QuoteCommandLineArgument(Utf8FromWide(imagePath.wstring()));
    const std::wstring wideParameters = WideFromUtf8(parameters);

    DWORD exitCode = 1;
    const bool launched = RunElevatedSelfAndWait(owner, wideParameters.c_str(), nullptr, SW_HIDE, &exitCode);
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
