#include "main/config_io.h"

#include <shellapi.h>

#include "config/config_parser.h"
#include "config/config_writer.h"
#include "diagnostics/diagnostics.h"
#include "util/paths.h"

std::filesystem::path GetRuntimeConfigPath() {
    return GetExecutableDirectory() / L"config.ini";
}

AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) {
    AppConfig config = LoadConfig(GetRuntimeConfigPath(), !options.defaultConfig);
    ApplyDiagnosticsScaleOverride(config, options);
    return config;
}

bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner) {
    const std::filesystem::path tempPath = CreateElevatedSaveConfigTempPath();
    if (tempPath.empty() || targetPath.empty()) {
        return false;
    }

    if (!SaveConfig(tempPath, config)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    std::wstring parameters = L"/save-config \"";
    parameters += tempPath.wstring();
    parameters += L"\" /save-config-target \"";
    parameters += targetPath.wstring();
    parameters += L"\"";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);

    std::error_code ignored;
    std::filesystem::remove(tempPath, ignored);
    return exitCode == 0;
}
