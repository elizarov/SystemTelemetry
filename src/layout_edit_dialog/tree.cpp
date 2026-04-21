#include "layout_edit_dialog/tree.h"

#include <commctrl.h>

#include "resource.h"
#include "layout_edit_dialog/editors.h"
#include "layout_edit_dialog/pane.h"
#include "layout_edit_dialog/trace.h"
#include "util/utf8.h"

namespace {

class ScopedWindowRedrawSuspension {
public:
    explicit ScopedWindowRedrawSuspension(HWND hwnd) : hwnd_(hwnd) {
        if (hwnd_ != nullptr) {
            SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
        }
    }

    ~ScopedWindowRedrawSuspension() {
        if (hwnd_ != nullptr) {
            SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }

    ScopedWindowRedrawSuspension(const ScopedWindowRedrawSuspension&) = delete;
    ScopedWindowRedrawSuspension& operator=(const ScopedWindowRedrawSuspension&) = delete;

private:
    HWND hwnd_ = nullptr;
};

void InsertLayoutEditTreeNodes(
    LayoutEditDialogState* state, HWND tree, const std::vector<LayoutEditTreeNode>& nodes, HTREEITEM parent) {
    for (const auto& node : nodes) {
        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        std::wstring label = WideFromUtf8(node.label);
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

    TVITEMW treeItem{};
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

void ExpandTreeAncestors(HWND tree, HTREEITEM item) {
    while (item != nullptr) {
        TreeView_Expand(tree, item, TVE_EXPAND);
        item = TreeView_GetParent(tree, item);
    }
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
    const ScopedWindowRedrawSuspension redrawSuspension(tree);
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:tree_rebuild_begin",
        "preferred_focus=" + QuoteTraceText(preferredFocus.has_value() ? "set" : "none") +
            " filter=" + QuoteTraceText(Utf8FromWide(state->currentFilter)));

    std::string preferredLocation;
    if (preferredFocus.has_value()) {
        if (const LayoutEditTreeLeaf* leaf = FindLayoutEditTreeLeaf(state->treeModel, *preferredFocus);
            leaf != nullptr) {
            preferredLocation = "[" + leaf->sectionName + "] " + leaf->memberName;
        }
    } else if (state->selectedNode != nullptr) {
        preferredLocation = state->selectedNode->locationText;
    }

    state->visibleTreeModel = FilterLayoutEditTreeModel(state->treeModel, Utf8FromWide(state->currentFilter));
    state->treeItems.clear();
    TreeView_DeleteAllItems(tree);
    InsertLayoutEditTreeNodes(state, tree, state->visibleTreeModel.roots, TVI_ROOT);

    HTREEITEM selectedItem = nullptr;
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
        TreeView_EnsureVisible(tree, selectedItem);
        HandleLayoutEditTreeSelection(state, hwnd, selectedItem);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:tree_rebuild_done",
            "roots=" + std::to_string(state->visibleTreeModel.roots.size()) +
                " items=" + std::to_string(state->treeItems.size()) + " selected=" + QuoteTraceText("true"));
        return;
    }

    state->selectedNode = nullptr;
    state->selectedLeaf = nullptr;
    state->dialog->UpdateSelectionHighlight(std::nullopt);
    PopulateLayoutEditSelection(state, hwnd);
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:tree_rebuild_done",
        "roots=" + std::to_string(state->visibleTreeModel.roots.size()) +
            " items=" + std::to_string(state->treeItems.size()) + " selected=" + QuoteTraceText("false"));
}

void HandleLayoutEditTreeSelection(LayoutEditDialogState* state, HWND hwnd, HTREEITEM item) {
    const LayoutEditTreeNode* node = TreeNodeFromItem(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE), item);
    state->selectedNode = node;
    state->selectedLeaf = node != nullptr && node->leaf.has_value() ? &(*node->leaf) : nullptr;
    state->dialog->UpdateSelectionHighlight(SelectionHighlightForTreeNode(node));
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:tree_select", BuildTraceNodeText(node));
    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
}

void RefreshLayoutEditDialogControls(LayoutEditDialogState* state,
    HWND hwnd,
    const std::optional<LayoutEditFocusKey>& preferredFocus,
    bool rebuildTree) {
    if (state == nullptr || hwnd == nullptr) {
        return;
    }

    if (rebuildTree) {
        RebuildLayoutEditTree(state, hwnd, preferredFocus);
        return;
    }

    if (preferredFocus.has_value()) {
        if (HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE); tree != nullptr) {
            if (HTREEITEM item = FindTreeItemByFocusKey(state, *preferredFocus); item != nullptr) {
                ExpandTreeAncestors(tree, item);
                TreeView_SelectItem(tree, item);
                TreeView_EnsureVisible(tree, item);
                HandleLayoutEditTreeSelection(state, hwnd, item);
                return;
            }
        }
    }

    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
}
