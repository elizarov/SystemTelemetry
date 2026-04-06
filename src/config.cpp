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

double ParseDoubleOrDefault(const std::string& value, double fallback) {
    try {
        size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
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
                if (!ParseParameter(node.parameter)) {
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
        return name == "rows" || name == "columns" || name == "stack" ||
            name == "stack_top" || name == "center";
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
    bool ParseParameter(std::string& parameter);

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

bool LayoutExpressionParser::ParseParameter(std::string& parameter) {
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
    parameter = body;
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
        if (card.id == id) {
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
    } else if (key == "graph_marker_color") {
        layout.graphMarkerColor = ParseHexColorOrDefault(value, layout.graphMarkerColor);
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
    } else if (key == "widget_line_gap") {
        layout.widgetLineGap = ParseIntOrDefault(value, layout.widgetLineGap);
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

void ApplyMetricListWidgetValue(MetricListWidgetConfig& widget, const std::string& key, const std::string& value) {
    if (key == "label_width") {
        widget.labelWidth = ParseIntOrDefault(value, widget.labelWidth);
    } else if (key == "value_gap") {
        widget.valueGap = ParseIntOrDefault(value, widget.valueGap);
    } else if (key == "bar_height") {
        widget.barHeight = ParseIntOrDefault(value, widget.barHeight);
    } else if (key == "vertical_gap") {
        widget.verticalGap = ParseIntOrDefault(value, widget.verticalGap);
    }
}

void ApplyDriveUsageListWidgetValue(DriveUsageListWidgetConfig& widget, const std::string& key,
    const std::string& value) {
    if (key == "free_width") {
        widget.freeWidth = ParseIntOrDefault(value, widget.freeWidth);
    } else if (key == "bar_gap") {
        widget.barGap = ParseIntOrDefault(value, widget.barGap);
    } else if (key == "value_gap") {
        widget.valueGap = ParseIntOrDefault(value, widget.valueGap);
    } else if (key == "bar_height") {
        widget.barHeight = ParseIntOrDefault(value, widget.barHeight);
    } else if (key == "vertical_gap") {
        widget.verticalGap = ParseIntOrDefault(value, widget.verticalGap);
    } else if (key == "label_padding") {
        widget.labelPadding = ParseIntOrDefault(value, widget.labelPadding);
    } else if (key == "percent_padding") {
        widget.percentPadding = ParseIntOrDefault(value, widget.percentPadding);
    }
}

void ApplyThroughputWidgetValue(ThroughputWidgetConfig& widget, const std::string& key, const std::string& value) {
    if (key == "header_gap") {
        widget.headerGap = ParseIntOrDefault(value, widget.headerGap);
    } else if (key == "graph_height") {
        widget.graphHeight = ParseIntOrDefault(value, widget.graphHeight);
    } else if (key == "value_padding") {
        widget.valuePadding = ParseIntOrDefault(value, widget.valuePadding);
    } else if (key == "label_padding") {
        widget.labelPadding = ParseIntOrDefault(value, widget.labelPadding);
    } else if (key == "axis_padding") {
        widget.axisPadding = ParseIntOrDefault(value, widget.axisPadding);
    } else if (key == "scale_label_padding") {
        widget.scaleLabelPadding = ParseIntOrDefault(value, widget.scaleLabelPadding);
    } else if (key == "scale_label_min_height") {
        widget.scaleLabelMinHeight = ParseIntOrDefault(value, widget.scaleLabelMinHeight);
    } else if (key == "guide_stroke_width") {
        widget.guideStrokeWidth = ParseIntOrDefault(value, widget.guideStrokeWidth);
    } else if (key == "plot_stroke_width") {
        widget.plotStrokeWidth = ParseIntOrDefault(value, widget.plotStrokeWidth);
    } else if (key == "leader_diameter") {
        widget.leaderDiameter = ParseIntOrDefault(value, widget.leaderDiameter);
    }
}

void ApplyGaugeWidgetValue(GaugeWidgetConfig& widget, const std::string& key, const std::string& value) {
    if (key == "preferred_size") {
        widget.preferredSize = ParseIntOrDefault(value, widget.preferredSize);
    } else if (key == "outer_padding") {
        widget.outerPadding = ParseIntOrDefault(value, widget.outerPadding);
    } else if (key == "min_radius") {
        widget.minRadius = ParseIntOrDefault(value, widget.minRadius);
    } else if (key == "ring_thickness") {
        widget.ringThickness = ParseIntOrDefault(value, widget.ringThickness);
    } else if (key == "sweep_degrees") {
        widget.sweepDegrees = ParseDoubleOrDefault(value, widget.sweepDegrees);
    } else if (key == "segment_count") {
        widget.segmentCount = ParseIntOrDefault(value, widget.segmentCount);
    } else if (key == "segment_gap_degrees") {
        widget.segmentGapDegrees = ParseDoubleOrDefault(value, widget.segmentGapDegrees);
    } else if (key == "text_half_width") {
        widget.textHalfWidth = ParseIntOrDefault(value, widget.textHalfWidth);
    } else if (key == "value_top") {
        widget.valueTop = ParseIntOrDefault(value, widget.valueTop);
    } else if (key == "value_bottom") {
        widget.valueBottom = ParseIntOrDefault(value, widget.valueBottom);
    } else if (key == "label_top") {
        widget.labelTop = ParseIntOrDefault(value, widget.labelTop);
    } else if (key == "label_bottom") {
        widget.labelBottom = ParseIntOrDefault(value, widget.labelBottom);
    }
}

void ApplyTextWidgetValue(TextWidgetConfig& widget, const std::string& key, const std::string& value) {
    if (key == "preferred_padding") {
        widget.preferredPadding = ParseIntOrDefault(value, widget.preferredPadding);
    }
}

void ApplyNetworkFooterWidgetValue(NetworkFooterWidgetConfig& widget, const std::string& key,
    const std::string& value) {
    if (key == "preferred_padding") {
        widget.preferredPadding = ParseIntOrDefault(value, widget.preferredPadding);
    }
}

void ApplyClockTimeWidgetValue(ClockTimeWidgetConfig& widget, const std::string& key, const std::string& value) {
    if (key == "padding") {
        widget.padding = ParseIntOrDefault(value, widget.padding);
    }
}

void ApplyClockDateWidgetValue(ClockDateWidgetConfig& widget, const std::string& key, const std::string& value) {
    if (key == "padding") {
        widget.padding = ParseIntOrDefault(value, widget.padding);
    }
}

void ApplyMetricScaleValue(MetricScaleConfig& metricScales, const std::string& key, const std::string& value) {
    if (key == "cpu_clock_ghz") {
        metricScales.cpuClockGHz = ParseDoubleOrDefault(value, metricScales.cpuClockGHz);
    } else if (key == "gpu_temperature_c") {
        metricScales.gpuTemperatureC = ParseDoubleOrDefault(value, metricScales.gpuTemperatureC);
    } else if (key == "gpu_clock_mhz") {
        metricScales.gpuClockMHz = ParseDoubleOrDefault(value, metricScales.gpuClockMHz);
    } else if (key == "gpu_fan_rpm") {
        metricScales.gpuFanRpm = ParseDoubleOrDefault(value, metricScales.gpuFanRpm);
    } else if (key == "board_temperature_c") {
        metricScales.boardTemperatureC = ParseDoubleOrDefault(value, metricScales.boardTemperatureC);
    } else if (key == "board_fan_rpm") {
        metricScales.boardFanRpm = ParseDoubleOrDefault(value, metricScales.boardFanRpm);
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
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));

        if (section == "display" && key == "monitor_name") {
            config.monitorName = value;
        } else if (section == "display" && key == "position") {
            ParseIntPair(value, config.positionX, config.positionY);
        } else if (section == "network" && key == "adapter_name") {
            config.networkAdapter = value;
        } else if (section == "metric_scales") {
            ApplyMetricScaleValue(config.metricScales, key, value);
        } else if (section == "metric_list") {
            ApplyMetricListWidgetValue(config.layout.metricList, key, value);
        } else if (section == "drive_usage_list") {
            ApplyDriveUsageListWidgetValue(config.layout.driveUsageList, key, value);
        } else if (section == "throughput") {
            ApplyThroughputWidgetValue(config.layout.throughput, key, value);
        } else if (section == "gauge") {
            ApplyGaugeWidgetValue(config.layout.gauge, key, value);
        } else if (section == "text") {
            ApplyTextWidgetValue(config.layout.text, key, value);
        } else if (section == "network_footer") {
            ApplyNetworkFooterWidgetValue(config.layout.networkFooter, key, value);
        } else if (section == "clock_time") {
            ApplyClockTimeWidgetValue(config.layout.clockTime, key, value);
        } else if (section == "clock_date") {
            ApplyClockDateWidgetValue(config.layout.clockDate, key, value);
        } else if (section == "layout") {
            ApplyLayoutValue(config.layout, key, value);
        } else if (section.rfind("card.", 0) == 0) {
            ApplyCardValue(config.layout, section, key, value);
        }
    }
}

void ReplaceOrAppendKey(std::vector<std::string>& lines, size_t sectionStart, size_t sectionEnd,
    const std::string& key, const std::string& value) {
    for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (Trim(trimmed.substr(0, eq)) == key) {
            lines[i] = key + " = " + value;
            return;
        }
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), key + " = " + value);
}

void AddUniqueValue(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }

    for (const auto& existing : values) {
        if (existing == value) {
            return;
        }
    }
    values.push_back(value);
}

