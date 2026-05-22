#include "layout_edit_dialog/impl/pane.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cstdint>
#include <cstring>
#include <string>

#include "config/color_math.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit_dialog/impl/editors.h"
#include "layout_edit_dialog/impl/util.h"
#include "layout_edit_dialog/theme_preview.h"
#include "resource.h"
#include "util/localization_catalog.h"
#include "util/text_format.h"

void ShowDialogControls(HWND hwnd, const int* controlIds, size_t controlCount, bool show);

namespace {

struct LayoutEditRightPaneMetrics {
    int paneLeftGap = 8;
    int paneRightMargin = 8;
    int topMargin = 10;
    int headerGap = 4;
    int headerToGroupGap = 8;
    int groupToStatusGap = 8;
    int statusToFooterGap = 10;
    int footerToButtonsGap = 10;
    int bottomMargin = 8;
    int buttonGap = 8;
    int groupPadding = 14;
    int rowGap = 10;
    int inlineGap = 10;
    int labelGap = 10;
    int hintGap = 10;
    int sampleGap = 10;
};

struct ControlIdList {
    const int* ids = nullptr;
    size_t count = 0;
};

struct LabeledControlIds {
    int labelId = 0;
    int controlId = 0;
};

enum class LayoutEditControlKind : std::uint8_t {
    Label,
    Edit,
    ComboList,
    ComboEdit,
    Button,
    OwnerDrawButton,
    Check,
    GroupBox,
    OwnerDrawStatic,
    BorderStatic,
    EtchedVertical,
    Tree,
    Trackbar,
    Tab,
    Count
};

enum class LayoutEditControlText : std::uint8_t {
    Empty,
    Filter,
    Theme,
    FontName,
    Size,
    Weight,
    Mode,
    Base,
    RotateHue,
    Mix,
    Color,
    Alpha,
    AlphaLabel,
    Hex,
    Pick,
    FirstItemWeight,
    SecondItemWeight,
    Style,
    Scale,
    Unit,
    Label,
    Binding,
    Format,
    RevertField,
    FooterHint,
    Red,
    Green,
    Blue,
    Lightness,
    Chroma,
    Hue,
    Saturation,
    Value,
    AddRow,
    Count
};

struct LayoutEditControlSpec {
    std::uint16_t controlId = 0;
    std::uint8_t left = 0;
    std::uint8_t top = 0;
    std::uint8_t width = 1;
    std::uint8_t height = 1;
    LayoutEditControlKind kind = LayoutEditControlKind::Label;
    LayoutEditControlText text = LayoutEditControlText::Empty;
};

static_assert(sizeof(LayoutEditControlSpec) == 8);

struct ColorChannelControlIds {
    std::uint16_t labelId = 0;
    std::uint16_t editId = 0;
    std::uint16_t sliderId = 0;
    std::uint16_t gradientId = 0;
};

static_assert(sizeof(ColorChannelControlIds) == 8);

struct CheckSliderRowLayout {
    int labelLeft = 0;
    int labelWidth = 0;
    int valueLeft = 0;
    int valueWidth = 0;
    int sliderLeft = 0;
    int sliderWidth = 0;
    int fieldHeight = 0;
    int sliderHeight = 0;
    int rowGap = 0;
};

constexpr LayoutEditRightPaneMetrics kLayoutEditRightPaneMetrics{};
constexpr char kDialogRedrawSuspendCountProperty[] = "CaseDash.LayoutEdit.RedrawSuspendCount";
constexpr char kDialogBlankText[] = " ";
constexpr char kDialogMeasureSampleText[] = "Ag";
constexpr double kLchGradientChromaMax = 0.4;
constexpr int kColorModeControls[] = {IDC_LAYOUT_EDIT_COLOR_MODE_LABEL, IDC_LAYOUT_EDIT_COLOR_MODE_COMBO};
constexpr int kDerivedColorControls[] = {IDC_LAYOUT_EDIT_COLOR_BASE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_BASE_COMBO,
    IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK,
    IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT,
    IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_MIX_CHECK,
    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO,
    IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT,
    IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL};
constexpr int kLiteralColorControls[] = {IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HEX_EDIT,
    IDC_LAYOUT_EDIT_COLOR_VIEW_TAB,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_PICK};
constexpr int kRgbColorControls[] = {IDC_LAYOUT_EDIT_COLOR_RED_LABEL,
    IDC_LAYOUT_EDIT_COLOR_RED_EDIT,
    IDC_LAYOUT_EDIT_COLOR_RED_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL,
    IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT,
    IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT,
    IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT};
constexpr int kLchColorControls[] = {IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL,
    IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT,
    IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL,
    IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT,
    IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT,
    IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT};
constexpr int kHsvColorControls[] = {IDC_LAYOUT_EDIT_COLOR_HSV_HUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT,
    IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_HSV_HUE_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT,
    IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT,
    IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
    IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_GRADIENT};
constexpr int kColorPreviewControls[] = {IDC_LAYOUT_EDIT_COLOR_SWATCH, IDC_LAYOUT_EDIT_COLOR_SAMPLE};
constexpr int kSectionSelectorControls[] = {IDC_LAYOUT_EDIT_THEME_LABEL, IDC_LAYOUT_EDIT_THEME_COMBO};
constexpr int kFontFaceControls[] = {IDC_LAYOUT_EDIT_FONT_FACE_LABEL, IDC_LAYOUT_EDIT_FONT_FACE_EDIT};
constexpr int kFontControls[] = {IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
    IDC_LAYOUT_EDIT_FONT_SIZE_EDIT,
    IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
    IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT,
    IDC_LAYOUT_EDIT_FONT_SAMPLE};
constexpr int kWeightControls[] = {IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL,
    IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT,
    IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL,
    IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT};
constexpr int kMetricControls[] = {IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL,
    IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE,
    IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL,
    IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT,
    IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL,
    IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT,
    IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL,
    IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT};
constexpr int kMetricBindingControls[] = {IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT};
constexpr int kDateTimeFormatControls[] = {
    IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO};
constexpr int kColorPreviewInvalidationControls[] = {IDC_LAYOUT_EDIT_COLOR_SWATCH,
    IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL,
    IDC_LAYOUT_EDIT_COLOR_SAMPLE,
    IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_HSV_HUE_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_GRADIENT,
    IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_GRADIENT};
constexpr int kFontEditorLabelControls[] = {
    IDC_LAYOUT_EDIT_FONT_FACE_LABEL, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL};
constexpr int kGlobalFontFamilyLabelControls[] = {IDC_LAYOUT_EDIT_FONT_FACE_LABEL};
constexpr int kColorEditorLabelControls[] = {IDC_LAYOUT_EDIT_COLOR_MODE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_BASE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_RED_LABEL,
    IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL,
    IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
    IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL,
    IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL,
    IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HSV_HUE_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_LABEL,
    IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_LABEL};
constexpr int kWeightEditorLabelControls[] = {IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL};
constexpr int kMetricEditorLabelControls[] = {IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL,
    IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL,
    IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL,
    IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL};
constexpr int kMetricEditorBindingLabelControls[] = {IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL,
    IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL,
    IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL,
    IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL,
    IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL};
constexpr int kDateTimeFormatLabelControls[] = {IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL};
constexpr int kThemeOrLayoutLabelControls[] = {IDC_LAYOUT_EDIT_THEME_LABEL};
constexpr LabeledControlIds kWeightEditorRows[] = {
    {IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT},
    {IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT}};
constexpr LabeledControlIds kMetricEditorRows[] = {
    {IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE},
    {IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT},
    {IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT},
    {IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT},
    {IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT}};
constexpr ColorChannelControlIds kRgbColorChannelRows[] = {
    {IDC_LAYOUT_EDIT_COLOR_RED_LABEL,
        IDC_LAYOUT_EDIT_COLOR_RED_EDIT,
        IDC_LAYOUT_EDIT_COLOR_RED_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL,
        IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT,
        IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL,
        IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT,
        IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT}};
constexpr ColorChannelControlIds kLchColorChannelRows[] = {
    {IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL,
        IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT,
        IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT},
    {IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL,
        IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT,
        IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT},
    {IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL,
        IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT,
        IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT}};
constexpr ColorChannelControlIds kHsvColorChannelRows[] = {
    {IDC_LAYOUT_EDIT_COLOR_HSV_HUE_LABEL,
        IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT,
        IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_HSV_HUE_GRADIENT},
    {IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_LABEL,
        IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT,
        IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_GRADIENT},
    {IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_LABEL,
        IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT,
        IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
        IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_GRADIENT}};

constexpr const char* kLayoutEditControlClassNames[] = {WC_STATICA,
    WC_EDITA,
    WC_COMBOBOXA,
    WC_COMBOBOXA,
    WC_BUTTONA,
    WC_BUTTONA,
    WC_BUTTONA,
    WC_BUTTONA,
    WC_STATICA,
    WC_STATICA,
    WC_STATICA,
    WC_TREEVIEWA,
    TRACKBAR_CLASSA,
    WC_TABCONTROLA};
constexpr DWORD kLayoutEditControlStyles[] = {0,
    WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_LEFT,
    CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
    CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
    BS_PUSHBUTTON | WS_TABSTOP,
    BS_PUSHBUTTON | BS_OWNERDRAW | WS_TABSTOP,
    BS_AUTOCHECKBOX | WS_TABSTOP,
    BS_GROUPBOX,
    SS_OWNERDRAW,
    WS_BORDER,
    SS_ETCHEDVERT,
    WS_BORDER | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
    WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
    TCS_TABS | WS_CLIPSIBLINGS | WS_TABSTOP};
static_assert(static_cast<size_t>(LayoutEditControlKind::Count) == ARRAYSIZE(kLayoutEditControlClassNames));
static_assert(static_cast<size_t>(LayoutEditControlKind::Count) == ARRAYSIZE(kLayoutEditControlStyles));

constexpr const char* kLayoutEditControlTexts[] = {"",
    "Filter:",
    "Theme:",
    "Font name:",
    "Size:",
    "Weight:",
    "Mode:",
    "Base:",
    "Rotate hue",
    "Mix",
    "Color:",
    "Alpha",
    "Alpha:",
    "Hex:",
    "Pick...",
    "First item weight:",
    "Second item weight:",
    "Style:",
    "Scale:",
    "Unit:",
    "Label:",
    "Binding:",
    "Format:",
    "Revert Field",
    "",
    "Red:",
    "Green:",
    "Blue:",
    "Lightness:",
    "Chroma:",
    "Hue:",
    "Saturation:",
    "Value:",
    "Add row"};
static_assert(static_cast<size_t>(LayoutEditControlText::Count) == ARRAYSIZE(kLayoutEditControlTexts));

const char* LayoutEditControlTextValue(LayoutEditControlText text) {
    if (text == LayoutEditControlText::FooterHint) {
        return FindLocalizedText(RES_STR("layout_edit.dialog.footer_hint"));
    }
    return kLayoutEditControlTexts[static_cast<size_t>(text)];
}

constexpr LayoutEditControlSpec kLayoutEditControlSpecs[] = {
    {IDC_LAYOUT_EDIT_FILTER_LABEL, 8, 10, 26, 8, LayoutEditControlKind::Label, LayoutEditControlText::Filter},
    {IDC_LAYOUT_EDIT_FILTER_EDIT, 38, 8, 134, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_TREE, 8, 28, 164, 234, LayoutEditControlKind::Tree, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_DIVIDER, 178, 8, 1, 254, LayoutEditControlKind::EtchedVertical, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_TITLE, 0, 0, 236, 10, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_LOCATION, 0, 0, 236, 10, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_DESCRIPTION, 0, 0, 236, 20, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_EDITOR_GROUP, 0, 0, 236, 146, LayoutEditControlKind::GroupBox, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_VALUE_EDIT, 0, 0, 210, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_SUMMARY, 0, 0, 210, 24, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_THEME_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Theme},
    {IDC_LAYOUT_EDIT_THEME_COMBO, 0, 0, 172, 70, LayoutEditControlKind::ComboList, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_THEME_PREVIEW,
        0,
        0,
        210,
        86,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_FONT_FACE_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::FontName},
    {IDC_LAYOUT_EDIT_FONT_FACE_EDIT, 0, 0, 172, 100, LayoutEditControlKind::ComboEdit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, 0, 0, 28, 8, LayoutEditControlKind::Label, LayoutEditControlText::Size},
    {IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, 0, 0, 56, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Weight},
    {IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, 0, 0, 78, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_FONT_SAMPLE, 0, 0, 210, 34, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_SWATCH, 0, 0, 18, 18, LayoutEditControlKind::BorderStatic, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL, 0, 0, 186, 8, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_MODE_LABEL, 0, 0, 28, 8, LayoutEditControlKind::Label, LayoutEditControlText::Mode},
    {IDC_LAYOUT_EDIT_COLOR_MODE_COMBO, 0, 0, 172, 70, LayoutEditControlKind::ComboList, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_BASE_LABEL, 0, 0, 28, 8, LayoutEditControlKind::Label, LayoutEditControlText::Base},
    {IDC_LAYOUT_EDIT_COLOR_BASE_COMBO, 0, 0, 172, 70, LayoutEditControlKind::ComboList, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK, 0, 0, 70, 10, LayoutEditControlKind::Check, LayoutEditControlText::RotateHue},
    {IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT, 0, 0, 60, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER, 0, 0, 106, 14, LayoutEditControlKind::Trackbar, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_MIX_CHECK, 0, 0, 44, 10, LayoutEditControlKind::Check, LayoutEditControlText::Mix},
    {IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT, 0, 0, 60, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER,
        0,
        0,
        106,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Color},
    {IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO,
        0,
        0,
        172,
        70,
        LayoutEditControlKind::ComboList,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK, 0, 0, 44, 10, LayoutEditControlKind::Check, LayoutEditControlText::Alpha},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT, 0, 0, 60, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER,
        0,
        0,
        106,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HEX_LABEL, 0, 0, 18, 8, LayoutEditControlKind::Label, LayoutEditControlText::Hex},
    {IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, 0, 0, 60, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_PICK, 0, 0, 64, 14, LayoutEditControlKind::Button, LayoutEditControlText::Pick},
    {IDC_LAYOUT_EDIT_COLOR_SAMPLE, 0, 0, 210, 12, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_VIEW_TAB, 0, 0, 210, 70, LayoutEditControlKind::Tab, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_RED_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Red},
    {IDC_LAYOUT_EDIT_COLOR_RED_EDIT, 0, 0, 30, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_RED_SLIDER, 0, 0, 150, 14, LayoutEditControlKind::Trackbar, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT,
        0,
        0,
        150,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Green},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT, 0, 0, 30, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER, 0, 0, 150, 14, LayoutEditControlKind::Trackbar, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT,
        0,
        0,
        150,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Blue},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT, 0, 0, 30, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER, 0, 0, 150, 14, LayoutEditControlKind::Trackbar, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT,
        0,
        0,
        150,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::AlphaLabel},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT, 0, 0, 30, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER, 0, 0, 150, 14, LayoutEditControlKind::Trackbar, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL,
        0,
        0,
        1,
        8,
        LayoutEditControlKind::Label,
        LayoutEditControlText::Lightness},
    {IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT, 0, 0, 48, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
        0,
        0,
        132,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT,
        0,
        0,
        132,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Chroma},
    {IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT, 0, 0, 48, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
        0,
        0,
        132,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT,
        0,
        0,
        132,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Hue},
    {IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT, 0, 0, 48, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
        0,
        0,
        132,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT,
        0,
        0,
        132,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_HUE_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Hue},
    {IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT, 0, 0, 48, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
        0,
        0,
        132,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_HUE_GRADIENT,
        0,
        0,
        132,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_LABEL,
        0,
        0,
        1,
        8,
        LayoutEditControlKind::Label,
        LayoutEditControlText::Saturation},
    {IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT,
        0,
        0,
        48,
        14,
        LayoutEditControlKind::Edit,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
        0,
        0,
        132,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_GRADIENT,
        0,
        0,
        132,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_LABEL, 0, 0, 1, 8, LayoutEditControlKind::Label, LayoutEditControlText::Value},
    {IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT, 0, 0, 48, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
        0,
        0,
        132,
        14,
        LayoutEditControlKind::Trackbar,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_GRADIENT,
        0,
        0,
        132,
        8,
        LayoutEditControlKind::OwnerDrawStatic,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL,
        0,
        0,
        98,
        8,
        LayoutEditControlKind::Label,
        LayoutEditControlText::FirstItemWeight},
    {IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, 0, 0, 60, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL,
        0,
        0,
        98,
        8,
        LayoutEditControlKind::Label,
        LayoutEditControlText::SecondItemWeight},
    {IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, 0, 0, 60, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Style},
    {IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE, 0, 0, 172, 8, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Scale},
    {IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, 0, 0, 172, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Unit},
    {IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT, 0, 0, 172, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Label},
    {IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT, 0, 0, 172, 14, LayoutEditControlKind::Edit, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Binding},
    {IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT,
        0,
        0,
        172,
        70,
        LayoutEditControlKind::ComboList,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL, 0, 0, 34, 8, LayoutEditControlKind::Label, LayoutEditControlText::Format},
    {IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO,
        0,
        0,
        172,
        90,
        LayoutEditControlKind::ComboList,
        LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_HINT, 0, 0, 210, 18, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_STATUS_TEXT, 0, 0, 154, 12, LayoutEditControlKind::Label, LayoutEditControlText::Empty},
    {IDC_LAYOUT_EDIT_REVERT, 0, 0, 76, 14, LayoutEditControlKind::Button, LayoutEditControlText::RevertField},
    {IDC_LAYOUT_EDIT_FOOTER_HINT, 0, 0, 206, 18, LayoutEditControlKind::Label, LayoutEditControlText::FooterHint}};

