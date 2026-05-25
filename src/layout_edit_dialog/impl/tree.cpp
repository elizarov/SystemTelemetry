#include "layout_edit_dialog/impl/tree.h"

#include <commctrl.h>
#include <utility>

#include "layout_edit_dialog/impl/editors.h"
#include "layout_edit_dialog/impl/pane.h"
#include "layout_model/layout_edit_helpers.h"
#include "resource.h"
#include "util/resource_strings.h"
#include "util/text_format.h"

namespace {

void InsertLayoutEditTreeNodes(
    LayoutEditDialogState* state, HWND tree, const std::vector<LayoutEditTreeNode>& nodes, HTREEITEM parent) {
    for (const auto& node : nodes) {
        TVINSERTSTRUCTA insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        std::string label = node.label;
        insert.item.pszText = label.data();
        insert.item.lParam = reinterpret_cast<LPARAM>(&node);
        HTREEITEM item = TreeView_InsertItem(tree, &insert);
        state->treeItems.push_back(LayoutEditTreeItemBinding{&node, item});
        InsertLayoutEditTreeNodes(state, tree, node.children, item);
        if (node.initiallyExpanded) {
            TreeView_Expand(tree, item, TVE_EXPAND);
        }
    }
}

const LayoutEditTreeNode* TreeNodeFromItem(HWND tree, HTREEITEM item) {
    if (item == nullptr) {
        return nullptr;
    }

    TVITEMA treeItem{};
    treeItem.mask = TVIF_PARAM;
    treeItem.hItem = item;
    if (!TreeView_GetItem(tree, &treeItem)) {
        return nullptr;
    }
    return reinterpret_cast<const LayoutEditTreeNode*>(treeItem.lParam);
}

HTREEITEM FindTreeItemByFocusKey(LayoutEditDialogState* state, const LayoutEditFocusKey& focusKey) {
    for (const auto& binding : state->treeItems) {
        if (binding.node == nullptr) {
            continue;
        }
        if (binding.node->leaf.has_value() && MatchesLayoutEditFocusKey(binding.node->leaf->focusKey, focusKey)) {
            return binding.item;
        }
        if (binding.node->selectionHighlight.has_value()) {
            if (const auto* nodeFocus = std::get_if<LayoutEditFocusKey>(&*binding.node->selectionHighlight);
                nodeFocus != nullptr && MatchesLayoutEditFocusKey(*nodeFocus, focusKey)) {
                return binding.item;
            }
        }
    }
    return nullptr;
}

HTREEITEM FindTreeItemByLocationText(LayoutEditDialogState* state, std::string_view locationText) {
    for (const auto& binding : state->treeItems) {
        if (binding.node != nullptr && binding.node->locationText == locationText) {
            return binding.item;
        }
    }
    return nullptr;
}

HTREEITEM FindFirstLeafTreeItem(const LayoutEditDialogState& state) {
    for (const auto& binding : state.treeItems) {
        if (binding.node != nullptr && binding.node->leaf.has_value()) {
            return binding.item;
        }
    }
    return nullptr;
}

HTREEITEM FindFirstTreeItem(const LayoutEditDialogState& state) {
    return state.treeItems.empty() ? nullptr : state.treeItems.front().item;
}

struct TreeViewportSnapshot {
    std::string firstVisibleLocation;
    std::string selectedLocation;
    int selectedOffsetRows = -1;
};

std::string TreeNodeViewportLocation(LayoutEditDialogState* state, const LayoutEditTreeNode* node) {
    if (state == nullptr || node == nullptr) {
        return {};
    }
    const DisplayConfig& display = state->dialog->Host().CurrentConfig().display;
    if (node->kind == LayoutEditTreeNodeKind::Section && node->label.rfind("theme.", 0) == 0) {
        return FormatText(RES_STR("[theme.%s]"), display.theme.c_str());
    }
    if (node->kind == LayoutEditTreeNodeKind::Section && node->label.rfind("layout.", 0) == 0) {
        return FormatText(RES_STR("[layout.%s]"), display.layout.c_str());
    }
    if (node->locationText.rfind("[theme.", 0) == 0) {
        if (const std::string::size_type sectionEnd = node->locationText.find(']'); sectionEnd != std::string::npos) {
            return FormatText(
                RES_STR("[theme.%s]%s"), display.theme.c_str(), node->locationText.substr(sectionEnd + 1).c_str());
        }
    }
    if (node->locationText.rfind("[layout.", 0) == 0) {
        if (const std::string::size_type sectionEnd = node->locationText.find(']'); sectionEnd != std::string::npos) {
            return FormatText(
                RES_STR("[layout.%s]%s"), display.layout.c_str(), node->locationText.substr(sectionEnd + 1).c_str());
        }
    }
    return node->locationText;
}

TreeViewportSnapshot CaptureTreeViewportSnapshot(LayoutEditDialogState* state, HWND tree) {
    TreeViewportSnapshot snapshot;
    if (state == nullptr || tree == nullptr) {
        return snapshot;
    }

    const HTREEITEM firstVisible = TreeView_GetFirstVisible(tree);
    snapshot.firstVisibleLocation = TreeNodeViewportLocation(state, TreeNodeFromItem(tree, firstVisible));

    const HTREEITEM selected = TreeView_GetSelection(tree);
    const int visibleCount = static_cast<int>(TreeView_GetVisibleCount(tree));
    int offset = 0;
    for (HTREEITEM item = firstVisible; item != nullptr && offset < visibleCount;
        item = TreeView_GetNextVisible(tree, item), ++offset) {
        if (item == selected) {
            snapshot.selectedLocation = TreeNodeViewportLocation(state, TreeNodeFromItem(tree, item));
            snapshot.selectedOffsetRows = offset;
            break;
        }
    }
    return snapshot;
}

void ExpandTreeAncestors(HWND tree, HTREEITEM item) {
    while (item != nullptr) {
        TreeView_Expand(tree, item, TVE_EXPAND);
        item = TreeView_GetParent(tree, item);
    }
}

HTREEITEM VisibleItemBefore(HWND tree, HTREEITEM item, int rowsBefore) {
    HTREEITEM anchor = item;
    for (int row = 0; row < rowsBefore && anchor != nullptr; ++row) {
        HTREEITEM previous = TreeView_GetPrevVisible(tree, anchor);
        if (previous == nullptr) {
            break;
        }
        anchor = previous;
    }
    return anchor;
}

bool RestoreTreeViewportFromSnapshot(
    LayoutEditDialogState* state, HWND tree, HTREEITEM selectedItem, const TreeViewportSnapshot& snapshot) {
    if (state == nullptr || tree == nullptr) {
        return false;
    }

    if (!snapshot.selectedLocation.empty() && snapshot.selectedOffsetRows >= 0) {
        if (HTREEITEM restoredSelected = FindTreeItemByLocationText(state, snapshot.selectedLocation);
            restoredSelected == selectedItem) {
            if (HTREEITEM anchor = VisibleItemBefore(tree, selectedItem, snapshot.selectedOffsetRows);
                anchor != nullptr) {
                TreeView_SelectSetFirstVisible(tree, anchor);
                return true;
            }
        }
    }

    if (!snapshot.firstVisibleLocation.empty()) {
        if (HTREEITEM firstVisibleItem = FindTreeItemByLocationText(state, snapshot.firstVisibleLocation);
            firstVisibleItem != nullptr) {
            TreeView_SelectSetFirstVisible(tree, firstVisibleItem);
            return true;
        }
    }

    return false;
}

std::optional<LayoutEditSelectionHighlight> SelectionHighlightForTreeNode(const LayoutEditTreeNode* node) {
    return node != nullptr ? node->selectionHighlight : std::nullopt;
}

}  // namespace

