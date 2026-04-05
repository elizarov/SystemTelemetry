#include "config.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../resources/resource.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {

std::string Trim(const std::string& input) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(input.begin(), input.end(), isSpace);
    if (first == input.end()) {
        return {};
    }
    const auto last = std::find_if_not(input.rbegin(), input.rend(), isSpace).base();
    return std::string(first, last);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsAutoChannelValue(const std::string& value) {
    return value.empty() || value == "0" || ToLower(value) == "auto";
}

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(Trim(item));
    }
    return parts;
}

int ParseIntOrDefault(const std::string& value, int fallback) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

bool WriteFileUtf8(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

std::string LoadUtf8Resource(WORD resourceId, const wchar_t* resourceType) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return {};
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), resourceType);
    if (resource == nullptr) {
        return {};
    }

    HGLOBAL loadedResource = LoadResource(module, resource);
    if (loadedResource == nullptr) {
        return {};
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0) {
        return {};
    }

    const void* resourceData = LockResource(loadedResource);
    if (resourceData == nullptr) {
        return {};
    }

    std::string text(static_cast<const char*>(resourceData), static_cast<size_t>(resourceSize));
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

void ApplyConfigText(const std::string& text, AppConfig& config) {
    std::string section;
    std::stringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = ToLower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));

        if (section == "display" && key == "monitor_name") {
            config.monitorName = value;
        } else if (section == "display" && key == "position_x") {
            config.positionX = ParseIntOrDefault(value, 0);
        } else if (section == "display" && key == "position_y") {
            config.positionY = ParseIntOrDefault(value, 0);
        } else if (section == "network" && key == "adapter_name") {
            config.networkAdapter = value;
        } else if (section == "storage" && key == "drives") {
            config.driveLetters = Split(value, ',');
        } else if (section == "vendor.gigabyte" && key == "fan_channel") {
            config.gigabyteFanChannelName = IsAutoChannelValue(value) ? std::string() : value;
        } else if (section == "vendor.gigabyte" && key == "temperature_channel") {
            config.gigabyteTemperatureChannelName = IsAutoChannelValue(value) ? std::string() : value;
        }
    }
}

void ReplaceOrAppendKey(std::vector<std::string>& lines, size_t sectionStart, size_t sectionEnd,
    const std::string& key, const std::string& value) {
    const std::string normalizedKey = ToLower(key);
    for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (ToLower(Trim(trimmed.substr(0, eq))) == normalizedKey) {
            lines[i] = key + " = " + value;
            return;
        }
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), key + " = " + value);
}

}  // namespace

std::string LoadEmbeddedConfigTemplate() {
    return LoadUtf8Resource(IDR_CONFIG_TEMPLATE, RT_RCDATA);
}

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    ApplyConfigText(LoadEmbeddedConfigTemplate(), config);
    ApplyConfigText(ReadFileUtf8(path), config);

    if (config.driveLetters.empty()) {
        config.driveLetters = {"C", "D", "E"};
    }
    return config;
}

bool SaveConfig(const std::filesystem::path& path, const AppConfig& config) {
    std::string text = ReadFileUtf8(path);
    if (text.empty() && !std::filesystem::exists(path)) {
        text = LoadEmbeddedConfigTemplate();
    }
    std::vector<std::string> lines;
    {
        std::stringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
    }

    if (lines.empty()) {
        lines = {
            "[display]",
            "monitor_name = ",
            "position_x = 0",
            "position_y = 0",
            "",
            "[network]",
            "adapter_name = ",
            "",
            "[storage]",
            "drives = C,D,E",
            "",
            "[vendor.gigabyte]",
            "fan_channel = ",
            "temperature_channel = "
        };
    }

    const auto ensureSection = [&lines](const std::string& sectionName) -> size_t {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (Trim(lines[i]) == sectionName) {
                return i;
            }
        }
        if (!lines.empty() && !lines.back().empty()) {
            lines.push_back("");
        }
        lines.push_back(sectionName);
        return lines.size() - 1;
    };

    const auto findSectionEnd = [&lines](size_t sectionStart) -> size_t {
        size_t sectionEnd = lines.size();
        for (size_t j = sectionStart + 1; j < lines.size(); ++j) {
            const std::string next = Trim(lines[j]);
            if (!next.empty() && next.front() == '[' && next.back() == ']') {
                sectionEnd = j;
                break;
            }
        }
        return sectionEnd;
    };

    auto updateKey = [&lines, &ensureSection, &findSectionEnd](const std::string& sectionName,
        const std::string& key, const std::string& value) {
        size_t sectionStart = ensureSection(sectionName);
        if (Trim(lines[sectionStart]) != sectionName) {
            lines[sectionStart] = sectionName;
        }
        const size_t sectionEnd = findSectionEnd(sectionStart);
        ReplaceOrAppendKey(lines, sectionStart, sectionEnd, key, value);
    };

    updateKey("[display]", "monitor_name", config.monitorName);
    updateKey("[display]", "position_x", std::to_string(config.positionX));
    updateKey("[display]", "position_y", std::to_string(config.positionY));
    updateKey("[network]", "adapter_name", config.networkAdapter);

    std::string drives;
    for (size_t i = 0; i < config.driveLetters.size(); ++i) {
        if (i > 0) {
            drives += ",";
        }
        drives += config.driveLetters[i];
    }
    updateKey("[storage]", "drives", drives);
    updateKey("[vendor.gigabyte]", "fan_channel", config.gigabyteFanChannelName);
    updateKey("[vendor.gigabyte]", "temperature_channel", config.gigabyteTemperatureChannelName);

    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += "\r\n";
    }
    return WriteFileUtf8(path, output);
}
