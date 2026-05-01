#include "config/config_parser.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <set>
#include <type_traits>

#include "config/config_file_io.h"
#include "config/config_resolution.h"
#include "config/config_runtime_fields.h"
#include "config/widget_class.h"
#include "resource.h"
#include "util/resource_loader.h"
#include "util/strings.h"

namespace {

bool ParseMetricDefinition(
    const std::string& value, MetricDefinitionConfig& definition, const ConfigParseContext& context) {
    const std::vector<std::string> parts = SplitTrimmedPreservingEmpty(value, ',');
    const std::optional<MetricDisplayStyle> metadataStyle = context.metricCatalog.FindMetricDisplayStyle(definition.id);
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
        errno = 0;
        char* end = nullptr;
        const double parsedScale = std::strtod(parts[0].c_str(), &end);
        if (errno != 0 || end == parts[0].c_str() || *end != '\0') {
            return false;
        }
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

template <typename Section>
bool ApplyStructuredSectionFields(
    typename Section::owner_type& owner, const std::string& key, const std::string& value) {
    for (const RuntimeConfigFieldDescriptor& field : RuntimeConfigFieldDescriptors<Section>()) {
        if (key == field.key) {
            DecodeRuntimeConfigField(field, &owner, value);
            return true;
        }
    }
    return false;
}

template <typename Codec, typename Owner> struct CustomSectionHandler {
    static bool Apply(Owner&, const std::string&, const std::string&, const ConfigParseContext&) {
        return false;
    }
};

template <typename Section>
bool ApplySectionValue(typename Section::owner_type& owner,
    const std::string& key,
    const std::string& value,
    const ConfigParseContext& context) {
    if constexpr (std::is_same_v<typename Section::codec_type, configschema::StructuredSectionCodec>) {
        return ApplyStructuredSectionFields<Section>(owner, key, value);
    } else {
        return CustomSectionHandler<typename Section::codec_type, typename Section::owner_type>::Apply(
            owner, key, value, context);
    }
}

template <typename BindingList, typename Owner, typename Fn> void ForEachKnownBinding(Owner&& owner, Fn&& fn) {
    BindingList::ForEach([&](auto binding) { fn(std::remove_cvref_t<decltype(binding)>{}, owner); });
}

template <typename BindingList, typename Owner>
bool DispatchKnownBindingSection(Owner& owner,
    const std::string& section,
    const std::string& key,
    const std::string& value,
    const ConfigParseContext& context) {
    bool handled = false;
    ForEachKnownBinding<BindingList>(owner, [&](auto binding, auto& currentOwner) {
        using Binding = decltype(binding);
        if (!handled) {
            if constexpr (Binding::is_recursive) {
                handled = DispatchKnownBindingSection<typename Binding::nested_owner_type::BindingList>(
                    Binding::Get(currentOwner), section, key, value, context);
            } else if constexpr (Binding::is_dynamic) {
                using Section = typename Binding::section_type;
                if (Section::Matches(section)) {
                    typename Section::owner_type& item = Binding::Ensure(currentOwner, Section::Suffix(section));
                    ApplySectionValue<Section>(item, key, value, context);
                    handled = true;
                }
            } else {
                using Section = typename Binding::section_type;
                if (section == Section::name.view()) {
                    ApplySectionValue<Section>(Binding::Get(currentOwner), key, value, context);
                    handled = true;
                }
            }
        }
    });
    return handled;
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
        errno = 0;
        const std::string token = text_.substr(begin, index_ - begin);
        char* end = nullptr;
        const long parsed = std::strtol(token.c_str(), &end, 10);
        if (errno != 0 || end == token.c_str() || *end != '\0') {
            return fallback;
        }
        return static_cast<int>(parsed);
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
    return name == "rows" || name == "columns" || (!name.empty() && EnumFromString<WidgetClass>(name).has_value());
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

template <> struct CustomSectionHandler<configschema::BoardSectionCodec, BoardConfig> {
    static bool Apply(BoardConfig& board, const std::string& key, const std::string& value, const ConfigParseContext&) {
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
    static bool Apply(MetricsSectionConfig& metrics,
        const std::string& key,
        const std::string& value,
        const ConfigParseContext& context) {
        if (key.empty()) {
            return false;
        }
        if (IsRuntimePlaceholderMetricId(key)) {
            return true;
        }

        MetricDefinitionConfig* definition = FindMetricDefinition(metrics, key);
        MetricDefinitionConfig candidate = definition != nullptr ? *definition : MetricDefinitionConfig{};
        candidate.id = key;
        if (const auto style = context.metricCatalog.FindMetricDisplayStyle(key); style.has_value()) {
            candidate.style = *style;
        }
        if (!ParseMetricDefinition(value, candidate, context)) {
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

void ApplyConfigText(const std::string& text, AppConfig& config, const ConfigParseContext& context) {
    std::string section;

    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }

        std::string line = text.substr(lineStart, lineEnd - lineStart);
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            if (lineEnd == text.size()) {
                break;
            }
            lineStart = lineEnd + 1;
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            if (lineEnd == text.size()) {
                break;
            }
            lineStart = lineEnd + 1;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            if (lineEnd == text.size()) {
                break;
            }
            lineStart = lineEnd + 1;
            continue;
        }

        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));

        DispatchKnownBindingSection<AppConfig::BindingList>(config, section, key, value, context);
        if (lineEnd == text.size()) {
            break;
        }
        lineStart = lineEnd + 1;
    }
}

}  // namespace

std::string LoadEmbeddedConfigTemplate() {
    return LoadUtf8ResourceData(IDR_CONFIG_TEMPLATE);
}

AppConfig LoadConfig(const FilePath& path, bool includeOverlay, const ConfigParseContext& context) {
    AppConfig config;
    ApplyConfigText(LoadEmbeddedConfigTemplate(), config, context);
    if (includeOverlay) {
        ApplyConfigText(ReadConfigFileUtf8(path), config, context);
    }
    MarkCardLayoutReferences(config.layout);
    SelectResolvedLayout(config, config.display.layout);

    const LayoutBindingSelection layoutBindings = CollectLayoutBindings(config.layout);
    config.layout.board.requestedTemperatureNames = layoutBindings.boardTemperatureNames;
    config.layout.board.requestedFanNames = layoutBindings.boardFanNames;
    return config;
}
