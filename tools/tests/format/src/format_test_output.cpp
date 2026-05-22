#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "vendor/library.h"

#include "Alpha/thing.h"
#include "format_test_fixture.h"
#include "zeta/thing.h"

#define FORMAT_FIXTURE_SUM(firstValue, secondValue, thirdValue) \
    ((firstValue) + (secondValue) + (thirdValue) + (firstValue) + (secondValue) + (thirdValue))
#define FORMAT_FIXTURE_SHORT_MACRO(value) (value)
#define FORMAT_FIXTURE_MUCH_LONGER_MACRO(value) (value)
#define FORMAT_FIXTURE_ITEMS(X) \
    X(Alpha, "alpha") \
    X(Beta, "beta") \
    X(Gamma, "gamma")
#define FORMAT_FIXTURE_TEMP_MACRO(value) (value)

#undef FORMAT_FIXTURE_TEMP_MACRO

namespace format_fixture {

class LayoutEditWidgetIdentity {};

namespace std_fixture {

template <typename T>
class vector {};

class string {};

}

class FormattingExample {
public:
    int* pointer;

    int& reference;

    FormattingExample(int* pointerValue, int& referenceValue) : pointer(pointerValue), reference(referenceValue) {}

private:
    int value;
};

class MacroSeparatedMethodHost {
#define FORMAT_FIXTURE_METHOD_MARKER(value) (value)

    void MethodAfterMacro() {}

    int fieldAfterMethod;
};

class DashboardShellHost {
public:
    virtual ~DashboardShellHost() = default;
    virtual std::optional<FilePath> PromptDiagnosticsSavePath(
        std::string_view defaultFileName,
        std::string_view filter,
        std::string_view defaultExtension
    ) const = 0;
    virtual DashboardOverlayState& LayoutDashboardOverlayState() = 0;
};

class DialogRedrawScope {
public:
    DialogRedrawScope(const DialogRedrawScope&) = delete;
    DialogRedrawScope& operator=(const DialogRedrawScope&) = delete;
};

__declspec(noinline) bool DashboardController::FinishConfigMutation(
    DashboardShellHost& shell,
    bool refreshThemedIcons
) {
    return refreshThemedIcons;
}

struct FormatTableRow {
    const char* name;
    int labelControl;
    int editControl;
    int flags;
};

struct FormatBitFields {
    unsigned shortBits : 1;
    unsigned muchLongerBits : 2;
};

// Defaulted operator fixture.
struct ColorMixExpression {
    std::string target;
    double amount = 0.0;

    bool operator==(const ColorMixExpression& other) const = default;
};

void FormatAlphaNibble(char* text, unsigned int alpha) {
    constexpr char kHex[] = "0123456789ABCDEF";
    text[2] = kHex[(alpha >> 4) & 0x0Fu];
    text[3] = kHex[alpha & 0x0Fu];
}

struct OklabColor {
    double l;
    double a;
    double b;
};

OklabColor MixOklab(OklabColor from, OklabColor to, double amount) {
    return OklabColor{
        from.l + (to.l - from.l) * amount,
        from.a + (to.a - from.a) * amount,
        from.b + (to.b - from.b) * amount
    };
}

enum class RuntimeConfigFieldValueKind {
    HexColor,
    Integer
};

enum class ValueFormat : std::uint8_t {
    String,
    Integer,
    FloatingPoint,
    ColorHex,
    FontSpec,
};

struct RuntimeConfigFieldDescriptor {
    RuntimeConfigFieldValueKind kind;
    const char* key;
    int keyLength;
};

bool RuntimeConfigFieldEquals(const RuntimeConfigFieldDescriptor& field, const void* owner, const void* compareOwner);
// Implemented by generated file build/cmake/generated/config/config_meta.generated.cpp.
std::span<const RuntimeConfigSectionDescriptor> RuntimeConfigSectionDescriptors();

struct ColorConfig {};

template <typename UpdateKeyFn>
void SaveBoardSectionDifferences(
    const BoardConfig& board,
    const BoardConfig* compareBoard,
    const std::string& sectionName,
    UpdateKeyFn& updateKey
) {
    DynamicSectionSaveContext<UpdateKeyFn> context{&board, compareBoard, &updateKey};
    updateKey(board, compareBoard, sectionName);
}

using ConfigMetricAvailabilityResolver = bool (*)(std::string_view metricRef);
using RuntimeConfigEnsureDynamicItem = void* (*)(AppConfig& config, std::string_view key);
using DumpValues = std::vector<std::pair<std::string, std::string>>;

using RuntimeConfigForEachDynamicItem = void (*)(
    const AppConfig& config,
    void* context,
    RuntimeConfigDynamicItemVisitor visitor
);

AppConfig LoadConfig(const FilePath& path, bool includeOverlay = true, const ConfigParseContext& context = {});

ColorConfig& MutableColorField(void* owner, const RuntimeConfigFieldDescriptor& field) {
    return *reinterpret_cast<ColorConfig*>(static_cast<char*>(owner) + field.offset);
}

struct NetworkFooterWidgetConfig {
    int bottomGap{};  // config_meta: policy=non_negative_int

