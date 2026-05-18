#include "widget/animation_types.h"

bool AnimationDataKey::operator==(const AnimationDataKey& other) const {
    return subject == other.subject && lane == other.lane;
}
