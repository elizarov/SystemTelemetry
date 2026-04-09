#include "display_configuration_service.h"

#include <fstream>

#include "config_writer.h"

bool DisplayConfigurationService::ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) const {
    return ::ApplyConfiguredWallpaper(config, traceStream);
}

std::vector<DisplayMenuOption> DisplayConfigurationService::EnumerateDisplayOptions(const AppConfig& config) const {
    return ::EnumerateDisplayMenuOptions(config);
}

std::optional<TargetMonitorInfo> DisplayConfigurationService::FindTargetMonitor(const std::string& requestedName) const {
    return ::FindTargetMonitor(requestedName);
}

bool DisplayConfigurationService::ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, UINT targetDpi,
    std::ostream* traceStream, HWND owner) const {
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    const std::filesystem::path imagePath = GetExecutableDirectory() / kDefaultBlankWallpaperFileName;

    if (CanWriteRuntimeConfig(configPath) && CanWriteRuntimeConfig(imagePath)) {
        std::string screenshotError;
        const bool imageSaved = SaveDumpScreenshot(
            imagePath,
            dump.snapshot,
            config,
            ScaleFromDpi(targetDpi),
            DashboardRenderer::RenderMode::Blank,
            false,
            DashboardRenderer::SimilarityIndicatorMode::ActiveGuide,
            std::string{},
            traceStream,
            &screenshotError);
        return imageSaved && SaveConfig(configPath, config) && ::ApplyConfiguredWallpaper(config, traceStream);
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
