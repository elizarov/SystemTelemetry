#include "util/paths.h"

#include <windows.h>

#include <array>

namespace {

constexpr size_t kModulePathBufferLength = 32768;

FilePath CaptureLaunchWorkingDirectory() {
    return CurrentDirectoryPath();
}

}  // namespace

FilePath GetExecutableDirectory() {
    std::array<char, kModulePathBufferLength> modulePath{};
    const auto length = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return CurrentDirectoryPath();
    }
    return FilePath(std::string(modulePath.data(), length)).ParentPath();
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

std::optional<FilePath> GetExecutablePath() {
    std::array<char, kModulePathBufferLength> modulePath{};
    const auto length = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return std::nullopt;
    }
    return FilePath(std::string(modulePath.data(), length));
}
