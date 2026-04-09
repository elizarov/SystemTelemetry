#include "app_shared.h"

#include <cmath>
#include <cstdio>

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

SIZE MeasureTextSize(HDC hdc, HFONT font, const std::string& text) {
    SIZE size{};
    const std::wstring wide = WideFromUtf8(text);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    if (!wide.empty()) {
        GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), &size);
    }
    SelectObject(hdc, oldFont);
    return size;
}

int MeasureFontHeight(HDC hdc, HFONT font) {
    TEXTMETRICW metrics{};
    HGDIOBJ oldFont = SelectObject(hdc, font);
    GetTextMetricsW(hdc, &metrics);
    SelectObject(hdc, oldFont);
    return static_cast<int>(metrics.tmHeight);
}

int MeasureWrappedTextHeight(HDC hdc, HFONT font, const std::string& text, int width) {
    RECT rect{0, 0, std::max(1, width), 0};
    const std::wstring wide = WideFromUtf8(text);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    DrawTextW(hdc, wide.c_str(), -1, &rect, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hdc, oldFont);
    return rect.bottom - rect.top;
}

bool ContainsInsensitive(const std::string& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::string::npos;
}

bool EqualsInsensitive(const std::string& left, const std::string& right) {
    return ToLower(left) == ToLower(right);
}

bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (::towlower(left[i]) != ::towlower(right[i])) {
            return false;
        }
    }
    return true;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            joined += ",";
        }
        joined += names[i];
    }
    return joined;
}

bool RectsEqual(const RECT& lhs, const RECT& rhs) {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

void WriteOptionalTrace(std::ostream* traceStream, const std::string& text) {
    if (traceStream == nullptr) {
        return;
    }
    tracing::Trace trace(traceStream);
    trace.Write(text);
}

std::string FormatHresult(HRESULT value) {
    char buffer[32];
    sprintf_s(buffer, "0x%08lX", static_cast<unsigned long>(value));
    return buffer;
}

std::filesystem::path CaptureLaunchWorkingDirectory() {
    try {
        return std::filesystem::current_path();
    } catch (...) {
        return {};
    }
}

std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(modulePath).parent_path();
}

std::filesystem::path GetWorkingDirectory() {
    static const std::filesystem::path workingDirectory = CaptureLaunchWorkingDirectory();
    if (!workingDirectory.empty()) {
        return workingDirectory;
    }
    try {
        return std::filesystem::current_path();
    } catch (...) {
        return {};
    }
}

std::filesystem::path ResolveExecutableRelativePath(const std::filesystem::path& configuredPath) {
    if (configuredPath.empty()) {
        return {};
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return GetExecutableDirectory() / configuredPath;
}

std::optional<std::wstring> GetExecutablePath() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::nullopt;
    }
    return std::wstring(modulePath, length);
}

std::wstring TrimWhitespace(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return iswspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::wstring(first, last);
}

std::wstring StripOuterQuotes(std::wstring value) {
    value = TrimWhitespace(std::move(value));
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::wstring NormalizeWindowsPath(std::wstring value) {
    value = StripOuterQuotes(std::move(value));
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::optional<std::wstring> ReadAutoStartCommand() {
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        kAutoStartRunSubKey,
        0,
        KEY_QUERY_VALUE,
        &key);
    if (openStatus != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS queryStatus = RegQueryValueExW(key, kAutoStartValueName, nullptr, &type, nullptr, &size);
    if (queryStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    const LSTATUS readStatus = RegQueryValueExW(
        key,
        kAutoStartValueName,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(value.data()),
        &size);
    RegCloseKey(key);
    if (readStatus != ERROR_SUCCESS || value.empty()) {
        return std::nullopt;
    }

    const size_t terminator = value.find(L'\0');
    if (terminator != std::wstring::npos) {
        value.resize(terminator);
    }
    return value;
}

bool IsAutoStartEnabledForCurrentExecutable() {
    const auto executablePath = GetExecutablePath();
    const auto registeredCommand = ReadAutoStartCommand();
    if (!executablePath.has_value() || !registeredCommand.has_value()) {
        return false;
    }
    return NormalizeWindowsPath(*registeredCommand) == NormalizeWindowsPath(*executablePath);
}

LSTATUS WriteAutoStartRegistryValue(bool enabled) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    const LSTATUS createStatus = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        kAutoStartRunSubKey,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        &disposition);
    if (createStatus != ERROR_SUCCESS) {
        return createStatus;
    }

    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const auto executablePath = GetExecutablePath();
        if (!executablePath.has_value()) {
            RegCloseKey(key);
            return ERROR_FILE_NOT_FOUND;
        }
        const std::wstring command = QuoteCommandLineArgument(*executablePath);
        result = RegSetValueExW(
            key,
            kAutoStartValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kAutoStartValueName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    return result;
}

int RunElevatedAutoStartMode(bool enabled) {
    const LSTATUS status = WriteAutoStartRegistryValue(enabled);
    return status == ERROR_SUCCESS ? 0 : 1;
}

bool UpdateAutoStartElevated(bool enabled, HWND owner) {
    const auto executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        return false;
    }

    std::wstring parameters = enabled ? L"/set-autostart on" : L"/set-autostart off";
    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = executablePath->c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);
    return exitCode == 0;
}

