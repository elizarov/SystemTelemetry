#include "util/file_path.h"

#include <windows.h>

#include <cstdio>
#include <utility>

#include "util/text_encoding.h"
#include "util/text_format.h"

namespace {

constexpr char kReadBinaryMode[] = "rb";
constexpr char kWriteBinaryMode[] = "wb";

bool IsSeparator(char ch) {
    return ch == '\\' || ch == '/';
}

bool HasDrivePrefix(std::string_view path) {
    return path.size() >= 2 && path[1] == ':' &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}

size_t RootLength(std::string_view path) {
    if (path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1])) {
        size_t serverEnd = path.find_first_of("\\/", 2);
        if (serverEnd == std::string_view::npos) {
            return path.size();
        }
        size_t shareEnd = path.find_first_of("\\/", serverEnd + 1);
        return shareEnd == std::string_view::npos ? path.size() : shareEnd + 1;
    }
    if (HasDrivePrefix(path)) {
        return path.size() >= 3 && IsSeparator(path[2]) ? 3 : 2;
    }
    return !path.empty() && IsSeparator(path[0]) ? 1 : 0;
}

std::string TrimTrailingSeparators(std::string path) {
    const size_t rootLength = RootLength(path);
    while (path.size() > rootLength && IsSeparator(path.back())) {
        path.pop_back();
    }
    return path;
}

}  // namespace

FilePath::FilePath(const char* path) :
    path_(path != nullptr ? path : "") {}

FilePath::FilePath(std::string path) :
    path_(std::move(path)) {}

FilePath::FilePath(std::string_view path) :
    path_(path) {}

bool FilePath::Empty() const {
    return path_.empty();
}

bool FilePath::empty() const {
    return Empty();
}

bool FilePath::IsAbsolute() const {
    return RootLength(path_) > 0 && (IsSeparator(path_[0]) || path_.size() >= 3);
}

bool FilePath::is_absolute() const {
    return IsAbsolute();
}

bool FilePath::HasParentPath() const {
    return !ParentPath().Empty();
}

bool FilePath::has_parent_path() const {
    return HasParentPath();
}

FilePath FilePath::ParentPath() const {
    std::string trimmed = TrimTrailingSeparators(path_);
    const size_t rootLength = RootLength(trimmed);
    if (trimmed.size() <= rootLength) {
        return {};
    }
    const size_t separator = trimmed.find_last_of("\\/");
    if (separator == std::string::npos) {
        return {};
    }
    if (separator < rootLength) {
        return FilePath(trimmed.substr(0, rootLength));
    }
    return FilePath(trimmed.substr(0, separator));
}

FilePath FilePath::parent_path() const {
    return ParentPath();
}

std::wstring FilePath::WideForNativeApi() const {
    return WideFromText(path_);
}

std::string FilePath::string() const {
    return path_;
}

FilePath JoinPath(const FilePath& base, const FilePath& child) {
    if (base.Empty() || child.IsAbsolute()) {
        return child;
    }
    if (child.Empty()) {
        return base;
    }
    const std::string baseText = base.string();
    const std::string childText = child.string();
    return FilePath(IsSeparator(baseText.back()) ?
            FormatText("%s%s", baseText.c_str(), childText.c_str()) :
            FormatText("%s\\%s", baseText.c_str(), childText.c_str()));
}

FilePath JoinPath(const FilePath& base, const char* child) {
    return JoinPath(base, FilePath(child));
}

FilePath operator/(const FilePath& base, const FilePath& child) {
    return JoinPath(base, child);
}

FilePath operator/(const FilePath& base, const char* child) {
    return JoinPath(base, child);
}

FilePath CurrentDirectoryPath() {
    DWORD length = GetCurrentDirectoryA(0, nullptr);
    if (length == 0) {
        return {};
    }
    std::string path(length, '\0');
    const DWORD written = GetCurrentDirectoryA(length, path.data());
    if (written == 0 || written >= length) {
        return {};
    }
    path.resize(written);
    return FilePath(path);
}

FilePath TempDirectoryPath() {
    DWORD length = GetTempPathA(0, nullptr);
    if (length == 0) {
        return {};
    }
    std::string path(length, '\0');
    const DWORD written = GetTempPathA(length, path.data());
    if (written == 0 || written >= length) {
        return {};
    }
    path.resize(written);
    return FilePath(path);
}

bool FileExists(const FilePath& path) {
    if (path.Empty()) {
        return false;
    }
    const DWORD attributes = GetFileAttributesA(path.string().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool RemoveFileIfExists(const FilePath& path) {
    if (path.Empty()) {
        return false;
    }
    if (DeleteFileA(path.string().c_str())) {
        return true;
    }
    return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

std::optional<std::string> ReadFileBinary(const FilePath& path) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.string().c_str(), kReadBinaryMode) != 0 || file == nullptr) {
        return std::nullopt;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return std::nullopt;
    }
    const long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return std::nullopt;
    }
    rewind(file);
    std::string content(static_cast<size_t>(length), '\0');
    if (!content.empty() && fread(content.data(), 1, content.size(), file) != content.size()) {
        fclose(file);
        return std::nullopt;
    }
    fclose(file);
    return content;
}

bool WriteFileBinary(const FilePath& path, std::string_view text) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.string().c_str(), kWriteBinaryMode) != 0 || file == nullptr) {
        return false;
    }
    const bool ok = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
    fclose(file);
    return ok;
}
