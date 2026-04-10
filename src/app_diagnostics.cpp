#include "app_diagnostics.h"
#include "app_monitor.h"
#include "app_paths.h"
#include "app_strings.h"
#include "config_parser.h"
#include "config_writer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

std::vector<std::wstring> GetCommandLineArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return {};
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return arguments;
}

bool HasSwitch(const std::string& target) {
    const std::wstring wideTarget = WideFromUtf8(target);
    for (const std::wstring& argument : GetCommandLineArguments()) {
        if (_wcsicmp(argument.c_str(), wideTarget.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::wstring> GetSwitchValue(const std::wstring& target) {
    const std::vector<std::wstring> arguments = GetCommandLineArguments();
    for (size_t i = 0; i + 1 < arguments.size(); ++i) {
        if (_wcsicmp(arguments[i].c_str(), target.c_str()) == 0) {
            return arguments[i + 1];
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> GetColonSwitchValue(const std::wstring& target) {
    for (const std::wstring& argument : GetCommandLineArguments()) {
        if (argument.size() > target.size() && _wcsnicmp(argument.c_str(), target.c_str(), target.size()) == 0 &&
            argument[target.size()] == L':') {
            return argument.substr(target.size() + 1);
        }
    }
    return std::nullopt;
}

std::optional<double> TryParseScaleValue(const std::wstring& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::string narrow = Utf8FromWide(text);
    std::replace(narrow.begin(), narrow.end(), ',', '.');
    char* end = nullptr;
    const double value = std::strtod(narrow.c_str(), &end);
    if (end == narrow.c_str() || end == nullptr || *end != '\0' || !std::isfinite(value) || value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> GetScaleSwitchValue() {
    if (const auto value = GetColonSwitchValue(L"/scale"); value.has_value()) {
        return TryParseScaleValue(*value);
    }
    return std::nullopt;
}

std::optional<std::string> GetLayoutSwitchValue() {
    if (const auto value = GetColonSwitchValue(L"/layout"); value.has_value()) {
        const std::string layoutName = Trim(Utf8FromWide(*value));
        if (!layoutName.empty()) {
            return layoutName;
        }
    }
    return std::nullopt;
}

DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options) {
    return options.blank ? DashboardRenderer::RenderMode::Blank : DashboardRenderer::RenderMode::Normal;
}

DashboardRenderer::SimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options) {
    switch (options.layoutSimilarityMode) {
        case DiagnosticsLayoutSimilarityMode::HorizontalSizes:
            return DashboardRenderer::SimilarityIndicatorMode::AllHorizontal;
        case DiagnosticsLayoutSimilarityMode::VerticalSizes:
            return DashboardRenderer::SimilarityIndicatorMode::AllVertical;
        case DiagnosticsLayoutSimilarityMode::None:
        default:
            return DashboardRenderer::SimilarityIndicatorMode::ActiveGuide;
    }
}

DiagnosticsOptions GetDiagnosticsOptions() {
    DiagnosticsOptions options;
    options.trace = HasSwitch("/trace");
    options.dump = HasSwitch("/dump");
    options.screenshot = HasSwitch("/screenshot");
    options.exit = HasSwitch("/exit");
    options.fake = HasSwitch("/fake");
    options.blank = HasSwitch("/blank");
    options.editLayout = HasSwitch("/edit-layout");
    options.reload = HasSwitch("/reload");
    options.defaultConfig = HasSwitch("/default-config");
    if (const auto editLayoutValue = GetColonSwitchValue(L"/edit-layout"); editLayoutValue.has_value()) {
        const std::string mode = ToLower(Trim(Utf8FromWide(*editLayoutValue)));
        options.editLayout = true;
        if (mode == "horizontal-sizes" || mode == "horizonatal-sizes") {
            options.layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::HorizontalSizes;
        } else if (mode == "vertical-sizes") {
            options.layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::VerticalSizes;
        } else if (!mode.empty()) {
            options.editLayoutWidgetName = mode;
        }
    }
    if (const auto layoutName = GetLayoutSwitchValue(); layoutName.has_value()) {
        options.layoutName = *layoutName;
    }
    if (const auto scale = GetScaleSwitchValue(); scale.has_value()) {
        options.hasScaleOverride = true;
        options.scale = *scale;
    }
    if (const auto tracePath = GetColonSwitchValue(L"/trace"); tracePath.has_value()) {
        options.trace = true;
        options.tracePath = *tracePath;
    }
    if (const auto dumpPath = GetColonSwitchValue(L"/dump"); dumpPath.has_value()) {
        options.dump = true;
        options.dumpPath = *dumpPath;
    }
    if (const auto screenshotPath = GetColonSwitchValue(L"/screenshot"); screenshotPath.has_value()) {
        options.screenshot = true;
        options.screenshotPath = *screenshotPath;
    }
    if (const auto saveConfigPath = GetColonSwitchValue(L"/save-config"); saveConfigPath.has_value()) {
        options.saveConfig = true;
        options.saveConfigPath = *saveConfigPath;
    } else if (HasSwitch("/save-config")) {
        options.saveConfig = true;
    }
    if (const auto saveFullConfigPath = GetColonSwitchValue(L"/save-full-config"); saveFullConfigPath.has_value()) {
        options.saveFullConfig = true;
        options.saveFullConfigPath = *saveFullConfigPath;
    } else if (HasSwitch("/save-full-config")) {
        options.saveFullConfig = true;
    }
    if (const auto fakePath = GetColonSwitchValue(L"/fake"); fakePath.has_value()) {
        options.fake = true;
        options.fakePath = *fakePath;
    }
    return options;
}

bool ValidateDiagnosticsOptions(const DiagnosticsOptions& options) {
    if (options.blank && options.fake) {
        if (!options.trace) {
            MessageBoxW(nullptr, L"/blank cannot be used together with /fake.", L"System Telemetry", MB_ICONERROR);
        }
        return false;
    }
    return true;
}

bool ApplyDiagnosticsLayoutOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics) {
    if (options.layoutName.empty()) {
        return true;
    }
    if (SelectLayout(config, options.layoutName)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:layout_override name=\"" + options.layoutName + "\"");
        }
        return true;
    }

    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:layout_override_failed name=\"" + options.layoutName + "\"");
        return false;
    }

    const std::wstring message = WideFromUtf8("Unknown layout name:\n" + options.layoutName);
    MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
    return false;
}

void ApplyDiagnosticsScaleOverride(AppConfig& config, const DiagnosticsOptions& options) {
    if (options.hasScaleOverride) {
        config.display.scale = options.scale;
    }
}

double ResolveSavedScreenshotScale(const AppConfig& config) {
    return HasExplicitDisplayScale(config.display.scale) ? config.display.scale : 1.0;
}

DiagnosticsSession::DiagnosticsSession(const DiagnosticsOptions& options) : options_(options) {}

bool DiagnosticsSession::Initialize() {
    const std::filesystem::path workingDirectory = GetWorkingDirectory();
    if (options_.trace) {
        tracePath_ = ResolveDiagnosticsOutputPath(workingDirectory, options_.tracePath, kDefaultTraceFileName);
        traceStream_.open(tracePath_, std::ios::binary | std::ios::app);
        if (!traceStream_.is_open()) {
            ShowFileOpenError("trace file", tracePath_);
            return false;
        }
    }
    if (options_.dump) {
        dumpPath_ = ResolveDiagnosticsOutputPath(workingDirectory, options_.dumpPath, kDefaultDumpFileName);
    }
    if (options_.screenshot) {
        screenshotPath_ =
            ResolveDiagnosticsOutputPath(workingDirectory, options_.screenshotPath, kDefaultScreenshotFileName);
    }
    if (options_.saveConfig) {
        saveConfigPath_ =
            ResolveDiagnosticsOutputPath(workingDirectory, options_.saveConfigPath, kDefaultSavedConfigFileName);
    }
    if (options_.saveFullConfig) {
        saveFullConfigPath_ = ResolveDiagnosticsOutputPath(
            workingDirectory, options_.saveFullConfigPath, kDefaultSavedFullConfigFileName);
    }
    return true;
}

bool DiagnosticsSession::ShouldShowDialogs() const {
    return !options_.trace;
}

std::ostream* DiagnosticsSession::TraceStream() {
    return traceStream_.is_open() ? &traceStream_ : nullptr;
}

void DiagnosticsSession::WriteTraceMarker(const std::string& text) {
    if (!traceStream_.is_open()) {
        return;
    }
    tracing::Trace trace(&traceStream_);
    trace.Write(text);
}

void DiagnosticsSession::ReportError(const std::string& traceText, const std::wstring& message) {
    WriteTraceMarker(traceText);
    if (ShouldShowDialogs()) {
        MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
    }
}

bool DiagnosticsSession::WriteOutputs(const TelemetryDump& dump, const AppConfig& config) {
    if (options_.dump) {
        std::ofstream dumpStream(dumpPath_, std::ios::binary | std::ios::trunc);
        if (!dumpStream.is_open()) {
            ShowFileOpenError("dump file", dumpPath_);
            return false;
        }
        if (!WriteTelemetryDump(dumpStream, dump)) {
            const std::wstring message =
                WideFromUtf8("Failed to write dump file:\n" + Utf8FromWide(dumpPath_.wstring()));
            ReportError("diagnostics:dump_write_failed path=\"" + Utf8FromWide(dumpPath_.wstring()) + "\"", message);
            return false;
        }
    }

    std::string screenshotError;
    if (options_.screenshot && !SaveDumpScreenshot(screenshotPath_,
                                   dump.snapshot,
                                   config,
                                   ResolveSavedScreenshotScale(config),
                                   GetDiagnosticsRenderMode(options_),
                                   options_.editLayout,
                                   GetSimilarityIndicatorMode(options_),
                                   options_.editLayoutWidgetName,
                                   TraceStream(),
                                   &screenshotError)) {
        const std::wstring message =
            WideFromUtf8("Failed to save screenshot:\n" + Utf8FromWide(screenshotPath_.wstring()));
        std::string traceText =
            "diagnostics:screenshot_save_failed path=\"" + Utf8FromWide(screenshotPath_.wstring()) + "\"";
        if (!screenshotError.empty()) {
            traceText += " detail=\"" + screenshotError + "\"";
        }
        ReportError(traceText, message);
        return false;
    }

    if (options_.saveConfig && !SaveConfig(saveConfigPath_, config)) {
        const std::wstring message =
            WideFromUtf8("Failed to save config file:\n" + Utf8FromWide(saveConfigPath_.wstring()));
        ReportError("diagnostics:config_save_failed path=\"" + Utf8FromWide(saveConfigPath_.wstring()) + "\"", message);
        return false;
    }

    if (options_.saveFullConfig && !SaveFullConfig(saveFullConfigPath_, config)) {
        const std::wstring message =
            WideFromUtf8("Failed to save full config file:\n" + Utf8FromWide(saveFullConfigPath_.wstring()));
        ReportError("diagnostics:full_config_save_failed path=\"" + Utf8FromWide(saveFullConfigPath_.wstring()) + "\"",
            message);
        return false;
    }

    return true;
}

void DiagnosticsSession::ShowFileOpenError(const char* label, const std::filesystem::path& path) {
    const std::wstring message =
        WideFromUtf8(std::string("Failed to open ") + label + ":\n" + Utf8FromWide(path.wstring()));
    ReportError("diagnostics:file_open_failed label=\"" + std::string(label) + "\" path=\"" +
                    Utf8FromWide(path.wstring()) + "\"",
        message);
}

std::filesystem::path ResolveDiagnosticsOutputPath(const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredPath,
    const wchar_t* defaultFileName) {
    if (configuredPath.empty()) {
        return workingDirectory / defaultFileName;
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return workingDirectory / configuredPath;
}

std::optional<std::filesystem::path> PromptSavePath(HWND owner,
    const std::filesystem::path& initialDirectory,
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension) {
    wchar_t fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, defaultFileName != nullptr ? defaultFileName : L"", _TRUNCATE);

    std::wstring initialDirectoryText = initialDirectory.wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = ARRAYSIZE(fileBuffer);
    dialog.lpstrInitialDir = initialDirectoryText.empty() ? nullptr : initialDirectoryText.c_str();
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(dialog.lpstrFile);
}

bool CanWriteRuntimeConfig(const std::filesystem::path& path) {
    const std::wstring widePath = path.wstring();
    if (std::filesystem::exists(path)) {
        HANDLE file = CreateFileW(widePath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        CloseHandle(file);
        return true;
    }

    const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    const std::wstring probeName = L".config-write-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::to_wstring(GetTickCount64()) + L".tmp";
    const std::filesystem::path probePath = parent / probeName;
    HANDLE probe = CreateFileW(probePath.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(probe);
    return true;
}

std::filesystem::path CreateTempFilePath(const wchar_t* prefix) {
    wchar_t tempPathBuffer[MAX_PATH];
    const DWORD length = GetTempPathW(ARRAYSIZE(tempPathBuffer), tempPathBuffer);
    if (length == 0 || length >= ARRAYSIZE(tempPathBuffer)) {
        return {};
    }

    wchar_t tempFileBuffer[MAX_PATH];
    if (GetTempFileNameW(tempPathBuffer, prefix, 0, tempFileBuffer) == 0) {
        return {};
    }
    return std::filesystem::path(tempFileBuffer);
}

std::filesystem::path CreateElevatedSaveConfigTempPath() {
    return CreateTempFilePath(L"stc");
}

int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath) {
    if (sourcePath.empty() || targetPath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourcePath);
    if (!SaveConfig(targetPath, config)) {
        return 1;
    }

    std::error_code ignored;
    std::filesystem::remove(sourcePath, ignored);
    return 0;
}

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(
    const AppConfig& runtimeConfig, const DiagnosticsOptions& diagnosticsOptions, std::ostream* traceStream) {
    std::unique_ptr<TelemetryRuntime> runtime = CreateTelemetryRuntime(diagnosticsOptions, GetWorkingDirectory());
    if (runtime == nullptr) {
        return nullptr;
    }
    if (!runtime->Initialize(runtimeConfig, traceStream)) {
        return nullptr;
    }
    return runtime;
}

bool ReloadTelemetryRuntimeFromDisk(const std::filesystem::path& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    DiagnosticsSession* diagnostics) {
    const AppConfig reloadedConfig = LoadConfig(configPath, !diagnosticsOptions.defaultConfig);
    AppConfig effectiveReloadedConfig = reloadedConfig;
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:reload_config_begin");
    }
    if (!ApplyDiagnosticsLayoutOverride(effectiveReloadedConfig, diagnosticsOptions, diagnostics)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    ApplyDiagnosticsScaleOverride(effectiveReloadedConfig, diagnosticsOptions);

    telemetry.reset();
    std::unique_ptr<TelemetryRuntime> reloadedTelemetry = InitializeTelemetryRuntimeInstance(
        effectiveReloadedConfig, diagnosticsOptions, diagnostics != nullptr ? diagnostics->TraceStream() : nullptr);
    if (reloadedTelemetry == nullptr) {
        telemetry = InitializeTelemetryRuntimeInstance(
            activeConfig, diagnosticsOptions, diagnostics != nullptr ? diagnostics->TraceStream() : nullptr);
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }

    activeConfig = effectiveReloadedConfig;
    telemetry = std::move(reloadedTelemetry);
    telemetry->UpdateSnapshot();
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:reload_config_done");
    }
    return true;
}

bool SaveDumpScreenshot(const std::filesystem::path& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    DashboardRenderer::RenderMode renderMode,
    bool showLayoutEditGuides,
    DashboardRenderer::SimilarityIndicatorMode similarityIndicatorMode,
    const std::string& editLayoutWidgetName,
    std::ostream* traceStream,
    std::string* errorText) {
    DashboardRenderer renderer;
    DashboardRenderer::EditOverlayState overlayState;
    overlayState.showLayoutEditGuides = showLayoutEditGuides;
    overlayState.similarityIndicatorMode = similarityIndicatorMode;
    renderer.SetRenderScale(scale);
    renderer.SetConfig(config);
    renderer.SetRenderMode(renderMode);
    renderer.SetTraceOutput(traceStream);
    if (!renderer.Initialize()) {
        if (errorText != nullptr) {
            *errorText = renderer.LastError();
        }
        return false;
    }
    if (!editLayoutWidgetName.empty()) {
        if (!renderer.SetLayoutEditPreviewWidgetType(overlayState, editLayoutWidgetName)) {
            if (errorText != nullptr) {
                *errorText = "renderer:edit_layout_widget_not_found name=\"" + editLayoutWidgetName + "\"";
            }
            return false;
        }
        tracing::Trace(traceStream).Write("diagnostics:edit_layout_widget name=\"" + editLayoutWidgetName + "\"");
    }
    const bool saved = renderer.SaveSnapshotPng(imagePath, snapshot, overlayState);
    if (!saved && errorText != nullptr) {
        *errorText = renderer.LastError();
    }
    return saved;
}

int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions) {
    AppConfig config = LoadRuntimeConfig(diagnosticsOptions);
    DiagnosticsSession diagnostics(diagnosticsOptions);
    if (!diagnostics.Initialize()) {
        return 1;
    }
    if (!ApplyDiagnosticsLayoutOverride(config, diagnosticsOptions, &diagnostics)) {
        return 1;
    }

    diagnostics.WriteTraceMarker(
        "diagnostics:headless_start scale=" + std::to_string(ResolveSavedScreenshotScale(config)));
    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_begin");

    std::unique_ptr<TelemetryRuntime> telemetry =
        InitializeTelemetryRuntimeInstance(config, diagnosticsOptions, diagnostics.TraceStream());
    if (telemetry == nullptr) {
        diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_failed");
        if (diagnostics.ShouldShowDialogs()) {
            MessageBoxW(nullptr, L"Failed to initialize telemetry collector.", L"System Telemetry", MB_ICONERROR);
        }
        return 1;
    }

    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialized");
    Sleep(1000);
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_begin");
    telemetry->UpdateSnapshot();
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_done");
    if (diagnosticsOptions.reload) {
        if (!ReloadTelemetryRuntimeFromDisk(
                GetRuntimeConfigPath(), config, telemetry, diagnosticsOptions, &diagnostics)) {
            return 1;
        }
    }
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_begin");
    if (!diagnostics.WriteOutputs(telemetry->Dump(), config)) {
        diagnostics.WriteTraceMarker("diagnostics:write_outputs_failed");
        return 1;
    }
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_done");
    diagnostics.WriteTraceMarker("diagnostics:headless_done");
    return 0;
}
