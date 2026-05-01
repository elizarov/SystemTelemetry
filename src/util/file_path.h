#pragma once

#include <optional>
#include <string>
#include <string_view>

class FilePath {
public:
    FilePath() = default;
    FilePath(const wchar_t* path);
    FilePath(const char* utf8Path);
    FilePath(std::wstring path);
    FilePath(std::string_view utf8Path);

    bool Empty() const;
    bool IsAbsolute() const;
    bool HasParentPath() const;
    FilePath ParentPath() const;
    bool empty() const;
    bool is_absolute() const;
    bool has_parent_path() const;
    FilePath parent_path() const;

    const std::wstring& Wide() const;
    const std::wstring& wstring() const;
    std::string string() const;
    const wchar_t* c_str() const;
    operator std::wstring() const;

private:
    std::wstring path_;
};

FilePath PathFromUtf8(std::string_view path);
std::string PathToUtf8(const FilePath& path);
FilePath JoinPath(const FilePath& base, const FilePath& child);
FilePath JoinPath(const FilePath& base, const wchar_t* child);
FilePath JoinPath(const FilePath& base, const char* child);
FilePath operator/(const FilePath& base, const FilePath& child);
FilePath operator/(const FilePath& base, const wchar_t* child);
FilePath operator/(const FilePath& base, const char* child);
FilePath operator/(const FilePath& base, const std::wstring& child);
FilePath CurrentDirectoryPath();
FilePath TempDirectoryPath();
bool FileExists(const FilePath& path);
bool RemoveFileIfExists(const FilePath& path);
std::optional<std::string> ReadFileBinary(const FilePath& path);
bool WriteFileBinary(const FilePath& path, std::string_view text);
