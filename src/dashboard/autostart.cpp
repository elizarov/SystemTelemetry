#include "dashboard/autostart.h"

#include "dashboard/fps_service.h"
#include "util/command_line.h"
#include "util/elevated_process.h"
#include "util/paths.h"

namespace {

constexpr char kAutoStartRunSubKey[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr char kAutoStartValueName[] = "CaseDash";

struct AutoStartUpdateResult {
    bool success = false;
    bool accessDenied = false;
};

void BringOwnerToFront(HWND owner) {
    if (owner == nullptr || !IsWindow(owner)) {
        return;
    }
    ShowWindow(owner, SW_SHOWNORMAL);
    SetWindowPos(owner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(owner);
}

bool IsAccessDenied(DWORD status) {
    return status == ERROR_ACCESS_DENIED;
}

bool IsAccessDenied(LSTATUS status) {
    return status == ERROR_ACCESS_DENIED;
}

void CleanupAutoStartRegistration() {
    (void)WriteAutoStartRegistryValue(false);
    (void)StopAndDeleteFpsService();
}

AutoStartUpdateResult EnableAutoStartRegistration() {
    const DWORD serviceStatus = InstallOrUpdateFpsService();
    if (serviceStatus != ERROR_SUCCESS) {
        if (!IsAccessDenied(serviceStatus)) {
            CleanupAutoStartRegistration();
        }
        return {false, IsAccessDenied(serviceStatus)};
    }

    const LSTATUS registryStatus = WriteAutoStartRegistryValue(true);
    if (registryStatus != ERROR_SUCCESS) {
        CleanupAutoStartRegistration();
        return {false, IsAccessDenied(registryStatus)};
    }

    return {true, false};
}

AutoStartUpdateResult DisableAutoStartRegistration() {
    const LSTATUS registryStatus = WriteAutoStartRegistryValue(false);
    const DWORD serviceStatus = StopAndDeleteFpsService();
    return {
        registryStatus == ERROR_SUCCESS && serviceStatus == ERROR_SUCCESS,
        IsAccessDenied(registryStatus) || IsAccessDenied(serviceStatus)};
}

}  // namespace

std::optional<std::string> ReadAutoStartCommand() {
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExA(HKEY_LOCAL_MACHINE, kAutoStartRunSubKey, 0, KEY_QUERY_VALUE, &key);
    if (openStatus != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS queryStatus = RegQueryValueExA(key, kAutoStartValueName, nullptr, &type, nullptr, &size);
    if (queryStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::string value(size, '\0');
    const LSTATUS readStatus =
        RegQueryValueExA(key, kAutoStartValueName, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size);
    RegCloseKey(key);
    if (readStatus != ERROR_SUCCESS || value.empty()) {
        return std::nullopt;
    }

    const size_t terminator = value.find('\0');
    if (terminator != std::string::npos) {
        value.resize(terminator);
    }
    return value;
}

bool IsAutoStartEnabledForCurrentExecutable() {
    const auto executablePath = GetExecutablePath();
    const auto registeredCommand = ReadAutoStartCommand();
    if (!executablePath.has_value() || !registeredCommand.has_value()) {
        return false;
    }
    return NormalizeCommandPath(*registeredCommand) == NormalizeCommandPath(executablePath->string()) &&
        IsFpsServiceRunningForCurrentExecutable();
}

LSTATUS WriteAutoStartRegistryValue(bool enabled) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    const LSTATUS createStatus = RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        kAutoStartRunSubKey,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        &disposition);
    if (createStatus != ERROR_SUCCESS) {
        return createStatus;
    }

    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const auto executablePath = GetExecutablePath();
        if (!executablePath.has_value()) {
            RegCloseKey(key);
            return ERROR_FILE_NOT_FOUND;
        }
        const std::string command = QuoteCommandLineArgument(executablePath->string());
        result = RegSetValueExA(
            key,
            kAutoStartValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>(command.size() + 1));
    } else {
        result = RegDeleteValueA(key, kAutoStartValueName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    return result;
}

int RunElevatedAutoStartMode(bool enabled) {
    const AutoStartUpdateResult result = enabled ? EnableAutoStartRegistration() : DisableAutoStartRegistration();
    return result.success ? 0 : 1;
}

bool UpdateAutoStartElevated(bool enabled, HWND owner) {
    DWORD exitCode = 1;
    const bool launched =
        RunElevatedSelfAndWait(owner, enabled ? "/set-autostart on" : "/set-autostart off", {}, SW_HIDE, &exitCode);
    BringOwnerToFront(owner);
    return launched && exitCode == 0;
}

bool UpdateAutoStartRegistration(bool enabled, HWND owner) {
    const AutoStartUpdateResult result = enabled ? EnableAutoStartRegistration() : DisableAutoStartRegistration();
    if (result.success) {
        return true;
    }
    if (result.accessDenied) {
        return UpdateAutoStartElevated(enabled, owner);
    }
    return false;
}
