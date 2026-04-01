#include "config.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cwctype>
#include <sstream>

namespace {

std::wstring Trim(const std::wstring& input) {
    const auto first = input.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = input.find_last_not_of(L" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::wstring ToLower(std::wstring value) {
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

std::vector<std::wstring> Split(const std::wstring& input, wchar_t delimiter) {
    std::vector<std::wstring> parts;
    std::wstringstream stream(input);
    std::wstring item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(Trim(item));
    }
    return parts;
}

int ParseIntOrDefault(const std::wstring& value, int fallback) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::wstring ReadFileUtf8(const std::filesystem::path& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
        return L"";
    }

    std::string bytes;
    std::array<char, 4096> buffer{};
    while (true) {
        const size_t read = fread(buffer.data(), 1, buffer.size(), file);
        if (read == 0) {
            break;
        }
        bytes.append(buffer.data(), read);
    }
    fclose(file);

    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring result(required, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
        result.data(), required);
    return result;
}

bool WriteFileUtf8(const std::filesystem::path& path, const std::wstring& text) {
    const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return false;
    }

    std::string bytes(required, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        bytes.data(), required, nullptr, nullptr);

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        return false;
    }

    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, sizeof(bom), file);
    const size_t written = fwrite(bytes.data(), 1, bytes.size(), file);
    fclose(file);
    return written == bytes.size();
}

void ReplaceOrAppendKey(std::vector<std::wstring>& lines, size_t sectionStart, size_t sectionEnd,
    const std::wstring& key, const std::wstring& value) {
    const std::wstring normalizedKey = ToLower(key);
    for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
        const std::wstring trimmed = Trim(lines[i]);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#') {
            continue;
        }
        const size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }
        if (ToLower(Trim(trimmed.substr(0, eq))) == normalizedKey) {
            lines[i] = key + L" = " + value;
            return;
        }
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), key + L" = " + value);
}

}  // namespace

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    const std::wstring text = ReadFileUtf8(path);
    std::wstring section;
    std::wstringstream stream(text);
    std::wstring line;

    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }

        const std::wstring key = ToLower(Trim(line.substr(0, eq)));
        const std::wstring value = Trim(line.substr(eq + 1));

        if (section == L"display" && key == L"monitor_name") {
            config.monitorName = value;
        } else if (section == L"display" && key == L"position_x") {
            config.positionX = ParseIntOrDefault(value, 0);
        } else if (section == L"display" && key == L"position_y") {
            config.positionY = ParseIntOrDefault(value, 0);
        } else if (section == L"network" && key == L"adapter_name") {
            config.networkAdapter = value;
        } else if (section == L"storage" && key == L"drives") {
            config.driveLetters = Split(value, L',');
        } else if (section == L"sensors") {
            auto parts = Split(value, L'|');
            if (parts.size() < 4) {
                continue;
            }

            SensorBinding binding;
            if (ToLower(parts[0]) == L"auto" || parts[0].empty()) {
                binding.namespaces = {L"root\\LibreHardwareMonitor", L"root\\OpenHardwareMonitor"};
            } else {
                binding.namespaces = Split(parts[0], L',');
            }
            binding.matchField = parts[1];
            binding.matchValue = parts[2];
            binding.valueField = parts[3];
            config.sensors[key] = binding;
        }
    }

    if (config.driveLetters.empty()) {
        config.driveLetters = {L"C", L"D", L"E"};
    }
    return config;
}

bool SaveDisplayConfig(
    const std::filesystem::path& path,
    const std::wstring& monitorName,
    int positionX,
    int positionY) {
    std::wstring text = ReadFileUtf8(path);
    std::vector<std::wstring> lines;
    {
        std::wstringstream stream(text);
        std::wstring line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
    }

    if (lines.empty()) {
        lines = {
            L"; System Telemetry dashboard configuration example.",
            L"; Keep this file in sync with supported configuration fields.",
            L"; monitor_name matches the display identifier used by the app.",
            L"; position_x and position_y are the window's relative top-left coordinates on that monitor.",
            L"",
            L"[display]"
        };
    }

    size_t sectionStart = lines.size();
    size_t sectionEnd = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring trimmed = Trim(lines[i]);
        if (trimmed == L"[display]") {
            sectionStart = i;
            sectionEnd = lines.size();
            for (size_t j = i + 1; j < lines.size(); ++j) {
                const std::wstring next = Trim(lines[j]);
                if (!next.empty() && next.front() == L'[' && next.back() == L']') {
                    sectionEnd = j;
                    break;
                }
            }
            break;
        }
    }

    if (sectionStart == lines.size()) {
        if (!lines.empty() && !lines.back().empty()) {
            lines.push_back(L"");
        }
        sectionStart = lines.size();
        lines.push_back(L"[display]");
        sectionEnd = lines.size();
    }

    ReplaceOrAppendKey(lines, sectionStart, sectionEnd, L"monitor_name", monitorName);
    sectionEnd = lines.size();
    for (size_t j = sectionStart + 1; j < lines.size(); ++j) {
        const std::wstring next = Trim(lines[j]);
        if (!next.empty() && next.front() == L'[' && next.back() == L']') {
            sectionEnd = j;
            break;
        }
    }

    ReplaceOrAppendKey(lines, sectionStart, sectionEnd, L"position_x", std::to_wstring(positionX));
    sectionEnd = lines.size();
    for (size_t j = sectionStart + 1; j < lines.size(); ++j) {
        const std::wstring next = Trim(lines[j]);
        if (!next.empty() && next.front() == L'[' && next.back() == L']') {
            sectionEnd = j;
            break;
        }
    }

    ReplaceOrAppendKey(lines, sectionStart, sectionEnd, L"position_y", std::to_wstring(positionY));

    std::wstring output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += L"\r\n";
    }
    return WriteFileUtf8(path, output);
}
