#include "tools/impl/tools_common.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "util/file_path.h"
#include "util/strings.h"

namespace {

bool IsSeparator(char ch) {
    return ch == '\\' || ch == '/';
}

bool IsDrivePrefix(std::string_view path) {
    return path.size() >= 2 &&
        path[1] == ':' &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}

std::string TrimTrailingSeparators(std::string value) {
    while (value.size() > 1 && IsSeparator(value.back())) {
        if (value.size() == 3 && IsDrivePrefix(value)) {
            break;
        }
        value.pop_back();
    }
    return value;
}

bool CreateDirectoryTree(std::string_view path) {
    if (path.empty() || DirectoryExists(path)) {
        return true;
    }
    const std::string parent = FilePath(path).ParentPath().string();
    if (!parent.empty() && parent != path && !CreateDirectoryTree(parent)) {
        return false;
    }
    if (CreateDirectoryA(std::string(path).c_str(), nullptr)) {
        return true;
    }
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS;
}

void RecursiveFilesInto(std::string_view root, std::vector<std::string>& files) {
    const std::string pattern = (FilePath(root) / "*").string();
    WIN32_FIND_DATAA data{};
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::string name = data.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        const std::string path = (FilePath(root) / name).string();
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            RecursiveFilesInto(path, files);
        } else {
            files.push_back(AbsolutePath(path));
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

}  // namespace

std::string ExecutablePath() {
    std::string path(MAX_PATH, '\0');
    DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (length == path.size()) {
        path.resize(path.size() * 2);
        length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(length);
    return AbsolutePath(path);
}

std::string AbsolutePath(std::string_view path) {
    DWORD length = GetFullPathNameA(std::string(path).c_str(), 0, nullptr, nullptr);
    if (length == 0) {
        return NormalizeSeparators(std::string(path));
    }
    std::string absolute(length, '\0');
    const DWORD written = GetFullPathNameA(std::string(path).c_str(), length, absolute.data(), nullptr);
    if (written == 0 || written >= length) {
        return NormalizeSeparators(std::string(path));
    }
    absolute.resize(written);
    return NormalizeSeparators(absolute);
}

std::string RelativePath(std::string_view path, std::string_view root) {
    const std::string normalizedPath = NormalizeSeparators(AbsolutePath(path));
    std::string normalizedRoot = TrimTrailingSeparators(NormalizeSeparators(AbsolutePath(root)));
    const std::string lowerPath = ToLower(normalizedPath);
    const std::string lowerRoot = ToLower(normalizedRoot);
    if (lowerPath == lowerRoot) {
        return {};
    }
    if (StartsWith(lowerPath, lowerRoot + "/")) {
        return normalizedPath.substr(normalizedRoot.size() + 1);
    }
    return normalizedPath;
}

std::string NormalizeSeparators(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string NormalizePathKey(std::string_view path) {
    return ToLower(NormalizeSeparators(AbsolutePath(path)));
}

std::string Extension(std::string_view path) {
    const size_t slash = path.find_last_of("\\/");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    return std::string(path.substr(dot));
}

std::string RemoveExtension(std::string_view path) {
    const size_t slash = path.find_last_of("\\/");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return std::string(path);
    }
    return std::string(path.substr(0, dot));
}

bool DirectoryExists(std::string_view path) {
    const DWORD attributes = GetFileAttributesA(std::string(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureParentDirectory(std::string_view path) {
    const std::string parent = FilePath(path).ParentPath().string();
    return parent.empty() || CreateDirectoryTree(parent);
}

std::optional<std::uint64_t> LastWriteTime(std::string_view path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExA(std::string(path).c_str(), GetFileExInfoStandard, &data)) {
        return std::nullopt;
    }
    ULARGE_INTEGER value{};
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return value.QuadPart;
}

std::vector<std::string> RecursiveFiles(std::string_view root) {
    std::vector<std::string> files;
    RecursiveFilesInto(root, files);
    return files;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

bool Contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

std::vector<std::string> SplitLines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    size_t index = 0;
    while (index < text.size()) {
        if (text[index] == '\r' || text[index] == '\n') {
            lines.emplace_back(text.substr(start, index - start));
            if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            ++index;
            start = index;
            continue;
        }
        ++index;
    }
    if (start < text.size()) {
        lines.emplace_back(text.substr(start));
    }
    return lines;
}

std::vector<std::string> Split(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t index = text.find(delimiter, start);
        if (index == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, index - start));
        start = index + 1;
    }
    return parts;
}

std::string ReplaceAll(std::string value, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return value;
    }
    size_t index = 0;
    while ((index = value.find(from, index)) != std::string::npos) {
        value.replace(index, from.size(), to);
        index += to.size();
    }
    return value;
}

std::string CollapseWhitespace(const std::vector<std::string>& parts) {
    std::string result;
    for (const std::string& part : parts) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += part;
    }
    return result;
}

std::string NormalizeInclude(std::string value) {
    return NormalizeSeparators(std::move(value));
}

bool HasRoot(std::string_view relative, const std::vector<std::string>& roots) {
    for (const std::string& root : roots) {
        if (relative == root || StartsWith(relative, root + "/")) {
            return true;
        }
    }
    return false;
}

bool IsExcluded(std::string_view relative, const std::vector<std::string>& prefixes) {
    for (const std::string& prefix : prefixes) {
        if (StartsWith(relative, prefix)) {
            return true;
        }
    }
    return false;
}

std::string FormatCount(int value) {
    std::string digits = std::to_string(value);
    std::string formatted;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count == 3) {
            formatted.push_back(',');
            count = 0;
        }
        formatted.push_back(*it);
        ++count;
    }
    std::reverse(formatted.begin(), formatted.end());
    return formatted;
}

std::string StripCommentsAndStrings(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool inChar = false;
    while (i < text.size()) {
        const char ch = text[i];
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (inLineComment) {
            if (ch == '\n') {
                inLineComment = false;
                result.push_back('\n');
            } else {
                result.push_back(' ');
            }
            ++i;
            continue;
        }
        if (inBlockComment) {
            if (ch == '*' && next == '/') {
                result += "  ";
                inBlockComment = false;
                i += 2;
            } else {
                result.push_back(ch == '\n' ? '\n' : ' ');
                ++i;
            }
            continue;
        }
        if (inString) {
            if (ch == '\\' && next != '\0') {
                result += "  ";
                i += 2;
                continue;
            }
            result.push_back(ch == '\n' ? '\n' : ' ');
            if (ch == '"') {
                inString = false;
            }
            ++i;
            continue;
        }
        if (inChar) {
            if (ch == '\\' && next != '\0') {
                result += "  ";
                i += 2;
                continue;
            }
            result.push_back(ch == '\n' ? '\n' : ' ');
            if (ch == '\'') {
                inChar = false;
            }
            ++i;
            continue;
        }
        if (ch == '/' && next == '/') {
            result += "  ";
            inLineComment = true;
            i += 2;
            continue;
        }
        if (ch == '/' && next == '*') {
            result += "  ";
            inBlockComment = true;
            i += 2;
            continue;
        }
        if (ch == '"') {
            result.push_back(' ');
            inString = true;
            ++i;
            continue;
        }
        if (ch == '\'') {
            result.push_back(' ');
            inChar = true;
            ++i;
            continue;
        }
        result.push_back(ch);
        ++i;
    }
    return result;
}
