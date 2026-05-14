#pragma once

#include <memory>

#include "widget/animation_types.h"

class Renderer;
class WidgetAnimationState;
class WidgetAnimationTransition;

using WidgetAnimationStatePtr = std::unique_ptr<WidgetAnimationState>;
using WidgetAnimationTransitionPtr = std::unique_ptr<WidgetAnimationTransition>;

enum class WidgetAnimationLayer {
    Snapshot,
    Overlay,
};

class WidgetAnimationTransition {
public:
    virtual ~WidgetAnimationTransition() = default;

    virtual WidgetAnimationStatePtr Sample(double progress) const = 0;
    virtual bool HasActiveChange() const = 0;
};

class WidgetAnimationState {
public:
    virtual ~WidgetAnimationState() = default;

    virtual const void* TypeToken() const = 0;
    virtual WidgetAnimationStatePtr Clone() const = 0;
    virtual bool Equals(const WidgetAnimationState& other) const = 0;
    virtual WidgetAnimationStatePtr InitialState() const = 0;
    virtual WidgetAnimationStatePtr RetargetStart(const WidgetAnimationState& sampled) const = 0;
    virtual WidgetAnimationTransitionPtr TransitionFrom(const WidgetAnimationState& start) const = 0;
};

class WidgetAnimation {
public:
    virtual ~WidgetAnimation() = default;

    virtual const AnimationDataKey& Key() const = 0;
    virtual WidgetAnimationLayer Layer() const = 0;
    virtual WidgetAnimationStatePtr TargetState() const = 0;
    virtual void Draw(::Renderer& renderer, const WidgetAnimationState& state) const = 0;
};

using WidgetAnimationPtr = std::unique_ptr<WidgetAnimation>;
