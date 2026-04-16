#pragma once

#include "state.h"

class DialogRedrawScope {
public:
    explicit DialogRedrawScope(HWND hwnd);
    ~DialogRedrawScope();

    DialogRedrawScope(const DialogRedrawScope&) = delete;
    DialogRedrawScope& operator=(const DialogRedrawScope&) = delete;

private:
    HWND hwnd_ = nullptr;
};

void ShowDialogControl(HWND hwnd, int controlId, bool show);
std::optional<RECT> DialogControlRect(HWND hwnd, int controlId);
int DialogControlWidth(HWND hwnd, int controlId);
int DialogControlHeight(HWND hwnd, int controlId);
int DialogControlVisibleHeight(HWND hwnd, int controlId);
int DialogControlLayoutHeightForVisibleHeight(HWND hwnd, int controlId, int desiredVisibleHeight);
int MeasureSingleLineFieldVisibleHeight(HWND hwnd);
void SetDialogControlBounds(HWND hwnd, int controlId, int left, int top, int width, int height);
std::wstring ReadDialogControlTextWide(HWND hwnd, int controlId);
int MeasureTextWidthForControl(HWND hwnd, int controlId, std::wstring_view text);
int MeasureTextHeightForControl(HWND hwnd, int controlId, std::wstring_view text, int width, bool singleLine = false);
int DialogUnitsToPixelsY(HWND hwnd, int dialogUnitsY);

void ConfigureDialogFonts(LayoutEditDialogState* state, HWND hwnd);
void DestroyDialogFonts(LayoutEditDialogState* state);
void SetLayoutEditStatus(LayoutEditDialogState* state, HWND hwnd, LayoutEditStatusKind kind, const std::wstring& text);
void SetColorSamplePreview(LayoutEditDialogState* state, HWND hwnd, unsigned int color);
void SetFontSamplePreview(
    LayoutEditDialogState* state, HWND hwnd, std::optional<LayoutEditParameter> parameter, const UiFontConfig* font);
void ShowLayoutEditEditors(
    HWND hwnd, bool showNumeric, bool showFont, bool showColor, bool showWeights, bool showMetric, bool showBinding);
void LayoutLayoutEditRightPane(LayoutEditDialogState* state, HWND hwnd);
void UpdateLayoutEditActionState(LayoutEditDialogState* state, HWND hwnd);
void SetLayoutEditDescription(HWND hwnd, const LayoutEditTreeNode* node);