    bool operator==(const NetworkFooterWidgetConfig& other) const = default;
};

ColorConfig EmptyColor() {
    return {};
}

std::string_view LayoutNodeFieldEditTitle(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? FindLocalizedText(descriptor->titleKey) : std::string_view{};
}

DashboardApp::DashboardApp(
    const DiagnosticsOptions& diagnosticsOptions,
    bool bringToFrontOnRun
) :
    renderer_(trace_),
    diagnosticsOptions_(diagnosticsOptions),
    layoutEditController_(*this),
    shellUi_(std::make_unique<DashboardShellUi>(*this)),
    bringToFrontOnRun_(bringToFrontOnRun)
{
    renderer_.SetLiveAnimationEnabled(true);
}

HANDLE OpenProbe(FilePath probePath) {
    HANDLE probe = CreateFileA(
        probePath.string().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr
    );
    return probe;
}

HBITMAP CreateBitmap(BITMAPINFOHEADER header) {
    void* bits = nullptr;
    HBITMAP colorBitmap =
        CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    return colorBitmap;
}

double MaxSegmentGap(double totalSweep, double minSegmentSweep, int segmentCount) {
    const double maxSegmentGap = (std::max)(
        0.0,
        (totalSweep - (minSegmentSweep * static_cast<double>(segmentCount))) / static_cast<double>(segmentCount - 1)
    );
    return maxSegmentGap;
}

bool HasIdentifier(std::string identifier) {
    return !identifier.empty();
}

unsigned int PackedRgb(ColorConfig color) {
    return (color.rgba >> 8) & 0xFFFFFFu;
}

void ReturnOnly() {
    return;
}

void BuildSearchContext() {
    struct SearchContext {
        const AppConfig* config = nullptr;
        const std::optional<TargetMonitorInfo>* configuredMonitor = nullptr;
        DisplayMenuOption* options = nullptr;
        size_t capacity = 0;
        size_t count = 0;
        bool hasConfiguredWallpaper = false;
        bool isConfiguredAtOrigin = false;
    } context{&config, &configuredMonitor, options, capacity, 0, hasConfiguredWallpaper, isConfiguredAtOrigin};
    Use(context);
}

void StandaloneLockBlock(LightweightMutex& mutex, TelemetryUpdate update, HWND hwnd) {
    {
        const LightweightMutexLock lock(mutex);
        pendingTelemetryUpdate_ = update;
        hasPendingTelemetryUpdate_ = true;
    }

    if (hwnd != nullptr) {
        PostMessageA(hwnd, kTelemetryUpdateMessage, 0, 0);
    }
}

int UnarySigns(int value) {
    int negative = -value;
    int positive = +value;
    return value - -negative + +positive + (-negative) + (+positive);
}

void DiscardServiceResult(Service& service) {
    (void)StopServiceIfRunning(service.Get());
}

void StructuredBindingLoop(const BoardSelections& resolvedSelections) {
    for (const auto& [logicalName, sensorName] : resolvedSelections.boardFanSensorNames) {
        Use(logicalName, sensorName);
    }
}

double NormalizeAngleCandidate(double angleDegrees) {
    for (double candidate : {angleDegrees - 360.0, angleDegrees + 360.0}) {
        Use(candidate);
    }
    return angleDegrees;
}

const auto noLookup = [](std::string_view) -> std::optional<ColorConfig> { return std::nullopt; };

const auto preserveLambdaSeparator = []() {
    FirstStep();

    SecondStep();
};

const auto findSectionIndex = [&lines](const std::string& sectionName) -> size_t {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (Trim(lines[i]) == sectionName) {
            return i;
        }
    }
    return lines.size();
};

const auto ensureSection = [&lines, &findSectionIndex, shape](const std::string& sectionName) -> size_t {
    const size_t existingIndex = findSectionIndex(sectionName);
    if (existingIndex < lines.size()) {
        return existingIndex;
    }
    return lines.size();
};

const auto ensureSectionAfter = [
    &lines,
    &findSectionIndex,
    shape
](const std::string& sectionName, const std::string& afterSectionName) -> size_t {
    const size_t existingIndex = findSectionIndex(sectionName);
    if (existingIndex < lines.size()) {
        return existingIndex;
    }

    const size_t afterIndex = findSectionIndex(afterSectionName);
    return afterIndex;
};