int Max3Int(int first, int second, int third) {
    // Size: avoid std::max initializer_list helper code in cold layout math.
    return std::max(std::max(first, second), third);
}

int Max4Int(int first, int second, int third, int fourth) {
    return std::max(std::max(first, second), std::max(third, fourth));
}

int WindowRedrawSuspendCount(HWND hwnd) {
    return static_cast<int>(reinterpret_cast<intptr_t>(GetPropA(hwnd, kDialogRedrawSuspendCountProperty)));
}

void BeginWindowRedrawSuspension(HWND hwnd) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }

    const int count = WindowRedrawSuspendCount(hwnd);
    if (count == 0) {
        SendMessageA(hwnd, WM_SETREDRAW, FALSE, 0);
    }
    SetPropA(hwnd, kDialogRedrawSuspendCountProperty, reinterpret_cast<HANDLE>(static_cast<intptr_t>(count + 1)));
}

void EndWindowRedrawSuspension(HWND hwnd, const RECT* redrawRect, UINT redrawFlags) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }

    const int count = WindowRedrawSuspendCount(hwnd);
    if (count <= 1) {
        RemovePropA(hwnd, kDialogRedrawSuspendCountProperty);
        SendMessageA(hwnd, WM_SETREDRAW, TRUE, 0);
        if (redrawFlags != 0) {
            RedrawWindow(hwnd, redrawRect, nullptr, redrawFlags);
        }
        return;
    }
    SetPropA(hwnd, kDialogRedrawSuspendCountProperty, reinterpret_cast<HANDLE>(static_cast<intptr_t>(count - 1)));
}

bool DialogControlHasClass(HWND hwnd, int controlId, const char* expectedClassName) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return false;
    }
    char className[64] = {};
    if (GetClassNameA(control, className, ARRAYSIZE(className)) == 0) {
        return false;
    }
    return lstrcmpiA(className, expectedClassName) == 0;
}

bool IsDialogComboBoxControl(HWND hwnd, int controlId) {
    return DialogControlHasClass(hwnd, controlId, WC_COMBOBOXA);
}

bool IsDialogEditControl(HWND hwnd, int controlId) {
    return DialogControlHasClass(hwnd, controlId, WC_EDITA);
}

bool UsesSingleLineFieldFrame(HWND hwnd, int controlId) {
    return IsDialogEditControl(hwnd, controlId) || IsDialogComboBoxControl(hwnd, controlId);
}

int DialogComboBoxSelectionHeight(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }

    const LRESULT selectionHeight = SendMessageA(control, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    if (selectionHeight != CB_ERR && selectionHeight > 0) {
        return static_cast<int>(selectionHeight);
    }

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(control, &info) != FALSE) {
        const int itemHeight = (std::max)(0, static_cast<int>(info.rcItem.bottom - info.rcItem.top));
        if (itemHeight > 0) {
            return itemHeight;
        }
    }

    RECT rect{};
    if (GetClientRect(control, &rect) != FALSE) {
        return (std::max)(1, static_cast<int>(rect.bottom - rect.top));
    }
    return 0;
}

