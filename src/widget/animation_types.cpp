#include "widget/animation_types.h"

#include <functional>

bool AnimationDataKey::operator==(const AnimationDataKey& other) const {
    return kind == other.kind && subject == other.subject && lane == other.lane;
}

size_t AnimationDataKeyHash::operator()(const AnimationDataKey& key) const {
    const size_t kindHash = std::hash<int>{}(static_cast<int>(key.kind));
    const size_t subjectHash = std::hash<std::string>{}(key.subject);
    const size_t laneHash = std::hash<std::string>{}(key.lane);
    return kindHash ^ (subjectHash + 0x9e3779b9u + (kindHash << 6) + (kindHash >> 2)) ^
           (laneHash + 0x9e3779b9u + (subjectHash << 6) + (subjectHash >> 2));
}
