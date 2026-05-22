#pragma once

#include <optional>
#include <string>
#include <string_view>

class FilePath {
public:
    FilePath() = default;
    FilePath(const char* path);
    FilePath(std::string path);
    FilePath(std::string_view path);

    bool Empty() const;
    bool IsAbsolute() const;
    bool HasParentPath() const;
    FilePath ParentPath() const;
    bool empty() const;
    bool is_absolute() const;
    bool has_parent_path() const;
    FilePath parent_path() const;

    std::wstring WideForNativeApi() const;
    std::string string() const;

private:
    std::string path_;
};

FilePath JoinPath(const FilePath& base, const FilePath& child);
FilePath JoinPath(const FilePath& base, const char* child);
FilePath operator/(const FilePath& base, const FilePath& child);
FilePath operator/(const FilePath& base, const char* child);
FilePath CurrentDirectoryPath();
FilePath TempDirectoryPath();
bool FileExists(const FilePath& path);
bool RemoveFileIfExists(const FilePath& path);
std::optional<std::string> ReadFileBinary(const FilePath& path);
bool WriteFileBinary(const FilePath& path, std::string_view text);
