#include "config.h"

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

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    const std::string text = ReadFileUtf8(path);
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
        }
    }

    if (config.driveLetters.empty()) {
        config.driveLetters = {"C", "D", "E"};
    }
    return config;
}

bool SaveDisplayConfig(
    const std::filesystem::path& path,
    const std::string& monitorName,
    int positionX,
    int positionY) {
    const std::string text = ReadFileUtf8(path);
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
            "; System Telemetry dashboard configuration example.",
            "; Keep this file in sync with supported configuration fields.",
            "; monitor_name matches the display identifier used by the app.",
            "; position_x and position_y are the window's relative top-left coordinates on that monitor.",
            "",
            "[display]"
        };
    }

    size_t sectionStart = lines.size();
    size_t sectionEnd = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (trimmed == "[display]") {
            sectionStart = i;
            sectionEnd = lines.size();
            for (size_t j = i + 1; j < lines.size(); ++j) {
                const std::string next = Trim(lines[j]);
                if (!next.empty() && next.front() == '[' && next.back() == ']') {
                    sectionEnd = j;
                    break;
                }
            }
            break;
        }
    }

    if (sectionStart == lines.size()) {
        if (!lines.empty() && !lines.back().empty()) {
            lines.push_back("");
        }
        sectionStart = lines.size();
        lines.push_back("[display]");
        sectionEnd = lines.size();
    }

    ReplaceOrAppendKey(lines, sectionStart, sectionEnd, "monitor_name", monitorName);
    sectionEnd = lines.size();
    for (size_t j = sectionStart + 1; j < lines.size(); ++j) {
        const std::string next = Trim(lines[j]);
        if (!next.empty() && next.front() == '[' && next.back() == ']') {
            sectionEnd = j;
            break;
        }
    }

    ReplaceOrAppendKey(lines, sectionStart, sectionEnd, "position_x", std::to_string(positionX));
    sectionEnd = lines.size();
    for (size_t j = sectionStart + 1; j < lines.size(); ++j) {
        const std::string next = Trim(lines[j]);
        if (!next.empty() && next.front() == '[' && next.back() == ']') {
            sectionEnd = j;
            break;
        }
    }

    ReplaceOrAppendKey(lines, sectionStart, sectionEnd, "position_y", std::to_string(positionY));

    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += "\r\n";
    }
    return WriteFileUtf8(path, output);
}
