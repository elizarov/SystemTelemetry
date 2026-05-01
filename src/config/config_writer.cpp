#include "config/config_writer.h"

#include <array>
#include <cstdio>
#include <type_traits>

#include "config/config_parser.h"
#include "util/strings.h"
#include "util/utf8.h"

namespace {

std::string FormatHexColor(ColorConfig color) {
    return FormatHexColorText(color.ToRgba());
}

std::string FormatLogicalSize(const LogicalSizeConfig& size) {
    return std::to_string(size.width) + "," + std::to_string(size.height);
}

std::string FormatFontSpec(const UiFontConfig& font) {
    return font.face + "," + std::to_string(font.size) + "," + std::to_string(font.weight);
}

}  // namespace

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

namespace {

template <typename Codec, typename Value> std::string EncodeConfigValue(const Value& value);

template <> std::string EncodeConfigValue<configschema::IntCodec, int>(const int& value) {
    return std::to_string(value);
}

template <> std::string EncodeConfigValue<configschema::DoubleCodec, double>(const double& value) {
    return FormatDoubleGeneral(value);
}

template <> std::string EncodeConfigValue<configschema::StringCodec, std::string>(const std::string& value) {
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

template <> std::string EncodeConfigValue<configschema::HexColorCodec, ColorConfig>(const ColorConfig& value) {
    return FormatHexColor(value);
}

template <> std::string EncodeConfigValue<configschema::FontSpecCodec, UiFontConfig>(const UiFontConfig& value) {
    return FormatFontSpec(value);
}

template <>
std::string EncodeConfigValue<configschema::StringCodec, std::vector<std::string>>(
    const std::vector<std::string>& value) {
    std::string encoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i > 0) {
            encoded += ",";
        }
        encoded += value[i];
    }
    return encoded;
}

template <>
std::string EncodeConfigValue<configschema::LayoutExpressionCodec, LayoutNodeConfig>(const LayoutNodeConfig& value) {
    return FormatLayoutExpression(value);
}

struct RuntimeFieldWriter {
    std::string_view key;
    std::string (*encode)(const void* owner);
    bool (*equals)(const void* owner, const void* compareOwner);
};

template <typename Field> std::string EncodeRuntimeField(const void* owner) {
    const auto& typedOwner = *static_cast<const typename Field::owner_type*>(owner);
    return EncodeConfigValue<typename Field::codec_type>(Field::RawGet(typedOwner));
}

template <typename Field> bool RuntimeFieldEquals(const void* owner, const void* compareOwner) {
    const auto& typedOwner = *static_cast<const typename Field::owner_type*>(owner);
    const auto& typedCompareOwner = *static_cast<const typename Field::owner_type*>(compareOwner);
    return Field::RawGet(typedOwner) == Field::RawGet(typedCompareOwner);
}

template <typename... Field> constexpr auto MakeRuntimeFieldWriters(std::tuple<Field...>) {
    return std::array<RuntimeFieldWriter, sizeof...(Field)>{
        RuntimeFieldWriter{Field::key.view(), &EncodeRuntimeField<Field>, &RuntimeFieldEquals<Field>}...};
}

template <typename Section> const auto& RuntimeFieldWriters() {
    static constexpr auto fields = MakeRuntimeFieldWriters(typename Section::fields_type{});
    return fields;
}

template <typename Codec, typename Owner> struct CustomSectionHandler {
    template <typename UpdateKeyFn> static void Save(const Owner&, UpdateKeyFn&&) {}

    template <typename UpdateKeyFn> static void SaveDifferences(const Owner&, const Owner*, UpdateKeyFn&&) {}
};