std::string ExtractMetricReference(const std::string& token) {
    const size_t equals = token.find('=');
    return Trim(token.substr(0, equals));
}

void CollectLayoutBindingsRecursive(const LayoutNodeConfig& node, std::vector<std::string>& drives,
    std::vector<std::string>& boardTemperatures, std::vector<std::string>& boardFans) {
    if (node.name == "drive_usage_list") {
        for (const std::string& drive : Split(node.parameter, ',')) {
            const std::string normalized = drive.substr(0, 1);
            AddUniqueValue(drives, normalized);
        }
    }

    for (const std::string& token : Split(node.parameter, ',')) {
        const std::string metricRef = ExtractMetricReference(token);
        if (metricRef.rfind("board.temp.", 0) == 0) {
            AddUniqueValue(boardTemperatures, metricRef.substr(std::string("board.temp.").size()));
        } else if (metricRef.rfind("board.fan.", 0) == 0) {
            AddUniqueValue(boardFans, metricRef.substr(std::string("board.fan.").size()));
        }
    }

    for (const auto& child : node.children) {
        CollectLayoutBindingsRecursive(child, drives, boardTemperatures, boardFans);
    }
}

}  // namespace

std::string LoadEmbeddedConfigTemplate() {
    return LoadUtf8Resource(IDR_CONFIG_TEMPLATE, RT_RCDATA);
}

