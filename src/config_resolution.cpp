#include "config_resolution.h"

#include <algorithm>
#include <cctype>

namespace {

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : input) {
        if (ch == delimiter) {
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

void AddUniqueValue(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

std::string ExtractMetricReference(const std::string& token) {
    const size_t equals = token.find('=');
    return Trim(token.substr(0, equals));
}

void CollectLayoutBindingsRecursive(const LayoutNodeConfig& node,
    std::vector<std::string>& boardTemperatures, std::vector<std::string>& boardFans) {
    for (const std::string& token : Split(node.parameter, ',')) {
        const std::string metricRef = ExtractMetricReference(token);
        if (metricRef.rfind("board.temp.", 0) == 0) {
            AddUniqueValue(boardTemperatures, metricRef.substr(std::string("board.temp.").size()));
        } else if (metricRef.rfind("board.fan.", 0) == 0) {
            AddUniqueValue(boardFans, metricRef.substr(std::string("board.fan.").size()));
        }
    }

    for (const auto& child : node.children) {
        CollectLayoutBindingsRecursive(child, boardTemperatures, boardFans);
    }
}

std::string NormalizeDriveLetter(const std::string& drive) {
    const std::string trimmed = Trim(drive);
    if (trimmed.empty()) {
        return {};
    }

    const unsigned char ch = static_cast<unsigned char>(trimmed.front());
    if (!std::isalpha(ch)) {
        return {};
    }
    return std::string(1, static_cast<char>(std::toupper(ch)));
}

}  // namespace

LayoutBindingSelection CollectLayoutBindings(const LayoutConfig& layout) {
    LayoutBindingSelection result;
    for (const auto& card : layout.cards) {
        CollectLayoutBindingsRecursive(card.layout, result.boardTemperatureNames, result.boardFanNames);
    }
    return result;
}

std::vector<std::string> NormalizeConfiguredDrives(const std::vector<std::string>& drives) {
    std::vector<std::string> normalizedDrives;
    for (const auto& drive : drives) {
        AddUniqueValue(normalizedDrives, NormalizeDriveLetter(drive));
    }
    return normalizedDrives;
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
