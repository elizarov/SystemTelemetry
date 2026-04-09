#include "config.h"

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
