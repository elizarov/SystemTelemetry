#include "config.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../resources/resource.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <set>
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
        const std::string trimmed = Trim(item);
        if (!trimmed.empty()) {
            parts.push_back(trimmed);
        }
    }
    return parts;
}

std::vector<std::string> SplitTopLevel(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char ch : input) {
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            depth = std::max(0, depth - 1);
        }

        if (ch == delimiter && depth == 0) {
            const std::string trimmed = Trim(current);
            if (!trimmed.empty()) {
                parts.push_back(trimmed);
            }
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    const std::string trimmed = Trim(current);
    if (!trimmed.empty()) {
        parts.push_back(trimmed);
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

unsigned int ParseHexColorOrDefault(const std::string& value, unsigned int fallback) {
    std::string text = Trim(value);
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 6) {
        return fallback;
    }
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) {
            return fallback;
        }
    }
    try {
        return static_cast<unsigned int>(std::stoul(text, nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

std::string FormatHexColor(unsigned int color) {
    std::ostringstream stream;
    stream << '#' << std::uppercase << std::hex << std::setfill('0') << std::setw(6) << (color & 0xFFFFFFu);
    return stream.str();
}

void ApplyFontValue(UiFontConfig& font, const std::string& key, const std::string& value) {
    if (key == "face") {
        font.face = value;
    } else if (key == "size") {
        font.size = ParseIntOrDefault(value, font.size);
    } else if (key == "weight") {
        font.weight = ParseIntOrDefault(value, font.weight);
    }
}

bool ParseIntPair(const std::string& value, int& first, int& second) {
    const std::vector<std::string> parts = Split(value, ',');
    if (parts.size() != 2) {
        return false;
    }
    first = ParseIntOrDefault(parts[0], first);
    second = ParseIntOrDefault(parts[1], second);
    return true;
}

bool ParseIntTriple(const std::string& value, int& first, int& second, int& third) {
    const std::vector<std::string> parts = Split(value, ',');
    if (parts.size() != 3) {
        return false;
    }
    first = ParseIntOrDefault(parts[0], first);
    second = ParseIntOrDefault(parts[1], second);
    third = ParseIntOrDefault(parts[2], third);
    return true;
}

void ParseFontSpec(UiFontConfig& font, const std::string& value) {
    const std::vector<std::string> parts = Split(value, ',');
    if (parts.size() != 3) {
        return;
    }
    font.face = parts[0];
    font.size = ParseIntOrDefault(parts[1], font.size);
    font.weight = ParseIntOrDefault(parts[2], font.weight);
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

class LayoutExpressionParser {
public:
    explicit LayoutExpressionParser(const std::string& text) : text_(text) {}

    bool ParseNode(LayoutNodeConfig& node) {
        SkipWhitespace();
        if (!ParseIdentifier(node.name)) {
            return false;
        }
        SkipWhitespace();
        if (Consume(':')) {
            SkipWhitespace();
            node.weight = ParseInteger(node.weight);
            SkipWhitespace();
        }
        if (Consume('(')) {
            if (IsContainer(node.name)) {
                if (!ParseChildren(node.children)) {
                    return false;
                }
            } else {
                if (!ParseParameters(node.parameters)) {
                    return false;
                }
            }
            SkipWhitespace();
            if (!Consume(')')) {
                return false;
            }
        }
        SkipWhitespace();
        if (Consume('*')) {
            SkipWhitespace();
            node.weight = ParseInteger(node.weight);
        }
        SkipWhitespace();
        return !node.name.empty();
    }

    bool AtEnd() {
        SkipWhitespace();
        return index_ >= text_.size();
    }

private:
    static bool IsContainer(const std::string& name) {
        const std::string lowered = ToLower(name);
        return lowered == "rows" || lowered == "columns" || lowered == "stack" ||
            lowered == "stack_top" || lowered == "center";
    }

    void SkipWhitespace() {
        while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_])) != 0) {
            ++index_;
        }
    }

    bool Consume(char ch) {
        if (index_ < text_.size() && text_[index_] == ch) {
            ++index_;
            return true;
        }
        return false;
    }

    bool ParseIdentifier(std::string& identifier) {
        const size_t begin = index_;
        while (index_ < text_.size()) {
            const char ch = text_[index_];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '-') {
                ++index_;
            } else {
                break;
            }
        }
        identifier = text_.substr(begin, index_ - begin);
        return !identifier.empty();
    }

    int ParseInteger(int fallback) {
        const size_t begin = index_;
        while (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) {
            ++index_;
        }
        if (begin == index_) {
            return fallback;
        }
        return ParseIntOrDefault(text_.substr(begin, index_ - begin), fallback);
    }

    bool ParseChildren(std::vector<LayoutNodeConfig>& children);
    bool ParseParameters(std::vector<std::pair<std::string, std::string>>& parameters);

    std::string text_;
    size_t index_ = 0;
};