void RebuildLayoutEditTree(
    LayoutEditDialogState* state, HWND hwnd, const std::optional<LayoutEditFocusKey>& preferredFocus) {
    if (state == nullptr) {
        return;
    }
    HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE);
    if (tree == nullptr) {
        return;
    }
    const DialogDescendantRedrawScope redrawScope(hwnd);

    std::string preferredLocation;
    if (preferredFocus.has_value()) {
        if (const LayoutEditTreeLeaf* leaf = FindLayoutEditTreeLeaf(state->treeModel, *preferredFocus);
            leaf != nullptr) {
            preferredLocation = FormatText(RES_STR("[%s] %s"), leaf->sectionName.c_str(), leaf->memberName.c_str());
        }
    } else if (state->selectedNode != nullptr) {
        preferredLocation = TreeNodeViewportLocation(state, state->selectedNode);
    }
    const TreeViewportSnapshot viewportSnapshot =
        preferredFocus.has_value() ? TreeViewportSnapshot{} : CaptureTreeViewportSnapshot(state, tree);

    HTREEITEM selectedItem = nullptr;
    {
        const DialogRedrawScope redrawSuspension(tree, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        LayoutEditTreeModel visibleTreeModel = FilterLayoutEditTreeModel(state->treeModel, state->currentFilter);
        state->treeItems.clear();
        const bool wasSuppressingSelection = state->suppressTreeSelectionNotification;
        state->suppressTreeSelectionNotification = true;
        TreeView_DeleteAllItems(tree);
        state->visibleTreeModel = std::move(visibleTreeModel);
        InsertLayoutEditTreeNodes(state, tree, state->visibleTreeModel.roots, TVI_ROOT);

        if (preferredFocus.has_value()) {
            selectedItem = FindTreeItemByFocusKey(state, *preferredFocus);
        }
        if (selectedItem == nullptr && !preferredLocation.empty()) {
            selectedItem = FindTreeItemByLocationText(state, preferredLocation);
        }
        if (selectedItem == nullptr) {
            selectedItem = FindFirstLeafTreeItem(*state);
        }
        if (selectedItem == nullptr) {
            selectedItem = FindFirstTreeItem(*state);
        }

        if (selectedItem != nullptr) {
            ExpandTreeAncestors(tree, selectedItem);
            TreeView_SelectItem(tree, selectedItem);
        }
        state->suppressTreeSelectionNotification = wasSuppressingSelection;
    }

    if (selectedItem != nullptr) {
        if (preferredFocus.has_value()) {
            TreeView_EnsureVisible(tree, selectedItem);
        } else if (!RestoreTreeViewportFromSnapshot(state, tree, selectedItem, viewportSnapshot)) {
            TreeView_EnsureVisible(tree, selectedItem);
        }
        HandleLayoutEditTreeSelection(state, hwnd, selectedItem);
        return;
    }

    state->selectedNode = nullptr;
    state->selectedLeaf = nullptr;
    state->dialog->UpdateSelectionHighlight(std::nullopt);
    PopulateLayoutEditSelection(state, hwnd);
}

