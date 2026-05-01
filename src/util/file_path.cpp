#include "util/file_path.h"

#include <windows.h>

#include <cstdio>
#include <utility>

#include "util/utf8.h"

namespace {

bool IsSeparator(wchar_t ch) {
    return ch == L'\\' || ch == L'/';
}

bool HasDrivePrefix(std::wstring_view path) {
    return path.size() >= 2 && path[1] == L':' &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'));
}

size_t RootLength(std::wstring_view path) {
    if (path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1])) {
        size_t serverEnd = path.find_first_of(L"\\/", 2);
        if (serverEnd == std::wstring_view::npos) {
            return path.size();
        }
        size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
        return shareEnd == std::wstring_view::npos ? path.size() : shareEnd + 1;
    }
    if (HasDrivePrefix(path)) {
        return path.size() >= 3 && IsSeparator(path[2]) ? 3 : 2;
    }
    return !path.empty() && IsSeparator(path[0]) ? 1 : 0;
}

std::wstring TrimTrailingSeparators(std::wstring path) {
    const size_t rootLength = RootLength(path);
    while (path.size() > rootLength && IsSeparator(path.back())) {
        path.pop_back();
    }
    return path;
}

}  // namespace

FilePath::FilePath(const wchar_t* path) : path_(path != nullptr ? path : L"") {}

FilePath::FilePath(const char* utf8Path)
    : path_(WideFromUtf8(utf8Path != nullptr ? std::string_view(utf8Path) : std::string_view())) {}

FilePath::FilePath(std::wstring path) : path_(std::move(path)) {}

FilePath::FilePath(std::string_view utf8Path) : path_(WideFromUtf8(utf8Path)) {}

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
    std::wstring trimmed = TrimTrailingSeparators(path_);
    const size_t rootLength = RootLength(trimmed);
    if (trimmed.size() <= rootLength) {
        return {};
    }
    const size_t separator = trimmed.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
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

const std::wstring& FilePath::Wide() const {
    return path_;
}

const std::wstring& FilePath::wstring() const {
    return Wide();
}

std::string FilePath::string() const {
    return Utf8FromWide(path_);
}

const wchar_t* FilePath::c_str() const {
    return path_.c_str();
}

FilePath::operator std::wstring() const {
    return path_;
}

FilePath PathFromUtf8(std::string_view path) {
    return FilePath(WideFromUtf8(path));
}

std::string PathToUtf8(const FilePath& path) {
    return Utf8FromWide(path.Wide());
}

FilePath JoinPath(const FilePath& base, const FilePath& child) {
    if (base.Empty() || child.IsAbsolute()) {
        return child;
    }
    if (child.Empty()) {
        return base;
    }
    std::wstring joined = base.Wide();
    if (!joined.empty() && !IsSeparator(joined.back())) {
        joined.push_back(L'\\');
    }
    joined += child.Wide();
    return FilePath(std::move(joined));
}

FilePath JoinPath(const FilePath& base, const wchar_t* child) {
    return JoinPath(base, FilePath(child));
}

FilePath JoinPath(const FilePath& base, const char* child) {
    return JoinPath(base, FilePath(child));
}

FilePath operator/(const FilePath& base, const FilePath& child) {
    return JoinPath(base, child);
}

FilePath operator/(const FilePath& base, const wchar_t* child) {
    return JoinPath(base, child);
}

FilePath operator/(const FilePath& base, const char* child) {
    return JoinPath(base, child);
}

FilePath operator/(const FilePath& base, const std::wstring& child) {
    return JoinPath(base, FilePath(child));
}

FilePath CurrentDirectoryPath() {
    DWORD length = GetCurrentDirectoryW(0, nullptr);
    if (length == 0) {
        return {};
    }
    std::wstring path(length, L'\0');
    const DWORD written = GetCurrentDirectoryW(length, path.data());
    if (written == 0 || written >= length) {
        return {};
    }
    path.resize(written);
    return FilePath(std::move(path));
}

FilePath TempDirectoryPath() {
    DWORD length = GetTempPathW(0, nullptr);
    if (length == 0) {
        return {};
    }
    std::wstring path(length, L'\0');
    const DWORD written = GetTempPathW(length, path.data());
    if (written == 0 || written >= length) {
        return {};
    }
    path.resize(written);
    return FilePath(std::move(path));
}

bool FileExists(const FilePath& path) {
    if (path.Empty()) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool RemoveFileIfExists(const FilePath& path) {
    if (path.Empty()) {
        return false;
    }
    if (DeleteFileW(path.c_str())) {
        return true;
    }
    return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

std::optional<std::string> ReadFileBinary(const FilePath& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
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
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        return false;
    }
    const bool ok = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
    fclose(file);
    return ok;
}
