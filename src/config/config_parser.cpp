#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "dashboard_metrics.h"
#include "widget/widget_class.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "resource.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <type_traits>

#include "util/utf8.h"

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

std::vector<std::string> SplitPreservingEmpty(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : input) {
        if (ch == delimiter) {
            parts.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    parts.push_back(Trim(current));
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

ColorConfig ParseHexColorOrDefault(const std::string& value, ColorConfig fallback) {
    std::string text = Trim(value);
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 8) {
        return fallback;
    }
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) {
            return fallback;
        }
    }
    try {
        return ColorConfig::FromRgba(static_cast<unsigned int>(std::stoul(text, nullptr, 16)));
    } catch (...) {
        return fallback;
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

bool ParseLogicalPoint(const std::string& value, LogicalPointConfig& point) {
    return ParseIntPair(value, point.x, point.y);
}

bool ParseLogicalSize(const std::string& value, LogicalSizeConfig& size) {
    return ParseIntPair(value, size.width, size.height);
}

bool ParseMetricDefinition(const std::string& value, MetricDefinitionConfig& definition) {
    const std::vector<std::string> parts = SplitPreservingEmpty(value, ',');
    const std::optional<MetricDisplayStyle> metadataStyle = FindDashboardMetricDisplayStyle(definition.id);
    if (!metadataStyle.has_value()) {
        return false;
    }
    if (parts.size() != 3) {
        return false;
    }

    MetricDefinitionConfig parsed = definition;
    parsed.style = *metadataStyle;
    if (parts[0] == "*") {
        parsed.telemetryScale = true;
        parsed.scale = 0.0;
    } else {
        const double parsedScale = ParseDoubleOrDefault(parts[0], 0.0);
        if (!(parsedScale > 0.0)) {
            return false;
        }
        parsed.telemetryScale = false;
        parsed.scale = parsedScale;
    }

    parsed.unit = parts[1];
    parsed.label = parts[2];
    definition = std::move(parsed);
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

template <typename Codec, typename Value> void DecodeConfigValue(Value& target, const std::string& value);

template <> void DecodeConfigValue<configschema::IntCodec, int>(int& target, const std::string& value) {
    target = ParseIntOrDefault(value, target);
}

template <> void DecodeConfigValue<configschema::DoubleCodec, double>(double& target, const std::string& value) {
    target = ParseDoubleOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::StringCodec, std::string>(std::string& target, const std::string& value) {
    target = value;
}

template <>
void DecodeConfigValue<configschema::LogicalPointCodec, LogicalPointConfig>(
    LogicalPointConfig& target, const std::string& value) {
    ParseLogicalPoint(value, target);
}

template <>
void DecodeConfigValue<configschema::LogicalSizeCodec, LogicalSizeConfig>(
    LogicalSizeConfig& target, const std::string& value) {
    ParseLogicalSize(value, target);
}

template <>
void DecodeConfigValue<configschema::HexColorCodec, ColorConfig>(ColorConfig& target, const std::string& value) {
    target = ParseHexColorOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::FontSpecCodec, UiFontConfig>(UiFontConfig& target, const std::string& value) {
    ParseFontSpec(target, value);
}

template <>
void DecodeConfigValue<configschema::StringCodec, std::vector<std::string>>(
    std::vector<std::string>& target, const std::string& value) {
    target = Split(value, ',');
}

template <typename Section>
bool ApplyStructuredSectionFields(
    typename Section::owner_type& owner, const std::string& key, const std::string& value) {
    bool handled = false;
    std::apply(
        [&](auto... field) {
            (..., [&] {
                using Field = std::remove_cvref_t<decltype(field)>;
                if (!handled && key == Field::key.view()) {
                    typename Field::field_type decoded = Field::RawGet(owner);
                    DecodeConfigValue<typename Field::codec_type>(decoded, value);
                    Field::Set(owner, std::move(decoded));
                    handled = true;
                }
            }());
        },
        Section::fields);
    return handled;
}

template <typename Codec, typename Owner> struct CustomSectionHandler {
    static bool Apply(Owner&, const std::string&, const std::string&) {
        return false;
    }
};

template <typename Section>
bool ApplySectionValue(typename Section::owner_type& owner, const std::string& key, const std::string& value) {
    if constexpr (std::is_same_v<typename Section::codec_type, configschema::StructuredSectionCodec>) {
        return ApplyStructuredSectionFields<Section>(owner, key, value);
    } else {
        return CustomSectionHandler<typename Section::codec_type, typename Section::owner_type>::Apply(
            owner, key, value);
    }
}

template <typename BindingList, typename Owner, typename Fn> void ForEachKnownBinding(Owner&& owner, Fn&& fn) {
    BindingList::ForEach([&](auto binding) { fn(std::remove_cvref_t<decltype(binding)>{}, owner); });
}

template <typename BindingList, typename Owner>
bool DispatchKnownBindingSection(
    Owner& owner, const std::string& section, const std::string& key, const std::string& value) {
    bool handled = false;
    ForEachKnownBinding<BindingList>(owner, [&](auto binding, auto& currentOwner) {
        using Binding = decltype(binding);
        if (!handled) {
            if constexpr (Binding::is_recursive) {
                handled = DispatchKnownBindingSection<typename Binding::nested_owner_type::BindingList>(
                    Binding::Get(currentOwner), section, key, value);
            } else if constexpr (Binding::is_dynamic) {
                using Section = typename Binding::section_type;
                if (Section::Matches(section)) {
                    typename Section::owner_type& item = Binding::Ensure(currentOwner, Section::Suffix(section));
                    ApplySectionValue<Section>(item, key, value);
                    handled = true;
                }
            } else {
                using Section = typename Binding::section_type;
                if (section == Section::name.view()) {
                    ApplySectionValue<Section>(Binding::Get(currentOwner), key, value);
                    handled = true;
                }
            }
        }
    });
    return handled;
}

std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!IsValidUtf8(text)) {
        return {};
    }
    return text;
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
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!IsValidUtf8(text)) {
        return {};
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
        return name == "rows" || name == "columns";
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

    parameter = Trim(text_.substr(begin, index_ - begin));
    return true;
}

}  // namespace

bool ParseLayoutExpression(const std::string& text, LayoutNodeConfig& node) {
    node = {};
    LayoutExpressionParser parser(text);
    if (!parser.ParseNode(node)) {
        return false;
    }
    return parser.AtEnd();
}

namespace {

bool IsWidgetOrContainerNodeName(const std::string& name) {
    return name == "rows" || name == "columns" ||
           (!name.empty() && EnumFromString<DashboardWidgetClass>(name).has_value());
}

void MarkCardReferencesRecursive(LayoutNodeConfig& node, const std::set<std::string>& cardIds) {
    node.cardReference = false;
    if (node.children.empty() && node.parameter.empty() && !IsWidgetOrContainerNodeName(node.name) &&
        cardIds.find(node.name) != cardIds.end()) {
        node.cardReference = true;
    }
    for (auto& child : node.children) {
        MarkCardReferencesRecursive(child, cardIds);
    }
}

void MarkCardLayoutReferences(LayoutConfig& layout) {
    std::set<std::string> cardIds;
    for (const auto& card : layout.cards) {
        if (!card.id.empty()) {
            cardIds.insert(card.id);
        }
    }
    for (auto& card : layout.cards) {
        MarkCardReferencesRecursive(card.layout, cardIds);
    }
}

template <>
void DecodeConfigValue<configschema::LayoutExpressionCodec, LayoutNodeConfig>(
    LayoutNodeConfig& target, const std::string& value) {
    LayoutNodeConfig parsed;
    if (ParseLayoutExpression(value, parsed)) {
        target = std::move(parsed);
    }
}

template <> struct CustomSectionHandler<configschema::BoardSectionCodec, BoardConfig> {
    static bool Apply(BoardConfig& board, const std::string& key, const std::string& value) {
        if (key.rfind("board.temp.", 0) == 0) {
            board.temperatureSensorNames[key.substr(std::string("board.temp.").size())] = value;
            return true;
        }
        if (key.rfind("board.fan.", 0) == 0) {
            board.fanSensorNames[key.substr(std::string("board.fan.").size())] = value;
            return true;
        }
        return false;
    }
};

template <> struct CustomSectionHandler<configschema::MetricsSectionCodec, MetricsSectionConfig> {
    static bool Apply(MetricsSectionConfig& metrics, const std::string& key, const std::string& value) {
        if (key.empty()) {
            return false;
        }
        if (IsRuntimePlaceholderMetricId(key)) {
            return true;
        }

        MetricDefinitionConfig* definition = FindMetricDefinition(metrics, key);
        MetricDefinitionConfig candidate = definition != nullptr ? *definition : MetricDefinitionConfig{};
        candidate.id = key;
        if (const auto style = FindDashboardMetricDisplayStyle(key); style.has_value()) {
            candidate.style = *style;
        }
        if (!ParseMetricDefinition(value, candidate)) {
            return false;
        }
        if (definition != nullptr) {
            *definition = std::move(candidate);
        } else {
            metrics.definitions.push_back(std::move(candidate));
        }
        return true;
    }
};

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

        DispatchKnownBindingSection<AppConfig::BindingList>(config, section, key, value);
    }
}

}  // namespace

std::string LoadEmbeddedConfigTemplate() {
    return LoadUtf8Resource(IDR_CONFIG_TEMPLATE, RT_RCDATA);
}

AppConfig LoadConfig(const std::filesystem::path& path, bool includeOverlay) {
    AppConfig config;
    ApplyConfigText(LoadEmbeddedConfigTemplate(), config);
    if (includeOverlay) {
        ApplyConfigText(ReadFileUtf8(path), config);
    }
    MarkCardLayoutReferences(config.layout);
    SelectResolvedLayout(config, config.display.layout);

    const LayoutBindingSelection layoutBindings = CollectLayoutBindings(config.layout);
    config.layout.board.requestedTemperatureNames = layoutBindings.boardTemperatureNames;
    config.layout.board.requestedFanNames = layoutBindings.boardFanNames;
    return config;
}
