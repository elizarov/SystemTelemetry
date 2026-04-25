#pragma once

#include <windows.h>

#include <commctrl.h>
#include <optional>
#include <string>
#include <vector>

#include "layout_edit/layout_edit_tree.h"
#include "layout_edit_dialog/layout_edit_dialog.h"

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
    MetricListOrder,
};

inline constexpr int IDC_LAYOUT_EDIT_METRIC_LIST_ROW_COMBO_BASE = 11000;
inline constexpr int IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE = 11100;
inline constexpr int IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE = 11200;
inline constexpr int IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE = 11300;
inline constexpr int IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW = 11400;

struct LayoutEditMetricListRowControls {
    HWND combo = nullptr;
    HWND upButton = nullptr;
    HWND downButton = nullptr;
    HWND deleteButton = nullptr;
    int comboId = 0;
    int upButtonId = 0;
    int downButtonId = 0;
    int deleteButtonId = 0;
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
    std::vector<LayoutEditMetricListRowControls> metricListRowControls;
    HWND metricListAddRowButton = nullptr;
};

LayoutEditDialogState* DialogStateFromWindow(HWND hwnd);