bool LayoutExpressionParser::ParseChildren(std::vector<LayoutNodeConfig>& children) {
    while (true) {
        LayoutNodeConfig child;
        if (!ParseNode(child)) {
            return false;
        }
        children.push_back(std::move(child));
        SkipWhitespace();
        if (index_ >= text_.size() || text_[index_] == ')') {
            return true;
        }
        if (!Consume(',')) {
            return false;
        }
        SkipWhitespace();
    }
}

bool LayoutExpressionParser::ParseParameters(std::vector<std::pair<std::string, std::string>>& parameters) {
    SkipWhitespace();
    if (index_ >= text_.size() || text_[index_] == ')') {
        return true;
    }

    const size_t begin = index_;
    int depth = 0;
    while (index_ < text_.size()) {
        const char ch = text_[index_];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            if (depth == 0) {
                break;
            }
            --depth;
        }
        ++index_;
    }

    const std::string body = Trim(text_.substr(begin, index_ - begin));
    if (body.empty()) {
        return true;
    }

    const size_t eq = body.find('=');
    if (eq == std::string::npos) {
        parameters.emplace_back("value", body);
    } else {
        parameters.emplace_back(ToLower(Trim(body.substr(0, eq))), Trim(body.substr(eq + 1)));
    }
    return true;
}

bool ParseLayoutExpression(const std::string& text, LayoutNodeConfig& node) {
    node = {};
    LayoutExpressionParser parser(text);
    if (!parser.ParseNode(node)) {
        return false;
    }
    return parser.AtEnd();
}

LayoutCardConfig* FindCardConfig(LayoutConfig& layout, const std::string& id) {
    for (auto& card : layout.cards) {
        if (ToLower(card.id) == ToLower(id)) {
            return &card;
        }
    }
    return nullptr;
}

LayoutCardConfig& EnsureCardConfig(LayoutConfig& layout, const std::string& id) {
    if (LayoutCardConfig* card = FindCardConfig(layout, id)) {
        return *card;
    }
    layout.cards.push_back(LayoutCardConfig{id});
    return layout.cards.back();
}

