#include "layout_model/layout_edit_hit_priority.h"

#include "layout_model/layout_edit_helpers.h"

int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter) {
    return static_cast<int>(parameter);
}

int LayoutEditAnchorHitPriority(const LayoutEditAnchorKey& key) {
    if (const auto parameter = LayoutEditAnchorParameter(key); parameter.has_value()) {
        return GetLayoutEditParameterHitPriority(*parameter);
    }
    return -1;
}
