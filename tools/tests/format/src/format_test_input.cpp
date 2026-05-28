#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <string_view>
#include <algorithm>
#include <vector>
#include <string>

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
#define ENUM_STRING_DECLARE(EnumType, ItemsMacro) \
    enum class EnumType { \
        ItemsMacro(ENUM_STRING_DECLARE_ENUMERATOR), \
    }; \
    template <> \
    struct EnumStringTraits<EnumType> { \
        static constexpr auto names = std::to_array<std::string_view>({ItemsMacro(ENUM_STRING_DECLARE_NAME)}); \
        static_assert(enum_string_detail::ValidateCanonicalNames(names)); \
    }
#define FORMAT_FIXTURE_TEMP_MACRO(value) (value)

#undef FORMAT_FIXTURE_TEMP_MACRO

namespace format_fixture {

class LayoutEditWidgetIdentity {};

namespace std_fixture {

template <typename T>
class vector {};

class string {};

}

constexpr wchar_t kFilterCueText[]=L"Filter settings";

constexpr auto kFixtureSyntaxKindMappings =
    std::to_array<SyntaxKindMapping>({Kind(SyntaxNodeKind::Tree, Bit(TokenClass::Tree)),

Tree(SyntaxNodeKind::TranslationUnit, "translation_unit"), Tree(SyntaxNodeKind::IncludeRun, "include_run"), Tree(
    SyntaxNodeKind::MacroReplacementList,
    "macro_replacement_list"
), Tree(SyntaxNodeKind::Declaration, "declaration", Bit(TokenClass::MacroDeclarationFragment)), Tree(
    SyntaxNodeKind::FieldDeclaration,
    "field_declaration",
    Bit(TokenClass::MacroDeclarationFragment)
)});

constexpr auto kFixtureCommentedSyntaxKindMappings =
    std::to_array<SyntaxKindMapping>({Kind(SyntaxNodeKind::Tree, Bit(TokenClass::Tree)),
// tree nodes
Tree(SyntaxNodeKind::TranslationUnit, "translation_unit"), Tree(SyntaxNodeKind::IncludeRun, "include_run")});

class FormattingExample {
public:
    int* pointer;

    int& reference;
    mutable ID2D1RenderTarget* cachedD2DBitmapTarget_ = nullptr;
    mutable RenderState& cachedRenderState_;

