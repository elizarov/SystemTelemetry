#pragma once

#include "layout_edit_dialog/impl/state.h"

LayoutEditEditorKind CurrentLayoutEditEditorKind(const LayoutEditDialogState* state);
bool CurrentLayoutEditShowsMetricBinding(const LayoutEditDialogState* state);

void PopulateLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd);
LayoutEditValidationResult ValidateCurrentSelectionInput(LayoutEditDialogState* state, HWND hwnd);
void RefreshLayoutEditValidationState(LayoutEditDialogState* state, HWND hwnd);
bool PreviewSelectedValue(LayoutEditDialogState* state, HWND hwnd);
bool PreviewSelectedFont(LayoutEditDialogState* state, HWND hwnd, UINT notificationCode = 0);
bool PreviewSelectedGlobalFontFamily(LayoutEditDialogState* state, HWND hwnd, UINT notificationCode = 0);
bool PreviewSelectedColor(LayoutEditDialogState* state, HWND hwnd);
bool SetSelectedDialogColor(LayoutEditDialogState* state, HWND hwnd, unsigned int color);
bool PreviewSelectedWeights(LayoutEditDialogState* state, HWND hwnd);
bool PreviewSelectedMetric(LayoutEditDialogState* state, HWND hwnd);
bool PreviewSelectedDateTimeFormat(LayoutEditDialogState* state, HWND hwnd);
bool HandleMetricListOrderEditorCommand(LayoutEditDialogState* state, HWND hwnd, int controlId, UINT notificationCode);
bool RevertSelectedLayoutEditField(LayoutEditDialogState* state, HWND hwnd);