const auto guideSheetLookup = [
    &config,
    activeTheme,
    &colorsSection
](std::string_view name) -> std::optional<ColorConfig> {
    if (std::optional<ColorConfig> themeColor = FindThemeToken(*activeTheme, name); themeColor.has_value()) {
        return themeColor;
    }
    return FindColorFieldByKey(RuntimeConfigFields(colorsSection), &config.layout.colors, name);
};

constexpr int kPrimaryFlag = 1;
constexpr int kSecondaryFlag = 2;
constexpr int kTertiaryFlag = 4;
constexpr std::string_view kRuntimePlaceholderMetricId = "nothing";

const MetricDefinitionConfig kRuntimePlaceholderMetricDefinition{
    std::string(kRuntimePlaceholderMetricId),
    MetricDisplayStyle::Scalar,
    false,
    1.0,
    "",
    "Nothing",
};

constexpr FormatTableRow kFormatRows[] = {
    {
        "alpha.metric.row.with.extra.detail.and.column.limit.coverage",
        100,
        200,
        kPrimaryFlag | kSecondaryFlag | kTertiaryFlag
    }, {
        "beta.metric.row.with.extra.detail",
        300,
        400,
        kPrimaryFlag | kTertiaryFlag
    }, {
        "gamma.metric.row",
        500,
        600,
        kSecondaryFlag
    }
};

constexpr FormatTableRow kInitializerChainRows[] = {
    {
        "chain.metric.row.with.extra.detail",
        100,
        200,
        firstInitializerFlagWithVeryLongName |
            secondInitializerFlagWithVeryLongName |
            thirdInitializerFlagWithVeryLongName |
            fourthInitializerFlagWithVeryLongName
    }
};

static constexpr OutputPath kOutputPaths[] = {
    {
        &DiagnosticsOptions::trace,
        &DiagnosticsOptions::tracePath,
        &DiagnosticsSession::tracePath_,
        kDefaultTraceFileName
    }, {
        &DiagnosticsOptions::dump,
        &DiagnosticsOptions::dumpPath,
        &DiagnosticsSession::dumpPath_,
        kDefaultDumpFileName
    }, {
        &DiagnosticsOptions::screenshot,
        &DiagnosticsOptions::screenshotPath,
        &DiagnosticsSession::screenshotPath_,
        kDefaultScreenshotFileName
    }
};

void DiagnosticsSession::ResolveOutputPathMember(const OutputPath& outputPath, const FilePath& workingDirectory) {
    this->*outputPath.resolvedPath =
        ResolveDiagnosticsOutputPath(workingDirectory, options_.*outputPath.configuredPath, outputPath.defaultFileName);
}

int kAlignedAssignment = 1;
int kMuchLongerAlignedAssignment = 2;
int kTrailingComment = 1;  // short
int kMuchLongerTrailingComment = 2;  // long

class BenchmarkLikeHost {
    bool ApplyMetricListOrder(
        const LayoutEditWidgetIdentity& widget,
        const std_fixture::vector<std_fixture::string>& metricRefs
    ) override {
        return true;
    }
};

int ShortNonEmpty() {
    return 1;
}

void EmptyFunction() {}

std::string FormatNamedMenuLabel(std::string_view name, std::string_view description) {
    return description.empty() ? std::string(name) :
        FormatText(
            "%.*s - %.*s",
            static_cast<int>(name.size()),
            name.data(),
            static_cast<int>(description.size()),
            description.data()
        );
}

const char* SelectRevertLabel(bool isFontsSection, bool isThemeSection, bool isLayoutSection, bool isMetricsSection) {
    return isFontsSection ? "Revert Font Changes" :
        isThemeSection ? "Revert Theme" :
        isLayoutSection ? "Revert Layout" :
        isMetricsSection ? "Revert Metrics" :
        "Revert Field";
}

std::string SelectCurrentSensorName(const BoardConfig& board, const std::string& logicalName) {
    auto currentIt = board.temperatureSensorNames.find(logicalName);
    const std::string currentValue =
        currentIt != board.temperatureSensorNames.end() && !currentIt->second.empty() ? currentIt->second : logicalName;
    return currentValue;
}

std::optional<double> LayoutEditAnchorValue(const LayoutEditAnchor* anchor) {
    return LayoutEditAnchorParameter(anchor->key).has_value() ?
        std::optional<double>(static_cast<double>(anchor->value)) :
        std::nullopt;
}