std::string ReadWindowTextValue(HWND window) {
    if (window == nullptr) {
        return {};
    }

    const int length = GetWindowTextLengthA(window);
    std::string text(static_cast<size_t>(length) + 1, '\0');
    if (length > 0) {
        GetWindowTextA(window, text.data(), static_cast<int>(text.size()));
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

void DestroyDialogFont(HFONT& font) {
    if (font != nullptr) {
        DeleteObject(font);
        font = nullptr;
    }
}

RECT DialogUnitRect(HWND hwnd, int left, int top, int width, int height) {
    RECT rect{left, top, left + std::max(1, width), top + std::max(1, height)};
    MapDialogRect(hwnd, &rect);
    if (rect.right <= rect.left) {
        rect.right = rect.left + 1;
    }
    if (rect.bottom <= rect.top) {
        rect.bottom = rect.top + 1;
    }
    return rect;
}

WPARAM LayoutEditDialogFont(HWND hwnd) {
    WPARAM font = static_cast<WPARAM>(SendMessageA(hwnd, WM_GETFONT, 0, 0));
    if (font == 0) {
        if (HWND templateControl = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT); templateControl != nullptr) {
            font = static_cast<WPARAM>(SendMessageA(templateControl, WM_GETFONT, 0, 0));
        }
    }
    return font;
}

HWND CreateLayoutEditControl(HWND hwnd, const LayoutEditControlSpec& spec, WPARAM font) {
    const size_t kind = static_cast<size_t>(spec.kind);
    const char* text = LayoutEditControlTextValue(spec.text);

    const RECT initialRect = DialogUnitRect(hwnd, spec.left, spec.top, spec.width, spec.height);
    HWND control = CreateWindowExA(0,
        kLayoutEditControlClassNames[kind],
        text,
        WS_CHILD | WS_VISIBLE | kLayoutEditControlStyles[kind],
        initialRect.left,
        initialRect.top,
        initialRect.right - initialRect.left,
        initialRect.bottom - initialRect.top,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.controlId)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(hwnd, GWLP_HINSTANCE)),
        nullptr);
    if (control != nullptr && font != 0) {
        SendMessageA(control, WM_SETFONT, font, TRUE);
    }
    return control;
}

HWND CreateMetricListEditorControl(HWND hwnd, LayoutEditControlKind kind, LayoutEditControlText text, int controlId) {
    const LayoutEditControlSpec spec{static_cast<std::uint16_t>(controlId), 0, 0, 1, 1, kind, text};
    return CreateLayoutEditControl(hwnd, spec, LayoutEditDialogFont(hwnd));
}

HFONT CreateDerivedDialogFont(HWND hwnd, int controlId, int weight, int heightDelta = 0) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return nullptr;
    }
    HFONT baseFont = reinterpret_cast<HFONT>(SendMessageA(control, WM_GETFONT, 0, 0));
    LOGFONTA logFont{};
    if (baseFont != nullptr && GetObjectA(baseFont, sizeof(logFont), &logFont) == sizeof(logFont)) {
        logFont.lfWeight = weight;
        logFont.lfHeight -= heightDelta;
        return CreateFontIndirectA(&logFont);
    }
    return nullptr;
}

std::string_view FontSampleText(LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::FontTitle:
            return "Card Title";
        case LayoutEditParameter::FontBig:
            return "88";
        case LayoutEditParameter::FontValue:
            return "42%";
        case LayoutEditParameter::FontLabel:
            return "CPU";
        case LayoutEditParameter::FontText:
            return "Sample text";
        case LayoutEditParameter::FontSmall:
            return "42 C";
        case LayoutEditParameter::FontFooter:
            return "192.168.1.20";
        case LayoutEditParameter::FontClockTime:
            return "12:34";
        case LayoutEditParameter::FontClockDate:
            return "Wed 31";
        default:
            return "Sample";
    }
}

HFONT CreatePreviewFontToFit(HWND hwnd, int controlId, const UiFontConfig& font, std::string_view sampleText) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr || sampleText.empty()) {
        return nullptr;
    }

    RECT rect{};
    GetClientRect(control, &rect);
    const int controlWidth = static_cast<int>(rect.right - rect.left);
    const int controlHeight = static_cast<int>(rect.bottom - rect.top);
    const int availableWidth = std::max(1, controlWidth - 6);
    const int availableHeight = std::max(1, controlHeight - 6);
    const int dpi = GetDpiForWindow(hwnd);
    HDC dc = GetDC(control);
    if (dc == nullptr) {
        return nullptr;
    }

    HFONT fittedFont = nullptr;
    const int startingSize = std::max(1, font.size);
    for (int previewSize = startingSize; previewSize >= 1; --previewSize) {
        LOGFONTA logFont{};
        logFont.lfHeight = -MulDiv(previewSize, dpi, 72);
        logFont.lfWeight = font.weight;
        strncpy_s(logFont.lfFaceName, font.face.c_str(), _TRUNCATE);
        HFONT candidate = CreateFontIndirectA(&logFont);
        if (candidate == nullptr) {
            continue;
        }

        HFONT previous = reinterpret_cast<HFONT>(SelectObject(dc, candidate));
        SIZE sampleSize{};
        const BOOL measured =
            GetTextExtentPoint32A(dc, sampleText.data(), static_cast<int>(sampleText.size()), &sampleSize);
        SelectObject(dc, previous);
        if (measured == TRUE && sampleSize.cx <= availableWidth && sampleSize.cy <= availableHeight) {
            fittedFont = candidate;
            break;
        }
        DeleteObject(candidate);
    }

    ReleaseDC(control, dc);
    return fittedFont;
}

ControlIdList MakeControlIdList(const int* ids, size_t count) {
    return {ids, count};
}

ControlIdList ActiveEditorLabelControls(LayoutEditEditorKind kind, bool showBinding) {
    switch (kind) {
        case LayoutEditEditorKind::Font:
            return MakeControlIdList(kFontEditorLabelControls, ARRAYSIZE(kFontEditorLabelControls));
        case LayoutEditEditorKind::GlobalFontFamily:
            return MakeControlIdList(kGlobalFontFamilyLabelControls, ARRAYSIZE(kGlobalFontFamilyLabelControls));
        case LayoutEditEditorKind::Color:
            return MakeControlIdList(kColorEditorLabelControls, ARRAYSIZE(kColorEditorLabelControls));
        case LayoutEditEditorKind::Weights:
            return MakeControlIdList(kWeightEditorLabelControls, ARRAYSIZE(kWeightEditorLabelControls));
        case LayoutEditEditorKind::Metric:
            return showBinding ?
                MakeControlIdList(kMetricEditorBindingLabelControls, ARRAYSIZE(kMetricEditorBindingLabelControls)) :
                MakeControlIdList(kMetricEditorLabelControls, ARRAYSIZE(kMetricEditorLabelControls));
        case LayoutEditEditorKind::DateTimeFormat:
            return MakeControlIdList(kDateTimeFormatLabelControls, ARRAYSIZE(kDateTimeFormatLabelControls));
        case LayoutEditEditorKind::LayoutSelector:
        case LayoutEditEditorKind::ThemeSelector:
            return MakeControlIdList(kThemeOrLayoutLabelControls, ARRAYSIZE(kThemeOrLayoutLabelControls));
        case LayoutEditEditorKind::MetricListOrder:
        case LayoutEditEditorKind::Numeric:
        case LayoutEditEditorKind::Summary:
            return {};
    }
    return {};
}

bool ColorSelectionSupportsDerived(const LayoutEditDialogState* state) {
    return state != nullptr && state->selectedLeaf != nullptr &&
        std::holds_alternative<LayoutEditParameter>(state->selectedLeaf->focusKey) &&
        state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex;
}

bool ColorEditorDerivedMode(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MODE_COMBO);
    if (combo == nullptr) {
        return false;
    }
    return SendMessageA(combo, CB_GETCURSEL, 0, 0) == 1;
}

bool ColorEditorLchView(HWND hwnd) {
    HWND tab = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB);
    return tab != nullptr && TabCtrl_GetCurSel(tab) == 1;
}

void ShowColorEditorControls(HWND hwnd, bool showColor, bool supportsDerived, bool derivedMode) {
    ShowDialogControls(hwnd, kColorModeControls, ARRAYSIZE(kColorModeControls), showColor && supportsDerived);
    ShowDialogControls(
        hwnd, kDerivedColorControls, ARRAYSIZE(kDerivedColorControls), showColor && supportsDerived && derivedMode);

    const bool showLiteral = showColor && (!supportsDerived || !derivedMode);
    const bool showLch = showLiteral && ColorEditorLchView(hwnd);
    HWND tab = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB);
    const bool showHsv = showLiteral && tab != nullptr && TabCtrl_GetCurSel(tab) == 2;
    const bool showRgb = showLiteral && !showLch && !showHsv;
    ShowDialogControls(hwnd, kLiteralColorControls, ARRAYSIZE(kLiteralColorControls), showLiteral);
    ShowDialogControls(hwnd, kRgbColorControls, ARRAYSIZE(kRgbColorControls), showRgb);
    ShowDialogControls(hwnd, kLchColorControls, ARRAYSIZE(kLchColorControls), showLch);
    ShowDialogControls(hwnd, kHsvColorControls, ARRAYSIZE(kHsvColorControls), showHsv);

    ShowDialogControls(hwnd, kColorPreviewControls, ARRAYSIZE(kColorPreviewControls), showColor);
}

int MeasureLabelColumnWidth(HWND hwnd, ControlIdList labelIds) {
    int width = 0;
    for (size_t i = 0; i < labelIds.count; ++i) {
        const int labelId = labelIds.ids[i];
        width = std::max(width, MeasureTextWidthForControl(hwnd, labelId, ReadDialogControlText(hwnd, labelId)));
    }
    return width;
}

void EnableStaticVerticalCentering(HWND hwnd, int labelId) {
    HWND label = GetDlgItem(hwnd, labelId);
    if (label == nullptr) {
        return;
    }
    const LONG_PTR style = GetWindowLongPtrA(label, GWL_STYLE);
    if ((style & SS_CENTERIMAGE) == 0) {
        SetWindowLongPtrA(label, GWL_STYLE, style | SS_CENTERIMAGE);
    }
}

int RowLabelVisualTopAdjustment(HWND hwnd) {
    return -DialogUnitsToPixelsY(hwnd, 2);
}

COLORREF RgbColor(int red, int green, int blue) {
    return RGB(std::clamp(red, 0, 255), std::clamp(green, 0, 255), std::clamp(blue, 0, 255));
}

COLORREF ColorRefFromBytes(ColorBytes color) {
    return RgbColor(static_cast<int>(std::lround(color.r)),
        static_cast<int>(std::lround(color.g)),
        static_cast<int>(std::lround(color.b)));
}

std::optional<OklchColor> ReadDialogLchForGradient(HWND hwnd) {
    const auto lightness = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT);
    const auto chroma = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT);
    const auto hue = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT);
    if (!lightness.has_value() || !chroma.has_value() || !hue.has_value()) {
        return std::nullopt;
    }
    return OklchColor{
        std::clamp(*lightness, 0.0, 1.0),
        std::max(0.0, *chroma),
        std::clamp(*hue, 0.0, 360.0),
    };
}

OklchColor CurrentLchForGradient(HWND hwnd) {
    if (const auto lch = ReadDialogLchForGradient(hwnd); lch.has_value()) {
        return *lch;
    }
    if (const auto color = ReadColorDialogValue(hwnd); color.has_value()) {
        return OklchFromColorBytes(ColorBytesFromRgba(*color));
    }
    return OklchColor{0.5, 0.0, 0.0};
}

