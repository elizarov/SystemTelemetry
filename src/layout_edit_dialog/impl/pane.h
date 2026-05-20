#pragma once

#include <optional>
#include <string_view>

#include "layout_edit_dialog/impl/state.h"

class DialogRedrawScope {
public:
    explicit DialogRedrawScope(HWND hwnd, UINT redrawFlags = RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DialogRedrawScope(
        HWND hwnd, const RECT& redrawRect, UINT redrawFlags = RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    ~DialogRedrawScope();

    DialogRedrawScope(const DialogRedrawScope&) = delete;
    DialogRedrawScope& operator=(const DialogRedrawScope&) = delete;
    DialogRedrawScope(DialogRedrawScope&& other) noexcept;
    DialogRedrawScope& operator=(DialogRedrawScope&& other) noexcept;

private:
    HWND hwnd_ = nullptr;
    std::optional<RECT> redrawRect_;
    UINT redrawFlags_ = 0;
};

class DialogDescendantRedrawScope {
public:
    explicit DialogDescendantRedrawScope(
        HWND hwnd, UINT redrawFlags = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    ~DialogDescendantRedrawScope();

    DialogDescendantRedrawScope(const DialogDescendantRedrawScope&) = delete;
    DialogDescendantRedrawScope& operator=(const DialogDescendantRedrawScope&) = delete;

private:
    HWND root_ = nullptr;
    UINT redrawFlags_ = 0;
};

void ShowDialogControl(HWND hwnd, int controlId, bool show);
void BringDialogControlToTop(HWND hwnd, int controlId);
std::optional<RECT> DialogControlRect(HWND hwnd, int controlId);
int DialogControlWidth(HWND hwnd, int controlId);
int DialogControlHeight(HWND hwnd, int controlId);
int DialogControlVisibleHeight(HWND hwnd, int controlId);
int DialogControlLayoutHeightForVisibleHeight(HWND hwnd, int controlId, int desiredVisibleHeight);
int MeasureSingleLineFieldVisibleHeight(HWND hwnd);
void SetDialogControlBounds(HWND hwnd, int controlId, int left, int top, int width, int height);
int MeasureTextWidthForControl(HWND hwnd, int controlId, std::string_view text);
int MeasureTextHeightForControl(HWND hwnd, int controlId, std::string_view text, int width, bool singleLine = false);
int DialogUnitsToPixelsY(HWND hwnd, int dialogUnitsY);
std::optional<RECT> LayoutEditRightPaneRect(HWND hwnd);
void RefreshLayoutEditRightPane(HWND hwnd);

void ConfigureDialogFonts(LayoutEditDialogState* state, HWND hwnd);
void EnsureLayoutEditDialogControls(HWND hwnd);
void DestroyDialogFonts(LayoutEditDialogState* state);
void SetLayoutEditStatus(LayoutEditDialogState* state, HWND hwnd, LayoutEditStatusKind kind, std::string_view text);
void SetColorSamplePreview(LayoutEditDialogState* state, HWND hwnd, unsigned int color);
bool IsColorGradientBarControlId(int controlId);
void DrawColorGradientBar(HWND hwnd, const DRAWITEMSTRUCT& drawItem);
void DrawThemePreview(LayoutEditDialogState* state, const DRAWITEMSTRUCT& drawItem);
void SetFontSamplePreview(
    LayoutEditDialogState* state, HWND hwnd, std::optional<LayoutEditParameter> parameter, const UiFontConfig* font);
void ShowLayoutEditEditors(HWND hwnd, LayoutEditEditorKind kind, bool showBinding = false);
void DestroyMetricListOrderEditorControls(LayoutEditDialogState* state);
void EnsureMetricListOrderEditorControls(LayoutEditDialogState* state, HWND hwnd, size_t rowCount);
void LayoutLayoutEditRightPane(LayoutEditDialogState* state, HWND hwnd);
void UpdateLayoutEditActionState(LayoutEditDialogState* state, HWND hwnd);
void SetLayoutEditDescription(LayoutEditDialogState* state, HWND hwnd, const LayoutEditTreeNode* node);