bool UpdateAutoStartRegistration(bool enabled, HWND owner) {
    const LSTATUS status = WriteAutoStartRegistryValue(enabled);
    if (status == ERROR_SUCCESS) {
        return true;
    }
    if (status == ERROR_ACCESS_DENIED) {
        return UpdateAutoStartElevated(enabled, owner);
    }
    return false;
}

std::filesystem::path ResolveDiagnosticsOutputPath(
    const std::filesystem::path& workingDirectory,
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

std::optional<std::filesystem::path> PromptSavePath(
    HWND owner,
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

std::string FormatSpeed(double mbps) {
    char buffer[64];
    if (mbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", mbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", mbps);
    }
    return buffer;
}

double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory) {
    double rawMax = 10.0;
    for (double value : firstHistory) {
        rawMax = std::max(rawMax, value);
    }
    for (double value : secondHistory) {
        rawMax = std::max(rawMax, value);
    }
    return std::max(10.0, std::ceil(rawMax / 5.0) * 5.0);
}

using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

UINT GetMonitorDpi(HMONITOR monitor) {
    if (monitor == nullptr) {
        return kDefaultDpi;
    }

    static GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
        HMODULE module = LoadLibraryW(L"Shcore.dll");
        if (module == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(module, "GetDpiForMonitor"));
    }();

    if (getDpiForMonitor != nullptr) {
        UINT dpiX = kDefaultDpi;
        UINT dpiY = kDefaultDpi;
        if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
            return dpiX;
        }
    }
    return kDefaultDpi;
}

double ScaleFromDpi(UINT dpi) {
    return static_cast<double>(std::max(kDefaultDpi, dpi)) / static_cast<double>(kDefaultDpi);
}

int ScaleLogicalToPhysical(int logicalValue, UINT dpi) {
    if (logicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(logicalValue) * ScaleFromDpi(dpi)));
}

int ScalePhysicalToLogical(int physicalValue, UINT dpi) {
    if (physicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(physicalValue) / ScaleFromDpi(dpi)));
}

SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi) {
    return SIZE{
        ScaleLogicalToPhysical(config.layout.structure.window.width, dpi),
        ScaleLogicalToPhysical(config.layout.structure.window.height, dpi)
    };
}

LayoutNodeConfig* FindLayoutNodeByPath(LayoutNodeConfig& root, const std::vector<size_t>& path) {
    LayoutNodeConfig* node = &root;
    for (size_t index : path) {
        if (index >= node->children.size()) {
            return nullptr;
        }
        node = &node->children[index];
    }
    return node;
}

const LayoutNodeConfig* FindLayoutNodeByPath(const LayoutNodeConfig& root, const std::vector<size_t>& path) {
    const LayoutNodeConfig* node = &root;
    for (size_t index : path) {
        if (index >= node->children.size()) {
            return nullptr;
        }
        node = &node->children[index];
    }
    return node;
}

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditHost::LayoutTarget& target) {
    if (target.editCardId.empty()) {
        return FindLayoutNodeByPath(config.layout.structure.cardsLayout, target.nodePath);
    }
    const auto cardIt = std::find_if(config.layout.cards.begin(), config.layout.cards.end(), [&](const auto& card) {
        return card.id == target.editCardId;
    });
    if (cardIt == config.layout.cards.end()) {
        return nullptr;
    }
    return FindLayoutNodeByPath(cardIt->layout, target.nodePath);
}