std::optional<HsvColor> ReadDialogHsvForGradient(HWND hwnd) {
    const auto hue = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT);
    const auto saturation = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT);
    const auto value = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT);
    if (!hue.has_value() || !saturation.has_value() || !value.has_value()) {
        return std::nullopt;
    }
    return HsvColor{
        std::clamp(*hue, 0.0, 360.0),
        std::clamp(*saturation, 0.0, 1.0),
        std::clamp(*value, 0.0, 1.0),
    };
}

HsvColor CurrentHsvForGradient(HWND hwnd) {
    if (const auto hsv = ReadDialogHsvForGradient(hwnd); hsv.has_value()) {
        return *hsv;
    }
    if (const auto color = ReadColorDialogValue(hwnd); color.has_value()) {
        return HsvFromColorBytes(ColorBytesFromRgba(*color));
    }
    return HsvColor{0.0, 0.0, 0.0};
}

COLORREF ColorGradientBarColor(HWND hwnd, int controlId, double position) {
    const double t = std::clamp(position, 0.0, 1.0);
    switch (controlId) {
        case IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT:
            return RgbColor(static_cast<int>(std::lround(t * 255.0)), 0, 0);
        case IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT:
            return RgbColor(0, static_cast<int>(std::lround(t * 255.0)), 0);
        case IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT:
            return RgbColor(0, 0, static_cast<int>(std::lround(t * 255.0)));
        case IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT: {
            const int value = static_cast<int>(std::lround(t * 255.0));
            return RgbColor(value, value, value);
        }
        case IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT: {
            const OklchColor current = CurrentLchForGradient(hwnd);
            return ColorRefFromBytes(
                ColorBytesFromOklch(OklchColor{current.l, t * kLchGradientChromaMax, current.h}, 255.0));
        }
        case IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT:
            return ColorRefFromBytes(ColorBytesFromOklch(OklchColor{0.65, kLchGradientChromaMax, t * 360.0}, 255.0));
        case IDC_LAYOUT_EDIT_COLOR_HSV_HUE_GRADIENT:
            return ColorRefFromBytes(ColorBytesFromHsv(HsvColor{t * 360.0, 1.0, 1.0}, 255.0));
        case IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_GRADIENT: {
            const HsvColor current = CurrentHsvForGradient(hwnd);
            return ColorRefFromBytes(ColorBytesFromHsv(HsvColor{current.h, t, current.v}, 255.0));
        }
        case IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_GRADIENT: {
            const HsvColor current = CurrentHsvForGradient(hwnd);
            return ColorRefFromBytes(ColorBytesFromHsv(HsvColor{current.h, current.s, t}, 255.0));
        }
    }
    return GetSysColor(COLOR_3DFACE);
}

std::pair<int, int> SliderTrackHorizontalBounds(HWND hwnd, int sliderId, int fallbackLeft, int fallbackWidth) {
    HWND slider = GetDlgItem(hwnd, sliderId);
    if (slider == nullptr) {
        return {fallbackLeft, fallbackWidth};
    }

    RECT channelRect{};
    SendMessageA(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channelRect));
    const int channelWidth = static_cast<int>(channelRect.right - channelRect.left);
    if (channelWidth <= 0) {
        return {fallbackLeft, fallbackWidth};
    }
    return {fallbackLeft + static_cast<int>(channelRect.left), channelWidth};
}

int LayoutLabeledControlRow(HWND hwnd,
    int labelId,
    int controlId,
    int left,
    int top,
    int labelWidth,
    int gap,
    int controlWidth,
    int forcedRowHeight = 0) {
    const int visibleControlHeight = DialogControlVisibleHeight(hwnd, controlId);
    const int desiredVisibleControlHeight =
        forcedRowHeight > 0 && UsesSingleLineFieldFrame(hwnd, controlId) ? forcedRowHeight : visibleControlHeight;
    const int controlHeight = DialogControlLayoutHeightForVisibleHeight(hwnd, controlId, desiredVisibleControlHeight);
    const int labelHeight =
        MeasureTextHeightForControl(hwnd, labelId, ReadDialogControlText(hwnd, labelId), std::max(1, labelWidth), true);
    const int controlLeft = left + labelWidth + gap;
    const int labelBoxHeight = std::max(desiredVisibleControlHeight, labelHeight);
    const int rowHeight = std::max(forcedRowHeight, std::max(controlHeight, labelBoxHeight));
    const int controlTop = top + ((rowHeight - controlHeight) / 2);
    SetDialogControlBounds(hwnd, controlId, controlLeft, controlTop, controlWidth, controlHeight);

    const int labelTop =
        controlTop + ((desiredVisibleControlHeight - labelBoxHeight) / 2) + RowLabelVisualTopAdjustment(hwnd);
    EnableStaticVerticalCentering(hwnd, labelId);
    SetDialogControlBounds(hwnd, labelId, left, labelTop, labelWidth, labelBoxHeight);
    return rowHeight;
}

void SetDialogCenteredRowBounds(
    HWND hwnd, int controlId, int left, int top, int width, int height, int rowHeight, bool applyVisualAdjustment) {
    SetDialogControlBounds(hwnd,
        controlId,
        left,
        top + ((rowHeight - height) / 2) + (applyVisualAdjustment ? RowLabelVisualTopAdjustment(hwnd) : 0),
        width,
        height);
}

int LayoutCheckSliderRow(
    HWND hwnd, const CheckSliderRowLayout& layout, int top, int checkId, int editId, int sliderId) {
    const int checkHeight = DialogControlHeight(hwnd, checkId);
    const int rowHeight = Max3Int(checkHeight, layout.fieldHeight, layout.sliderHeight);
    SetDialogCenteredRowBounds(hwnd, checkId, layout.labelLeft, top, layout.labelWidth, checkHeight, rowHeight, true);
    SetDialogCenteredRowBounds(
        hwnd, editId, layout.valueLeft, top, layout.valueWidth, layout.fieldHeight, rowHeight, false);
    SetDialogCenteredRowBounds(
        hwnd, sliderId, layout.sliderLeft, top, layout.sliderWidth, layout.sliderHeight, rowHeight, false);
    return top + rowHeight + layout.rowGap;
}

int LayoutEditorHint(HWND hwnd, int left, int top, int width) {
    const std::string hintText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_HINT);
    const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, width);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, left, top, width, hintHeight);
    return top + hintHeight;
}

int LayoutLabeledControlRows(HWND hwnd,
    const LabeledControlIds* rows,
    size_t rowCount,
    int left,
    int top,
    int labelWidth,
    int labelGap,
    int controlWidth,
    int forcedRowHeight,
    int rowGap,
    int finalGap) {
    for (size_t i = 0; i < rowCount; ++i) {
        const int rowHeight = LayoutLabeledControlRow(
            hwnd, rows[i].labelId, rows[i].controlId, left, top, labelWidth, labelGap, controlWidth, forcedRowHeight);
        top += rowHeight + (i + 1 < rowCount ? rowGap : finalGap);
    }
    return top;
}

}  // namespace

DialogRedrawScope::DialogRedrawScope(HWND hwnd, UINT redrawFlags) :
    hwnd_(hwnd),
    redrawFlags_(redrawFlags) {
    BeginWindowRedrawSuspension(hwnd_);
}

DialogRedrawScope::DialogRedrawScope(HWND hwnd, const RECT& redrawRect, UINT redrawFlags) :
    hwnd_(hwnd),
    redrawRect_(redrawRect),
    redrawFlags_(redrawFlags | RDW_ERASE) {
    BeginWindowRedrawSuspension(hwnd_);
}

DialogRedrawScope::~DialogRedrawScope() {
    const RECT* rect = redrawRect_.has_value() ? &*redrawRect_ : nullptr;
    EndWindowRedrawSuspension(hwnd_, rect, redrawFlags_);
}

DialogRedrawScope::DialogRedrawScope(DialogRedrawScope&& other) noexcept :
    hwnd_(other.hwnd_),
    redrawRect_(other.redrawRect_),
    redrawFlags_(other.redrawFlags_) {
    other.hwnd_ = nullptr;
    other.redrawRect_.reset();
    other.redrawFlags_ = 0;
}

DialogRedrawScope& DialogRedrawScope::operator=(DialogRedrawScope&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    const RECT* rect = redrawRect_.has_value() ? &*redrawRect_ : nullptr;
    EndWindowRedrawSuspension(hwnd_, rect, redrawFlags_);
    hwnd_ = other.hwnd_;
    redrawRect_ = other.redrawRect_;
    redrawFlags_ = other.redrawFlags_;
    other.hwnd_ = nullptr;
    other.redrawRect_.reset();
    other.redrawFlags_ = 0;
    return *this;
}

DialogDescendantRedrawScope::DialogDescendantRedrawScope(HWND hwnd, UINT redrawFlags) :
    root_(hwnd),
    redrawFlags_(redrawFlags) {
    BeginWindowRedrawSuspension(root_);
}

DialogDescendantRedrawScope::~DialogDescendantRedrawScope() {
    EndWindowRedrawSuspension(root_, nullptr, 0);
    if (root_ != nullptr && redrawFlags_ != 0) {
        RedrawWindow(root_, nullptr, nullptr, redrawFlags_);
    }
}

void ShowDialogControl(HWND hwnd, int controlId, bool show) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void ShowDialogControls(HWND hwnd, const int* controlIds, size_t controlCount, bool show) {
    for (size_t i = 0; i < controlCount; ++i) {
        ShowDialogControl(hwnd, controlIds[i], show);
    }
}

void InvalidateDialogControls(HWND hwnd, const int* controlIds, size_t controlCount) {
    for (size_t i = 0; i < controlCount; ++i) {
        InvalidateRect(GetDlgItem(hwnd, controlIds[i]), nullptr, TRUE);
    }
}

void BringDialogControlToTop(HWND hwnd, int controlId) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        SetWindowPos(control, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

std::optional<RECT> DialogControlRect(HWND hwnd, int controlId) {
    RECT rect{};
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr || !GetWindowRect(control, &rect)) {
        return std::nullopt;
    }
    MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rect), 2);
    return rect;
}

int DialogControlWidth(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    return rect.has_value() ? static_cast<int>(rect->right - rect->left) : 0;
}

int DialogControlHeight(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    return rect.has_value() ? static_cast<int>(rect->bottom - rect->top) : 0;
}

int DialogControlVisibleHeight(HWND hwnd, int controlId) {
    if (IsDialogComboBoxControl(hwnd, controlId)) {
        const int comboHeight = DialogComboBoxSelectionHeight(hwnd, controlId);
        if (comboHeight > 0) {
            return comboHeight;
        }
    }
    const auto rect = DialogControlRect(hwnd, controlId);
    if (!rect.has_value()) {
        return 0;
    }
    return std::max(1, static_cast<int>(rect->bottom - rect->top));
}