template <> struct CustomSectionHandler<configschema::BoardSectionCodec, BoardConfig> {
    template <typename UpdateKeyFn> static void Save(const BoardConfig& board, UpdateKeyFn&& updateKey) {
        for (const std::string& logicalName : board.requestedTemperatureNames) {
            const auto it = board.temperatureSensorNames.find(logicalName);
            const std::string sensorName =
                it != board.temperatureSensorNames.end() && !it->second.empty() ? it->second : logicalName;
            updateKey("[board]", "board.temp." + logicalName, sensorName);
        }
        for (const std::string& logicalName : board.requestedFanNames) {
            const auto it = board.fanSensorNames.find(logicalName);
            const std::string sensorName =
                it != board.fanSensorNames.end() && !it->second.empty() ? it->second : logicalName;
            updateKey("[board]", "board.fan." + logicalName, sensorName);
        }
    }

    template <typename UpdateKeyFn>
    static void SaveDifferences(const BoardConfig& board, const BoardConfig* compareBoard, UpdateKeyFn&& updateKey) {
        const auto saveBoardKey =
            [&](const std::string& key, const std::string& currentValue, const std::string& compareValue) {
                if (compareBoard == nullptr || currentValue != compareValue) {
                    updateKey("[board]", key, currentValue);
                }
            };

        for (const std::string& logicalName : board.requestedTemperatureNames) {
            const auto currentIt = board.temperatureSensorNames.find(logicalName);
            const std::string currentValue =
                currentIt != board.temperatureSensorNames.end() && !currentIt->second.empty() ? currentIt->second
                                                                                              : logicalName;

            std::string compareValue = logicalName;
            if (compareBoard != nullptr) {
                const auto compareIt = compareBoard->temperatureSensorNames.find(logicalName);
                if (compareIt != compareBoard->temperatureSensorNames.end() && !compareIt->second.empty()) {
                    compareValue = compareIt->second;
                }
            }
            saveBoardKey("board.temp." + logicalName, currentValue, compareValue);
        }

        for (const std::string& logicalName : board.requestedFanNames) {
            const auto currentIt = board.fanSensorNames.find(logicalName);
            const std::string currentValue =
                currentIt != board.fanSensorNames.end() && !currentIt->second.empty() ? currentIt->second : logicalName;

            std::string compareValue = logicalName;
            if (compareBoard != nullptr) {
                const auto compareIt = compareBoard->fanSensorNames.find(logicalName);
                if (compareIt != compareBoard->fanSensorNames.end() && !compareIt->second.empty()) {
                    compareValue = compareIt->second;
                }
            }
            saveBoardKey("board.fan." + logicalName, currentValue, compareValue);
        }
    }
};

template <> struct CustomSectionHandler<configschema::MetricsSectionCodec, MetricsSectionConfig> {
    template <typename UpdateKeyFn> static void Save(const MetricsSectionConfig& metrics, UpdateKeyFn&& updateKey) {
        for (const auto& definition : metrics.definitions) {
            if (definition.id.empty() || IsRuntimePlaceholderMetricId(definition.id)) {
                continue;
            }
            updateKey("[metrics]", definition.id, FormatMetricDefinitionValue(definition));
        }
    }

    template <typename UpdateKeyFn>
    static void SaveDifferences(
        const MetricsSectionConfig& metrics, const MetricsSectionConfig* compareMetrics, UpdateKeyFn&& updateKey) {
        for (const auto& definition : metrics.definitions) {
            if (definition.id.empty() || IsRuntimePlaceholderMetricId(definition.id)) {
                continue;
            }
            const MetricDefinitionConfig* compareDefinition =
                compareMetrics != nullptr ? FindMetricDefinition(*compareMetrics, definition.id) : nullptr;
            const std::string currentValue = FormatMetricDefinitionValue(definition);
            const std::string compareValue =
                compareDefinition != nullptr ? FormatMetricDefinitionValue(*compareDefinition) : std::string{};
            if (compareDefinition == nullptr || currentValue != compareValue) {
                updateKey("[metrics]", definition.id, currentValue);
            }
        }
    }
};

template <typename Section, typename UpdateKeyFn>
void SaveStructuredSection(const typename Section::owner_type& owner, UpdateKeyFn&& updateKey) {
    if constexpr (std::is_same_v<typename Section::codec_type, configschema::StructuredSectionCodec>) {
        const std::string sectionName = "[" + std::string(Section::name.view()) + "]";
        for (const RuntimeFieldWriter& field : RuntimeFieldWriters<Section>()) {
            updateKey(sectionName, std::string(field.key), field.encode(&owner));
        }
    } else {
        CustomSectionHandler<typename Section::codec_type, typename Section::owner_type>::Save(
            owner, std::forward<UpdateKeyFn>(updateKey));
    }
}

template <typename Section, typename UpdateKeyFn>
void SaveDynamicStructuredSection(
    const typename Section::owner_type& owner, std::string_view suffix, UpdateKeyFn&& updateKey) {
    const std::string sectionName = Section::FormatName(suffix);
    for (const RuntimeFieldWriter& field : RuntimeFieldWriters<Section>()) {
        updateKey(sectionName, std::string(field.key), field.encode(&owner));
    }
}

template <typename Section, typename CompareOwner, typename UpdateKeyFn>
void SaveStructuredSectionDifferences(
    const typename Section::owner_type& owner, const CompareOwner* compareOwner, UpdateKeyFn&& updateKey) {
    if constexpr (std::is_same_v<typename Section::codec_type, configschema::StructuredSectionCodec>) {
        const std::string sectionName = "[" + std::string(Section::name.view()) + "]";
        for (const RuntimeFieldWriter& field : RuntimeFieldWriters<Section>()) {
            if (compareOwner == nullptr || !field.equals(&owner, compareOwner)) {
                updateKey(sectionName, std::string(field.key), field.encode(&owner));
            }
        }
    } else {
        CustomSectionHandler<typename Section::codec_type, typename Section::owner_type>::SaveDifferences(
            owner, compareOwner, std::forward<UpdateKeyFn>(updateKey));
    }
}

