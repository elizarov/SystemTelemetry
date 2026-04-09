#include "app_autostart.h"

#include <shellapi.h>

#include "app_constants.h"
#include "app_paths.h"

std::optional<std::wstring> ReadAutoStartCommand() {
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        kAutoStartRunSubKey,
        0,
        KEY_QUERY_VALUE,
        &key);
    if (openStatus != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS queryStatus = RegQueryValueExW(key, kAutoStartValueName, nullptr, &type, nullptr, &size);
    if (queryStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    const LSTATUS readStatus = RegQueryValueExW(
        key,
        kAutoStartValueName,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(value.data()),
        &size);
    RegCloseKey(key);
    if (readStatus != ERROR_SUCCESS || value.empty()) {
        return std::nullopt;
    }

    const size_t terminator = value.find(L'\0');
    if (terminator != std::wstring::npos) {
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
    return NormalizeWindowsPath(*registeredCommand) == NormalizeWindowsPath(*executablePath);
}

LSTATUS WriteAutoStartRegistryValue(bool enabled) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    const LSTATUS createStatus = RegCreateKeyExW(
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
        const std::wstring command = QuoteCommandLineArgument(*executablePath);
        result = RegSetValueExW(
            key,
            kAutoStartValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kAutoStartValueName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    return result;
}

int RunElevatedAutoStartMode(bool enabled) {
    const LSTATUS status = WriteAutoStartRegistryValue(enabled);
    return status == ERROR_SUCCESS ? 0 : 1;
}

bool UpdateAutoStartElevated(bool enabled, HWND owner) {
    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        return false;
    }

    std::wstring parameters = enabled ? L"/set-autostart on" : L"/set-autostart off";
    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);
    return exitCode == 0;
}

bool UpdateAutoStartRegistration(bool enabled, HWND owner) {
    const LSTATUS status = WriteAutoStartRegistryValue(enabled);
    if (status == ERROR_SUCCESS) {
        return true;
    }
    if (status == ERROR_ACCESS_DENIED) {
        return UpdateAutoStartElevated(enabled, owner);
    }
    return false;
}
