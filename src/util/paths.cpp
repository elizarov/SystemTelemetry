#include "util/paths.h"

#include <array>

namespace {

extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(
    void* module, wchar_t* filename, unsigned long size);

constexpr size_t kModulePathBufferLength = 32768;

std::filesystem::path CaptureLaunchWorkingDirectory() {
    try {
        return std::filesystem::current_path();
    } catch (...) {
        return {};
    }
}

}  // namespace

std::filesystem::path GetExecutableDirectory() {
    std::array<wchar_t, kModulePathBufferLength> modulePath{};
    const auto length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(modulePath.data()).parent_path();
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
    std::array<wchar_t, kModulePathBufferLength> modulePath{};
    const auto length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return std::nullopt;
    }
    return std::wstring(modulePath.data(), length);
}
