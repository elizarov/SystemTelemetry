#include "app_diagnostics.h"
#include "app_startup.h"
#include "dashboard_app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const DiagnosticsOptions diagnosticsOptions = GetDiagnosticsOptions();

    if (const auto elevatedSaveSource = GetSwitchValue(L"/save-config"); elevatedSaveSource.has_value()) {
        const auto elevatedSaveTarget = GetSwitchValue(L"/save-config-target");
        return RunElevatedSaveConfigMode(*elevatedSaveSource, elevatedSaveTarget.value_or(std::filesystem::path{}));
    }
    if (const auto configureDisplaySource = GetSwitchValue(L"/configure-display"); configureDisplaySource.has_value()) {
        const auto configureDisplayTarget = GetSwitchValue(L"/configure-display-target");
        const auto configureDisplayDump = GetSwitchValue(L"/configure-display-dump");
        const auto configureDisplayImageTarget = GetSwitchValue(L"/configure-display-image-target");
        return RunElevatedConfigureDisplayMode(*configureDisplaySource,
            configureDisplayDump.value_or(std::filesystem::path{}),
            configureDisplayTarget.value_or(std::filesystem::path{}),
            configureDisplayImageTarget.value_or(std::filesystem::path{}));
    }
    if (const auto autoStartSetting = GetSwitchValue(L"/set-autostart"); autoStartSetting.has_value()) {
        if (_wcsicmp(autoStartSetting->c_str(), L"on") == 0) {
            return RunElevatedAutoStartMode(true);
        }
        if (_wcsicmp(autoStartSetting->c_str(), L"off") == 0) {
            return RunElevatedAutoStartMode(false);
        }
        return 2;
    }

    if (!ValidateDiagnosticsOptions(diagnosticsOptions)) {
        return 2;
    }

    if (diagnosticsOptions.exit) {
        return RunDiagnosticsHeadlessMode(diagnosticsOptions);
    }

    ShutdownPreviousInstance();

    DashboardApp app(diagnosticsOptions);
    if (!app.Initialize(instance)) {
        MessageBoxW(nullptr, L"Failed to initialize the telemetry dashboard.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
