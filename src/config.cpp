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

bool ParseIntPair(const std::string& value, int& first, int& second) {
    const std::vector<std::string> parts = Split(value, ',');
    if (parts.size() != 2) {
        return false;
    }
    first = ParseIntOrDefault(parts[0], first);
    second = ParseIntOrDefault(parts[1], second);
    return true;
}

bool ParseLogicalPoint(const std::string& value, LogicalPointConfig& point) {
    return ParseIntPair(value, point.x, point.y);
}

bool ParseLogicalSize(const std::string& value, LogicalSizeConfig& size) {
    return ParseIntPair(value, size.width, size.height);
}

std::string FormatLogicalSize(const LogicalSizeConfig& size) {
    return std::to_string(size.width) + "," + std::to_string(size.height);
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

std::string FormatFontSpec(const UiFontConfig& font) {
    return font.face + "," + std::to_string(font.size) + "," + std::to_string(font.weight);
}

template <typename Codec, typename Value>
void DecodeConfigValue(Value& target, const std::string& value);

template <>
void DecodeConfigValue<configschema::IntCodec, int>(int& target, const std::string& value) {
    target = ParseIntOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::DoubleCodec, double>(double& target, const std::string& value) {
    target = ParseDoubleOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::StringCodec, std::string>(std::string& target, const std::string& value) {
    target = value;
}

template <>
void DecodeConfigValue<configschema::LogicalPointCodec, LogicalPointConfig>(LogicalPointConfig& target, const std::string& value) {
    ParseLogicalPoint(value, target);
}

template <>
void DecodeConfigValue<configschema::LogicalSizeCodec, LogicalSizeConfig>(LogicalSizeConfig& target, const std::string& value) {
    ParseLogicalSize(value, target);
}

template <>
void DecodeConfigValue<configschema::HexColorCodec, unsigned int>(unsigned int& target, const std::string& value) {
    target = ParseHexColorOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::FontSpecCodec, UiFontConfig>(UiFontConfig& target, const std::string& value) {
    ParseFontSpec(target, value);
}

template <typename Codec, typename Value>
std::string EncodeConfigValue(const Value& value);

template <>
std::string EncodeConfigValue<configschema::IntCodec, int>(const int& value) {
    return std::to_string(value);
}

template <>
std::string EncodeConfigValue<configschema::DoubleCodec, double>(const double& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

template <>
std::string EncodeConfigValue<configschema::StringCodec, std::string>(const std::string& value) {
    return value;
}

template <>
std::string EncodeConfigValue<configschema::LogicalPointCodec, LogicalPointConfig>(const LogicalPointConfig& value) {
    return std::to_string(value.x) + "," + std::to_string(value.y);
}

template <>
std::string EncodeConfigValue<configschema::LogicalSizeCodec, LogicalSizeConfig>(const LogicalSizeConfig& value) {
    return FormatLogicalSize(value);
}

template <>
std::string EncodeConfigValue<configschema::HexColorCodec, unsigned int>(const unsigned int& value) {
    return FormatHexColor(value);
}

template <>
std::string EncodeConfigValue<configschema::FontSpecCodec, UiFontConfig>(const UiFontConfig& value) {
    return FormatFontSpec(value);
}

template <typename Section>
bool ApplyStructuredSectionValue(typename Section::owner_type& owner, const std::string& key, const std::string& value) {
    bool handled = false;
    std::apply([&](auto... field) {
        (..., [&] {
            using Field = std::remove_cvref_t<decltype(field)>;
            if (!handled && key == Field::key.view()) {
                DecodeConfigValue<typename Field::codec_type>(owner.*(Field::member), value);
                handled = true;
            }
        }());
    }, Section::fields);
    return handled;
}

template <typename Section, typename UpdateKeyFn>
void SaveStructuredSection(const typename Section::owner_type& owner, UpdateKeyFn&& updateKey) {
    const std::string sectionName = "[" + std::string(Section::name.view()) + "]";
    std::apply([&](auto... field) {
        (updateKey(
             sectionName,
             std::string(std::remove_cvref_t<decltype(field)>::key.view()),
             EncodeConfigValue<typename std::remove_cvref_t<decltype(field)>::codec_type>(
                 owner.*(std::remove_cvref_t<decltype(field)>::member))),
         ...);
    }, Section::fields);
}

template <typename Section, typename UpdateKeyFn>
void SaveDynamicStructuredSection(const typename Section::owner_type& owner, std::string_view suffix, UpdateKeyFn&& updateKey) {
    const std::string sectionName = Section::FormatName(suffix);
    std::apply([&](auto... field) {
        (updateKey(
             sectionName,
             std::string(std::remove_cvref_t<decltype(field)>::key.view()),
             EncodeConfigValue<typename std::remove_cvref_t<decltype(field)>::codec_type>(
                 owner.*(std::remove_cvref_t<decltype(field)>::member))),
         ...);
    }, Section::fields);
}

DisplayConfig& AccessDisplayConfig(AppConfig& config) {
    return config.display;
}

NetworkConfig& AccessNetworkConfig(AppConfig& config) {
    return config.network;
}

MetricScaleConfig& AccessMetricScalesConfig(AppConfig& config) {
    return config.metricScales;
}

DashboardSectionConfig& AccessDashboardSectionConfig(AppConfig& config) {
    return config.layout.dashboard;
}

CardStyleConfig& AccessCardStyleConfig(AppConfig& config) {
    return config.layout.cardStyle;
}

ColorConfig& AccessColorConfig(AppConfig& config) {
    return config.layout.colors;
}

UiFontSetConfig& AccessFontConfig(AppConfig& config) {
    return config.layout.fonts;
}

MetricListWidgetConfig& AccessMetricListWidgetConfig(AppConfig& config) {
    return config.layout.metricList;
}

DriveUsageListWidgetConfig& AccessDriveUsageListWidgetConfig(AppConfig& config) {
    return config.layout.driveUsageList;
}

ThroughputWidgetConfig& AccessThroughputWidgetConfig(AppConfig& config) {
    return config.layout.throughput;
}

GaugeWidgetConfig& AccessGaugeWidgetConfig(AppConfig& config) {
    return config.layout.gauge;
}

TextWidgetConfig& AccessTextWidgetConfig(AppConfig& config) {
    return config.layout.text;
}

NetworkFooterWidgetConfig& AccessNetworkFooterWidgetConfig(AppConfig& config) {
    return config.layout.networkFooter;
}

ClockTimeWidgetConfig& AccessClockTimeWidgetConfig(AppConfig& config) {
    return config.layout.clockTime;
}

ClockDateWidgetConfig& AccessClockDateWidgetConfig(AppConfig& config) {
    return config.layout.clockDate;
}

template <typename Section, auto Accessor>
struct StructuredSectionBinding {
    using section_type = Section;

    static typename Section::owner_type& Get(AppConfig& config) {
        return Accessor(config);
    }
};

template <typename... Bindings>
bool DispatchStructuredSection(AppConfig& config, const std::string& section, const std::string& key, const std::string& value) {
    bool handled = false;
    std::apply([&](auto... binding) {
        (..., [&] {
            using Binding = std::remove_cvref_t<decltype(binding)>;
            using Section = typename Binding::section_type;
            if (!handled && section == Section::name.view()) {
                ApplyStructuredSectionValue<Section>(Binding::Get(config), key, value);
                handled = true;
            }
        }());
    }, std::tuple<Bindings...>{});
    return handled;
}

template <typename Section, typename ResolveOwnerFn>
bool DispatchDynamicStructuredSection(const std::string& section, const std::string& key, const std::string& value,
    ResolveOwnerFn&& resolveOwner) {
    if (!Section::Matches(section)) {
        return false;
    }

    typename Section::owner_type& owner = resolveOwner(Section::Suffix(section));
    ApplyStructuredSectionValue<Section>(owner, key, value);
    return true;
}

bool DispatchKnownStructuredSection(AppConfig& config, const std::string& section, const std::string& key, const std::string& value) {
    return DispatchStructuredSection<
        StructuredSectionBinding<DisplayConfig::Section, AccessDisplayConfig>,
        StructuredSectionBinding<NetworkConfig::Section, AccessNetworkConfig>,
        StructuredSectionBinding<MetricScaleConfig::Section, AccessMetricScalesConfig>,
        StructuredSectionBinding<DashboardSectionConfig::Section, AccessDashboardSectionConfig>,
        StructuredSectionBinding<CardStyleConfig::Section, AccessCardStyleConfig>,
        StructuredSectionBinding<ColorConfig::Section, AccessColorConfig>,
        StructuredSectionBinding<UiFontSetConfig::Section, AccessFontConfig>,
        StructuredSectionBinding<MetricListWidgetConfig::Section, AccessMetricListWidgetConfig>,
        StructuredSectionBinding<DriveUsageListWidgetConfig::Section, AccessDriveUsageListWidgetConfig>,
        StructuredSectionBinding<ThroughputWidgetConfig::Section, AccessThroughputWidgetConfig>,
        StructuredSectionBinding<GaugeWidgetConfig::Section, AccessGaugeWidgetConfig>,
        StructuredSectionBinding<TextWidgetConfig::Section, AccessTextWidgetConfig>,
        StructuredSectionBinding<NetworkFooterWidgetConfig::Section, AccessNetworkFooterWidgetConfig>,
        StructuredSectionBinding<ClockTimeWidgetConfig::Section, AccessClockTimeWidgetConfig>,
        StructuredSectionBinding<ClockDateWidgetConfig::Section, AccessClockDateWidgetConfig>
    >(config, section, key, value);
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

std::string FormatLayoutExpression(const LayoutNodeConfig& node) {
    std::string text = node.name;
    if (node.weight != 1) {
        text += ":" + std::to_string(node.weight);
    }
    if (!node.children.empty()) {
        text += "(";
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) {
                text += ",";
            }
            text += FormatLayoutExpression(node.children[i]);
        }
        text += ")";
    } else if (!node.parameter.empty()) {
        text += "(" + node.parameter + ")";
    }
    return text;
}

template <>
void DecodeConfigValue<configschema::LayoutExpressionCodec, LayoutNodeConfig>(LayoutNodeConfig& target, const std::string& value) {
    LayoutNodeConfig parsed;
    if (ParseLayoutExpression(value, parsed)) {
        target = std::move(parsed);
    }
}

template <>
std::string EncodeConfigValue<configschema::LayoutExpressionCodec, LayoutNodeConfig>(const LayoutNodeConfig& value) {
    return FormatLayoutExpression(value);
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

NamedLayoutSectionConfig* FindNamedLayoutSection(AppConfig& config, const std::string& name) {
    for (auto& layout : config.layouts) {
        if (layout.name == name) {
            return &layout;
        }
    }
    return nullptr;
}

NamedLayoutSectionConfig& EnsureNamedLayoutSection(AppConfig& config, const std::string& name) {
    if (NamedLayoutSectionConfig* layout = FindNamedLayoutSection(config, name)) {
        return *layout;
    }
    config.layouts.push_back(NamedLayoutSectionConfig{name});
    return config.layouts.back();
}

bool ApplyCardValue(LayoutConfig& layout, const std::string& section, const std::string& key, const std::string& value) {
    return DispatchDynamicStructuredSection<LayoutCardConfig::Section>(section, key, value,
        [&layout](std::string_view id) -> LayoutCardConfig& {
            return EnsureCardConfig(layout, std::string(id));
        });
}

bool ApplyNamedLayoutValue(AppConfig& config, const std::string& section, const std::string& key, const std::string& value) {
    return DispatchDynamicStructuredSection<NamedLayoutSectionConfig::Section>(section, key, value,
        [&config](std::string_view name) -> NamedLayoutSectionConfig& {
            return EnsureNamedLayoutSection(config, std::string(name));
        });
}

void ApplyBoardValue(AppConfig& config, const std::string& key, const std::string& value) {
    if (key.rfind("board.temp.", 0) == 0) {
        config.boardTemperatureSensorNames[key.substr(std::string("board.temp.").size())] = value;
    } else if (key.rfind("board.fan.", 0) == 0) {
        config.boardFanSensorNames[key.substr(std::string("board.fan.").size())] = value;
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

        if (DispatchKnownStructuredSection(config, section, key, value)) {
            continue;
        } else if (section == "board") {
            ApplyBoardValue(config, key, value);
        } else if (ApplyNamedLayoutValue(config, section, key, value)) {
            continue;
        } else if (ApplyCardValue(config.layout, section, key, value)) {
            continue;
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

bool SelectResolvedLayout(AppConfig& config, const std::string& requestedName) {
    const NamedLayoutSectionConfig* selected = nullptr;
    if (!requestedName.empty()) {
        for (const auto& layout : config.layouts) {
            if (layout.name == requestedName) {
                selected = &layout;
                break;
            }
        }
    }
    if (selected == nullptr && !config.layouts.empty()) {
        selected = &config.layouts.front();
    }
    if (selected == nullptr) {
        return false;
    }

    config.display.layout = selected->name;
    config.layout.structure.window = selected->window;
    config.layout.structure.cardsLayout = selected->cardsLayout;
    return true;
}

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    ApplyConfigText(LoadEmbeddedConfigTemplate(), config);
    ApplyConfigText(ReadFileUtf8(path), config);
    SelectResolvedLayout(config, config.display.layout);

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

    const auto findSectionIndex = [&lines](const std::string& sectionName) -> size_t {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (Trim(lines[i]) == sectionName) {
                return i;
            }
        }
        return lines.size();
    };

    const auto ensureSection = [&lines, &findSectionIndex](const std::string& sectionName) -> size_t {
        const size_t existingIndex = findSectionIndex(sectionName);
        if (existingIndex < lines.size()) {
            return existingIndex;
        }
        if (!lines.empty() && !lines.back().empty()) {
            lines.push_back("");
        }
        lines.push_back(sectionName);
        return lines.size() - 1;
    };

    const auto ensureSectionAfter = [&lines, &findSectionIndex](const std::string& sectionName,
                                    const std::string& afterSectionName) -> size_t {
        const size_t existingIndex = findSectionIndex(sectionName);
        if (existingIndex < lines.size()) {
            return existingIndex;
        }

        const size_t afterIndex = findSectionIndex(afterSectionName);
        if (afterIndex >= lines.size()) {
            if (!lines.empty() && !lines.back().empty()) {
                lines.push_back("");
            }
            lines.push_back(sectionName);
            return lines.size() - 1;
        }

        size_t insertIndex = afterIndex + 1;
        while (insertIndex < lines.size()) {
            const std::string next = Trim(lines[insertIndex]);
            if (!next.empty() && next.front() == '[' && next.back() == ']') {
                break;
            }
            ++insertIndex;
        }

        std::vector<std::string> insertedLines;
        if (insertIndex > 0 && !lines[insertIndex - 1].empty()) {
            insertedLines.push_back("");
        }
        insertedLines.push_back(sectionName);
        if (insertIndex < lines.size() && !lines[insertIndex].empty()) {
            insertedLines.push_back("");
        }

        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertIndex),
            insertedLines.begin(), insertedLines.end());
        return insertIndex + (insertedLines.front().empty() ? 1 : 0);
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

    SaveStructuredSection<DisplayConfig::Section>(config.display, updateKey);
    SaveStructuredSection<NetworkConfig::Section>(config.network, updateKey);
    for (const auto& layout : config.layouts) {
        SaveDynamicStructuredSection<NamedLayoutSectionConfig::Section>(layout, layout.name, updateKey);
    }

    const size_t boardSectionStart = ensureSectionAfter("[board]", "[network]");
    for (const std::string& logicalName : config.boardTemperatureNames) {
        const auto it = config.boardTemperatureSensorNames.find(logicalName);
        const std::string sensorName = it != config.boardTemperatureSensorNames.end() && !it->second.empty()
            ? it->second
            : logicalName;
        ReplaceOrAppendKey(lines, boardSectionStart, findSectionEnd(boardSectionStart),
            "board.temp." + logicalName, sensorName);
    }
    for (const std::string& logicalName : config.boardFanNames) {
        const auto it = config.boardFanSensorNames.find(logicalName);
        const std::string sensorName = it != config.boardFanSensorNames.end() && !it->second.empty()
            ? it->second
            : logicalName;
        ReplaceOrAppendKey(lines, boardSectionStart, findSectionEnd(boardSectionStart),
            "board.fan." + logicalName, sensorName);
    }

    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += "\r\n";
    }
    return WriteFileUtf8(path, output);
}

bool SelectLayout(AppConfig& config, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const auto& layout : config.layouts) {
        if (layout.name == name) {
            config.display.layout = layout.name;
            config.layout.structure.window = layout.window;
            config.layout.structure.cardsLayout = layout.cardsLayout;
            return true;
        }
    }
    return false;
}
