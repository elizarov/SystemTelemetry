#include "util/paths.h"

#include <array>
#include <utility>

namespace {

extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(
    void* module, wchar_t* filename, unsigned long size);

constexpr size_t kModulePathBufferLength = 32768;

FilePath CaptureLaunchWorkingDirectory() {
    return CurrentDirectoryPath();
}

}  // namespace

FilePath GetExecutableDirectory() {
    std::array<wchar_t, kModulePathBufferLength> modulePath{};
    const auto length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return CurrentDirectoryPath();
    }
    return FilePath(std::wstring(modulePath.data(), length)).ParentPath();
}

FilePath GetWorkingDirectory() {
    static const FilePath workingDirectory = CaptureLaunchWorkingDirectory();
    if (!workingDirectory.Empty()) {
        return workingDirectory;
    }
    return CurrentDirectoryPath();
}

FilePath ResolveExecutableRelativePath(const FilePath& configuredPath) {
    if (configuredPath.Empty()) {
        return {};
    }
    if (configuredPath.IsAbsolute()) {
        return configuredPath;
    }
    return JoinPath(GetExecutableDirectory(), configuredPath);
}

std::optional<std::wstring> GetExecutablePath() {
    std::array<wchar_t, kModulePathBufferLength> modulePath{};
    const auto length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return std::nullopt;
    }
    return std::wstring(modulePath.data(), length);
}
