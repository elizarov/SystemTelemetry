#include "widget/widget_host.h"

void WidgetHost::AddWidgetAnimation(WidgetAnimationPtr animation, WidgetAnimationStatePtr targetState) {
    if (animation == nullptr || targetState == nullptr) {
        return;
    }
    animation->Draw(Renderer(), *targetState);
}
