#include "layout_edit/layout_edit_hit_priority.h"

int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter) {
    return static_cast<int>(parameter);
}

int LayoutEditAnchorHitPriority(const LayoutEditAnchorKey& key) {
    if (const auto parameter = LayoutEditAnchorParameter(key); parameter.has_value()) {
        return GetLayoutEditParameterHitPriority(*parameter);
    }
    return -1;
}