void HandleLayoutEditTreeSelection(LayoutEditDialogState* state, HWND hwnd, HTREEITEM item) {
    if (state == nullptr || state->suppressTreeSelectionNotification) {
        return;
    }
    const DialogDescendantRedrawScope redrawScope(hwnd);
    HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE);
    const LayoutEditTreeNode* node = TreeNodeFromItem(tree, item);
    state->selectedNode = node;
    state->selectedLeaf = node != nullptr && node->leaf.has_value() ? &(*node->leaf) : nullptr;
    state->dialog->UpdateSelectionHighlight(SelectionHighlightForTreeNode(node));
    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
}

void EnsureVisibleLayoutEditTreeSelection(HWND hwnd) {
    if (hwnd == nullptr) {
        return;
    }

    HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE);
    if (tree == nullptr) {
        return;
    }

    if (HTREEITEM selectedItem = TreeView_GetSelection(tree); selectedItem != nullptr) {
        ExpandTreeAncestors(tree, selectedItem);
        TreeView_EnsureVisible(tree, selectedItem);
    }
}

void RefreshLayoutEditDialogControls(LayoutEditDialogState* state,
    HWND hwnd,
    const std::optional<LayoutEditFocusKey>& preferredFocus,
    bool rebuildTree) {
    if (state == nullptr || hwnd == nullptr) {
        return;
    }
    HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE);

    if (rebuildTree) {
        RebuildLayoutEditTree(state, hwnd, preferredFocus);
        return;
    }

    if (preferredFocus.has_value()) {
        if (tree != nullptr) {
            if (HTREEITEM item = FindTreeItemByFocusKey(state, *preferredFocus); item != nullptr) {
                ExpandTreeAncestors(tree, item);
                state->suppressTreeSelectionNotification = true;
                TreeView_SelectItem(tree, item);
                state->suppressTreeSelectionNotification = false;
                TreeView_EnsureVisible(tree, item);
                HandleLayoutEditTreeSelection(state, hwnd, item);
                EnsureVisibleLayoutEditTreeSelection(hwnd);
                return;
            }
        }
    }

    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
}
