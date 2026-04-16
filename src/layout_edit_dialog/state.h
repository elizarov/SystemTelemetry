#pragma once

#include <optional>
#include <string>
#include <vector>

#include "layout_edit_dialog.h"
#include <commctrl.h>
#include "layout_edit_tree.h"

struct LayoutEditTreeItemBinding {
    const LayoutEditTreeNode* node = nullptr;
    HTREEITEM item = nullptr;
};

enum class LayoutEditStatusKind {
    Info,
    Error,
};

struct LayoutEditValidationResult {
    bool valid = true;
    std::wstring message;
};

enum class LayoutEditEditorKind {
    Summary,
    Numeric,
    Font,
    Color,
    Weights,
    Metric,
};

struct LayoutEditDialogState {
    LayoutEditDialog* dialog = nullptr;
    AppConfig originalConfig;
    LayoutEditTreeModel treeModel;
    LayoutEditTreeModel visibleTreeModel;
    std::optional<LayoutEditFocusKey> initialFocus;
    const LayoutEditTreeNode* selectedNode = nullptr;
    const LayoutEditTreeLeaf* selectedLeaf = nullptr;
    std::vector<LayoutEditTreeItemBinding> treeItems;
    COLORREF customColors[16]{};
    HFONT titleFont = nullptr;
    HFONT fontSampleFont = nullptr;
    std::wstring currentFilter;
    std::wstring statusText;
    bool statusIsError = false;
    bool activeSelectionValid = true;
    COLORREF previewColor = RGB(255, 255, 255);
    bool updatingControls = false;
};

LayoutEditDialogState* DialogStateFromWindow(HWND hwnd);