void ApplyLayoutValue(LayoutConfig& layout, const std::string& key, const std::string& value) {
    if (key == "window") {
        ParseIntPair(value, layout.windowWidth, layout.windowHeight);
    } else if (key == "window_width") {
        layout.windowWidth = ParseIntOrDefault(value, layout.windowWidth);
    } else if (key == "window_height") {
        layout.windowHeight = ParseIntOrDefault(value, layout.windowHeight);
    } else if (key == "background_color") {
        layout.backgroundColor = ParseHexColorOrDefault(value, layout.backgroundColor);
    } else if (key == "foreground_color") {
        layout.foregroundColor = ParseHexColorOrDefault(value, layout.foregroundColor);
    } else if (key == "accent_color") {
        layout.accentColor = ParseHexColorOrDefault(value, layout.accentColor);
    } else if (key == "panel_border_color") {
        layout.panelBorderColor = ParseHexColorOrDefault(value, layout.panelBorderColor);
    } else if (key == "muted_text_color") {
        layout.mutedTextColor = ParseHexColorOrDefault(value, layout.mutedTextColor);
    } else if (key == "track_color") {
        layout.trackColor = ParseHexColorOrDefault(value, layout.trackColor);
    } else if (key == "panel_fill_color") {
        layout.panelFillColor = ParseHexColorOrDefault(value, layout.panelFillColor);
    } else if (key == "graph_background_color") {
        layout.graphBackgroundColor = ParseHexColorOrDefault(value, layout.graphBackgroundColor);
    } else if (key == "graph_grid_color") {
        layout.graphGridColor = ParseHexColorOrDefault(value, layout.graphGridColor);
    } else if (key == "graph_axis_color") {
        layout.graphAxisColor = ParseHexColorOrDefault(value, layout.graphAxisColor);
    } else if (key == "outer_margin") {
        layout.outerMargin = ParseIntOrDefault(value, layout.outerMargin);
    } else if (key == "row_gap") {
        layout.rowGap = ParseIntOrDefault(value, layout.rowGap);
    } else if (key == "card_gap") {
        layout.cardGap = ParseIntOrDefault(value, layout.cardGap);
    } else if (key == "card_padding") {
        layout.cardPadding = ParseIntOrDefault(value, layout.cardPadding);
    } else if (key == "card_radius") {
        layout.cardRadius = ParseIntOrDefault(value, layout.cardRadius);
    } else if (key == "card_border" || key == "card_border_width") {
        layout.cardBorderWidth = ParseIntOrDefault(value, layout.cardBorderWidth);
    } else if (key == "header") {
        ParseIntTriple(value, layout.headerHeight, layout.headerIconSize, layout.headerGap);
    } else if (key == "header_height") {
        layout.headerHeight = ParseIntOrDefault(value, layout.headerHeight);
    } else if (key == "header_icon_size") {
        layout.headerIconSize = ParseIntOrDefault(value, layout.headerIconSize);
    } else if (key == "header_gap") {
        layout.headerGap = ParseIntOrDefault(value, layout.headerGap);
    } else if (key == "content_gap") {
        layout.contentGap = ParseIntOrDefault(value, layout.contentGap);
    } else if (key == "column_gap") {
        layout.columnGap = ParseIntOrDefault(value, layout.columnGap);
    } else if (key == "metric_row_height") {
        layout.metricRowHeight = ParseIntOrDefault(value, layout.metricRowHeight);
    } else if (key == "metric_label_width") {
        layout.metricLabelWidth = ParseIntOrDefault(value, layout.metricLabelWidth);
    } else if (key == "metric_value_gap") {
        layout.metricValueGap = ParseIntOrDefault(value, layout.metricValueGap);
    } else if (key == "metric_bar_height") {
        layout.metricBarHeight = ParseIntOrDefault(value, layout.metricBarHeight);
    } else if (key == "widget_line_gap") {
        layout.widgetLineGap = ParseIntOrDefault(value, layout.widgetLineGap);
    } else if (key == "drive_row_height") {
        layout.driveRowHeight = ParseIntOrDefault(value, layout.driveRowHeight);
    } else if (key == "drive_label_width") {
        layout.driveLabelWidth = ParseIntOrDefault(value, layout.driveLabelWidth);
    } else if (key == "drive_percent_width") {
        layout.drivePercentWidth = ParseIntOrDefault(value, layout.drivePercentWidth);
    } else if (key == "drive_free_width") {
        layout.driveFreeWidth = ParseIntOrDefault(value, layout.driveFreeWidth);
    } else if (key == "drive_bar_gap") {
        layout.driveBarGap = ParseIntOrDefault(value, layout.driveBarGap);
    } else if (key == "drive_value_gap") {
        layout.driveValueGap = ParseIntOrDefault(value, layout.driveValueGap);
    } else if (key == "drive_bar_height") {
        layout.driveBarHeight = ParseIntOrDefault(value, layout.driveBarHeight);
    } else if (key == "throughput_axis_width") {
        layout.throughputAxisWidth = ParseIntOrDefault(value, layout.throughputAxisWidth);
    } else if (key == "throughput_header_gap") {
        layout.throughputHeaderGap = ParseIntOrDefault(value, layout.throughputHeaderGap);
    } else if (key == "throughput_read_label_width") {
        layout.throughputReadLabelWidth = ParseIntOrDefault(value, layout.throughputReadLabelWidth);
    } else if (key == "throughput_write_label_width") {
        layout.throughputWriteLabelWidth = ParseIntOrDefault(value, layout.throughputWriteLabelWidth);
    } else if (key == "throughput_graph_height") {
        layout.throughputGraphHeight = ParseIntOrDefault(value, layout.throughputGraphHeight);
    } else if (key == "gauge_preferred_size") {
        layout.gaugePreferredSize = ParseIntOrDefault(value, layout.gaugePreferredSize);
    } else if (key == "cards") {
        ParseLayoutExpression(value, layout.cardsLayout);
    } else if (key == "rows") {
        LayoutNodeConfig parsed;
        if (ParseLayoutExpression("rows(" + value + ")", parsed)) {
            layout.cardsLayout = std::move(parsed);
        }
    } else if (key == "font.title") {
        ParseFontSpec(layout.titleFont, value);
    } else if (key == "font.big") {
        ParseFontSpec(layout.bigFont, value);
    } else if (key == "font.value") {
        ParseFontSpec(layout.valueFont, value);
    } else if (key == "font.label") {
        ParseFontSpec(layout.labelFont, value);
    } else if (key == "font.small") {
        ParseFontSpec(layout.smallFont, value);
    } else if (key == "title_font_face") {
        ApplyFontValue(layout.titleFont, "face", value);
    } else if (key == "title_font_size") {
        ApplyFontValue(layout.titleFont, "size", value);
    } else if (key == "title_font_weight") {
        ApplyFontValue(layout.titleFont, "weight", value);
    } else if (key == "big_font_face") {
        ApplyFontValue(layout.bigFont, "face", value);
    } else if (key == "big_font_size") {
        ApplyFontValue(layout.bigFont, "size", value);
    } else if (key == "big_font_weight") {
        ApplyFontValue(layout.bigFont, "weight", value);
    } else if (key == "value_font_face") {
        ApplyFontValue(layout.valueFont, "face", value);
    } else if (key == "value_font_size") {
        ApplyFontValue(layout.valueFont, "size", value);
    } else if (key == "value_font_weight") {
        ApplyFontValue(layout.valueFont, "weight", value);
    } else if (key == "label_font_face") {
        ApplyFontValue(layout.labelFont, "face", value);
    } else if (key == "label_font_size") {
        ApplyFontValue(layout.labelFont, "size", value);
    } else if (key == "label_font_weight") {
        ApplyFontValue(layout.labelFont, "weight", value);
    } else if (key == "small_font_face") {
        ApplyFontValue(layout.smallFont, "face", value);
    } else if (key == "small_font_size") {
        ApplyFontValue(layout.smallFont, "size", value);
    } else if (key == "small_font_weight") {
        ApplyFontValue(layout.smallFont, "weight", value);
    }
}