std::vector<int> SeedLayoutGuideWeights(const DashboardRenderer::LayoutEditGuide& guide, const LayoutNodeConfig* node) {
    if (node == nullptr || node->children.size() != guide.childExtents.size()) {
        return guide.childExtents;
    }

    std::vector<int> weights;
    weights.reserve(node->children.size());
    for (size_t i = 0; i < node->children.size(); ++i) {
        weights.push_back(std::max(1, guide.childExtents[i]));
    }

    std::vector<bool> fixed = guide.childFixedExtents;
    if (fixed.size() != weights.size()) {
        fixed.assign(weights.size(), false);
    }

    for (size_t i = 0; i < weights.size(); ++i) {
        if (fixed[i]) {
            weights[i] = std::max(1, node->children[i].weight);
        }
    }

    return weights;
}

bool ApplyLayoutGuideWeightsToConfig(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    if (weights.size() < 2) {
        return false;
    }

    const auto applyWeights = [&](LayoutNodeConfig* node) -> bool {
        if (node == nullptr || node->children.size() != weights.size()) {
            return false;
        }
        for (size_t i = 0; i < weights.size(); ++i) {
            node->children[i].weight = std::max(1, weights[i]);
        }
        return true;
    };

    bool updated = false;
    if (target.editCardId.empty()) {
        updated = applyWeights(FindLayoutNodeByPath(config.layout.structure.cardsLayout, target.nodePath));
        if (!updated) {
            return false;
        }
        if (NamedLayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            applyWeights(FindLayoutNodeByPath(namedLayout->cardsLayout, target.nodePath));
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, target.editCardId)) {
        updated = applyWeights(FindLayoutNodeByPath(card->layout, target.nodePath));
    }

    return updated;
}

LayoutCardConfig* FindCardLayoutById(LayoutConfig& layout, const std::string& cardId) {
    const auto it = std::find_if(layout.cards.begin(), layout.cards.end(), [&](LayoutCardConfig& card) {
        return card.id == cardId;
    });
    return it != layout.cards.end() ? &(*it) : nullptr;
}

NamedLayoutSectionConfig* FindNamedLayoutByName(AppConfig& config, const std::string& name) {
    const auto it = std::find_if(config.layouts.begin(), config.layouts.end(), [&](NamedLayoutSectionConfig& layout) {
        return layout.name == name;
    });
    return it != config.layouts.end() ? &(*it) : nullptr;
}

void SetMenuItemRadioStyle(HMENU menu, UINT commandId) {
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE;
    info.fType = MFT_RADIOCHECK;
    SetMenuItemInfoW(menu, commandId, FALSE, &info);
}

std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress) {
    if (adapterName.empty()) {
        return ipAddress;
    }
    if (ipAddress.empty()) {
        return adapterName;
    }
    return adapterName + " | " + ipAddress;
}

std::string FormatStorageDriveSize(double totalGb) {
    char buffer[64];
    if (totalGb >= 1024.0) {
        sprintf_s(buffer, "%.1f TB", totalGb / 1024.0);
    } else {
        sprintf_s(buffer, "%.0f GB", totalGb);
    }
    return buffer;
}

std::string FormatStorageDriveMenuText(const StorageDriveMenuOption& option) {
    std::string text = option.driveLetter + ":";
    if (!option.volumeLabel.empty()) {
        text += " | " + option.volumeLabel;
    }
    text += " | " + FormatStorageDriveSize(option.totalGb);
    return text;
}

std::string SimplifyDeviceName(const std::string& deviceName) {
    if (deviceName.rfind("\\\\.\\", 0) == 0) {
        return deviceName.substr(4);
    }
    return deviceName;
}

bool IsUsefulFriendlyName(const std::string& name) {
    const std::string lowered = ToLower(name);
    return !name.empty() &&
        lowered != "generic pnp monitor" &&
        lowered.find("\\\\?\\display") != 0;
}

