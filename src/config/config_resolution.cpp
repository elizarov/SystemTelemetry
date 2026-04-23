#include "config/config_resolution.h"

#include <algorithm>
#include <cctype>

#include "util/strings.h"

namespace {

bool IsValidMetricId(std::string_view metricId) {
    if (metricId.empty()) {
        return false;
    }
    for (const unsigned char ch : metricId) {
        if (std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-') {
            continue;
        }
        return false;
    }
    return true;
}

void AddUniqueValue(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void CollectLayoutBindingsRecursive(
    const LayoutNodeConfig& node, std::vector<std::string>& boardTemperatures, std::vector<std::string>& boardFans) {
    for (const std::string& metricRef : SplitTrimmed(node.parameter, ',')) {
        if (!IsValidMetricId(metricRef)) {
            continue;
        }
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

std::string NormalizeConfiguredDriveLetter(const std::string& drive) {
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
        AddUniqueValue(normalizedDrives, NormalizeConfiguredDriveLetter(drive));
    }
    return normalizedDrives;
}

bool SelectResolvedLayout(AppConfig& config, const std::string& requestedName) {
    const LayoutSectionConfig* selected = nullptr;
    if (!requestedName.empty()) {
        for (const auto& layout : config.layout.layouts) {
            if (layout.name == requestedName) {
                selected = &layout;
                break;
            }
        }
    }
    if (selected == nullptr && !config.layout.layouts.empty()) {
        selected = &config.layout.layouts.front();
    }
    if (selected == nullptr) {
        return false;
    }

    config.display.layout = selected->name;
    config.layout.structure.window = selected->window;
    config.layout.structure.cardsLayout = selected->cardsLayout;
    return true;
}

bool SelectLayout(AppConfig& config, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const auto& layout : config.layout.layouts) {
        if (layout.name == name) {
            config.display.layout = layout.name;
            config.layout.structure.window = layout.window;
            config.layout.structure.cardsLayout = layout.cardsLayout;
            return true;
        }
    }
    return false;
}