void ApplyCardValue(LayoutConfig& layout, const std::string& section, const std::string& key, const std::string& value) {
    const std::string id = section.substr(std::string("card.").size());
    LayoutCardConfig& card = EnsureCardConfig(layout, id);
    if (key == "title") {
        card.title = value;
    } else if (key == "icon") {
        card.icon = value;
    } else if (key == "layout") {
        LayoutNodeConfig parsed;
        if (ParseLayoutExpression(value, parsed)) {
            card.layout = std::move(parsed);
        }
    }
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
        } else if (section == "layout") {
            ApplyLayoutValue(config.layout, key, value);
        } else if (section.rfind("card.", 0) == 0) {
            ApplyCardValue(config.layout, section, key, value);
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

void CollectDriveLettersRecursive(const LayoutNodeConfig& node, std::vector<std::string>& drives) {
    if (ToLower(node.name) == "drive_usage_list") {
        for (const auto& parameter : node.parameters) {
            if (parameter.first == "drives") {
                for (const std::string& drive : Split(parameter.second, ',')) {
                    const std::string normalized = ToLower(drive.substr(0, 1));
                    if (std::find(drives.begin(), drives.end(), normalized) == drives.end()) {
                        drives.push_back(normalized);
                    }
                }
            }
        }
    }

    for (const auto& child : node.children) {
        CollectDriveLettersRecursive(child, drives);
    }
}

void EnsureDefaultLayout(LayoutConfig& layout) {
    if (layout.cardsLayout.name.empty()) {
        ParseLayoutExpression("rows(columns:3(cpu,gpu),columns:2(network:4,storage:9,time:3))", layout.cardsLayout);
    }

    const auto ensureCard = [&layout](const std::string& id, const std::string& title, const std::string& icon,
                                  const std::string& expression) {
        LayoutCardConfig& card = EnsureCardConfig(layout, id);
        if (card.title.empty()) {
            card.title = title;
        }
        if (card.icon.empty()) {
            card.icon = icon;
        }
        if (card.layout.name.empty()) {
            ParseLayoutExpression(expression, card.layout);
        }
    };

    ensureCard("cpu", "CPU", "cpu",
        "stack(text(cpu.name),columns:7(gauge:5(cpu.load),metric_list:7(items=cpu.temp,cpu.clock,cpu.fan,cpu.ram)))");
    ensureCard("gpu", "GPU", "gpu",
        "stack(text(gpu.name),columns:7(gauge:5(gpu.load),metric_list:7(items=gpu.temp,gpu.clock,gpu.fan,gpu.vram)))");
    ensureCard("network", "Network", "network",
        "stack(throughput:4(network.upload),throughput:4(network.download),network_footer)");
    ensureCard("storage", "Storage", "storage",
        "columns(stack:5(throughput:4(storage.read),throughput:4(storage.write),spacer),stack_top:7(drive_usage_list(drives=C,D,E)))");
    ensureCard("time", "Time", "time",
        "center(clock_time:5,clock_date:2)");
}

}  // namespace