template <typename Section, typename CompareOwner, typename UpdateKeyFn>
void SaveDynamicStructuredSectionDifferences(const typename Section::owner_type& owner,
    std::string_view suffix,
    const CompareOwner* compareOwner,
    UpdateKeyFn&& updateKey) {
    const std::string sectionName = Section::FormatName(suffix);
    for (const RuntimeFieldWriter& field : RuntimeFieldWriters<Section>()) {
        if (compareOwner == nullptr || !field.equals(&owner, compareOwner)) {
            updateKey(sectionName, std::string(field.key), field.encode(&owner));
        }
    }
}

template <typename BindingList, typename Owner, typename Fn> void ForEachKnownBinding(Owner&& owner, Fn&& fn) {
    BindingList::ForEach([&](auto binding) { fn(std::remove_cvref_t<decltype(binding)>{}, owner); });
}

template <typename BindingList, typename Owner, typename UpdateKeyFn>
void SaveKnownSections(const Owner& owner, UpdateKeyFn&& updateKey) {
    ForEachKnownBinding<BindingList>(owner, [&](auto binding, const auto& currentOwner) {
        using Binding = decltype(binding);
        if constexpr (Binding::is_recursive) {
            SaveKnownSections<typename Binding::nested_owner_type::BindingList>(Binding::Get(currentOwner), updateKey);
        } else if constexpr (Binding::is_dynamic) {
            using Section = typename Binding::section_type;
            for (const auto& item : Binding::Get(currentOwner)) {
                SaveDynamicStructuredSection<Section>(item, Binding::Key(item), updateKey);
            }
        } else {
            using Section = typename Binding::section_type;
            SaveStructuredSection<Section>(Binding::Get(currentOwner), updateKey);
        }
    });
}

template <typename BindingList, typename Owner, typename CompareOwner, typename UpdateKeyFn>
void SaveKnownSectionDifferences(const Owner& owner, const CompareOwner* compareOwner, UpdateKeyFn&& updateKey) {
    ForEachKnownBinding<BindingList>(owner, [&](auto binding, const auto& currentOwner) {
        using Binding = decltype(binding);
        if constexpr (Binding::is_recursive) {
            const typename Binding::nested_owner_type* compareNestedOwner =
                compareOwner != nullptr ? &Binding::Get(*compareOwner) : nullptr;
            SaveKnownSectionDifferences<typename Binding::nested_owner_type::BindingList>(
                Binding::Get(currentOwner), compareNestedOwner, updateKey);
        } else if constexpr (Binding::is_dynamic) {
            using Section = typename Binding::section_type;
            for (const auto& item : Binding::Get(currentOwner)) {
                const typename Binding::item_type* compareItem =
                    compareOwner != nullptr ? Binding::Find(*compareOwner, Binding::Key(item)) : nullptr;
                SaveDynamicStructuredSectionDifferences<Section>(item, Binding::Key(item), compareItem, updateKey);
            }
        } else {
            using CurrentSection = typename Binding::section_type;
            using CurrentOwner = typename CurrentSection::owner_type;
            const CurrentOwner* compareSectionOwner = compareOwner != nullptr ? &Binding::Get(*compareOwner) : nullptr;
            SaveStructuredSectionDifferences<CurrentSection>(
                Binding::Get(currentOwner), compareSectionOwner, updateKey);
        }
    });
}

std::string ReadFileUtf8(const FilePath& path) {
    std::FILE* input = nullptr;
    if (_wfopen_s(&input, path.c_str(), L"rb") != 0 || input == nullptr) {
        return {};
    }

    fseek(input, 0, SEEK_END);
    const long size = ftell(input);
    if (size < 0) {
        fclose(input);
        return {};
    }
    fseek(input, 0, SEEK_SET);
    std::string text(static_cast<size_t>(size), '\0');
    if (!text.empty()) {
        const size_t read = fread(text.data(), 1, text.size(), input);
        if (read != text.size()) {
            fclose(input);
            return {};
        }
    }
    fclose(input);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!IsValidUtf8(text)) {
        return {};
    }
    return text;
}

