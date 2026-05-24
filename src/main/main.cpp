#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

#include "dashboard/autostart.h"
#include "dashboard/constants.h"
#include "dashboard/dashboard_app.h"
#include "dashboard/fps_service.h"
#include "diagnostics/crash_report.h"
#include "diagnostics/diagnostics.h"
#include "display/display_config.h"
#include "util/command_line.h"
#include "util/elevated_process.h"
#include "util/file_path.h"
#include "util/message_box.h"
#include "util/paths.h"

namespace {

void ShutdownPreviousInstance() {
    HWND existing = FindWindowA(kWindowClassName, nullptr);
    if (existing == nullptr) {
        return;
    }

    const DWORD existingProcessId = [&]() {
        DWORD processId = 0;
        GetWindowThreadProcessId(existing, &processId);
        return processId;
    }();

    if (existingProcessId == GetCurrentProcessId()) {
        return;
    }

    PostMessageA(existing, WM_CLOSE, 0, 0);
    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(100);
        existing = FindWindowA(kWindowClassName, nullptr);
        if (existing == nullptr) {
            return;
        }
    }
}

void AppendCommandLineArgument(std::string& parameters, std::string_view argument) {
    if (!parameters.empty()) {
        parameters += ' ';
    }
    parameters += QuoteCommandLineArgument(argument);
}

std::optional<int> RelaunchElevatedIfRequested(const CommandLineArguments& commandLine) {
    if (!HasSwitch(commandLine, "/elevate") || IsCurrentProcessElevated()) {
        return std::nullopt;
    }

    std::string parameters = BuildCommandLineExcludingSwitch(commandLine, "/elevate");
    if (!HasSwitch(commandLine, "/bring-to-front")) {
        AppendCommandLineArgument(parameters, "/bring-to-front");
    }
    // Size: reuse util/paths fixed-buffer capture instead of keeping a second vector-based path reader in main.
    DWORD exitCode = 1;
    return RunElevatedSelfAndWait(nullptr, parameters, GetWorkingDirectory(), SW_SHOWNORMAL, &exitCode)
               ? static_cast<int>(exitCode)
               : 1;
}

void ReportMainDiagnosticsError(const DiagnosticsOptions& options, std::string_view message) {
    if (!options.trace) {
        ShowAppMessageBox(message, MB_ICONERROR);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const CommandLineArguments commandLine = GetCommandLineArguments();
    if (IsFpsServiceCommandLine(commandLine)) {
        return RunFpsServiceMode();
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    DiagnosticsOptions diagnosticsOptions = GetDiagnosticsOptions(commandLine);
    diagnosticsOptions.reportError = &ReportMainDiagnosticsError;
    InstallCrashReportHandler(diagnosticsOptions);

    if (const auto elevatedSaveSource = GetSwitchValue(commandLine, "/save-config"); elevatedSaveSource.has_value()) {
        const auto elevatedSaveTarget = GetSwitchValue(commandLine, "/save-config-target");
        return RunElevatedSaveConfigMode(
            FilePath(*elevatedSaveSource), elevatedSaveTarget.has_value() ? FilePath(*elevatedSaveTarget) : FilePath{});
    }
    if (const auto configureDisplayPayload = GetSwitchValue(commandLine, "/configure-display");
        configureDisplayPayload.has_value()) {
        const auto configureDisplayDump = GetSwitchValue(commandLine, "/configure-display-dump");
        return RunElevatedConfigureDisplayMode(FilePath(*configureDisplayPayload),
            configureDisplayDump.has_value() ? FilePath(*configureDisplayDump) : FilePath{},
            HasSwitch(commandLine, "/configure-display-write-wallpaper"));
    }
    if (const auto autoStartSetting = GetSwitchValue(commandLine, "/set-autostart"); autoStartSetting.has_value()) {
        if (_stricmp(autoStartSetting->c_str(), "on") == 0) {
            return RunElevatedAutoStartMode(true);
        }
        if (_stricmp(autoStartSetting->c_str(), "off") == 0) {
            return RunElevatedAutoStartMode(false);
        }
        return 2;
    }
    if (const auto elevatedExitCode = RelaunchElevatedIfRequested(commandLine); elevatedExitCode.has_value()) {
        return *elevatedExitCode;
    }

    const DiagnosticsValidationResult diagnosticsValidation = ValidateDiagnosticsOptions(diagnosticsOptions);
    if (!diagnosticsValidation.ok) {
        ReportDiagnosticsError(diagnosticsOptions, diagnosticsValidation.message);
        return 2;
    }

    if (diagnosticsOptions.exit) {
        return RunDiagnosticsHeadlessMode(diagnosticsOptions);
    }

    ShutdownPreviousInstance();

    DashboardApp app(diagnosticsOptions, HasSwitch(commandLine, "/bring-to-front"));
    if (!app.Initialize(instance)) {
        const std::string& message = app.LastError();
        if (message.empty()) {
            ShowAppMessageBox("Failed to initialize the telemetry dashboard.", MB_ICONERROR);
        } else {
            ShowAppMessageBox(message, MB_ICONERROR);
        }
        return 1;
    }
    return app.Run();
}
