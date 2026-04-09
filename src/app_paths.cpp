#include "app_paths.h"

#include "app_constants.h"
#include "config_parser.h"

namespace {

std::filesystem::path CaptureLaunchWorkingDirectory() {
    try {
        return std::filesystem::current_path();
    } catch (...) {
        return {};
    }
}

}  // namespace

std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(modulePath).parent_path();
}

std::filesystem::path GetWorkingDirectory() {
    static const std::filesystem::path workingDirectory = CaptureLaunchWorkingDirectory();
    if (!workingDirectory.empty()) {
        return workingDirectory;
    }
    try {
        return std::filesystem::current_path();
    } catch (...) {
        return {};
    }
}

std::filesystem::path ResolveExecutableRelativePath(const std::filesystem::path& configuredPath) {
    if (configuredPath.empty()) {
        return {};
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return GetExecutableDirectory() / configuredPath;
}

std::optional<std::wstring> GetExecutablePath() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::nullopt;
    }
    return std::wstring(modulePath, length);
}

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

std::filesystem::path GetRuntimeConfigPath() {
    return GetExecutableDirectory() / L"config.ini";
}

AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) {
    return LoadConfig(GetRuntimeConfigPath(), !options.defaultConfig);
}