int DialogControlLayoutHeightForVisibleHeight(HWND hwnd, int controlId, int desiredVisibleHeight) {
    const int currentHeight = DialogControlHeight(hwnd, controlId);
    if (IsDialogComboBoxControl(hwnd, controlId)) {
        return desiredVisibleHeight;
    }
    if (!UsesSingleLineFieldFrame(hwnd, controlId)) {
        return desiredVisibleHeight;
    }
    const int currentVisibleHeight = DialogControlVisibleHeight(hwnd, controlId);
    const int framePadding = std::max(0, currentHeight - currentVisibleHeight);
    return desiredVisibleHeight + framePadding;
}

int MeasureControlFontHeight(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }
    HDC dc = GetDC(control);
    if (dc == nullptr) {
        return 0;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageA(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    TEXTMETRICA metrics{};
    const BOOL measured = GetTextMetricsA(dc, &metrics);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(control, dc);
    return measured == TRUE ? std::max(1, static_cast<int>(metrics.tmHeight)) : 0;
}

int MeasureSingleLineFieldVisibleHeight(HWND hwnd) {
    const int textHeight = MeasureControlFontHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT);
    const int paddedTextHeight = textHeight > 0 ? textHeight + DialogUnitsToPixelsY(hwnd, 4) : 0;
    const int comboHeight = DialogComboBoxSelectionHeight(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    const int measuredHeight = std::max(paddedTextHeight, comboHeight);
    if (measuredHeight > 0) {
        return measuredHeight;
    }
    const int editHeight = DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT);
    if (editHeight > 0) {
        return editHeight;
    }
    return 14;
}

void SetDialogControlBounds(HWND hwnd, int controlId, int left, int top, int width, int height) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        SetWindowPos(control, nullptr, left, top, std::max(1, width), std::max(1, height), SWP_NOZORDER);
    }
}

int MeasureTextWidthForControl(HWND hwnd, int controlId, std::string_view text) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return DialogControlWidth(hwnd, controlId);
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageA(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SIZE size{};
    const std::string_view measuredText = text.empty() ? std::string_view(kDialogBlankText, 1) : text;
    GetTextExtentPoint32A(dc, measuredText.data(), static_cast<int>(measuredText.size()), &size);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(hwnd, dc);
    return size.cx;
}

int MeasureTextHeightForControl(HWND hwnd, int controlId, std::string_view text, int width, bool singleLine) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return DialogControlHeight(hwnd, controlId);
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageA(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    RECT rect{0, 0, std::max(1, width), 0};
    const std::string_view measuredText = text.empty() ? std::string_view(kDialogBlankText, 1) : text;
    UINT flags = DT_NOPREFIX | DT_CALCRECT | (singleLine ? DT_SINGLELINE : DT_WORDBREAK);
    DrawTextA(dc, measuredText.data(), static_cast<int>(measuredText.size()), &rect, flags);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(hwnd, dc);
    const int measuredHeight = static_cast<int>(rect.bottom - rect.top);
    return std::max(1, measuredHeight);
}

int DialogUnitsToPixelsY(HWND hwnd, int dialogUnitsY) {
    RECT rect{0, 0, 0, dialogUnitsY};
    MapDialogRect(hwnd, &rect);
    return rect.bottom - rect.top;
}

int DialogUnitsToPixelsX(HWND hwnd, int dialogUnitsX) {
    RECT rect{0, 0, dialogUnitsX, 0};
    MapDialogRect(hwnd, &rect);
    return rect.right - rect.left;
}

std::optional<RECT> LayoutEditRightPaneRect(HWND hwnd) {
    RECT clientRect{};
    if (hwnd == nullptr || !GetClientRect(hwnd, &clientRect)) {
        return std::nullopt;
    }

    const auto dividerRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_DIVIDER);
    if (!dividerRect.has_value()) {
        return clientRect;
    }

    RECT paneRect = clientRect;
    paneRect.left = dividerRect->right;
    return paneRect;
}

void RefreshLayoutEditRightPane(HWND hwnd) {
    if (const auto paneRect = LayoutEditRightPaneRect(hwnd); paneRect.has_value()) {
        RedrawWindow(hwnd, &*paneRect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void EnsureLayoutEditDialogControls(HWND hwnd) {
    if (hwnd == nullptr || GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE) != nullptr) {
        return;
    }
    const WPARAM font = LayoutEditDialogFont(hwnd);
    for (const LayoutEditControlSpec& spec : kLayoutEditControlSpecs) {
        CreateLayoutEditControl(hwnd, spec, font);
    }
}

void ConfigureDialogFonts(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }
    DestroyDialogFont(state->titleFont);
    DestroyDialogFont(state->fontSampleFont);
    state->titleFont = CreateDerivedDialogFont(hwnd, IDC_LAYOUT_EDIT_TITLE, FW_BOLD, 2);
    state->fontSampleFont = CreateDerivedDialogFont(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, FW_NORMAL);
    if (state->titleFont != nullptr) {
        SendDlgItemMessageA(hwnd, IDC_LAYOUT_EDIT_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
    }
}

void DestroyDialogFonts(LayoutEditDialogState* state) {
    if (state == nullptr) {
        return;
    }
    DestroyDialogFont(state->titleFont);
    DestroyDialogFont(state->fontSampleFont);
}

void SetLayoutEditStatus(LayoutEditDialogState* state, HWND hwnd, LayoutEditStatusKind kind, std::string_view text) {
    if (state == nullptr) {
        return;
    }
    state->statusIsError = kind == LayoutEditStatusKind::Error;
    state->statusText = text;
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_STATUS_TEXT, text);
}

void SetColorSamplePreview(LayoutEditDialogState* state, HWND hwnd, unsigned int color) {
    if (state == nullptr) {
        return;
    }
    state->previewColor = RGB((color >> 24) & 0xFFu, (color >> 16) & 0xFFu, (color >> 8) & 0xFFu);
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, "Sample text in the selected color");
    const std::string derivedHexText = FormatText("Hex: %s", FormatDialogColorHex(color).c_str());
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL, derivedHexText);
    InvalidateDialogControls(hwnd, kColorPreviewInvalidationControls, ARRAYSIZE(kColorPreviewInvalidationControls));
}

bool IsColorGradientBarControlId(int controlId) {
    return controlId == IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT || controlId == IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT ||
        controlId == IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT || controlId == IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT ||
        controlId == IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT || controlId == IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT ||
        controlId == IDC_LAYOUT_EDIT_COLOR_HSV_HUE_GRADIENT ||
        controlId == IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_GRADIENT ||
        controlId == IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_GRADIENT;
}

void DrawColorGradientBar(HWND hwnd, const DRAWITEMSTRUCT& drawItem) {
    if (drawItem.hDC == nullptr) {
        return;
    }

    const int width = std::max(1, static_cast<int>(drawItem.rcItem.right - drawItem.rcItem.left));
    RECT lineRect = drawItem.rcItem;
    for (int x = 0; x < width; ++x) {
        const double position = width <= 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(width - 1);
        lineRect.left = drawItem.rcItem.left + x;
        lineRect.right = lineRect.left + 1;
        HBRUSH brush = CreateSolidBrush(ColorGradientBarColor(hwnd, static_cast<int>(drawItem.CtlID), position));
        FillRect(drawItem.hDC, &lineRect, brush);
        DeleteObject(brush);
    }

    FrameRect(drawItem.hDC, &drawItem.rcItem, GetSysColorBrush(COLOR_3DSHADOW));
}

void DrawThemePreview(LayoutEditDialogState* state, const DRAWITEMSTRUCT& drawItem) {
    if (state == nullptr || drawItem.hDC == nullptr) {
        return;
    }
    const ThemeConfig* theme = FindActiveThemeConfig(state->dialog->Host().CurrentConfig());
    if (theme == nullptr) {
        FillRect(drawItem.hDC, &drawItem.rcItem, GetSysColorBrush(COLOR_3DFACE));
        return;
    }
    DrawThemePreviewTriangle(drawItem.hDC, drawItem.rcItem, *theme);
}

void SetFontSamplePreview(
    LayoutEditDialogState* state, HWND hwnd, std::optional<LayoutEditParameter> parameter, const UiFontConfig* font) {
    if (state == nullptr) {
        return;
    }
    DestroyDialogFont(state->fontSampleFont);
    state->fontSampleFont = nullptr;
    const std::string_view sampleTextView =
        font != nullptr && parameter.has_value() ? FontSampleText(*parameter) : std::string_view();
    const std::string sampleText(sampleTextView);
    SetDlgItemTextA(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, sampleText.c_str());
    if (font == nullptr || !parameter.has_value()) {
        return;
    }

    state->fontSampleFont = CreatePreviewFontToFit(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, *font, sampleText);
    if (state->fontSampleFont != nullptr) {
        SendDlgItemMessageA(
            hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, WM_SETFONT, reinterpret_cast<WPARAM>(state->fontSampleFont), TRUE);
    }
}

void ShowMetricListOrderEditorControls(LayoutEditDialogState* state, bool show) {
    if (state == nullptr) {
        return;
    }
    for (const auto& row : state->metricListRowControls) {
        if (row.combo != nullptr) {
            ShowWindow(row.combo, show ? SW_SHOW : SW_HIDE);
        }
        if (row.upButton != nullptr) {
            ShowWindow(row.upButton, show ? SW_SHOW : SW_HIDE);
        }
        if (row.downButton != nullptr) {
            ShowWindow(row.downButton, show ? SW_SHOW : SW_HIDE);
        }
        if (row.deleteButton != nullptr) {
            ShowWindow(row.deleteButton, show ? SW_SHOW : SW_HIDE);
        }
    }
    if (state->metricListAddRowButton != nullptr) {
        ShowWindow(state->metricListAddRowButton, show ? SW_SHOW : SW_HIDE);
    }
}

