#include "config/config_writer.h"

#include <string_view>

#include "config/config_file_io.h"
#include "config/config_parser.h"
#include "config/config_runtime_fields.h"
#include "config/config_telemetry.h"
#include "util/strings.h"
#include "util/text_format.h"

namespace {

constexpr std::string_view kLayoutGuideSheetSectionName = "layout_guide_sheet";
constexpr std::string_view kLayoutGuideSheetHeader = "[layout_guide_sheet]";
constexpr size_t kUnknownConfigSectionOrder = static_cast<size_t>(-1);

std::string RuntimeSectionName(const RuntimeConfigSectionDescriptor& section) {
    return FormatText("[%.*s]", static_cast<int>(section.nameLength), section.name);
}

std::string RuntimeDynamicSectionName(const RuntimeConfigSectionDescriptor& section, std::string_view suffix) {
    return FormatText("[%.*s%.*s]",
        static_cast<int>(section.nameLength),
        section.name,
        static_cast<int>(suffix.size()),
        suffix.data());
}

template <typename UpdateKeyFn>
void SaveBoardSectionDifferences(
    const BoardConfig& board, const BoardConfig* compareBoard, const std::string& sectionName, UpdateKeyFn& updateKey) {
    const auto saveBoardKey =
        [&](const std::string& key, const std::string& currentValue, const std::string& compareValue) {
            if (compareBoard == nullptr || currentValue != compareValue) {
                updateKey(sectionName, key, currentValue);
            }
        };

    for (const std::string& logicalName : board.requestedTemperatureNames) {
        const auto currentIt = board.temperatureSensorNames.find(logicalName);
        const std::string currentValue = currentIt != board.temperatureSensorNames.end() && !currentIt->second.empty()
                                             ? currentIt->second
                                             : logicalName;

        std::string compareValue = logicalName;
        if (compareBoard != nullptr) {
            const auto compareIt = compareBoard->temperatureSensorNames.find(logicalName);
            if (compareIt != compareBoard->temperatureSensorNames.end() && !compareIt->second.empty()) {
                compareValue = compareIt->second;
            }
        }
        saveBoardKey(FormatText("board.temp.%s", logicalName.c_str()), currentValue, compareValue);
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
        saveBoardKey(FormatText("board.fan.%s", logicalName.c_str()), currentValue, compareValue);
    }
}

template <typename UpdateKeyFn>
void SaveMetricsSectionDifferences(const MetricsSectionConfig& metrics,
    const MetricsSectionConfig* compareMetrics,
    const std::string& sectionName,
    UpdateKeyFn& updateKey) {
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
            updateKey(sectionName, definition.id, currentValue);
        }
    }
}

template <typename UpdateKeyFn>
void SaveStructuredSectionDifferences(const RuntimeConfigSectionDescriptor& section,
    const void* owner,
    const void* compareOwner,
    const std::string& sectionName,
    UpdateKeyFn& updateKey) {
    for (const RuntimeConfigFieldDescriptor& field : RuntimeConfigFields(section)) {
        if (compareOwner == nullptr || !RuntimeConfigFieldEquals(field, owner, compareOwner)) {
            updateKey(sectionName, std::string(field.key, field.keyLength), EncodeRuntimeConfigField(field, owner));
        }
    }
}

template <typename UpdateKeyFn>
void SaveSectionDifferences(const RuntimeConfigSectionDescriptor& section,
    const void* owner,
    const void* compareOwner,
    const std::string& sectionName,
    UpdateKeyFn& updateKey) {
    switch (section.codec) {
        case RuntimeConfigSectionCodec::Structured:
            SaveStructuredSectionDifferences(section, owner, compareOwner, sectionName, updateKey);
            break;
        case RuntimeConfigSectionCodec::Board:
            SaveBoardSectionDifferences(*reinterpret_cast<const BoardConfig*>(owner),
                reinterpret_cast<const BoardConfig*>(compareOwner),
                sectionName,
                updateKey);
            break;
        case RuntimeConfigSectionCodec::Metrics:
            SaveMetricsSectionDifferences(*reinterpret_cast<const MetricsSectionConfig*>(owner),
                reinterpret_cast<const MetricsSectionConfig*>(compareOwner),
                sectionName,
                updateKey);
            break;
    }
}

template <typename UpdateKeyFn> struct DynamicSectionSaveContext {
    const RuntimeConfigSectionDescriptor* section = nullptr;
    const AppConfig* compareConfig = nullptr;
    UpdateKeyFn* updateKey = nullptr;
};

