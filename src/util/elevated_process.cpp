#include "util/elevated_process.h"

#include <shellapi.h>

#include "util/paths.h"
#include "util/utf8.h"

namespace {

constexpr wchar_t kRunAsVerb[] = L"runas";  // ShellExecuteExW requires a UTF-16 elevation verb.

}  // namespace

bool RunElevatedSelfAndWait(
    HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand, DWORD* exitCode) {
    if (exitCode != nullptr) {
        *exitCode = 1;
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