void ShowLayoutEditEditors(HWND hwnd, LayoutEditEditorKind kind, bool showBinding) {
    const bool showNumeric = kind == LayoutEditEditorKind::Numeric;
    const bool showFont = kind == LayoutEditEditorKind::Font;
    const bool showColor = kind == LayoutEditEditorKind::Color;
    const bool showWeights = kind == LayoutEditEditorKind::Weights;
    const bool showMetric = kind == LayoutEditEditorKind::Metric;
    const bool showMetricListOrder = kind == LayoutEditEditorKind::MetricListOrder;
    const bool showGlobalFontFamily = kind == LayoutEditEditorKind::GlobalFontFamily;
    const bool showDateTimeFormat = kind == LayoutEditEditorKind::DateTimeFormat;
    const bool showThemeSelector = kind == LayoutEditEditorKind::ThemeSelector;
    const bool showLayoutSelector = kind == LayoutEditEditorKind::LayoutSelector;
    const bool showSectionSelector = showThemeSelector || showLayoutSelector;
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, showNumeric);
    ShowDialogControl(hwnd,
        IDC_LAYOUT_EDIT_SUMMARY,
        !(showNumeric || showFont || showColor || showWeights || showMetric || showMetricListOrder ||
            showGlobalFontFamily || showDateTimeFormat || showSectionSelector));
    ShowDialogControls(hwnd, kSectionSelectorControls, ARRAYSIZE(kSectionSelectorControls), showSectionSelector);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_THEME_PREVIEW, showThemeSelector);

    ShowDialogControls(hwnd, kFontFaceControls, ARRAYSIZE(kFontFaceControls), showFont || showGlobalFontFamily);
    ShowDialogControls(hwnd, kFontControls, ARRAYSIZE(kFontControls), showFont);

    ShowColorEditorControls(hwnd, showColor, false, false);

    ShowDialogControls(hwnd, kWeightControls, ARRAYSIZE(kWeightControls), showWeights);
    ShowDialogControls(hwnd, kMetricControls, ARRAYSIZE(kMetricControls), showMetric);
    ShowDialogControls(hwnd, kMetricBindingControls, ARRAYSIZE(kMetricBindingControls), showMetric && showBinding);
    ShowDialogControls(hwnd, kDateTimeFormatControls, ARRAYSIZE(kDateTimeFormatControls), showDateTimeFormat);
    ShowDialogControl(hwnd,
        IDC_LAYOUT_EDIT_HINT,
        showNumeric || showFont || showColor || showWeights || showMetric || showMetricListOrder ||
            showGlobalFontFamily || showDateTimeFormat || showSectionSelector);
}

void DestroyMetricListOrderEditorControls(LayoutEditDialogState* state) {
    if (state == nullptr) {
        return;
    }
    for (auto& row : state->metricListRowControls) {
        if (row.combo != nullptr) {
            DestroyWindow(row.combo);
        }
        if (row.upButton != nullptr) {
            DestroyWindow(row.upButton);
        }
        if (row.downButton != nullptr) {
            DestroyWindow(row.downButton);
        }
        if (row.deleteButton != nullptr) {
            DestroyWindow(row.deleteButton);
        }
    }
    state->metricListRowControls.clear();
    if (state->metricListAddRowButton != nullptr) {
        DestroyWindow(state->metricListAddRowButton);
        state->metricListAddRowButton = nullptr;
    }
}

void EnsureMetricListOrderEditorControls(LayoutEditDialogState* state, HWND hwnd, size_t rowCount) {
    if (state == nullptr || hwnd == nullptr) {
        return;
    }
    if (state->metricListRowControls.size() == rowCount) {
        if (state->metricListAddRowButton == nullptr) {
            state->metricListAddRowButton = CreateMetricListEditorControl(hwnd,
                LayoutEditControlKind::Button,
                LayoutEditControlText::AddRow,
                IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW);
        }
        return;
    }

    DestroyMetricListOrderEditorControls(state);

    state->metricListRowControls.resize(rowCount);
    for (size_t i = 0; i < rowCount; ++i) {
        LayoutEditMetricListRowControls& row = state->metricListRowControls[i];
        row.comboId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_COMBO_BASE + static_cast<int>(i);
        row.upButtonId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE + static_cast<int>(i);
        row.downButtonId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE + static_cast<int>(i);
        row.deleteButtonId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE + static_cast<int>(i);
        row.combo = CreateMetricListEditorControl(
            hwnd, LayoutEditControlKind::ComboList, LayoutEditControlText::Empty, row.comboId);
        row.upButton = CreateMetricListEditorControl(
            hwnd, LayoutEditControlKind::OwnerDrawButton, LayoutEditControlText::Empty, row.upButtonId);
        row.downButton = CreateMetricListEditorControl(
            hwnd, LayoutEditControlKind::OwnerDrawButton, LayoutEditControlText::Empty, row.downButtonId);
        row.deleteButton = CreateMetricListEditorControl(
            hwnd, LayoutEditControlKind::OwnerDrawButton, LayoutEditControlText::Empty, row.deleteButtonId);
    }

    state->metricListAddRowButton = CreateMetricListEditorControl(
        hwnd, LayoutEditControlKind::Button, LayoutEditControlText::AddRow, IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW);
}