bool WriteFileUtf8(const FilePath& path, const std::string& text) {
    if (!IsValidUtf8(text)) {
        return false;
    }

    std::FILE* output = nullptr;
    if (_wfopen_s(&output, path.c_str(), L"wb") != 0 || output == nullptr) {
        return false;
    }

    const bool written = fwrite(text.data(), 1, text.size(), output) == text.size();
    const bool closed = fclose(output) == 0;
    return written && closed;
}

void ReplaceOrAppendKey(std::vector<std::string>& lines,
    size_t sectionStart,
    size_t sectionEnd,
    const std::string& key,
    const std::string& value,
    bool appendWhenMissing) {
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

    if (appendWhenMissing) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), key + " = " + value);
    }
}

std::vector<std::string> SplitConfigLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        if (lineEnd == text.size()) {
            break;
        }
        lineStart = lineEnd + 1;
    }
    return lines;
}

std::string JoinConfigLines(const std::vector<std::string>& lines) {
    std::string output;
    for (const std::string& line : lines) {
        output += line;
        output += "\r\n";
    }
    return output;
}

template <typename UpdateKeyFn> void SaveKnownStructuredSections(const AppConfig& config, UpdateKeyFn&& updateKey) {
    SaveKnownSections<AppConfig::BindingList>(config, updateKey);
}

template <typename UpdateKeyFn>
void SaveKnownStructuredSectionDifferences(
    const AppConfig& config, const AppConfig* compareConfig, UpdateKeyFn&& updateKey) {
    SaveKnownSectionDifferences<AppConfig::BindingList>(config, compareConfig, updateKey);
}

}  // namespace

std::string BuildSavedConfigText(
    const std::string& initialText, const AppConfig& config, const AppConfig* compareConfig, ConfigSaveShape shape) {
    std::vector<std::string> lines = SplitConfigLines(initialText);

    const auto findSectionIndex = [&lines](const std::string& sectionName) -> size_t {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (Trim(lines[i]) == sectionName) {
                return i;
            }
        }
        return lines.size();
    };

    const auto ensureSection = [&lines, &findSectionIndex, shape](const std::string& sectionName) -> size_t {
        const size_t existingIndex = findSectionIndex(sectionName);
        if (existingIndex < lines.size()) {
            return existingIndex;
        }
        if (shape == ConfigSaveShape::ExistingTemplateOnly) {
            return lines.size();
        }
        if (!lines.empty() && !lines.back().empty()) {
            lines.push_back("");
        }
        lines.push_back(sectionName);
        return lines.size() - 1;
    };

    const auto ensureSectionAfter = [&lines, &findSectionIndex, shape](
                                        const std::string& sectionName, const std::string& afterSectionName) -> size_t {
        const size_t existingIndex = findSectionIndex(sectionName);
        if (existingIndex < lines.size()) {
            return existingIndex;
        }
        if (shape == ConfigSaveShape::ExistingTemplateOnly) {
            return lines.size();
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

        lines.insert(
            lines.begin() + static_cast<std::ptrdiff_t>(insertIndex), insertedLines.begin(), insertedLines.end());
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

    const auto updateKey = [&lines, &ensureSection, &ensureSectionAfter, &findSectionEnd, shape](
                               const std::string& sectionName, const std::string& key, const std::string& value) {
        size_t sectionStart = sectionName == "[storage]"   ? ensureSectionAfter(sectionName, "[network]")
                              : sectionName == "[board]"   ? ensureSectionAfter(sectionName, "[storage]")
                              : sectionName == "[metrics]" ? ensureSectionAfter(sectionName, "[board]")
                                                           : ensureSection(sectionName);
        if (sectionStart >= lines.size()) {
            return;
        }
        if (Trim(lines[sectionStart]) != sectionName) {
            lines[sectionStart] = sectionName;
        }
        const size_t sectionEnd = findSectionEnd(sectionStart);
        ReplaceOrAppendKey(lines, sectionStart, sectionEnd, key, value, shape == ConfigSaveShape::UpdateOrAppend);
    };

    if (compareConfig == nullptr) {
        SaveKnownStructuredSections(config, updateKey);
    } else {
        SaveKnownStructuredSectionDifferences(config, compareConfig, updateKey);
    }
    return JoinConfigLines(lines);
}

bool SaveConfig(const FilePath& path, const AppConfig& config, const ConfigParseContext& context) {
    const AppConfig compareConfig = LoadConfig(path, true, context);
    const std::string output = BuildSavedConfigText(ReadFileUtf8(path), config, &compareConfig);
    return WriteFileUtf8(path, output);
}

bool SaveFullConfig(const FilePath& path, const AppConfig& config) {
    const std::string output =
        BuildSavedConfigText(LoadEmbeddedConfigTemplate(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);
    return WriteFileUtf8(path, output);
}