struct LayoutBindingSelection {
    std::vector<std::string> driveLetters;
    std::vector<std::string> boardTemperatureNames;
    std::vector<std::string> boardFanNames;
};

static LayoutBindingSelection CollectLayoutBindings(const LayoutConfig& layout) {
    std::vector<std::string> drives;
    std::vector<std::string> boardTemperatures;
    std::vector<std::string> boardFans;
    for (const auto& card : layout.cards) {
        CollectLayoutBindingsRecursive(card.layout, drives, boardTemperatures, boardFans);
    }

    LayoutBindingSelection result;
    result.driveLetters.reserve(drives.size());
    for (const std::string& drive : drives) {
        if (!drive.empty()) {
            result.driveLetters.push_back(std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(drive[0])))));
        }
    }
    result.boardTemperatureNames = std::move(boardTemperatures);
    result.boardFanNames = std::move(boardFans);
    return result;
}

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    ApplyConfigText(LoadEmbeddedConfigTemplate(), config);
    ApplyConfigText(ReadFileUtf8(path), config);

    const LayoutBindingSelection layoutBindings = CollectLayoutBindings(config.layout);
    if (!layoutBindings.driveLetters.empty()) {
        config.driveLetters = layoutBindings.driveLetters;
    }
    config.boardTemperatureNames = layoutBindings.boardTemperatureNames;
    config.boardFanNames = layoutBindings.boardFanNames;
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
    updateKey("[display]", "position", std::to_string(config.positionX) + "," + std::to_string(config.positionY));
    updateKey("[network]", "adapter_name", config.networkAdapter);

    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += "\r\n";
    }
    return WriteFileUtf8(path, output);
}