size_t SelectConfigSectionStart(const std::string& sectionName) {
    size_t sectionStart =
        sectionName == "[gpu]" ? ensureSectionAfter(sectionName, "[display]") :
        sectionName == "[network]" ? ensureSectionAfter(sectionName, "[gpu]") :
        sectionName == "[storage]" ? ensureSectionAfter(sectionName, "[network]") :
        sectionName == "[board]" ? ensureSectionAfter(sectionName, "[storage]") :
        sectionName == "[metrics]" ? ensureSectionAfter(sectionName, "[board]") :
        ensureSection(sectionName);
    return sectionStart;
}

bool IsNamedColorField(const RuntimeConfigFieldDescriptor& field, std::string_view name) {
    if (
        field.kind == RuntimeConfigFieldValueKind::HexColor &&
        std::string_view(field.key, field.keyLength) == name &&
        field.keyLength > 0
    ) {
        return true;
    }
    return false;
}

bool ConfigureDisplayGuard(
    DisplayState& state,
    DisplayOption option,
    DashboardShellHost& shell,
    UpdatedConfig updatedConfig
) {
    if (!::ConfigureDisplay(
        updatedConfig,
        state.telemetryUpdate.dump,
        option.fittedScale,
        shell.TraceLog(),
        shell.WindowHandle()
    )) {
        return true;
    }
    return false;
}

void AddWidgetAnimation(PresentationAnimation animation, TargetState targetState) {
    WidgetAnimationsForLayer(currentWidgetAnimationLayer_)
        .push_back(DashboardPresentationAnimation{
            std::move(animation),
            std::move(targetState),
            currentWidgetAnimationTranslation_
        });
}

void TraceCaptureChanged(HWND hwnd, LPARAM lParam, bool handled) {
    TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditUi,
        "wm_capturechanged",
        "new_owner=\"%s\" handled=\"%s\"",
        reinterpret_cast<HWND>(lParam) == nullptr ? "none" :
            (reinterpret_cast<HWND>(lParam) == hwnd ? "dashboard" : "other"),
        firstValueWithLongName +
            secondValueWithLongName +
            thirdValueWithLongName +
            fourthValueWithLongName +
            fifthValueWithLongName,
        handled ? "true" : "false"
    );
}

int ManyParameters(
    int* firstPointerWithLongName,
    int& firstReferenceWithLongName,
    int secondValueWithLongName,
    int thirdValueWithLongName,
    int fourthValueWithLongName,
    int fifthValueWithLongName,
    int sixthValueWithLongName
) {
    int localValueWithLongName = firstPointerWithLongName ? *firstPointerWithLongName : 0;  // trailing
    bool combinedValue =
        firstReferenceWithLongName > 0 &&
        secondValueWithLongName > 0 &&
        thirdValueWithLongName > 0 &&
        fourthValueWithLongName > 0 &&
        fifthValueWithLongName > 0 &&
        sixthValueWithLongName > 0;
    if (localValueWithLongName) return firstReferenceWithLongName;
    while (localValueWithLongName < secondValueWithLongName) ++localValueWithLongName;
    for (int index = 0; index < thirdValueWithLongName; ++index) {
        localValueWithLongName += index;
    }
    switch (localValueWithLongName) {
        case 1: {
            int scopedValue = localValueWithLongName + fourthValueWithLongName;
            localValueWithLongName = scopedValue;
            break;
        }
        case 2:
            return fourthValueWithLongName;
        default:
            break;
    }
    return VeryLongFunctionCall(
        firstReferenceWithLongName,
        secondValueWithLongName,
        thirdValueWithLongName,
        fourthValueWithLongName,
        fifthValueWithLongName,
        sixthValueWithLongName,
        localValueWithLongName,
        123456789,
        987654321
    );
}

int NestedSwitchIndent(int message, int wParam) {
    switch (message) {
        case WM_WTSSESSION_CHANGE:
            switch (wParam) {
                case WTS_SESSION_LOCK:
                    return 1;
                default:
                    return 0;
            }
        case WM_ERASEBKGND:
            return 1;
        default:
            return 0;
    }
}

void ControlFlowVariety(int* values, int count) {
    if (count > 0) {
        values[0] += 1;
    } else values[0] = 0;

    if (values != nullptr) {
        values[0] = count;
    }
    while (count > 0) {
        --count;
    }

    for (int outer = 0; outer < count; ++outer) {
        if (values[outer] % 2 == 0) values[outer] += outer;
        else {
            values[outer] -= outer;
        }
    }
    int index = 0;
    for (;;) {
        break;
    }
    while (index < count) {
        values[index] += index;
        ++index;
    }
    do {
        --index;
    } while (index > 0);
}

void LongComment() {
    // This deliberately long comment should remain as one physical line because ReflowComments is false even though it is beyond the configured column limit for the fixture.
}

}