template <typename UpdateKeyFn> void SaveDynamicSectionItem(void* context, std::string_view suffix, const void* item) {
    auto& saveContext = *reinterpret_cast<DynamicSectionSaveContext<UpdateKeyFn>*>(context);
    const void* compareItem = saveContext.compareConfig != nullptr
                                  ? saveContext.section->dynamic.find(*saveContext.compareConfig, suffix)
                                  : nullptr;
    SaveSectionDifferences(*saveContext.section,
        item,
        compareItem,
        RuntimeDynamicSectionName(*saveContext.section, suffix),
        *saveContext.updateKey);
}

template <typename UpdateKeyFn>
void SaveKnownSectionDifferences(const AppConfig& config, const AppConfig* compareConfig, UpdateKeyFn& updateKey) {
    for (const RuntimeConfigSectionDescriptor& section : RuntimeConfigSectionDescriptors()) {
        if (std::string_view(section.name, section.nameLength) == kLayoutGuideSheetSectionName) {
            continue;
        }
        if (section.kind == RuntimeConfigSectionKind::Dynamic) {
            DynamicSectionSaveContext<UpdateKeyFn> context{&section, compareConfig, &updateKey};
            section.dynamic.forEach(config, &context, SaveDynamicSectionItem<UpdateKeyFn>);
        } else {
            const void* owner = reinterpret_cast<const char*>(&config) + section.rootOffset;
            const void* compareOwner =
                compareConfig != nullptr ? reinterpret_cast<const char*>(compareConfig) + section.rootOffset : nullptr;
            SaveSectionDifferences(section, owner, compareOwner, RuntimeSectionName(section), updateKey);
        }
    }
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
            lines[i] = FormatText("%s = %s", key.c_str(), value.c_str());
            return;
        }
    }

    if (appendWhenMissing) {
        size_t appendIndex = sectionEnd;
        while (appendIndex > sectionStart + 1 && Trim(lines[appendIndex - 1]).empty()) {
            --appendIndex;
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(appendIndex),
            FormatText("%s = %s", key.c_str(), value.c_str()));
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

bool IsConfigSectionHeader(const std::string& line) {
    const std::string trimmed = Trim(line);
    return !trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']';
}

size_t FindConfigSectionIndex(const std::vector<std::string>& lines, std::string_view sectionName) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (Trim(lines[i]) == sectionName) {
            return i;
        }
    }
    return lines.size();
}

size_t FindConfigSectionEnd(const std::vector<std::string>& lines, size_t sectionStart) {
    size_t sectionEnd = lines.size();
    for (size_t j = sectionStart + 1; j < lines.size(); ++j) {
        if (IsConfigSectionHeader(lines[j])) {
            sectionEnd = j;
            break;
        }
    }
    return sectionEnd;
}

std::string_view UnwrapConfigSectionName(std::string_view sectionName) {
    if (sectionName.size() >= 2 && sectionName.front() == '[' && sectionName.back() == ']') {
        sectionName.remove_prefix(1);
        sectionName.remove_suffix(1);
    }
    return sectionName;
}

bool RuntimeConfigSectionMatchesName(const RuntimeConfigSectionDescriptor& section, std::string_view sectionName) {
    const std::string_view descriptorName(section.name, section.nameLength);
    if (section.kind == RuntimeConfigSectionKind::Dynamic) {
        return sectionName.starts_with(descriptorName) && sectionName.size() > descriptorName.size();
    }
    return sectionName == descriptorName;
}

size_t ConfigSectionOrderIndex(std::string_view sectionName) {
    const std::string_view unwrappedName = UnwrapConfigSectionName(sectionName);
    for (const RuntimeConfigSectionDescriptor& section : RuntimeConfigSectionDescriptors()) {
        if (RuntimeConfigSectionMatchesName(section, unwrappedName)) {
            return section.defaultOrder;
        }
    }
    return kUnknownConfigSectionOrder;
}

size_t InsertConfigSection(std::vector<std::string>& lines, size_t insertIndex, const std::string& sectionName) {
    std::vector<std::string> insertedLines;
    if (insertIndex > 0 && !lines[insertIndex - 1].empty()) {
        insertedLines.push_back("");
    }
    insertedLines.push_back(sectionName);
    if (insertIndex < lines.size() && !lines[insertIndex].empty()) {
        insertedLines.push_back("");
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertIndex), insertedLines.begin(), insertedLines.end());
    return insertIndex + (insertedLines.front().empty() ? 1 : 0);
}

