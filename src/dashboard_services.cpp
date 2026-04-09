#include "dashboard_services.h"

#include <fstream>

#include "config_writer.h"

AppConfig ConfigPersistenceService::LoadRuntimeConfig(const DiagnosticsOptions& options) const {
    return ::LoadRuntimeConfig(options);
}

bool ConfigPersistenceService::SaveRuntimeConfig(const std::filesystem::path& path, const AppConfig& config, HWND owner) const {
    if (CanWriteRuntimeConfig(path)) {
        return SaveConfig(path, config);
    }
    return SaveConfigElevated(path, config, owner);
}

bool ConfigPersistenceService::SaveFullConfig(const std::filesystem::path& path, const AppConfig& config) const {
    return ::SaveFullConfig(path, config);
}

std::unique_ptr<DiagnosticsSession> DiagnosticsService::CreateSession(const DiagnosticsOptions& options) const {
    auto session = std::make_unique<DiagnosticsSession>(options);
    if (!session->Initialize()) {
        return nullptr;
    }
    return session;
}

bool DiagnosticsService::WriteOutputs(DiagnosticsSession* session, const TelemetryDump& dump, const AppConfig& config) const {
    return session != nullptr ? session->WriteOutputs(dump, config) : true;
}

bool DiagnosticsService::ReloadTelemetryRuntime(const std::filesystem::path& configPath, AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry, const DiagnosticsOptions& diagnosticsOptions,
    DiagnosticsSession* diagnostics) const {
    return ReloadTelemetryRuntimeFromDisk(configPath, activeConfig, telemetry, diagnosticsOptions, diagnostics);
}

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

bool AutoStartService::IsEnabled() const {
    return IsAutoStartEnabledForCurrentExecutable();
}

bool AutoStartService::Update(bool enabled, HWND owner) const {
    return UpdateAutoStartRegistration(enabled, owner);
}

std::unique_ptr<TelemetryRuntime> DashboardSessionService::InitializeRuntime(const AppConfig& config,
    const DiagnosticsOptions& options, std::ostream* traceStream) const {
    return InitializeTelemetryRuntimeInstance(config, options, traceStream);
}

void DashboardSessionService::UpdateSnapshot(TelemetryRuntime& telemetry) const {
    telemetry.UpdateSnapshot();
}

void DashboardSessionService::SetPreferredNetworkAdapter(TelemetryRuntime& telemetry, const std::string& adapterName) const {
    telemetry.SetPreferredNetworkAdapterName(adapterName);
}

void DashboardSessionService::SetSelectedStorageDrives(TelemetryRuntime& telemetry, const std::vector<std::string>& driveLetters) const {
    telemetry.SetSelectedStorageDrives(driveLetters);
}

bool LayoutEditingService::ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights) const {
    return layout_edit::ApplyGuideWeights(config, target, weights);
}

bool LayoutEditingService::ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) const {
    return layout_edit::ApplyValue(config, target, value);
}

std::optional<int> LayoutEditingService::EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer, const AppConfig& baseConfig,
    const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights,
    const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) const {
    return layout_edit::EvaluateWidgetExtentForGuideWeights(renderer, baseConfig, target, weights, widget, axis);
}
