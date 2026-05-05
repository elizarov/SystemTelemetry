#pragma once

#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/config.h"
#include "layout_edit/layout_edit_tree.h"
#include "layout_edit_dialog/impl/state.h"
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

std::string FindConfiguredBoardMetricBinding(const AppConfig& config, const LayoutMetricEditKey& key);
bool AreScalesEqual(double left, double right);
std::optional<double> TryParseDialogDouble(const wchar_t* text);
std::optional<int> TryParseDialogInteger(const wchar_t* text);
std::string LayoutGuideChildName(const LayoutNodeConfig& node);
std::string ReadDialogControlTextUtf8(HWND hwnd, int controlId);
void SetDialogControlTextUtf8(HWND hwnd, int controlId, std::string_view text);
void SetDialogControlInteger(HWND hwnd, int controlId, int value);
void SetDialogControlIntegerOrEmpty(HWND hwnd, int controlId, int value, bool hasValue);
void SetWindowTextUtf8(HWND hwnd, std::string_view text);
LRESULT AddComboStringUtf8(HWND combo, std::string_view text);

std::wstring FormatDialogColorHex(unsigned int color);
std::optional<unsigned int> TryParseDialogHexColor(const wchar_t* text);
std::wstring TitleCaseWords(std::string_view text);
void ConfigureColorSliders(HWND hwnd);
void ConfigureColorViewTabs(HWND hwnd, ColorEditViewMode selectedMode);
void SetColorDialogChannel(HWND hwnd, const ColorDialogControls& channel, unsigned int value);
void SetColorDialogHex(HWND hwnd, unsigned int color);
void SetColorDialogLch(HWND hwnd, unsigned int color);
void SetColorDialogHsv(HWND hwnd, unsigned int color);
void SetColorDialogRgbFromLch(HWND hwnd);
void SetColorDialogRgbFromHsv(HWND hwnd);
bool ColorDialogLchValueValid(HWND hwnd);
bool ColorDialogHsvValueValid(HWND hwnd);
bool IsColorLchControlId(int controlId);
bool IsColorLchSliderId(int controlId);
bool IsColorHsvControlId(int controlId);
bool IsColorHsvSliderId(int controlId);
void SyncColorLchSliderFromEdit(HWND hwnd, int editId);
void SetColorLchEditFromSlider(HWND hwnd, int sliderId);
void SyncColorHsvSliderFromEdit(HWND hwnd, int editId);
void SetColorHsvEditFromSlider(HWND hwnd, int sliderId);
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
