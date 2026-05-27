#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tools {

std::string CurrentDirectoryAbsolute();
std::string ExecutablePath();
std::string ParentPath(std::string_view path);
std::string JoinPath(std::string_view base, std::string_view child);
std::string AbsolutePath(std::string_view path);
std::string RelativePath(std::string_view path, std::string_view root);
std::string NormalizeSeparators(std::string value);
std::string NormalizePathKey(std::string_view path);
std::string Extension(std::string_view path);
std::string RemoveExtension(std::string_view path);
bool FileExists(std::string_view path);
bool DirectoryExists(std::string_view path);
std::optional<std::string> ReadFileText(std::string_view path);
bool WriteFileText(std::string_view path, std::string_view text);
bool EnsureParentDirectory(std::string_view path);
std::optional<std::uint64_t> LastWriteTime(std::string_view path);
std::vector<std::string> RecursiveFiles(std::string_view root);

std::string Trim(std::string_view value);
std::string ToLowerAscii(std::string value);
bool StartsWith(std::string_view value, std::string_view prefix);
bool EndsWith(std::string_view value, std::string_view suffix);
bool Contains(std::string_view value, std::string_view needle);
std::vector<std::string> SplitLines(std::string_view text);
std::vector<std::string> Split(std::string_view text, char delimiter);
std::string ReplaceAll(std::string value, std::string_view from, std::string_view to);
std::string CollapseWhitespace(const std::vector<std::string>& parts);
std::string NormalizeInclude(std::string value);
bool HasRoot(std::string_view relative, const std::vector<std::string>& roots);
bool IsExcluded(std::string_view relative, const std::vector<std::string>& prefixes);
std::string FormatCount(int value);
std::string StripCommentsAndStrings(std::string_view text);

}  // namespace tools
