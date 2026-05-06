#include <windows.h>

#include <optional>
#include <shellapi.h>
#include <string>

#include "dashboard/autostart.h"
#include "dashboard/constants.h"
#include "dashboard/dashboard_app.h"
#include "dashboard/fps_service.h"
#include "diagnostics/crash_report.h"
#include "diagnostics/diagnostics.h"
#include "display/display_config.h"
#include "util/command_line.h"
#include "util/file_path.h"
#include "util/paths.h"
#include "util/utf8.h"

namespace {

void ShutdownPreviousInstance() {
    HWND existing = FindWindowW(kWindowClassName, nullptr);
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
        existing = FindWindowW(kWindowClassName, nullptr);
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

    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value() || executablePath->empty()) {
        return 1;
    }

    const std::string parameters = BuildCommandLineExcludingSwitch(commandLine, "/elevate");
    const std::wstring wideParameters = WideFromUtf8(parameters);
    // Size: reuse util/paths fixed-buffer capture instead of keeping a second vector-based path reader in main.
    const std::wstring workingDirectory = GetWorkingDirectory().wstring();
    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = wideParameters.empty() ? nullptr : wideParameters.c_str();
    executeInfo.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    executeInfo.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&executeInfo)) {
        return 1;
    }

    if (executeInfo.hProcess == nullptr) {
        return 0;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL hasExitCode = GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);
    return hasExitCode ? static_cast<int>(exitCode) : 1;
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
        return RunElevatedSaveConfigMode(FilePath(WideFromUtf8(*elevatedSaveSource)),
            elevatedSaveTarget.has_value() ? FilePath(WideFromUtf8(*elevatedSaveTarget)) : FilePath{});
    }
    if (const auto configureDisplaySource = GetSwitchValue(commandLine, "/configure-display");
        configureDisplaySource.has_value()) {
        const auto configureDisplayTarget = GetSwitchValue(commandLine, "/configure-display-target");
        const auto configureDisplayDump = GetSwitchValue(commandLine, "/configure-display-dump");
        const auto configureDisplayImageTarget = GetSwitchValue(commandLine, "/configure-display-image-target");
        return RunElevatedConfigureDisplayMode(FilePath(WideFromUtf8(*configureDisplaySource)),
            configureDisplayDump.has_value() ? FilePath(WideFromUtf8(*configureDisplayDump)) : FilePath{},
            configureDisplayTarget.has_value() ? FilePath(WideFromUtf8(*configureDisplayTarget)) : FilePath{},
            configureDisplayImageTarget.has_value() ? FilePath(WideFromUtf8(*configureDisplayImageTarget))
                                                    : FilePath{});
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
        const std::wstring& message = app.LastError();
        MessageBoxW(nullptr,
            message.empty() ? L"Failed to initialize the telemetry dashboard." : message.c_str(),
            L"CaseDash",
            MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
