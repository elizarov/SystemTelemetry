#include "util/elevated_process.h"

#include <shellapi.h>

#include "util/paths.h"
#include "util/utf8.h"

namespace {

constexpr wchar_t kRunAsVerb[] = L"runas";  // ShellExecuteExW requires a UTF-16 elevation verb.

bool LaunchElevatedSelf(
    HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand, HANDLE* process) {
    if (process != nullptr) {
        *process = nullptr;
    }
    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        return false;
    }

    const std::wstring wideExecutablePath = executablePath->Wide();
    const std::wstring wideParameters = WideFromUtf8(parameters);
    const std::wstring wideWorkingDirectory = workingDirectory.Wide();

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = kRunAsVerb;
    executeInfo.lpFile = wideExecutablePath.c_str();
    executeInfo.lpParameters = !wideParameters.empty() ? wideParameters.c_str() : nullptr;
    executeInfo.lpDirectory = !wideWorkingDirectory.empty() ? wideWorkingDirectory.c_str() : nullptr;
    executeInfo.nShow = showCommand;
    if (!ShellExecuteExW(&executeInfo)) {
        return false;
    }
    if (process != nullptr) {
        *process = executeInfo.hProcess;
    } else if (executeInfo.hProcess != nullptr) {
        CloseHandle(executeInfo.hProcess);
    }
    return true;
}

}  // namespace

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

bool RunElevatedSelf(HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand) {
    return LaunchElevatedSelf(owner, parameters, workingDirectory, showCommand, nullptr);
}

bool RunElevatedSelfAndWait(
    HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand, DWORD* exitCode) {
    if (exitCode != nullptr) {
        *exitCode = 1;
    }

    HANDLE process = nullptr;
    if (!LaunchElevatedSelf(owner, parameters, workingDirectory, showCommand, &process)) {
        return false;
    }
    if (process == nullptr) {
        if (exitCode != nullptr) {
            *exitCode = 0;
        }
        return true;
    }

    WaitForSingleObject(process, INFINITE);
    DWORD childExitCode = 1;
    GetExitCodeProcess(process, &childExitCode);
    CloseHandle(process);
    if (exitCode != nullptr) {
        *exitCode = childExitCode;
    }
    return true;
}