void LayoutLayoutEditRightPane(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    const LayoutEditRightPaneMetrics& metrics = kLayoutEditRightPaneMetrics;
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    const auto dividerRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_DIVIDER);
    const auto treeRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_TREE);
    const auto filterEditRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_FILTER_EDIT);
    if (!dividerRect.has_value() || !treeRect.has_value() || !filterEditRect.has_value()) {
        return;
    }

    const int outerEdgeMargin = static_cast<int>(treeRect->left);
    const int dividerGap = std::max(0, static_cast<int>(dividerRect->left - treeRect->right));
    const int leftPaneBottom = clientRect.bottom - outerEdgeMargin;
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_TREE,
        static_cast<int>(treeRect->left),
        static_cast<int>(treeRect->top),
        static_cast<int>(treeRect->right - treeRect->left),
        std::max(1, leftPaneBottom - static_cast<int>(treeRect->top)));
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_DIVIDER,
        static_cast<int>(dividerRect->left),
        static_cast<int>(dividerRect->top),
        static_cast<int>(dividerRect->right - dividerRect->left),
        std::max(1, leftPaneBottom - static_cast<int>(dividerRect->top)));
    const int paneLeft = static_cast<int>(dividerRect->right) + dividerGap;
    const int paneRight = clientRect.right - outerEdgeMargin;
    const int paneWidth = std::max(1, paneRight - paneLeft);

    const std::string footerText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_FOOTER_HINT);
    const int footerHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_FOOTER_HINT, footerText, paneWidth);
    const int footerBottom = leftPaneBottom;
    const int footerTop = footerBottom - footerHeight;
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_FOOTER_HINT, paneLeft, footerTop, paneWidth, footerHeight);

    const int revertWidth = std::max(DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_REVERT),
        MeasureTextWidthForControl(hwnd, IDC_LAYOUT_EDIT_REVERT, ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_REVERT)) +
            24);
    const int revertHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_REVERT);
    const int statusWidth = std::max(1, paneWidth - revertWidth - metrics.inlineGap);
    const std::string statusText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_STATUS_TEXT);
    const int statusHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_STATUS_TEXT, statusText, statusWidth);
    const int statusRowHeight = std::max(statusHeight, revertHeight);
    const int statusTop = footerTop - metrics.statusToFooterGap - statusRowHeight;
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_STATUS_TEXT,
        paneLeft,
        statusTop + ((statusRowHeight - statusHeight) / 2),
        statusWidth,
        statusHeight);
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_REVERT,
        paneRight - revertWidth,
        statusTop + ((statusRowHeight - revertHeight) / 2),
        revertWidth,
        revertHeight);

    int y = std::min(dividerRect->top, filterEditRect->top);
    const std::string titleText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_TITLE);
    const int titleHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_TITLE, titleText, paneWidth, true);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_TITLE, paneLeft, y, paneWidth, titleHeight);
    y += titleHeight + metrics.headerGap;

    const std::string locationText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_LOCATION);
    const int locationHeight = MeasureTextHeightForControl(hwnd,
        IDC_LAYOUT_EDIT_LOCATION,
        locationText.empty() ? std::string_view(kDialogBlankText, 1) : std::string_view(locationText),
        paneWidth,
        true);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_LOCATION, paneLeft, y, paneWidth, locationHeight);
    y += locationHeight + metrics.headerGap;

    const std::string descriptionText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION);
    const int descriptionSingleLineHeight = MeasureTextHeightForControl(
        hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, std::string_view(kDialogMeasureSampleText, 2), paneWidth, true);
    const int descriptionHeight = std::max(
        MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, descriptionText, paneWidth),
        descriptionSingleLineHeight * 2);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, paneLeft, y, paneWidth, descriptionHeight);
    y += descriptionHeight + metrics.headerToGroupGap;

    const int groupTop = y;
    const int maxGroupHeight = std::max(60, (statusTop - metrics.groupToStatusGap) - groupTop);

    const int groupTopBorderInset = DialogUnitsToPixelsY(hwnd, 4);
    const int innerLeft = paneLeft + metrics.groupPadding;
    const int innerRight = paneRight - metrics.groupPadding;
    const int innerWidth = std::max(1, innerRight - innerLeft);
    const int innerTop = groupTop + metrics.groupPadding + groupTopBorderInset;

    const LayoutEditEditorKind kind = CurrentLayoutEditEditorKind(state);
    ShowMetricListOrderEditorControls(state, false);
    const bool showBinding = CurrentLayoutEditShowsMetricBinding(state);
    const int singleLineFieldHeight = MeasureSingleLineFieldVisibleHeight(hwnd);
    const ControlIdList labelControls = ActiveEditorLabelControls(kind, showBinding);
    const int labelColumnWidth = labelControls.count == 0 ? 0 : MeasureLabelColumnWidth(hwnd, labelControls) + 8;

    int cursorY = innerTop;
    int contentBottom = innerTop;
    switch (kind) {
        case LayoutEditEditorKind::Summary: {
            const std::string summaryText = ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_SUMMARY);
            const int summaryHeight =
                MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_SUMMARY, summaryText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_SUMMARY, innerLeft, cursorY, innerWidth, summaryHeight);
            contentBottom = cursorY + summaryHeight;
            break;
        }
        case LayoutEditEditorKind::Numeric: {
            const int editHeight =
                DialogControlLayoutHeightForVisibleHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, singleLineFieldHeight);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, innerLeft, cursorY, innerWidth, editHeight);
            cursorY += singleLineFieldHeight + metrics.hintGap;
            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::Font: {
            const int fontFaceRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_FONT_FACE_LABEL,
                IDC_LAYOUT_EDIT_FONT_FACE_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                innerWidth - labelColumnWidth - metrics.labelGap,
                singleLineFieldHeight);
            cursorY += fontFaceRowHeight + metrics.rowGap;

            const int labelHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
                ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL),
                labelColumnWidth,
                true);
            const int sizeEditWidth =
                std::max(56, (innerWidth - labelColumnWidth - metrics.labelGap - metrics.inlineGap) / 3);
            const int sizeEditHeight =
                DialogControlLayoutHeightForVisibleHeight(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, singleLineFieldHeight);
            const int weightEditHeight = DialogControlLayoutHeightForVisibleHeight(
                hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, singleLineFieldHeight);
            const int sizeRowHeight = Max4Int(singleLineFieldHeight, labelHeight, sizeEditHeight, weightEditHeight);
            SetDialogCenteredRowBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
                innerLeft,
                cursorY,
                labelColumnWidth,
                labelHeight,
                sizeRowHeight,
                false);
            const int sizeControlLeft = innerLeft + labelColumnWidth + metrics.labelGap;
            SetDialogCenteredRowBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_EDIT,
                sizeControlLeft,
                cursorY,
                sizeEditWidth,
                sizeEditHeight,
                sizeRowHeight,
                false);

            const int weightLabelWidth =
                MeasureTextWidthForControl(hwnd,
                    IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                    ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL)) +
                8;
            const int weightEditLeft = sizeControlLeft + sizeEditWidth + metrics.inlineGap + weightLabelWidth;
            const int weightEditWidth = std::max(72, innerRight - weightEditLeft);
            SetDialogCenteredRowBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                sizeControlLeft + sizeEditWidth + metrics.inlineGap,
                cursorY,
                weightLabelWidth,
                labelHeight,
                sizeRowHeight,
                false);
            SetDialogCenteredRowBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT,
                weightEditLeft,
                cursorY,
                weightEditWidth,
                weightEditHeight,
                sizeRowHeight,
                false);
            cursorY += sizeRowHeight + metrics.sampleGap;

            const int sampleHeight = std::max(28, DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE));
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, innerLeft, cursorY, innerWidth, sampleHeight);
            cursorY += sampleHeight + metrics.hintGap;

            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::GlobalFontFamily: {
            const int fontFaceRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_FONT_FACE_LABEL,
                IDC_LAYOUT_EDIT_FONT_FACE_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                innerWidth - labelColumnWidth - metrics.labelGap,
                singleLineFieldHeight);
            cursorY += fontFaceRowHeight + metrics.hintGap;

            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::LayoutSelector: {
            const int comboWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int comboHeight =
                std::max(DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO), singleLineFieldHeight);
            const int rowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_THEME_LABEL,
                IDC_LAYOUT_EDIT_THEME_COMBO,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                comboWidth,
                comboHeight);
            cursorY += rowHeight + metrics.hintGap;

            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::ThemeSelector: {
            const int comboWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int comboHeight =
                std::max(DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO), singleLineFieldHeight);
            const int rowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_THEME_LABEL,
                IDC_LAYOUT_EDIT_THEME_COMBO,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                comboWidth,
                comboHeight);
            cursorY += rowHeight + metrics.sampleGap;

            const int previewHeight = std::max(
                DialogUnitsToPixelsY(hwnd, 86), std::min(maxGroupHeight - metrics.groupPadding, innerWidth * 2 / 3));
            SetDialogControlBounds(
                hwnd, IDC_LAYOUT_EDIT_THEME_PREVIEW, innerLeft, cursorY, innerWidth, std::max(1, previewHeight));
            cursorY += std::max(1, previewHeight) + metrics.hintGap;

            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::Color: {
            const bool supportsDerived = ColorSelectionSupportsDerived(state);
            const bool derivedMode = supportsDerived && ColorEditorDerivedMode(hwnd);
            ShowColorEditorControls(hwnd, true, supportsDerived, derivedMode);

            if (supportsDerived) {
                const int modeRowHeight = LayoutLabeledControlRow(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MODE_LABEL,
                    IDC_LAYOUT_EDIT_COLOR_MODE_COMBO,
                    innerLeft,
                    cursorY,
                    labelColumnWidth,
                    metrics.labelGap,
                    innerWidth - labelColumnWidth - metrics.labelGap,
                    singleLineFieldHeight);
                cursorY += modeRowHeight + metrics.rowGap;
            }

            const int swatchSize = std::max(DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH),
                DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT) + 4);
            if (!derivedMode) {
                const int pickWidth = DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK);
                const int pickHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK);
                const int hexLabelWidth =
                    MeasureTextWidthForControl(hwnd,
                        IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                        ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL)) +
                    8;
                const int hexEditHeight = DialogControlLayoutHeightForVisibleHeight(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, singleLineFieldHeight);
                const int pickLeft = innerRight - pickWidth;
                const int hexLabelLeft = innerLeft + swatchSize + metrics.inlineGap;
                const int hexEditLeft = hexLabelLeft + hexLabelWidth + metrics.labelGap;
                const int hexEditWidth = std::max(76, pickLeft - metrics.inlineGap - hexEditLeft);
                const int firstRowHeight = Max3Int(swatchSize, hexEditHeight, pickHeight);
                const int hexLabelHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                    ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL),
                    hexLabelWidth,
                    true);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_SWATCH,
                    innerLeft,
                    cursorY,
                    swatchSize,
                    swatchSize,
                    firstRowHeight,
                    false);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                    hexLabelLeft,
                    cursorY,
                    hexLabelWidth,
                    hexLabelHeight,
                    firstRowHeight,
                    false);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_HEX_EDIT,
                    hexEditLeft,
                    cursorY,
                    hexEditWidth,
                    hexEditHeight,
                    firstRowHeight,
                    false);
                SetDialogCenteredRowBounds(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_PICK, pickLeft, cursorY, pickWidth, pickHeight, firstRowHeight, false);
                cursorY += firstRowHeight + metrics.sampleGap;
            }

            const int sampleHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_COLOR_SAMPLE,
                ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE),
                innerWidth,
                true);
            if (derivedMode) {
                const int derivedHexLeft = innerLeft + swatchSize + metrics.inlineGap;
                const int derivedHexWidth = std::max(1, innerRight - derivedHexLeft);
                const int derivedHexHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL,
                    ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL),
                    derivedHexWidth,
                    true);
                const int firstRowHeight = std::max(swatchSize, derivedHexHeight);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_SWATCH,
                    innerLeft,
                    cursorY,
                    swatchSize,
                    swatchSize,
                    firstRowHeight,
                    false);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL,
                    derivedHexLeft,
                    cursorY,
                    derivedHexWidth,
                    derivedHexHeight,
                    firstRowHeight,
                    false);
                cursorY += firstRowHeight + metrics.sampleGap;
            }
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, innerLeft, cursorY, innerWidth, sampleHeight);
            cursorY += sampleHeight + metrics.sampleGap;

            if (derivedMode) {
                const int baseRowHeight = LayoutLabeledControlRow(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_BASE_LABEL,
                    IDC_LAYOUT_EDIT_COLOR_BASE_COMBO,
                    innerLeft,
                    cursorY,
                    labelColumnWidth,
                    metrics.labelGap,
                    innerWidth - labelColumnWidth - metrics.labelGap,
                    singleLineFieldHeight);
                cursorY += baseRowHeight + metrics.rowGap;

                const int checkboxWidth = std::max(labelColumnWidth + metrics.labelGap + 82,
                    MeasureTextWidthForControl(hwnd,
                        IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
                        ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL)) +
                        metrics.labelGap);
                const int valueEditWidth = std::min(72, std::max(58, innerWidth - checkboxWidth));
                const int valueLeft = innerLeft + checkboxWidth;
                const int sliderLeft = valueLeft + valueEditWidth + metrics.inlineGap;
                const int sliderWidth = std::max(40, innerRight - sliderLeft);
                const int derivedFieldHeight = Max4Int(
                    DialogControlLayoutHeightForVisibleHeight(
                        hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT, singleLineFieldHeight),
                    DialogControlLayoutHeightForVisibleHeight(
                        hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO, singleLineFieldHeight),
                    DialogControlLayoutHeightForVisibleHeight(
                        hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT, singleLineFieldHeight),
                    DialogControlLayoutHeightForVisibleHeight(
                        hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT, singleLineFieldHeight));
                const int derivedSliderHeight = Max3Int(DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER),
                    DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER),
                    DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER));
                const CheckSliderRowLayout derivedSliderLayout{innerLeft,
                    checkboxWidth,
                    valueLeft,
                    valueEditWidth,
                    sliderLeft,
                    sliderWidth,
                    derivedFieldHeight,
                    derivedSliderHeight,
                    metrics.rowGap};
                cursorY = LayoutCheckSliderRow(hwnd,
                    derivedSliderLayout,
                    cursorY,
                    IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK,
                    IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT,
                    IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER);
                cursorY = LayoutCheckSliderRow(hwnd,
                    derivedSliderLayout,
                    cursorY,
                    IDC_LAYOUT_EDIT_COLOR_MIX_CHECK,
                    IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT,
                    IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER);

                const int mixTargetLabelHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
                    ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL),
                    checkboxWidth,
                    true);
                const int mixTargetRowHeight = std::max(mixTargetLabelHeight, derivedFieldHeight);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
                    innerLeft,
                    cursorY,
                    checkboxWidth,
                    mixTargetLabelHeight,
                    mixTargetRowHeight,
                    false);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO,
                    valueLeft,
                    cursorY,
                    std::max(1, innerRight - valueLeft),
                    derivedFieldHeight,
                    mixTargetRowHeight,
                    false);
                cursorY += mixTargetRowHeight + metrics.rowGap;

                cursorY = LayoutCheckSliderRow(hwnd,
                    derivedSliderLayout,
                    cursorY,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER);
            } else {
                const int valueEditWidth = Max3Int(DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT),
                    DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT),
                    DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT));
                const int sliderLeft =
                    innerLeft + labelColumnWidth + metrics.labelGap + valueEditWidth + metrics.inlineGap;
                const int sliderWidth = std::max(40, innerRight - sliderLeft);
                const bool lchView = state->colorEditViewMode == ColorEditViewMode::Lch;
                const bool hsvView = state->colorEditViewMode == ColorEditViewMode::Hsv;
                const ColorChannelControlIds* channelRows =
                    hsvView ? kHsvColorChannelRows : (lchView ? kLchColorChannelRows : kRgbColorChannelRows);
                const int alphaEditHeight = DialogControlLayoutHeightForVisibleHeight(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT, singleLineFieldHeight);
                const int alphaSliderHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER);
                const int gradientBarHeight = std::max(1,
                    MeasureTextHeightForControl(hwnd,
                        channelRows[0].labelId,
                        ReadDialogControlText(hwnd, channelRows[0].labelId),
                        labelColumnWidth,
                        true));
                const int gradientGap = DialogUnitsToPixelsY(hwnd, 2);
                int tabContentHeight = 0;
                int channelRowHeights[3] = {};
                int channelLabelHeights[3] = {};
                int channelEditHeights[3] = {};
                int channelSliderHeights[3] = {};
                for (int i = 0; i < 3; ++i) {
                    const ColorChannelControlIds& channel = channelRows[i];
                    channelEditHeights[i] =
                        DialogControlLayoutHeightForVisibleHeight(hwnd, channel.editId, singleLineFieldHeight);
                    channelSliderHeights[i] = DialogControlHeight(hwnd, channel.sliderId);
                    channelLabelHeights[i] = MeasureTextHeightForControl(
                        hwnd, channel.labelId, ReadDialogControlText(hwnd, channel.labelId), labelColumnWidth, true);
                    const int sliderColumnHeight = channelSliderHeights[i] + gradientGap + gradientBarHeight;
                    channelRowHeights[i] = Max3Int(channelEditHeights[i], sliderColumnHeight, channelLabelHeights[i]);
                    tabContentHeight += channelRowHeights[i] + (i < 2 ? metrics.rowGap : 0);
                }
                const int tabHeaderHeight = DialogUnitsToPixelsY(hwnd, 16);
                const int tabInsetX = DialogUnitsToPixelsX(hwnd, 6);
                const int tabInsetBottom = DialogUnitsToPixelsY(hwnd, 6);
                const int tabHeight = tabHeaderHeight + tabContentHeight + tabInsetBottom;
                SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB, innerLeft, cursorY, innerWidth, tabHeight);
                BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB);
                int tabCursorY = cursorY + tabHeaderHeight;
                const int rowLeft = innerLeft + tabInsetX;
                const int rowEditLeft = rowLeft + labelColumnWidth + metrics.labelGap;
                const int rowSliderLeft = rowEditLeft + valueEditWidth + metrics.inlineGap;
                const int rowSliderWidth = std::max(40, innerRight - tabInsetX - rowSliderLeft);
                for (int i = 0; i < 3; ++i) {
                    const ColorChannelControlIds& channel = channelRows[i];
                    const int rowHeight = channelRowHeights[i];
                    const int labelHeight = channelLabelHeights[i];
                    SetDialogControlBounds(
                        hwnd, channel.sliderId, rowSliderLeft, tabCursorY, rowSliderWidth, channelSliderHeights[i]);
                    const auto [gradientLeft, gradientWidth] =
                        SliderTrackHorizontalBounds(hwnd, channel.sliderId, rowSliderLeft, rowSliderWidth);
                    SetDialogControlBounds(hwnd,
                        channel.gradientId,
                        gradientLeft,
                        tabCursorY + channelSliderHeights[i] + gradientGap,
                        gradientWidth,
                        gradientBarHeight);
                    SetDialogControlBounds(hwnd,
                        channel.labelId,
                        rowLeft,
                        tabCursorY + ((channelSliderHeights[i] - labelHeight) / 2),
                        labelColumnWidth,
                        labelHeight);
                    SetDialogControlBounds(hwnd,
                        channel.editId,
                        rowEditLeft,
                        tabCursorY + ((channelSliderHeights[i] - channelEditHeights[i]) / 2),
                        valueEditWidth,
                        channelEditHeights[i]);
                    BringDialogControlToTop(hwnd, channel.labelId);
                    BringDialogControlToTop(hwnd, channel.editId);
                    BringDialogControlToTop(hwnd, channel.sliderId);
                    BringDialogControlToTop(hwnd, channel.gradientId);
                    tabCursorY += rowHeight + metrics.rowGap;
                }
                cursorY += tabHeight + metrics.rowGap;

                const int alphaLabelHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
                    ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL),
                    labelColumnWidth,
                    true);
                const int alphaRowHeight = Max3Int(alphaEditHeight, alphaSliderHeight, alphaLabelHeight);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
                    innerLeft,
                    cursorY,
                    labelColumnWidth,
                    alphaLabelHeight,
                    alphaRowHeight,
                    false);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT,
                    innerLeft + labelColumnWidth + metrics.labelGap,
                    cursorY,
                    valueEditWidth,
                    alphaEditHeight,
                    alphaRowHeight,
                    false);
                SetDialogCenteredRowBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER,
                    sliderLeft,
                    cursorY,
                    sliderWidth,
                    alphaSliderHeight,
                    alphaRowHeight,
                    false);
                cursorY += alphaRowHeight + metrics.rowGap;
            }

            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::Weights: {
            const int editWidth = std::min(88, std::max(60, innerWidth - labelColumnWidth - metrics.labelGap));
            cursorY = LayoutLabeledControlRows(hwnd,
                kWeightEditorRows,
                ARRAYSIZE(kWeightEditorRows),
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                editWidth,
                singleLineFieldHeight,
                metrics.rowGap,
                metrics.hintGap);
            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::Metric: {
            const int controlWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int metricRowHeight =
                std::max(DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE), singleLineFieldHeight);
            cursorY = LayoutLabeledControlRows(hwnd,
                kMetricEditorRows,
                showBinding ? ARRAYSIZE(kMetricEditorRows) : ARRAYSIZE(kMetricEditorRows) - 1,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                metricRowHeight,
                metrics.rowGap,
                showBinding ? metrics.hintGap : metrics.rowGap + metrics.hintGap);
            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::DateTimeFormat: {
            const int controlWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int comboHeight = std::max(
                DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO), singleLineFieldHeight);
            const int rowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL,
                IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                comboHeight);
            cursorY += rowHeight + metrics.hintGap;
            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
        case LayoutEditEditorKind::MetricListOrder: {
            const int buttonWidth = 38;
            const int comboFieldVisibleHeight =
                (std::max)(1, DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT));
            const int addButtonWidth = (std::max)(132,
                MeasureTextWidthForControl(hwnd,
                    IDC_LAYOUT_EDIT_REVERT,
                    state->metricListAddRowButton != nullptr ?
                        ReadWindowTextValue(state->metricListAddRowButton) :
                        std::string("Add row")) +
                    28);
            const int rowVisibleHeight = comboFieldVisibleHeight;
            const int comboDropHeight = std::max(220, rowVisibleHeight + 180);
            const int buttonsWidth = (buttonWidth * 3) + (metrics.inlineGap * 2);
            const int comboWidth = std::max(60, innerWidth - buttonsWidth - metrics.inlineGap);

            for (auto& row : state->metricListRowControls) {
                if (row.combo != nullptr) {
                    SetWindowPos(row.combo, nullptr, innerLeft, cursorY, comboWidth, comboDropHeight, SWP_NOZORDER);
                }
                int buttonLeft = innerLeft + comboWidth + metrics.inlineGap;
                if (row.upButton != nullptr) {
                    SetWindowPos(
                        row.upButton, nullptr, buttonLeft, cursorY, buttonWidth, rowVisibleHeight, SWP_NOZORDER);
                }
                buttonLeft += buttonWidth + metrics.inlineGap;
                if (row.downButton != nullptr) {
                    SetWindowPos(
                        row.downButton, nullptr, buttonLeft, cursorY, buttonWidth, rowVisibleHeight, SWP_NOZORDER);
                }
                buttonLeft += buttonWidth + metrics.inlineGap;
                if (row.deleteButton != nullptr) {
                    SetWindowPos(
                        row.deleteButton, nullptr, buttonLeft, cursorY, buttonWidth, rowVisibleHeight, SWP_NOZORDER);
                }
                cursorY += rowVisibleHeight + metrics.rowGap;
            }

            const int addButtonHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_REVERT);
            const int addButtonLeft = innerRight - addButtonWidth;
            if (state->metricListAddRowButton != nullptr) {
                SetWindowPos(state->metricListAddRowButton,
                    nullptr,
                    addButtonLeft,
                    cursorY,
                    addButtonWidth,
                    addButtonHeight,
                    SWP_NOZORDER);
            }
            cursorY += addButtonHeight + metrics.hintGap;

            contentBottom = LayoutEditorHint(hwnd, innerLeft, cursorY, innerWidth);
            break;
        }
    }

    const int desiredGroupHeight = std::max(60, (contentBottom - groupTop) + metrics.groupPadding);
    const int groupHeight = std::min(maxGroupHeight, desiredGroupHeight);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_EDITOR_GROUP, paneLeft, groupTop, paneWidth, groupHeight);
    if (HWND group = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_EDITOR_GROUP); group != nullptr) {
        SetWindowPos(group, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    switch (kind) {
        case LayoutEditEditorKind::Font:
        case LayoutEditEditorKind::GlobalFontFamily:
            BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
            break;
        case LayoutEditEditorKind::DateTimeFormat:
            BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO);
            break;
        case LayoutEditEditorKind::LayoutSelector:
        case LayoutEditEditorKind::ThemeSelector:
            BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO);
            break;
        case LayoutEditEditorKind::MetricListOrder:
            ShowMetricListOrderEditorControls(state, true);
            for (const auto& row : state->metricListRowControls) {
                if (row.combo != nullptr) {
                    SetWindowPos(row.combo, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            break;
        default:
            break;
    }

    if (kind == LayoutEditEditorKind::Font) {
        if (const auto* parameter = state->selectedLeaf != nullptr ?
                std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey) :
                nullptr;
            parameter != nullptr && state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            const auto font = FindLayoutEditTooltipFontValue(state->dialog->Host().CurrentConfig(), *parameter);
            SetFontSamplePreview(state,
                hwnd,
                std::optional<LayoutEditParameter>(*parameter),
                font.has_value() && *font != nullptr ? *font : nullptr);
        }
    }
}

void UpdateLayoutEditActionState(LayoutEditDialogState* state, HWND hwnd) {
    const bool isFontsSection = state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
        state->selectedNode->kind == LayoutEditTreeNodeKind::Section && state->selectedNode->label == "fonts";
    const bool isThemeSection = state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
        state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
        state->selectedNode->label.rfind("theme.", 0) == 0;
    const bool isLayoutSection = state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
        state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
        state->selectedNode->label.rfind("layout.", 0) == 0;
    const bool canRevert =
        state != nullptr && (state->selectedLeaf != nullptr || isFontsSection || isThemeSection || isLayoutSection);
    SetDialogControlText(hwnd,
        IDC_LAYOUT_EDIT_REVERT,
        isFontsSection ?
            "Revert Font Changes" :
            isThemeSection ?
            "Revert Theme" :
            isLayoutSection ?
            "Revert Layout" :
            "Revert Field");
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_REVERT), canRevert ? TRUE : FALSE);
}

