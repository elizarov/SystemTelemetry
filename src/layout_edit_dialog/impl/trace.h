#pragma once

#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "layout_edit_tree.h"

std::string EscapeTraceText(std::string_view text);
std::string QuoteTraceText(std::string_view text);
std::string FormatTraceColorHex(unsigned int color);
std::string JoinNodePath(const std::vector<size_t>& path);
std::string BuildTraceFocusKeyText(const LayoutEditTreeLeaf* leaf);
std::string BuildTraceNodeText(const LayoutEditTreeNode* node);
std::string BuildColorDialogTraceValues(HWND hwnd);
std::string BuildMetricDialogTraceValues(HWND hwnd);
