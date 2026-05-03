#include "runtime/config_io.h"

#include <shellapi.h>

#include "config/config_parser.h"
#include "config/config_writer.h"
#include "runtime/diagnostics_options.h"
#include "telemetry/metrics.h"
#include "util/paths.h"
#include "util/temp_file.h"

namespace {

FilePath CreateElevatedSaveConfigTempPath() {
    return CreateTempFilePath(L"stc");
}

}  // namespace

FilePath GetRuntimeConfigPath() {
    return GetExecutableDirectory() / L"config.ini";
}

ConfigParseContext RuntimeConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) {
    AppConfig config = LoadConfig(GetRuntimeConfigPath(), !options.defaultConfig, RuntimeConfigParseContext());
    ApplyDiagnosticsScaleOverride(config, options);
    return config;
}

bool SaveConfigElevated(const FilePath& targetPath, const AppConfig& config, HWND owner) {
    const FilePath tempPath = CreateElevatedSaveConfigTempPath();
    if (tempPath.empty() || targetPath.empty()) {
        return false;
    }

    if (!SaveConfig(tempPath, config, RuntimeConfigParseContext())) {
        RemoveFileIfExists(tempPath);
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
        RemoveFileIfExists(tempPath);
        return false;
    }
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        RemoveFileIfExists(tempPath);
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);
    RemoveFileIfExists(tempPath);
    return exitCode == 0;
}