size_t EnsureConfigSectionInDefaultOrder(
    std::vector<std::string>& lines, const std::string& sectionName, ConfigSaveShape shape) {
    const size_t existingIndex = FindConfigSectionIndex(lines, sectionName);
    if (existingIndex < lines.size()) {
        return existingIndex;
    }
    if (shape == ConfigSaveShape::ExistingTemplateOnly) {
        return lines.size();
    }

    const size_t targetOrder = ConfigSectionOrderIndex(sectionName);
    if (targetOrder != kUnknownConfigSectionOrder) {
        size_t insertAfterKnownSectionEnd = lines.size();
        for (size_t i = 0; i < lines.size(); ++i) {
            if (!IsConfigSectionHeader(lines[i])) {
                continue;
            }
            const std::string existingSection = Trim(lines[i]);
            const size_t existingOrder = ConfigSectionOrderIndex(existingSection);
            if (existingOrder > targetOrder && existingOrder != kUnknownConfigSectionOrder) {
                return InsertConfigSection(lines, i, sectionName);
            }
            if (existingOrder < targetOrder) {
                insertAfterKnownSectionEnd = FindConfigSectionEnd(lines, i);
            }
        }
        return InsertConfigSection(lines, insertAfterKnownSectionEnd, sectionName);
    }

    if (!lines.empty() && !lines.back().empty()) {
        lines.push_back("");
    }
    lines.push_back(sectionName);
    return lines.size() - 1;
}

void RemoveConfigSection(std::vector<std::string>& lines, std::string_view sectionName) {
    const size_t sectionStart = FindConfigSectionIndex(lines, sectionName);
    if (sectionStart >= lines.size()) {
        return;
    }
    size_t eraseStart = sectionStart;
    size_t eraseEnd = FindConfigSectionEnd(lines, sectionStart);
    if (eraseStart > 0 && lines[eraseStart - 1].empty()) {
        --eraseStart;
    }
    lines.erase(
        lines.begin() + static_cast<std::ptrdiff_t>(eraseStart), lines.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
}

std::string JoinConfigLines(const std::vector<std::string>& lines) {
    std::string output;
    for (const std::string& line : lines) {
        AppendFormat(output, "%s\r\n", line.c_str());
    }
    return output;
}

void RemoveLeadingEmptyLines(std::vector<std::string>& lines) {
    while (!lines.empty() && Trim(lines.front()).empty()) {
        lines.erase(lines.begin());
    }
}

template <typename UpdateKeyFn>
void SaveKnownStructuredSectionDifferences(
    const AppConfig& config, const AppConfig* compareConfig, UpdateKeyFn& updateKey) {
    SaveKnownSectionDifferences(config, compareConfig, updateKey);
}

}  // namespace

std::string BuildSavedConfigText(
    const std::string& initialText, const AppConfig& config, const AppConfig* compareConfig, ConfigSaveShape shape) {
    std::vector<std::string> lines = SplitConfigLines(initialText);
    RemoveConfigSection(lines, kLayoutGuideSheetHeader);

    const auto updateKey = [&lines, shape](
                               const std::string& sectionName, const std::string& key, const std::string& value) {
        size_t sectionStart = EnsureConfigSectionInDefaultOrder(lines, sectionName, shape);
        if (sectionStart >= lines.size()) {
            return;
        }
        if (Trim(lines[sectionStart]) != sectionName) {
            lines[sectionStart] = sectionName;
        }
        const size_t sectionEnd = FindConfigSectionEnd(lines, sectionStart);
        ReplaceOrAppendKey(lines, sectionStart, sectionEnd, key, value, shape == ConfigSaveShape::UpdateOrAppend);
    };

    SaveKnownStructuredSectionDifferences(config, compareConfig, updateKey);
    RemoveLeadingEmptyLines(lines);
    return JoinConfigLines(lines);
}

bool LayoutConfigHasDifferences(const LayoutConfig& config, const LayoutConfig& compareConfig) {
    if (config.structure != compareConfig.structure) {
        return true;
    }
    AppConfig current;
    current.layout = config;
    AppConfig saved;
    saved.layout = compareConfig;
    return !BuildSavedConfigText("", current, &saved).empty();
}

bool SaveConfig(const FilePath& path, const AppConfig& config, const ConfigParseContext& context) {
    const AppConfig compareConfig = LoadConfig(path, true, context);
    const std::string output = BuildSavedConfigText(ReadConfigFile(path), config, &compareConfig);
    return WriteConfigFile(path, output);
}

bool SaveFullConfig(const FilePath& path, const AppConfig& config) {
    const std::string output =
        BuildSavedConfigText(LoadEmbeddedConfigTemplate(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);
    return WriteConfigFile(path, output);
}
