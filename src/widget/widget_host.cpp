#include "widget/widget_host.h"

void WidgetHost::AddWidgetAnimation(
    WidgetAnimationPtr animation, WidgetAnimationStatePtr targetState, std::optional<RenderRect> clipRect) {
    if (animation == nullptr || targetState == nullptr) {
        return;
    }
    if (clipRect.has_value()) {
        if (clipRect->IsEmpty()) {
            return;
        }
        Renderer().PushClipRect(*clipRect);
    }
    animation->Draw(Renderer(), *targetState);
    if (clipRect.has_value()) {
        Renderer().PopClipRect();
    }
}