std::string LayoutEditConfiguredSectionDescription(const LayoutEditDialogState* state, const LayoutEditTreeNode* node) {
    if (state == nullptr || node == nullptr || node->kind != LayoutEditTreeNodeKind::Section) {
        return {};
    }
    const AppConfig& config = state->dialog->Host().CurrentConfig();
    if (node->label.rfind("theme.", 0) == 0) {
        for (const ThemeConfig& theme : config.layout.themes) {
            if (theme.name == config.display.theme) {
                return theme.description;
            }
        }
        return "";
    }
    if (node->label.rfind("layout.", 0) == 0) {
        for (const LayoutSectionConfig& layout : config.layout.layouts) {
            if (layout.name == config.display.layout) {
                return layout.description;
            }
        }
        return "";
    }
    return {};
}

void SetLayoutEditDescription(LayoutEditDialogState* state, HWND hwnd, const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        SetDialogControlText(
            hwnd, IDC_LAYOUT_EDIT_TITLE, FindLocalizedText(RES_STR("layout_edit.dialog.no_match_title")));
        SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_LOCATION, "");
        SetDialogControlText(
            hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, FindLocalizedText(RES_STR("layout_edit.dialog.no_match_description")));
        SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_SUMMARY, "");
        SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_HINT, FindLocalizedText(RES_STR("layout_edit.status.select_field")));
        return;
    }

    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_TITLE, BuildLayoutEditNodeTitle(node));
    std::string description = LayoutEditConfiguredSectionDescription(state, node);
    if (description.empty()) {
        description = FindLocalizedText(node->descriptionKey);
    }
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_LOCATION, node->locationText);
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, description);
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_SUMMARY, BuildLayoutEditSummaryText(node));
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_HINT, BuildLayoutEditHintText(node));
}
