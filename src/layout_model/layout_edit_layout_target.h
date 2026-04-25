#pragma once

#include <string>
#include <vector>

#include "widget/layout_edit_types.h"

struct LayoutEditLayoutTarget {
    std::string editCardId;
    std::vector<size_t> nodePath;

    static LayoutEditLayoutTarget ForGuide(const LayoutEditGuide& guide);
};
