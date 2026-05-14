#include "widget/widget_host.h"

WidgetAnimationLayer WidgetHost::CurrentWidgetAnimationLayer() const {
    return WidgetAnimationLayer::Snapshot;
}

void WidgetHost::AddWidgetAnimation(WidgetAnimationPtr animation) {
    if (animation == nullptr) {
        return;
    }
    WidgetAnimationStatePtr target = animation->TargetState();
    if (target == nullptr) {
        return;
    }
    animation->Draw(Renderer(), *target);
}
