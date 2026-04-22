#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config/config.h"
#include "layout_edit/layout_edit_tree.h"
#include "resource.h"

struct ColorDialogControls {
    int labelId = 0;
    int editId = 0;
    int sliderId = 0;
    const char* channelName = "";
};

inline constexpr std::array<ColorDialogControls, 4> kColorDialogControls = {{
    {IDC_LAYOUT_EDIT_COLOR_RED_LABEL, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, IDC_LAYOUT_EDIT_COLOR_RED_SLIDER, "red"},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT, IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER, "green"},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT, IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER, "blue"},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT, IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER, "alpha"},
}};

enum class BoardMetricBindingKind {
    Temperature,
    Fan,
};

struct BoardMetricBindingTarget {
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string logicalName;
};

std::optional<BoardMetricBindingTarget> ParseBoardMetricBindingTarget(std::string_view metricId);
std::string FindConfiguredBoardMetricBinding(const AppConfig& config, const LayoutMetricEditKey& key);
bool AreScalesEqual(double left, double right);
std::optional<double> TryParseDialogDouble(const wchar_t* text);
std::optional<int> TryParseDialogInteger(const wchar_t* text);
std::string LayoutGuideChildName(const LayoutNodeConfig& node);
std::string ReadDialogControlTextUtf8(HWND hwnd, int controlId);

std::wstring FormatDialogColorHex(unsigned int color);
std::optional<unsigned int> TryParseDialogHexColor(const wchar_t* text);
std::wstring TitleCaseWords(std::string_view text);
void ConfigureColorSliders(HWND hwnd);
void SetColorDialogChannel(HWND hwnd, const ColorDialogControls& channel, unsigned int value);
void SetColorDialogHex(HWND hwnd, unsigned int color);
std::optional<unsigned int> ParseColorDialogChannel(HWND hwnd, int editId);
std::optional<unsigned int> ReadColorDialogValue(HWND hwnd);
const ColorDialogControls* FindColorDialogControlsByEditId(int editId);
const ColorDialogControls* FindColorDialogControlsBySliderId(int sliderId);

std::vector<std::wstring> EnumerateInstalledFontFamilies(HWND hwnd);
void PopulateFontFaceComboBox(HWND hwnd, const std::wstring& selectedFace);
std::wstring ReadFontDialogFaceText(HWND hwnd, UINT notificationCode);
void PopulateMetricBindingComboBox(
    HWND hwnd, const std::vector<std::string>& options, std::string_view selectedBinding, bool enableSelection);

std::optional<std::string> FindCardTitleValue(const AppConfig& config, const LayoutCardTitleEditKey& key);
std::optional<std::pair<int, int>> FindWeightEditValues(const AppConfig& config, const LayoutWeightEditKey& key);
std::vector<std::string> AvailableMetricDefinitionIds(const AppConfig& config);
bool IsMetricListSupportedDisplayStyle(MetricDisplayStyle style);
std::wstring BuildWeightEditorLabel(const LayoutEditTreeLeaf& leaf, bool first);
std::wstring BuildLayoutEditNodeTitle(const LayoutEditTreeNode* node);
std::wstring BuildLayoutEditSummaryText(const LayoutEditTreeNode* node);
std::wstring BuildLayoutEditHintText(const LayoutEditTreeNode* node);