std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config) {
    struct SearchContext {
        const AppConfig* config = nullptr;
        std::vector<DisplayMenuOption> results;
    } context{&config, {}};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }

            const std::string deviceName = Utf8FromWide(info.szDevice);
            const MonitorIdentity identity = GetMonitorIdentity(deviceName);
            const UINT dpi = GetMonitorDpi(monitor);
            const SIZE scaledWindow = ComputeWindowSizeForDpi(*context->config, dpi);
            const LONG monitorWidth = info.rcMonitor.right - info.rcMonitor.left;
            const LONG monitorHeight = info.rcMonitor.bottom - info.rcMonitor.top;

            DisplayMenuOption option;
            option.commandId = kCommandConfigureDisplayBase + static_cast<UINT>(context->results.size());
            option.displayName = identity.displayName + " (" + std::to_string(monitorWidth) + "x" + std::to_string(monitorHeight) + ")";
            option.configMonitorName = !identity.configName.empty() ? identity.configName : deviceName;
            option.rect = info.rcMonitor;
            option.dpi = dpi;
            option.layoutFits = scaledWindow.cx == monitorWidth && scaledWindow.cy == monitorHeight;
            context->results.push_back(std::move(option));
            return context->results.size() < (kCommandConfigureDisplayMax - kCommandConfigureDisplayBase + 1);
        },
        reinterpret_cast<LPARAM>(&context));

    return context.results;
}

std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) {
    if (requestedName.empty()) {
        return std::nullopt;
    }
    struct SearchContext {
        std::string requestedName;
        std::optional<TargetMonitorInfo> result;
    } context{requestedName, std::nullopt};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }

            const std::string deviceName = Utf8FromWide(info.szDevice);
            const MonitorIdentity identity = GetMonitorIdentity(deviceName);
            if (ContainsInsensitive(identity.displayName, context->requestedName) ||
                ContainsInsensitive(identity.configName, context->requestedName) ||
                ContainsInsensitive(deviceName, context->requestedName)) {
                context->result = TargetMonitorInfo{info.rcMonitor, GetMonitorDpi(monitor)};
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    return context.result;
}

MonitorIdentity GetMonitorIdentity(const std::string& deviceName) {
    MonitorIdentity identity;
    identity.displayName = SimplifyDeviceName(deviceName);
    identity.configName = deviceName;

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return identity;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return identity;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        const std::wstring wideDeviceName = WideFromUtf8(deviceName);
        if (_wcsicmp(sourceName.viewGdiDeviceName, wideDeviceName.c_str()) != 0) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = paths[i].targetInfo.adapterId;
        targetName.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS) {
            continue;
        }

        const std::string friendlyName = Utf8FromWide(targetName.monitorFriendlyDeviceName);
        const std::string monitorPath = Utf8FromWide(targetName.monitorDevicePath);
        if (IsUsefulFriendlyName(friendlyName)) {
            identity.displayName = friendlyName + " (" + SimplifyDeviceName(deviceName) + ")";
            identity.configName = friendlyName;
        } else if (!monitorPath.empty()) {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = monitorPath;
        } else {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = deviceName;
        }
        return identity;
    }

    return identity;
}

MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd) {
    MonitorPlacementInfo info;
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        info.deviceName = Utf8FromWide(monitorInfo.szDevice);
        const MonitorIdentity identity = GetMonitorIdentity(info.deviceName);
        info.monitorName = identity.displayName;
        info.configMonitorName = identity.configName;
        info.monitorRect = monitorInfo.rcMonitor;
        info.dpi = GetMonitorDpi(monitor);
        info.relativePosition.x = ScalePhysicalToLogical(windowRect.left - monitorInfo.rcMonitor.left, info.dpi);
        info.relativePosition.y = ScalePhysicalToLogical(windowRect.top - monitorInfo.rcMonitor.top, info.dpi);
    }
    return info;
}

HFONT CreateUiFont(const UiFontConfig& font) {
    const std::wstring face = WideFromUtf8(font.face);
    return CreateFontW(-font.size, 0, 0, 0, font.weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, face.c_str());
}

void ShutdownPreviousInstance() {
    HWND existing = FindWindowW(kWindowClassName, nullptr);
    if (existing == nullptr) {
        return;
    }

    const DWORD existingProcessId = [&]() {
        DWORD processId = 0;
        GetWindowThreadProcessId(existing, &processId);
        return processId;
    }();

    if (existingProcessId == GetCurrentProcessId()) {
        return;
    }

    PostMessageW(existing, WM_CLOSE, 0, 0);
    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(100);
        existing = FindWindowW(kWindowClassName, nullptr);
        if (existing == nullptr) {
            return;
        }
    }
}

std::filesystem::path GetRuntimeConfigPath() {
    return GetExecutableDirectory() / L"config.ini";
}

AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) {
    return LoadConfig(GetRuntimeConfigPath(), !options.defaultConfig);
}

bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) {
    if (config.display.wallpaper.empty()) {
        return true;
    }
    if (config.display.monitorName.empty()) {
        WriteOptionalTrace(traceStream, "wallpaper:skipped_missing_monitor wallpaper=\"" + config.display.wallpaper + "\"");
        return false;
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        WriteOptionalTrace(traceStream, "wallpaper:monitor_unresolved monitor=\"" + config.display.monitorName +
            "\" wallpaper=\"" + config.display.wallpaper + "\"");
        return false;
    }

    const std::filesystem::path wallpaperPath = ResolveExecutableRelativePath(std::filesystem::path(WideFromUtf8(config.display.wallpaper)));
    if (wallpaperPath.empty()) {
        WriteOptionalTrace(traceStream, "wallpaper:path_empty monitor=\"" + config.display.monitorName + "\"");
        return false;
    }

    const HRESULT initStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE) {
        WriteOptionalTrace(traceStream, "wallpaper:coinitialize_failed hr=" + FormatHresult(initStatus));
        return false;
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    const HRESULT createStatus = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&desktopWallpaper));
    if (FAILED(createStatus) || desktopWallpaper == nullptr) {
        WriteOptionalTrace(traceStream, "wallpaper:create_failed hr=" + FormatHresult(createStatus));
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    bool applied = false;
    bool targetFound = false;
    UINT monitorCount = 0;
    const HRESULT countStatus = desktopWallpaper->GetMonitorDevicePathCount(&monitorCount);
    if (FAILED(countStatus)) {
        WriteOptionalTrace(traceStream, "wallpaper:monitor_count_failed hr=" + FormatHresult(countStatus));
    } else {
        for (UINT index = 0; index < monitorCount; ++index) {
            LPWSTR monitorId = nullptr;
            const HRESULT idStatus = desktopWallpaper->GetMonitorDevicePathAt(index, &monitorId);
            if (FAILED(idStatus) || monitorId == nullptr) {
                continue;
            }

            RECT monitorRect{};
            const HRESULT rectStatus = desktopWallpaper->GetMonitorRECT(monitorId, &monitorRect);
            if (SUCCEEDED(rectStatus) && RectsEqual(monitorRect, targetMonitor->rect)) {
                targetFound = true;
                const HRESULT setStatus = desktopWallpaper->SetWallpaper(monitorId, wallpaperPath.c_str());
                applied = SUCCEEDED(setStatus);
                WriteOptionalTrace(traceStream,
                    std::string("wallpaper:apply_") + (applied ? "done" : "failed") +
                    " monitor=\"" + config.display.monitorName +
                    "\" path=\"" + Utf8FromWide(wallpaperPath.wstring()) +
                    "\" hr=" + FormatHresult(setStatus));
                CoTaskMemFree(monitorId);
                break;
            }
            CoTaskMemFree(monitorId);
        }
    }

    if (!targetFound) {
        WriteOptionalTrace(traceStream, "wallpaper:target_not_found monitor=\"" + config.display.monitorName +
            "\" path=\"" + Utf8FromWide(wallpaperPath.wstring()) + "\"");
    }

    desktopWallpaper->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return applied;
}

int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath, const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath, const std::filesystem::path& targetImagePath) {
    if (sourceConfigPath.empty() || sourceDumpPath.empty() || targetConfigPath.empty() || targetImagePath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourceConfigPath);
    TelemetryDump dump;
    {
        std::ifstream input(sourceDumpPath, std::ios::binary);
        std::string error;
        if (!input.is_open() || !LoadTelemetryDump(input, dump, &error)) {
            return 1;
        }
    }

    const std::optional<TargetMonitorInfo> targetMonitor = FindTargetMonitor(config.display.monitorName);
    if (!targetMonitor.has_value()) {
        return 1;
    }

    std::string screenshotError;
    const bool imageSaved = SaveDumpScreenshot(
        targetImagePath,
        dump.snapshot,
        config,
        ScaleFromDpi(targetMonitor->dpi),
        DashboardRenderer::RenderMode::Blank,
        false,
        DashboardRenderer::SimilarityIndicatorMode::ActiveGuide,
        std::string{},
        nullptr,
        &screenshotError);
    const bool configSaved = imageSaved && SaveConfig(targetConfigPath, config);
    const bool wallpaperApplied = configSaved && ApplyConfiguredWallpaper(config, nullptr);

    std::error_code ignored;
    std::filesystem::remove(sourceConfigPath, ignored);
    std::filesystem::remove(sourceDumpPath, ignored);
    return wallpaperApplied ? 0 : 1;
}