std::string LoadEmbeddedConfigTemplate() {
    return LoadUtf8Resource(IDR_CONFIG_TEMPLATE, RT_RCDATA);
}

std::vector<std::string> CollectLayoutDriveLetters(const LayoutConfig& layout) {
    std::vector<std::string> drives;
    for (const auto& card : layout.cards) {
        CollectDriveLettersRecursive(card.layout, drives);
    }

    std::vector<std::string> result;
    result.reserve(drives.size());
    for (const std::string& drive : drives) {
        if (!drive.empty()) {
            result.push_back(std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(drive[0])))));
        }
    }
    return result;
}

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    ApplyConfigText(LoadEmbeddedConfigTemplate(), config);
    ApplyConfigText(ReadFileUtf8(path), config);
    EnsureDefaultLayout(config.layout);

    const std::vector<std::string> layoutDrives = CollectLayoutDriveLetters(config.layout);
    if (!layoutDrives.empty()) {
        config.driveLetters = layoutDrives;
    }
    if (config.driveLetters.empty()) {
        config.driveLetters = {"C", "D", "E"};
    }
    return config;
}

bool SaveConfig(const std::filesystem::path& path, const AppConfig& config) {
    std::string text = ReadFileUtf8(path);
    if (text.empty()) {
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
        std::stringstream stream(LoadEmbeddedConfigTemplate());
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
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
    updateKey("[vendor.gigabyte]", "fan_channel", config.gigabyteFanChannelName);
    updateKey("[vendor.gigabyte]", "temperature_channel", config.gigabyteTemperatureChannelName);

    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += "\r\n";
    }
    return WriteFileUtf8(path, output);
}
