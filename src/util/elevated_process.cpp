#include "util/elevated_process.h"

#include <shellapi.h>

#include "util/paths.h"

bool RunElevatedSelfAndWait(
    HWND owner, const wchar_t* parameters, const wchar_t* workingDirectory, int showCommand, DWORD* exitCode) {
    if (exitCode != nullptr) {
        *exitCode = 1;
    }
    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        return false;
    }

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = parameters != nullptr && parameters[0] != L'\0' ? parameters : nullptr;
    executeInfo.lpDirectory = workingDirectory != nullptr && workingDirectory[0] != L'\0' ? workingDirectory : nullptr;
    executeInfo.nShow = showCommand;
    if (!ShellExecuteExW(&executeInfo)) {
        return false;
    }
    if (executeInfo.hProcess == nullptr) {
        if (exitCode != nullptr) {
            *exitCode = 0;
        }
        return true;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD childExitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &childExitCode);
    CloseHandle(executeInfo.hProcess);
    if (exitCode != nullptr) {
        *exitCode = childExitCode;
    }
    return true;
}
