#include <windows.h>

#include <optional>
#include <string>

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
#include "util/utf8.h"

namespace {

void ShutdownPreviousInstance() {
    const std::wstring windowClassName = WideFromUtf8(kWindowClassName);
    HWND existing = FindWindowW(windowClassName.c_str(), nullptr);
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

    PostMessageW(existing, WM_CLOSE, 0, 0);
    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(100);
        existing = FindWindowW(windowClassName.c_str(), nullptr);
        if (existing == nullptr) {
            return;
        }
    }
}

bool IsCurrentProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returnedLength = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returnedLength);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

std::optional<int> RelaunchElevatedIfRequested(const CommandLineArguments& commandLine) {
    if (!HasSwitch(commandLine, "/elevate") || IsCurrentProcessElevated()) {
        return std::nullopt;
    }

    const std::string parameters = BuildCommandLineExcludingSwitch(commandLine, "/elevate");
    // Size: reuse util/paths fixed-buffer capture instead of keeping a second vector-based path reader in main.
    DWORD exitCode = 1;
    return RunElevatedSelfAndWait(nullptr, parameters, GetWorkingDirectory(), SW_SHOWNORMAL, &exitCode)
               ? static_cast<int>(exitCode)
               : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const CommandLineArguments commandLine = GetCommandLineArguments();
    if (IsFpsServiceCommandLine(commandLine)) {
        return RunFpsServiceMode();
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const DiagnosticsOptions diagnosticsOptions = GetDiagnosticsOptions(commandLine);
    InstallCrashReportHandler(diagnosticsOptions);

    if (const auto elevatedSaveSource = GetSwitchValue(commandLine, "/save-config"); elevatedSaveSource.has_value()) {
        const auto elevatedSaveTarget = GetSwitchValue(commandLine, "/save-config-target");
        return RunElevatedSaveConfigMode(
            FilePath(*elevatedSaveSource), elevatedSaveTarget.has_value() ? FilePath(*elevatedSaveTarget) : FilePath{});
    }
    if (const auto configureDisplaySource = GetSwitchValue(commandLine, "/configure-display");
        configureDisplaySource.has_value()) {
        const auto configureDisplayTarget = GetSwitchValue(commandLine, "/configure-display-target");
        const auto configureDisplayDump = GetSwitchValue(commandLine, "/configure-display-dump");
        const auto configureDisplayImageTarget = GetSwitchValue(commandLine, "/configure-display-image-target");
        return RunElevatedConfigureDisplayMode(FilePath(*configureDisplaySource),
            configureDisplayDump.has_value() ? FilePath(*configureDisplayDump) : FilePath{},
            configureDisplayTarget.has_value() ? FilePath(*configureDisplayTarget) : FilePath{},
            configureDisplayImageTarget.has_value() ? FilePath(*configureDisplayImageTarget) : FilePath{});
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

    if (!ValidateDiagnosticsOptions(diagnosticsOptions)) {
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
            MessageBoxUtf8("Failed to initialize the telemetry dashboard.", MB_ICONERROR);
        } else {
            MessageBoxUtf8(message, MB_ICONERROR);
        }
        return 1;
    }
    return app.Run();
}
