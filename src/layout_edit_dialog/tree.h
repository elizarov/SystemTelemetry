#pragma once

#include "layout_edit_dialog/state.h"

void RebuildLayoutEditTree(
    LayoutEditDialogState* state, HWND hwnd, const std::optional<LayoutEditFocusKey>& preferredFocus = std::nullopt);
void HandleLayoutEditTreeSelection(LayoutEditDialogState* state, HWND hwnd, HTREEITEM item);
void RefreshLayoutEditDialogControls(
    LayoutEditDialogState* state, HWND hwnd, const std::optional<LayoutEditFocusKey>& preferredFocus, bool rebuildTree);
