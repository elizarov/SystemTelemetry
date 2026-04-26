#include "layout_model/layout_edit_active_region.h"

LayoutEditActiveRegions::LayoutEditActiveRegions(std::vector<LayoutEditActiveRegion> regions)
    : regions_(std::move(regions)) {}

LayoutEditActiveRegions::LayoutEditActiveRegions(std::initializer_list<LayoutEditActiveRegion> regions)
    : regions_(regions) {}

void LayoutEditActiveRegions::Reserve(size_t count) {
    regions_.reserve(count);
}

void LayoutEditActiveRegions::Add(LayoutEditActiveRegion region) {
    regions_.push_back(std::move(region));
}

bool LayoutEditActiveRegions::Empty() const {
    return regions_.empty();
}

size_t LayoutEditActiveRegions::Size() const {
    return regions_.size();
}

LayoutEditActiveRegions::const_iterator LayoutEditActiveRegions::begin() const {
    return regions_.begin();
}

LayoutEditActiveRegions::const_iterator LayoutEditActiveRegions::end() const {
    return regions_.end();
}

LayoutEditActiveRegions::const_reverse_iterator LayoutEditActiveRegions::rbegin() const {
    return regions_.rbegin();
}

LayoutEditActiveRegions::const_reverse_iterator LayoutEditActiveRegions::rend() const {
    return regions_.rend();
}
