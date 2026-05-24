#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "layout_edit/layout_edit_tree.h"

enum class ResourceStringId : std::uint32_t;

std::string JoinNodePath(const std::vector<size_t>& path);
std::string BuildTraceFocusKeyText(const LayoutEditTreeLeaf* leaf);
std::string BuildTraceNodeText(const LayoutEditTreeNode* node);
std::string BuildTraceNodeDetail(const LayoutEditTreeNode* node, ResourceStringId format, ...);
std::string BuildColorDialogTraceValues(HWND hwnd);
std::string BuildMetricDialogTraceValues(HWND hwnd);