    FormattingExample(int* pointerValue,int& referenceValue):pointer(pointerValue),reference(referenceValue){}

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
    virtual ::Renderer& Renderer() = 0;
    virtual const ::Renderer& Renderer() const = 0;
    virtual DashboardOverlayState& LayoutDashboardOverlayState() = 0;
    virtual void Draw(::Renderer& renderer, const WidgetAnimationState& state) const = 0;
    virtual void ResolveLayoutState(const WidgetHost& renderer, const RenderRect& rect);
    virtual void Draw(WidgetHost& renderer, const struct WidgetLayout& widget, const MetricSource& metrics) const;
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

struct AlignedStorage {
    alignas(void*) unsigned char storage_[sizeof(void*)]{};
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

void IncrementSnapshotVersion(FrameState& frame) {
    frame.versions.snapshotVersion = ++snapshotVersion_;
    frame.versions.previousVersion = --snapshotVersion_;
}

void WriteTraceStringFragments(TraceLog& trace) {
    trace.WriteFmt(
        TracePrefix::Diagnostics,
        RES_STR("layout_guide_sheet stats selected_cards=%zu callouts=%zu"),
        stats.selectedCards,
        stats.callouts
    );
}

void ExpectJoinedHexEscapeFragment() {
    EXPECT_THAT(
        output,
        testing::HasSubstr(
            "gpu.temp = 100,\xC2\xB0"
                "C,Core Temp\r\n"
        )
    );
}

void WriteLongTraceStringFragments(TraceLog& trace, const char* adapterName) {
    trace.WriteFmt(
        TracePrefix::GpuVendor,
        RES_STR(
            "adapter_candidate index=%u vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X "
                "luid=0x%08x:0x%08x pci=%04X:%02X:%02X.%u vendor=%s match_rank=%d dedicated_gb=%.2f "
                "name=\"%s\""
        ),
        adapterIndex,
        adapterName
    );
}

std::string FormatLayoutEditTraceText(const LayoutEditTraceState& state, const char* captureText) {
    std::string trace = FormatText(
        "layout=\"%s\" editing=%s moving=%s modal_depth=%d tooltip_visible=%s tooltip_suppressed=%s "
            "tooltip_rect_valid=%s mouse_tracking=%s drag_active=%s capture=\"%s\"",
        state.config.display.layout.c_str(),
        Trace::BoolText(state.isEditingLayout),
        Trace::BoolText(state.isMoving),
        layoutEditModalUiDepth_,
        Trace::BoolText(dashboardTooltipOwner_ == DashboardTooltipOwner::LayoutEdit && dashboardTooltip_.Visible()),
        Trace::BoolText(layoutEditTooltipRefreshSuppressed_),
        Trace::BoolText(
            dashboardTooltipOwner_ == DashboardTooltipOwner::LayoutEdit && dashboardTooltip_.TargetRectValid()
        ),
        Trace::BoolText(layoutEditMouseTracking_),
        Trace::BoolText(layoutEditController_.HasActiveDrag()),
        captureText
    );
    return trace;
}

void ReportUnknownBenchmark(const std::string& firstArg, std::istream& input) {
    std::cerr
        << "unknown benchmark \"" << firstArg << "\"; supported benchmarks: " << SupportedBenchmarkNames() << "\n";
    input
        >> firstBenchmarkName
        >> secondBenchmarkNameWithLongName
        >> thirdBenchmarkNameWithLongName
        >> fourthBenchmarkNameWithLongName;
    std::cout
        << std::left << std::setw(18) << name
        << " total_ms="
        << std::fixed << std::setprecision(2) << result.total.count()
        << " per_iter_ms="
        << result.perIteration.count()
        << "\n";
}

void ExpectRectNoOverlap(RECT* rects) {
    if (check) {
        if (ready) {
            EXPECT_FALSE(IntersectRect(&intersection, &rects[i], &rects[j])) << "rect " << i << " overlapped rect " << j;
        }
    }
}

void ReserveGroupedRegionCount(LayoutEditActiveRegions& regions) {
    regions.Reserve(
        layoutResolver_->resolvedLayout_.cards.size() * 4 +
            layoutResolver_->layoutEditGuides_.size() +
            containerChildTargetCount +
            layoutResolver_->gapEditAnchors_.size() +
            layoutResolver_->widgetEditGuides_.size() +
            (
                layoutResolver_->staticEditableAnchorRegions_.size() +
                layoutResolver_->dynamicEditableAnchorRegions_.size()
            ) * 2 +
            layoutResolver_->staticColorEditRegions_.size() +
            layoutResolver_->dynamicColorEditRegions_.size()
    );
}

void WriteMetricConfig() {
    const FilePath path = WriteTestConfig(
        "[metrics]\n"
            "nothing = 7,ignored,Overridden Placeholder\n"
            "cpu.load = *,%,Processor Load\n"
    );
}

void WriteInitialConfigText() {
    const std::string initialText = "[display]\r\n"
        "monitor_name = TL160ADMP03-0\r\n"
        "position = 258,117\r\n"
        "scale = 2\r\n"
        "\r\n"
        "[network]\r\n"
        "adapter_name = Wi-Fi\r\n"
        "\r\n"
        "[storage]\r\n"
        "drives = C\r\n";
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

OklchColor NormalizeOklch(
    const double* lightnessOverrideWithLongName,
    const double* chromaOverrideWithLongName,
    const double* hueOverrideWithLongName
) {
    return OklchColor{
        std::clamp(*lightnessOverrideWithLongName, 0.0, 1.0),
        std::max(0.0, *chromaOverrideWithLongName),
        std::clamp(*hueOverrideWithLongName, 0.0, 360.0)
    };
}

enum class RuntimeConfigFieldValueKind {
    HexColor,
    Integer,
};

enum class ValueFormat : std::uint8_t {
    String,
    Integer,
    FloatingPoint,
    ColorHex,
    FontSpec,  // text values
    FontSmall,
    FontFooter,
    FontClockTime,
    FontClockDate,

    // Card style anchors
    CardRadius,
    CardBorder,
};

enum RuntimeMode {
    Default,
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

template <
    typename FirstTemplateParameter,
    typename SecondTemplateParameter,
    typename ThirdTemplateParameter,
    typename FourthTemplateParameter
>
struct LongTemplateParameterHost {};

template <typename Result, typename... Args>
class FunctionRef<Result(Args...)> {
public:
    template <typename Callable>
        requires(
            !std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef> &&
            std::is_invocable_r_v<Result, Callable&&, Args...>
        )
    FunctionRef(Callable&& callable) :
        context_(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
        invoke_([](void* context, Args... args) -> Result {
            return (*static_cast<std::remove_reference_t<Callable>*>(context))(std::forward<Args>(args)...);
        }) {}

    Result operator()(Args... args) const {
        return invoke_(context_, std::forward<Args>(args)...);
    }

private:
    void* context_ = nullptr;
    Result (*invoke_)(void*, Args...) = nullptr;
};

template <typename T> requires(HasValue<T>)
void UseShortRequires(T& value) {
    value.Use();
}

using ConfigMetricAvailabilityResolver = bool (*)(std::string_view metricRef);
using RuntimeConfigDynamicItemVisitor = void (*)(void* context, std::string_view key, const void* item);
using RuntimeConfigEnsureDynamicItem = void* (*)(AppConfig& config, std::string_view key);
using RuntimeConfigFindDynamicItem = const void* (*)(const AppConfig& config, std::string_view key);
using RuntimeConfigForEachDynamicItem =
    void (*)(const AppConfig& config, void* context, RuntimeConfigDynamicItemVisitor visitor);
using ZesDriver = void*;
using ZesInitFn = ZeResult(__cdecl*)(std::uint32_t);
using DumpValues = std::vector<std::pair<std::string, std::string>>;
using LayoutEditParameter = ::LayoutEditParameter;
using TextLayoutResult = ::TextLayoutResult;
using LayoutEditActiveRegionPayload = std::variant<
    LayoutEditCardRegion,
    LayoutEditWidgetRegion,
    LayoutEditGuide,
    LayoutEditContainerChildReorderRegion,
    LayoutEditGapAnchor,
    LayoutEditWidgetGuide,
    LayoutEditAnchorRegion,
    LayoutEditColorRegion
>;

struct LayoutEditAnchorKey {
    LayoutEditWidgetIdentity widget;
    std::variant<
        LayoutEditParameter,
        LayoutMetricEditKey,
        LayoutCardTitleEditKey,
        LayoutNodeFieldEditKey,
        LayoutContainerChildOrderEditKey
    > subject = LayoutEditParameter::MetricListBarHeight;
    std::variant<
        LayoutEditParameter,
        LayoutMetricEditKey,
        LayoutCardTitleEditKey,
        LayoutNodeFieldEditKey,
        LayoutContainerChildOrderEditKey
    > fallbackSubject;
    int anchorId = 0;
};

std::variant<
    LayoutEditParameter,
    LayoutMetricEditKey,
    LayoutCardTitleEditKey,
    LayoutNodeFieldEditKey,
    LayoutContainerChildOrderEditKey
> DefaultLayoutEditSubject();
void UseLayoutEditSubject(
    std::variant<
        LayoutEditParameter,
        LayoutMetricEditKey,
        LayoutCardTitleEditKey,
        LayoutNodeFieldEditKey,
        LayoutContainerChildOrderEditKey
    > subject
);

struct LayoutEditAnchorBinding {
    LayoutEditAnchorKey key;
    int value = 0;
    AnchorShape shape = AnchorShape::Circle;
    std::optional<LayoutEditAnchorDragSpec> drag =
        LayoutEditAnchorDragSpec{AnchorDragAxis::Vertical, AnchorDragMode::AxisDelta, 1.0};
};

AppConfig LoadConfig(const FilePath& path, bool includeOverlay = true, const ConfigParseContext& context = {});

ColorConfig& MutableColorField(void* owner, const RuntimeConfigFieldDescriptor& field) {
    return *reinterpret_cast<ColorConfig*>(static_cast<char*>(owner) + field.offset);
}

void StartLenovoSnapshot(void* contextPtr) {
    std::unique_ptr<LenovoServiceSnapshotThreadContext> context(
        static_cast<LenovoServiceSnapshotThreadContext*>(contextPtr)
    );
}

HRESULT CreateWriteFactory(ComPtr<IDWriteFactory>& dwriteFactory_) {
    return DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf())
    );
}

void GatewayAddress(const Gateway* gateway) {
    const sockaddr* address = gateway->Address.lpSockaddr;
    Use(address);
}

void ClearHistoryKeyIndexes(SystemSnapshot& snapshot) {
    for (uint16_t& encodedIndex : snapshot.retainedHistoryIndexByKey) {
        encodedIndex = 0;
    }
}

void ManagedReferenceSpacing(
    NativeType& nativeRef,
    NativeType&& nativeRvalueRef,
    NativeType*& pointerRef,
    NativeType*&& pointerRvalueRef,
    NativeType** pointerPointer,
    ManagedWidget^ managedHandle,
    ManagedWidget^% managedTrackingRef,
    ManagedWidget% managedReference
) {
    Use(
        nativeRef,
        nativeRvalueRef,
        pointerRef,
        pointerRvalueRef,
        pointerPointer,
        managedHandle,
        managedTrackingRef,
        managedReference
    );
}

void RegisterStaticTextAnchor(
    const RenderRect&,
    const std::string&,
    TextStyleId,
    const TextLayoutOptions&,
    const LayoutEditAnchorBinding&,
    std::optional<LayoutEditParameter>,
    LayoutEditTargetOutline
) override {}

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

void TrackCoveredParameters() {
    std::array<bool, static_cast<size_t>(LayoutEditParameter::Count)> coveredColorParameters{};
    Use(coveredColorParameters);
}

void TrackFeatureLevels() {
    const std::array<D3D_FEATURE_LEVEL, 4> preferredFeatureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    const std::array<D3D_FEATURE_LEVEL, 3>
        fallbackFeatureLevels{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    Use(preferredFeatureLevels, fallbackFeatureLevels);
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

TraceTimingScope::TraceTimingScope(TraceTimingScope&& other) noexcept :
    collector_(std::exchange(other.collector_, nullptr)),
    trace_(std::exchange(other.trace_, nullptr)),
    operation_(std::exchange(other.operation_, nullptr)),
    startedAt_(std::exchange(other.startedAt_, 0)) {}

int CardChromeWidget::PreferredHeight(const WidgetHost&) const {
    return 0;
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

void AttachCalloutBubble(Callout& callout, LayoutGuideSheetExitSide side) {
    callout.bubbleAttachment = RenderPoint{
        side == LayoutGuideSheetExitSide::Left ? callout.bubbleRect.right : callout.bubbleRect.left,
        callout.bubbleRect.Center().y
    };
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
    auto [parameterLine, descriptionLine] = SplitTooltipLines(tooltipText);
    for (const auto& [logicalName, sensorName] : resolvedSelections.boardFanSensorNames) {
        Use(logicalName, sensorName);
    }
}

void SelectFocusedMetric(const FocusKey* focusKey) {
    if (const auto* metricCandidate = std::get_if<LayoutMetricEditKey>(&*focusKey)) {
        metricKey = *metricCandidate;
    }
}

LayoutEditTreeLeaf* FindFocusedLeaf(LayoutEditTreeNode& node, const FocusKey& focusKey) {
    if (node.leaf.has_value() && MatchesLayoutEditFocusKey(node.leaf->focusKey, focusKey)) {
        return &(*node.leaf);
    }
    return nullptr;
}

double NormalizeAngleCandidate(double angleDegrees) {
    for (double candidate : {angleDegrees - 360.0, angleDegrees + 360.0}) {
        Use(candidate);
    }
    return angleDegrees;
}

const auto noLookup = [](std::string_view) -> std::optional<ColorConfig> { return std::nullopt; };

const auto handled = [&result](INT_PTR value) {
    result = value;
    return true;
};

const auto appendTrialLeaders = [&](
    std::vector<TrialLeader>& leaders,
    const std::vector<size_t>& plannedIndexes,
    LayoutGuideSheetExitSide side,
    const LayoutGuideSheetCardPlacement& placement,
    const BlockLayout& block
) {
    Use(leaders, plannedIndexes, side, placement, block);
};

const auto appendEmbeddedRect = [&](
    const RenderRect& rect,
    const std::vector<LayoutEditOverlayOwner>* owners,
    LayoutEditOverlayAffordanceLayer artifactLayer
) {
    if (rect.IsEmpty()) {
        return;
    }
    Use(owners, artifactLayer);
};

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

const auto updateKey = [&lines, &ensureSection, &ensureSectionAfter, &findSectionEnd, shape]
    (const std::string& sectionName, const std::string& key, const std::string& value)
{
    Use(sectionName, key, value);
};

const auto ensureSectionAfter = [&lines, &findSectionIndex, shape]
    (const std::string& sectionName, const std::string& afterSectionName) -> size_t
{
    const size_t existingIndex = findSectionIndex(sectionName);
    if (existingIndex < lines.size()) {
        return existingIndex;
    }

    const size_t afterIndex = findSectionIndex(afterSectionName);
    return afterIndex;
};

const auto guideSheetLookup = [&config, activeTheme, &colorsSection]
    (std::string_view name) -> std::optional<ColorConfig>
{
    if (std::optional<ColorConfig> themeColor = FindThemeToken(*activeTheme, name); themeColor.has_value()) {
        return themeColor;
    }
    return FindColorFieldByKey(RuntimeConfigFields(colorsSection), &config.layout.colors, name);
};

constexpr int kPrimaryFlag = 1;
constexpr int kSecondaryFlag = 2;
constexpr int kTertiaryFlag = 4;
// Shared telemetry update cadence and live animation duration.
inline constexpr auto kTelemetryRefreshInterval = std::chrono::milliseconds(250);
inline constexpr double kTelemetryRefreshIntervalSeconds =
    static_cast<double>(kTelemetryRefreshInterval.count()) / 1000.0;

constexpr std::string_view kRuntimePlaceholderMetricId = "nothing";

const MetricDefinitionConfig kRuntimePlaceholderMetricDefinition{
    std::string(kRuntimePlaceholderMetricId),
    MetricDisplayStyle::Scalar,
    false,
    1.0,
    "",
    "Nothing"
};

constexpr FormatTableRow kFormatRows[] = {
    {
        "alpha.metric.row.with.extra.detail.and.column.limit.coverage",
        100,
        200,
        kPrimaryFlag | kSecondaryFlag | kTertiaryFlag
    },
    {"beta.metric.row.with.extra.detail", 300, 400, kPrimaryFlag | kTertiaryFlag},
    {"gamma.metric.row", 500, 600, kSecondaryFlag}
};

constexpr FormatTableRow kInitializerChainRows[] = {{
    "chain.metric.row.with.extra.detail",
    100,
    200,
    firstInitializerFlagWithVeryLongName |
        secondInitializerFlagWithVeryLongName |
        thirdInitializerFlagWithVeryLongName |
        fourthInitializerFlagWithVeryLongName
}};

static constexpr OutputPath kOutputPaths[] = {
    {
        &DiagnosticsOptions::trace,
        &DiagnosticsOptions::tracePath,
        &DiagnosticsSession::tracePath_,
        kDefaultTraceFileName
    },
    {&DiagnosticsOptions::dump, &DiagnosticsOptions::dumpPath, &DiagnosticsSession::dumpPath_, kDefaultDumpFileName},
    {
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

inline constexpr std::array<ColorDialogControls, 4> kColorDialogControls = {{
    {IDC_LAYOUT_EDIT_COLOR_RED_LABEL, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, IDC_LAYOUT_EDIT_COLOR_RED_SLIDER, "red"},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT, IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER, "green"},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT, IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER, "blue"},
    {IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT, IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER, "alpha"}
}};

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
    return description.empty() ? std::string(name) : FormatText(
        "%.*s - %.*s",
        static_cast<int>(name.size()),
        name.data(),
        static_cast<int>(description.size()),
        description.data()
    );
}

void PreferOuterCallBreakForSingleArgument() {
    UseAVeryLongCallNameThatPushesPastLimit(
        DecorateValueWithLongName(firstValueWithLongName && SingleArgumentCallWithLongName(secondValueWithLongName))
    );
}

void BuildLayoutEditTargetMenuLabel(const LayoutEditTarget* layoutEditTarget) {
    if (layoutEditTarget != nullptr) {
        if (layoutEditTarget->payload.kind == TooltipPayloadKind::LayoutEdit) {
            std::string label;
            const auto focusKey = TooltipPayloadFocusKey(layoutEditTarget->payload);
            if (label.empty() && focusKey.has_value() && std::holds_alternative<LayoutCardTitleEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel("card title");
            } else if (label.empty() && focusKey.has_value() && std::get_if<LayoutNodeFieldEditKey>(&*focusKey) != nullptr) {
                label = BuildLayoutEditMenuLabel("node field");
            } else if (label.empty() && focusKey.has_value() && std::holds_alternative<LayoutContainerEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel("layout container");
            }
        }
    }
}

void BuildTitlebarTooltipControls() {
    const struct {
        DashboardTitlebarTooltipControl control;
        const RECT& rect;
    } controls[] = {
        {DashboardTitlebarTooltipControl::Close, closeRect},
        {DashboardTitlebarTooltipControl::Display, displayRect},
        {DashboardTitlebarTooltipControl::EditLayout, editLayoutRect},
        {DashboardTitlebarTooltipControl::Layout, layoutComboRect},
        {DashboardTitlebarTooltipControl::Theme, themeComboRect},
        {DashboardTitlebarTooltipControl::AppMenu, appMenuRect}
    };
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

const char* LayoutGuideAxisSizingKey(const LayoutGuide* guide) {
    return guide->axis == LayoutGuideAxis::Horizontal ?
        "overview_horizontal_sizing_guide" : "overview_vertical_sizing_guide";
}

std::optional<double> LayoutEditAnchorValue(const LayoutEditAnchor* anchor) {
    return LayoutEditAnchorParameter(anchor->key).has_value() ?
        std::optional<double>(static_cast<double>(anchor->value)) : std::nullopt;
}

size_t SelectConfigSectionStart(const std::string& sectionName) {
    size_t sectionStart = sectionName == "[gpu]" ? ensureSectionAfter(sectionName, "[display]") :
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

void EnumerateUninstallChildren(HKEY uninstallKey) {
    while (
        RegEnumKeyExA(uninstallKey, index, childName, &childNameLength, nullptr, nullptr, nullptr, nullptr) ==
            ERROR_SUCCESS
    ) {
        ++index;
    }
}

bool HasMissingReflectionMembers(const ReflectionContext* context) {
    if (
        context->initializeMethod == nullptr ||
        context->getCurrentMethod == nullptr ||
        context->titleProperty == nullptr ||
        context->valueProperty == nullptr
    ) {
        return true;
    }
    return false;
}

void CollectLayoutEditHighlights(const DashboardOverlayState& overlayState, const LayoutEditAnchorRegion& region) {
    const auto* special = std::get_if<LayoutEditSelectionHighlightSpecial>(&*overlayState.selectedTreeHighlight);
    if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) || (
        special != nullptr &&
        *special == LayoutEditSelectionHighlightSpecial::AllTexts &&
        LayoutEditAnchorParameter(region.key).has_value() &&
        IsFontEditParameter(*LayoutEditAnchorParameter(region.key))
    )) {
        appendHighlight(region, true);
    }
}

void SplitOperatorChainPartsLineByLine() {
    const int metricValue = firstValue + builder.WithSource(sourceValue).BuildMetricValue(
        firstArgumentWithLongName,
        secondArgumentWithLongName,
        thirdArgumentWithLongName,
        fourthArgumentWithLongName
    );
    const int chainedMetricValue = firstValue +
        builder.WithSource(sourceValue).BuildMetricValue(
            firstArgumentWithLongName,
            secondArgumentWithLongName,
            thirdArgumentWithLongName,
            fourthArgumentWithLongName
        ) +
        finalValueWithLongName;
    const bool found = currentKey == layoutLookupTable[ComputeLayoutKeyIndex(
        firstKeyPartWithLongName,
        secondKeyPartWithLongName,
        thirdKeyPartWithLongName,
        fourthKeyPartWithLongName,
        fifthKeyPartWithLongName
    )];
    const RenderRect bounds = baseBounds | RenderRect{
        leftValueWithLongName,
        topValueWithLongName,
        rightValueWithLongName,
        bottomValueWithLongName,
        extraValueWithLongName,
        finalValueWithLongName
    };
    const bool loaded = !LoadString(values, DumpKey(historyPrefix, ".series_ref"), history.seriesRef, error) ||
        !LoadDoubleArrayField(values, DumpKey(historyPrefix, ".samples"), history.samples, error) ||
        !LoadDoubleArrayField(
            values,
            DumpKey(historyPrefix, ".throughput_live_samples"),
            history.throughputLiveSamples,
            error
        ) ||
        !LoadDouble(values, DumpKey(historyPrefix, ".throughput_bucket_total"), history.throughputBucketTotal, error) ||
        !LoadUnsigned(
            values,
            DumpKey(historyPrefix, ".throughput_bucket_sample_count"),
            history.throughputBucketSampleCount,
            error
        );
}

void UniversalBreakSelectionCases() {
    const int singleBinaryValue=firstValue+BuildValue(firstArgumentWithLongName,secondArgumentWithLongName,thirdArgumentWithLongName,fourthArgumentWithLongName);
    const int sameOperatorChainValue = firstValue +
        BuildValue(
            firstArgumentWithLongName,
            secondArgumentWithLongName,
            thirdArgumentWithLongName,
            fourthArgumentWithLongName
        ) +
        finalValueWithLongName;
    const int nestedTieValue = OuterValue(
        FirstValue(firstArgumentWithLongName, secondArgumentWithLongName),
        SecondValue(thirdArgumentWithLongName, fourthArgumentWithLongName),
        finalArgumentWithLongName
    );
    const int singleTernaryValue = conditionWithLongName ?
        BuildValue(firstArgumentWithLongName, secondArgumentWithLongName, thirdArgumentWithLongName) :
        fallbackValueWithLongName;
    const int ternaryChainValue = firstConditionWithLongName ? firstValueWithLongName :
        secondConditionWithLongName ? BuildValue(
            firstArgumentWithLongName,
            secondArgumentWithLongName,
            thirdArgumentWithLongName
        ) :
        fallbackValueWithLongName;
    UseTemplate<
        FirstTemplateArgumentWithLongName,
        SecondTemplateArgumentWithLongName,
        ThirdTemplateArgumentWithLongName
    >();
    AppConfig config = extraTemplate.empty() ? LoadConfig(
        GetRuntimeConfigPath(),
        !options.defaultConfig,
        context
    ) : LoadConfigWithExtraTemplate(GetRuntimeConfigPath(), !options.defaultConfig, context, extraTemplate);
}

ColorMixParseResult ParseColorMixParts(const std::vector<std::string>& parts) {
    ColorMixParseResult parsed{};
    if (parts.size() != 2) {
        return std::nullopt;
    } const double amount=ParseDoubleOrDefault(parts[0],std::numeric_limits<double>::quiet_NaN());

    if (!std::isfinite(amount)||amount<0.0||amount>1.0) {
        return std::nullopt;
    } parsed.mix=ColorMixExpression{parts[1],amount};
    return parsed;
}

bool EqualStringVectors(const void* address, const void* compareAddress) {
    return *reinterpret_cast<const std::vector<std::string>*>(address)==*reinterpret_cast<const std::vector<std::string>*>(compareAddress);
}

void FormatterSelfBreakCases() {
    if (
        tokens[statementStart].text == "for" ||
        tokens[statementStart].text == "while" ||
        tokens[statementStart].text == "switch"
    ) {
        return;
    }
    if (next < tokens.size() && tokens[next].kind == TokenKind::Word && (
        tokens[next].text == "else" ||
        tokens[next].text == "catch" ||
        tokens[next].text == "finally" ||
        (tokens[next].text == "while" && closedBlock.kind == BlockKind::DoStatement)
    )) {
        return;
    }
    if (
        pendingTokens_.empty() &&
        pendingPrefix_.empty() &&
        IsTrailingCommentAfterEmittedClose(tokens, index) &&
        !outputLines_.empty()
    ) {
        return;
    }
}

void ApplyLayoutEditColorExpression(AppConfig& config, const LayoutEditParameter* parameter) {
    if (parameter == nullptr) {
        return;
    } else if (const auto currentColor = FindLayoutEditParameterColorConfigValue(config, *parameter); currentColor.has_value() && *currentColor != nullptr) {
        colorExpressionValue = TooltipColorExpression(**currentColor);
    }
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

int MeasureHexLabelWidth(HWND hwnd) {
    const int hexLabelWidth = MeasureTextWidthForControl(
        hwnd,
        IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
        ReadDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL)
    ) + 8;
    return hexLabelWidth;
}

int MeasureTextBlockRight(const RenderRect& measureRect, const std::wstring& wideText, const TextStyle& style) {
    const int width = std::max(
        0,
        static_cast<int>(
            MeasureTextBlockD2D(
                measureRect,
                wideText,
                style,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
                nullptr
            ).textRect.right
        )
    );
    return width;
}

size_t CountLeftCards(
    const std::vector<int>& cardPlanned,
    const std::vector<Callout>& plannedCalloutDetails,
    const std::vector<CardPlacement>& cardPlacements,
    size_t cardIndex
) {
    const size_t leftCount = cardPlanned.size() == 1 ? (
        plannedCalloutDetails[cardPlanned.front()].target.Center().x < cardPlacements[cardIndex].sourceRect.Center().x ?
            1 : 0
    ) : cardPlanned.size() / 2;
    return leftCount;
}

RenderRect BuildGuideSheetTargetRect(
    const PlannedCallout& planned,
    const std::vector<CardPlacement>& cardPlacements,
    double dx,
    double dy
) {
    const RenderRect targetRect = cardPlacements[planned.cardIndex].overview ? TransformRect(
        planned.target,
        cardPlacements[planned.cardIndex].sourceRect,
        cardPlacements[planned.cardIndex].destRect
    ) : OffsetRenderRect(planned.target, dx, dy);
    return targetRect;
}

double ComputeBackgroundWeight(const Geometry& geometry, double sampleX, double sampleY, double denom) {
    const double backgroundWeight = (
        (geometry.topY - geometry.bottomY) * (sampleX - geometry.bottomX) +
        (geometry.bottomX - geometry.rightX) * (sampleY - geometry.bottomY)
    ) / denom;
    return backgroundWeight;
}

double SampleSupersampledX(int x, int sx, int iconSize) {
    const double sampleX = (
        static_cast<double>(x) + (static_cast<double>(sx) + 0.5) / kSupersample
    ) * 256.0 / static_cast<double>(iconSize);
    return sampleX;
}

void BuildTrianglePoints(const RECT& rect, const Geometry& geometry) {
    POINT points[] = {
        {
            rect.left + static_cast<LONG>(std::lround(geometry.leftX)),
            rect.top + static_cast<LONG>(std::lround(geometry.topY))
        }, {
            rect.left + static_cast<LONG>(std::lround(geometry.rightX)),
            rect.top + static_cast<LONG>(std::lround(geometry.topY))
        }, {
            rect.left + static_cast<LONG>(std::lround(geometry.bottomX)),
            rect.top + static_cast<LONG>(std::lround(geometry.bottomY))
        }
    };
    Use(points);
}

void AddWidgetAnimation(PresentationAnimation animation, TargetState targetState) {
    WidgetAnimationsForLayer(currentWidgetAnimationLayer_).push_back(
        DashboardPresentationAnimation{std::move(animation), std::move(targetState), currentWidgetAnimationTranslation_}
    );
}

void AddMetricDefinition(MetricsConfig& metrics) {
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.load", MetricDisplayStyle::Percent, true, 0.0, "%", "Load"}
    );
}

int BracedReceiverChain(
    int firstCoordinateWithLongName,
    int secondCoordinateWithLongName,
    int thirdCoordinateWithLongName,
    int y,
    int deltaX,
    int deltaY
) {
    return RenderPoint{
        firstCoordinateWithLongName + secondCoordinateWithLongName + thirdCoordinateWithLongName,
        y
    }.OffsetBy(deltaX, deltaY).x;
}

void DrawGuideDot(RenderHost& renderer, int x, int y, int dotLength, int right, int strokeWidth) {
    renderer.Renderer().FillSolidRect(
        RenderRect{x, y, std::min(x + dotLength, right), y + strokeWidth},
        RenderColorId::LayoutGuide
    );
}

void DrawGuideDotFromAdapter(
    RenderHost& renderer,
    RenderState& state,
    int x,
    int y,
    int dotLength,
    int right,
    int strokeWidth
) {
    RenderHostAdapter{renderer, state}.Renderer().FillSolidRect(
        RenderRect{x, y, std::min(x + dotLength, right), y + strokeWidth},
        RenderColorId::LayoutGuide
    );
}

void RegisterStaticEditAnchor(
    RenderHost& renderer,
    Widget& widget,
    const RenderRect& barRect,
    const RenderRect& anchorRect,
    const Config& config,
    int rowIndex,
    int anchorCenterX,
    int anchorCenterY
) {
    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{
            LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::MetricListBarHeight,
            rowIndex
        },
        .targetRect = barRect,
        .anchorRect = anchorRect,
        .shape = AnchorShape::Circle,
        .value = config.barHeight,
        .drag = LayoutEditAnchorDrag::AxisDelta(AnchorDragAxis::Horizontal, RenderPoint{anchorCenterX, anchorCenterY})
    });
}

void AddThemeColorLeaf(
    Theme* theme,
    std::string token,
    LayoutEditTreeNode& leafNode,
    const LayoutEditTreeNode& sectionNode
) {
    leafNode.leaf.emplace(LayoutEditTreeLeaf{
        ThemeColorEditKey{theme->name, token},
        sectionNode.label,
        token,
        leafNode.descriptionKey,
        configschema::ValueFormat::ColorHex
    });
}

void AssignCompactBracedConstructor(LayoutEditGapAnchor& outerMarginAnchor) {
    outerMarginAnchor.key.widget =
        LayoutEditWidgetIdentity{"", "", {}, LayoutEditWidgetIdentity::Kind::DashboardChrome};
}

void PlaceEmptyLambdaCallout() {
    const LayoutGuideSheetPlacementResult result = PlaceLayoutGuideSheetCallouts(
        cardPlacements,
        callouts,
        LayoutGuideSheetPlacementStyle{10, 12, 4, 20, 0, 1},
        [](LayoutGuideSheetPlacementCallout&, int) {},
        nullptr
    );
}

void SnapGaugeWidth() {
    const bool snapped = layout_snap_solver::FindNearestSnapWeight(
        kCurrentGaugeWeight,
        kCombinedWeight,
        kThreshold,
        {layout_snap_solver::SnapCandidate{targetExtent, targetExtent - startExtent, 0}},
        [](int firstWeight, int& extent) -> bool {
            extent = ComputeCpuGaugeWidth(firstWeight);
            return true;
        },
        snappedWeight
    );
}

int CountPlusAnchors(RenderHost& renderer) {
    return static_cast<int>(std::count_if(
        renderer.editArtifacts.staticAnchors.begin(),
        renderer.editArtifacts.staticAnchors.end(),
        [](const LayoutEditAnchorRegion& region) { return region.shape == AnchorShape::Plus; }
    ));
}

void TraceCaptureChanged(HWND hwnd, LPARAM lParam, bool handled) {
    TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditUi,
        "wm_capturechanged",
        "new_owner=\"%s\" handled=\"%s\"",
        reinterpret_cast<HWND>(lParam) == nullptr ?
            "none" : (reinterpret_cast<HWND>(lParam) == hwnd ? "dashboard" : "other"),
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
    bool combinedValue = firstReferenceWithLongName > 0 &&
        secondValueWithLongName > 0 &&
        thirdValueWithLongName > 0 &&
        fourthValueWithLongName > 0 &&
        fifthValueWithLongName > 0 &&
        sixthValueWithLongName > 0;
    if (localValueWithLongName) {
        return firstReferenceWithLongName;
    }
    while (localValueWithLongName < secondValueWithLongName) {
        ++localValueWithLongName;
    }
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

void LongForCondition() {
    for (
        int rowIndex = 0;
        rowIndex < layoutState_.visibleRows &&
            rowIndex < static_cast<int>(layoutState_.rowBarRects.size()) &&
            rowIndex < static_cast<int>(layoutState_.rowBarAnchorRects.size());
        ++rowIndex
    ) {
        Use(rowIndex);
    }
}

void ControlFlowVariety(int* values, int count) {
    if (count > 0) {
        values[0] += 1;
    } else {
        values[0] = 0;
    }
    if (count == 0) {
        values[0] = 0;
    } else if (count == 1) {
        values[0] = 1;
    } else if (count == 2) {
        values[0] = 2;
    }

    if (values != nullptr) {
        values[0] = count;
    }
    while (count > 0) {
        --count;
    }

    for (int outer = 0; outer < count; ++outer) {
        if (values[outer] % 2 == 0) {
            values[outer] += outer;
        } else {
            values[outer] -= outer;
        }
    }
    for (int simple = 0; simple < count; ++simple) {
        values[simple] += 1;
    }
    switch (count) {
        default:
            values[0] = count;
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
        ++index;
    } while (index < count);
    do {
        --index;
    } while (index > 0);
}

void TryFinallyCleanup() {
    const auto originalDirectory = Environment::CurrentDirectory;
    try {
        RunWithTemporaryDirectory();
    } finally {
        Environment::CurrentDirectory = originalDirectory;
    }
}

namespace trailing_comment_fixture {

void UseNamespaceTrailingComment() {}

}  // namespace trailing_comment_fixture

void LongComment() {
    // This deliberately long comment should remain as one physical line because ReflowComments is false even though it is beyond the configured column limit for the fixture.
}

int SplitDivisionOperator() {
    return firstReallyLongNumeratorValueForOrdinaryDivisionOperatorSplit / secondReallyLongDivisorValueForOrdinaryDivisionOperatorSplit;
}

int SplitRemainderOperator() {
    return firstReallyLongDividendValueForOrdinaryRemainderOperatorSplit % secondReallyLongDivisorValueForOrdinaryRemainderOperatorSplit;
}

int SplitSubtractionOperator() {
    return firstReallyLongMinuendValueForOrdinarySubtractionOperatorSplit - secondReallyLongSubtrahendValueForOrdinarySubtractionOperatorSplit;
}

class BaseClassSpacingRoot {};

class BaseClassSpacingPublic : public BaseClassSpacingRoot {};

class BaseClassSpacingPrivate : private BaseClassSpacingRoot {};

class BaseClassSpacingProtected : protected BaseClassSpacingRoot {};

bool AttachedOpenChainKeepsFollowingOperator(const PrintToken* previous, KnownToken prev, const PrintToken& current) {
    if (
        previous->kind == PrintTokenKind::Known && (
            prev == KnownToken::RightParen ||
            prev == KnownToken::RightBracket ||
            prev == KnownToken::RightBrace ||
            prev == KnownToken::Greater
        ) &&
        IsWordLike(current)
    ) {
        return true;
    }
    return false;
}

struct BracedMacroInitializerStressEntry {};

enum class BracedMacroInitializerStressKind {
    Value
};

constexpr BracedMacroInitializerStressEntry BracedMacroInitializerStressValues[] = {
    BRACED_MACRO_INITIALIZER_STRESS("field1", BracedMacroInitializerStressKind::Value, source.path1),
    BRACED_MACRO_INITIALIZER_STRESS("field2", BracedMacroInitializerStressKind::Value, source.path2),
    BRACED_MACRO_INITIALIZER_STRESS("field3", BracedMacroInitializerStressKind::Value, source.path3),
    BRACED_MACRO_INITIALIZER_STRESS("field4", BracedMacroInitializerStressKind::Value, source.path4),
    BRACED_MACRO_INITIALIZER_STRESS("field5", BracedMacroInitializerStressKind::Value, source.path5),
    BRACED_MACRO_INITIALIZER_STRESS("field6", BracedMacroInitializerStressKind::Value, source.path6),
    BRACED_MACRO_INITIALIZER_STRESS("field7", BracedMacroInitializerStressKind::Value, source.path7),
    BRACED_MACRO_INITIALIZER_STRESS("field8", BracedMacroInitializerStressKind::Value, source.path8),
    BRACED_MACRO_INITIALIZER_STRESS("field9", BracedMacroInitializerStressKind::Value, source.path9),
    BRACED_MACRO_INITIALIZER_STRESS("field10", BracedMacroInitializerStressKind::Value, source.path10),
    BRACED_MACRO_INITIALIZER_STRESS("field11", BracedMacroInitializerStressKind::Value, source.path11),
    BRACED_MACRO_INITIALIZER_STRESS("field12", BracedMacroInitializerStressKind::Value, source.path12),
    BRACED_MACRO_INITIALIZER_STRESS("field13", BracedMacroInitializerStressKind::Value, source.path13),
    BRACED_MACRO_INITIALIZER_STRESS("field14", BracedMacroInitializerStressKind::Value, source.path14),
    BRACED_MACRO_INITIALIZER_STRESS("field15", BracedMacroInitializerStressKind::Value, source.path15),
    BRACED_MACRO_INITIALIZER_STRESS("field16", BracedMacroInitializerStressKind::Value, source.path16),
    BRACED_MACRO_INITIALIZER_STRESS("field17", BracedMacroInitializerStressKind::Value, source.path17),
    BRACED_MACRO_INITIALIZER_STRESS("field18", BracedMacroInitializerStressKind::Value, source.path18)
};

int DeepDelimiterStressCase(int y) {
    int x = ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((y))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
    return x;
}

}
