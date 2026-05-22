#include "tools/impl/lint_common.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace tools::lint {

namespace {

bool IsSeparator(char ch) {
    return ch == '\\' || ch == '/';
}

bool IsDrivePrefix(std::string_view path) {
    return path.size() >= 2 && path[1] == ':' &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}

bool IsAbsolutePath(std::string_view path) {
    return path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1]) ||
        path.size() >= 3 && IsDrivePrefix(path) && IsSeparator(path[2]) || !path.empty() && IsSeparator(path[0]);
}

std::string QuoteCommandArgument(std::string_view value) {
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
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
    const std::string parent = ParentPath(path);
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
    const std::string pattern = JoinPath(root, "*");
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
        const std::string path = JoinPath(root, name);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            RecursiveFilesInto(path, files);
        } else {
            files.push_back(AbsolutePath(path));
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

}  // namespace

int FileRecord::LineCount() const {
    return static_cast<int>(lines.size());
}

bool CheckResult::Failed() const {
    return !findings.empty() || !errors.empty();
}

std::string CurrentDirectoryAbsolute() {
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
    return AbsolutePath(path);
}

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

std::string ParentPath(std::string_view path) {
    std::string value = TrimTrailingSeparators(std::string(path));
    const size_t separator = value.find_last_of("\\/");
    if (separator == std::string::npos) {
        return {};
    }
    if (separator == 2 && IsDrivePrefix(value)) {
        return value.substr(0, 3);
    }
    if (separator == 0) {
        return value.substr(0, 1);
    }
    return value.substr(0, separator);
}

std::string JoinPath(std::string_view base, std::string_view child) {
    if (base.empty() || IsAbsolutePath(child)) {
        return std::string(child);
    }
    if (child.empty()) {
        return std::string(base);
    }
    std::string joined(base);
    if (!IsSeparator(joined.back())) {
        joined.push_back('\\');
    }
    joined += child;
    return joined;
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
    const std::string lowerPath = ToLowerAscii(normalizedPath);
    const std::string lowerRoot = ToLowerAscii(normalizedRoot);
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
    return ToLowerAscii(NormalizeSeparators(AbsolutePath(path)));
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

bool FileExists(std::string_view path) {
    const DWORD attributes = GetFileAttributesA(std::string(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(std::string_view path) {
    const DWORD attributes = GetFileAttributesA(std::string(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<std::string> ReadFileText(std::string_view path) {
    FILE* file = nullptr;
    if (fopen_s(&file, std::string(path).c_str(), "rb") != 0 || file == nullptr) {
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

bool WriteFileText(std::string_view path, std::string_view text) {
    if (!EnsureParentDirectory(path)) {
        return false;
    }
    FILE* file = nullptr;
    if (fopen_s(&file, std::string(path).c_str(), "wb") != 0 || file == nullptr) {
        return false;
    }
    const bool ok = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
    fclose(file);
    return ok;
}

bool EnsureParentDirectory(std::string_view path) {
    const std::string parent = ParentPath(path);
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

std::string Trim(std::string_view value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string ToLowerAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
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

std::vector<std::string> ConfigStrings(const JsonValue& config, std::string_view key) {
    const JsonValue* value = config.Find(key);
    if (value == nullptr || value->IsNull()) {
        return {};
    }
    std::vector<std::string> strings;
    for (const JsonValue& item : value->AsArray()) {
        strings.push_back(item.AsString());
    }
    return strings;
}

std::set<std::string> RequireSuffixGroup(
    const std::map<std::string, std::set<std::string>>& suffixGroups,
    std::string_view configPath,
    std::string_view groupName) {
    const auto found = suffixGroups.find(std::string(groupName));
    if (found == suffixGroups.end()) {
        throw std::runtime_error(
            std::string(configPath) + " references unknown suffix group " + std::string(groupName));
    }
    return found->second;
}

std::optional<std::vector<std::string>> RunGitLsFiles(const std::vector<std::string>& args) {
    std::string command = "git ls-files";
    for (const std::string& arg : args) {
        command.push_back(' ');
        command += QuoteCommandArgument(arg);
    }
    command += " 2>nul";

    FILE* pipe = _popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = _pclose(pipe);
    if (status != 0) {
        return std::nullopt;
    }

    std::vector<std::string> lines;
    for (std::string line : SplitLines(output)) {
        line = Trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

}  // namespace tools::lint
