#include "dashboard/autostart.h"

#include "dashboard/fps_service.h"
#include "util/command_line.h"
#include "util/elevated_process.h"
#include "util/paths.h"
#include "util/utf8.h"

namespace {

constexpr char kAutoStartRunSubKey[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr char kAutoStartValueName[] = "CaseDash";

}  // namespace

std::optional<std::string> ReadAutoStartCommand() {
    const std::wstring runSubKey = WideFromUtf8(kAutoStartRunSubKey);
    const std::wstring valueName = WideFromUtf8(kAutoStartValueName);
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(HKEY_LOCAL_MACHINE, runSubKey.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (openStatus != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS queryStatus = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, nullptr, &size);
    if (queryStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(size / sizeof(wchar_t), wchar_t{});
    const LSTATUS readStatus =
        RegQueryValueExW(key, valueName.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size);
    RegCloseKey(key);
    if (readStatus != ERROR_SUCCESS || value.empty()) {
        return std::nullopt;
    }

    const size_t terminator = value.find(wchar_t{});
    if (terminator != std::wstring::npos) {
        value.resize(terminator);
    }
    return Utf8FromWide(value);
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
    const std::wstring runSubKey = WideFromUtf8(kAutoStartRunSubKey);
    const std::wstring valueName = WideFromUtf8(kAutoStartValueName);
    HKEY key = nullptr;
    DWORD disposition = 0;
    const LSTATUS createStatus = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        runSubKey.c_str(),
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
        const std::wstring command = WideFromUtf8(QuoteCommandLineArgument(executablePath->string()));
        result = RegSetValueExW(key,
            valueName.c_str(),
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, valueName.c_str());
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    return result;
}

int RunElevatedAutoStartMode(bool enabled) {
    const LSTATUS status = WriteAutoStartRegistryValue(enabled);
    if (status != ERROR_SUCCESS) {
        return 1;
    }

    const DWORD serviceStatus = enabled ? InstallOrUpdateFpsService() : StopAndDeleteFpsService();
    return serviceStatus == ERROR_SUCCESS ? 0 : 1;
}

bool UpdateAutoStartElevated(bool enabled, HWND owner) {
    DWORD exitCode = 1;
    return RunElevatedSelfAndWait(
               owner, enabled ? "/set-autostart on" : "/set-autostart off", {}, SW_HIDE, &exitCode) &&
           exitCode == 0;
}

bool UpdateAutoStartRegistration(bool enabled, HWND owner) {
    const LSTATUS status = WriteAutoStartRegistryValue(enabled);
    if (status == ERROR_ACCESS_DENIED) {
        return UpdateAutoStartElevated(enabled, owner);
    }
    if (status != ERROR_SUCCESS) {
        return false;
    }

    const DWORD serviceStatus = enabled ? InstallOrUpdateFpsService() : StopAndDeleteFpsService();
    if (serviceStatus == ERROR_ACCESS_DENIED) {
        return UpdateAutoStartElevated(enabled, owner);
    }
    return serviceStatus == ERROR_SUCCESS;
}
