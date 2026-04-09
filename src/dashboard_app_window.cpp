#include "app_shared.h"

#include <cmath>
#include <cstdio>

void DashboardApp::ShowContextMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    HMENU diagnosticsMenu = CreatePopupMenu();
    HMENU layoutMenu = CreatePopupMenu();
    HMENU networkMenu = CreatePopupMenu();
    HMENU storageDrivesMenu = CreatePopupMenu();
    HMENU configureDisplayMenu = CreatePopupMenu();
    const UINT autoStartFlags = MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    layoutMenuOptions_.clear();
    for (size_t i = 0; i < config_.layouts.size() && (kCommandLayoutBase + i) <= kCommandLayoutMax; ++i) {
        LayoutMenuOption option;
        option.commandId = kCommandLayoutBase + static_cast<UINT>(i);
        option.name = config_.layouts[i].name;
        layoutMenuOptions_.push_back(option);
    }
    if (layoutMenuOptions_.empty()) {
        AppendMenuW(layoutMenu, MF_STRING | MF_GRAYED, kCommandLayoutBase, L"No layouts found");
    } else {
        for (const auto& option : layoutMenuOptions_) {
            const std::wstring label = WideFromUtf8(option.name);
            const UINT flags = MF_STRING | (config_.display.layout == option.name ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(layoutMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(layoutMenu, option.commandId);
        }
    }
    networkMenuOptions_.clear();
    const auto& networkCandidates = telemetry_->NetworkAdapterCandidates();
    for (size_t i = 0; i < networkCandidates.size() && (kCommandNetworkAdapterBase + i) <= kCommandNetworkAdapterMax; ++i) {
        NetworkMenuOption option;
        option.commandId = kCommandNetworkAdapterBase + static_cast<UINT>(i);
        option.adapterName = networkCandidates[i].adapterName;
        option.ipAddress = networkCandidates[i].ipAddress;
        option.selected = networkCandidates[i].selected;
        networkMenuOptions_.push_back(std::move(option));
    }
    if (networkMenuOptions_.empty()) {
        AppendMenuW(networkMenu, MF_STRING | MF_GRAYED, kCommandNetworkAdapterBase, L"No adapters found");
    } else {
        for (const auto& option : networkMenuOptions_) {
            const std::wstring label = WideFromUtf8(FormatNetworkFooterText(option.adapterName, option.ipAddress));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(networkMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(networkMenu, option.commandId);
        }
    }
    storageDriveMenuOptions_.clear();
    const auto& storageDriveCandidates = telemetry_->StorageDriveCandidates();
    for (size_t i = 0; i < storageDriveCandidates.size() && (kCommandStorageDriveBase + i) <= kCommandStorageDriveMax; ++i) {
        StorageDriveMenuOption option;
        option.commandId = kCommandStorageDriveBase + static_cast<UINT>(i);
        option.driveLetter = storageDriveCandidates[i].letter;
        option.volumeLabel = storageDriveCandidates[i].volumeLabel;
        option.totalGb = storageDriveCandidates[i].totalGb;
        option.selected = storageDriveCandidates[i].selected;
        storageDriveMenuOptions_.push_back(std::move(option));
    }
    if (storageDriveMenuOptions_.empty()) {
        AppendMenuW(storageDrivesMenu, MF_STRING | MF_GRAYED, kCommandStorageDriveBase, L"No drives found");
    } else {
        for (const auto& option : storageDriveMenuOptions_) {
            const std::wstring label = WideFromUtf8(FormatStorageDriveMenuText(option));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(storageDrivesMenu, flags, option.commandId, label.c_str());
        }
    }
    configDisplayOptions_ = EnumerateDisplayMenuOptions(config_);
    if (configDisplayOptions_.empty()) {
        AppendMenuW(configureDisplayMenu, MF_STRING | MF_GRAYED, kCommandConfigureDisplayBase, L"No displays found");
    } else {
        for (const auto& option : configDisplayOptions_) {
            const std::wstring label = WideFromUtf8(option.displayName);
            const UINT flags = MF_STRING | (option.layoutFits ? MF_ENABLED : MF_GRAYED);
            AppendMenuW(configureDisplayMenu, flags, option.commandId, label.c_str());
        }
    }
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveFullConfigAs, L"Save Full Config To...");
    AppendMenuW(diagnosticsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveDumpAs, L"Save Dump To...");
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveScreenshotAs, L"Save Screenshot To...");
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(menu, MF_STRING | (isEditingLayout_ ? MF_CHECKED : MF_UNCHECKED), kCommandEditLayout, L"Edit layout");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandReloadConfig, L"Reload Config");
    AppendMenuW(menu, MF_STRING, kCommandSaveConfig, L"Save Config");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layoutMenu), L"Layout");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(networkMenu), L"Network");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(storageDrivesMenu), L"Storage drives");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(configureDisplayMenu), L"Config To Display");
    AppendMenuW(menu, autoStartFlags, kCommandAutoStart, L"Auto-start on user logon");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(diagnosticsMenu), L"Diagnostics");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
    SetForegroundWindow(hwnd_);
    const UINT selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (selected) {
    case kCommandMove:
        StartMoveMode();
        break;
    case kCommandEditLayout:
        if (isEditingLayout_) {
            StopLayoutEditMode();
        } else {
            StartLayoutEditMode();
        }
        break;
    case kCommandBringOnTop:
        BringOnTop();
        break;
    case kCommandReloadConfig:
        if (!ReloadConfigFromDisk()) {
            MessageBoxW(hwnd_, L"Failed to reload config.ini.", L"System Telemetry", MB_ICONERROR);
        }
        break;
    case kCommandSaveConfig:
        UpdateConfigFromCurrentPlacement();
        break;
    case kCommandAutoStart:
        ToggleAutoStart();
        break;
    case kCommandSaveDumpAs:
        SaveDumpAs();
        break;
    case kCommandSaveScreenshotAs:
        SaveScreenshotAs();
        break;
    case kCommandSaveFullConfigAs:
        SaveFullConfigAs();
        break;
    case kCommandExit:
        DestroyWindow(hwnd_);
        break;
    default:
        if (selected >= kCommandLayoutBase && selected <= kCommandLayoutMax) {
            const auto it = std::find_if(layoutMenuOptions_.begin(), layoutMenuOptions_.end(),
                [selected](const LayoutMenuOption& option) { return option.commandId == selected; });
            if (it != layoutMenuOptions_.end() && !SwitchLayout(it->name)) {
                MessageBoxW(hwnd_, L"Failed to switch layout.", L"System Telemetry", MB_ICONERROR);
            }
            break;
        }
        if (selected >= kCommandNetworkAdapterBase && selected <= kCommandNetworkAdapterMax) {
            const auto it = std::find_if(networkMenuOptions_.begin(), networkMenuOptions_.end(),
                [selected](const NetworkMenuOption& option) { return option.commandId == selected; });
            if (it != networkMenuOptions_.end()) {
                SelectNetworkAdapter(*it);
            }
            break;
        }
        if (selected >= kCommandStorageDriveBase && selected <= kCommandStorageDriveMax) {
            const auto it = std::find_if(storageDriveMenuOptions_.begin(), storageDriveMenuOptions_.end(),
                [selected](const StorageDriveMenuOption& option) { return option.commandId == selected; });
            if (it != storageDriveMenuOptions_.end()) {
                ToggleStorageDrive(*it);
            }
            break;
        }
        if (selected >= kCommandConfigureDisplayBase && selected <= kCommandConfigureDisplayMax) {
            const auto it = std::find_if(configDisplayOptions_.begin(), configDisplayOptions_.end(),
                [selected](const DisplayMenuOption& option) { return option.commandId == selected; });
            if (it != configDisplayOptions_.end()) {
                ConfigureDisplay(*it);
            }
        }
        break;
    }
}

void DashboardApp::DrawMoveOverlay(HDC hdc) {
    const int margin = ScaleLogicalToPhysical(16, CurrentWindowDpi());
    const int padding = ScaleLogicalToPhysical(12, CurrentWindowDpi());
    const int lineGap = ScaleLogicalToPhysical(6, CurrentWindowDpi());
    const int cornerRadius = ScaleLogicalToPhysical(14, CurrentWindowDpi());
    const int borderWidth = std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi()));

    char positionText[96];
    sprintf_s(positionText, "Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);
    char scaleText[96];
    const double scale = ScaleFromDpi(movePlacementInfo_.dpi);
    sprintf_s(scaleText, "Scale: %.0f%% (%.2fx)", scale * 100.0, scale);

    const std::string titleText = "Move Mode";
    const std::string monitorText = "Monitor: " + movePlacementInfo_.monitorName;
    const std::string hintText = "Left-click to place. Copy monitor name, scale, and x/y into config.";

    const int titleHeight = MeasureFontHeight(hdc, renderer_.LabelFont());
    const int bodyHeight = MeasureFontHeight(hdc, renderer_.SmallFont());
    const int minContentWidth = ScaleLogicalToPhysical(220, CurrentWindowDpi());
    const int maxContentWidth = std::max(minContentWidth, WindowWidth() - margin * 2 - padding * 2);
    int preferredContentWidth = minContentWidth;
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.LabelFont(), titleText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), monitorText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), positionText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), scaleText).cx));
    const int contentWidth = std::min(maxContentWidth, preferredContentWidth);
    const int hintHeight = MeasureWrappedTextHeight(hdc, renderer_.SmallFont(), hintText, contentWidth);
    const int overlayWidth = contentWidth + padding * 2;
    const int overlayHeight = padding * 2 + titleHeight + lineGap + bodyHeight + lineGap +
        bodyHeight + lineGap + bodyHeight + lineGap + hintHeight;
    RECT overlay{margin, margin, margin + overlayWidth, margin + overlayHeight};

    HBRUSH fill = CreateSolidBrush(BackgroundColor());
    HPEN border = CreatePen(PS_SOLID, borderWidth, AccentColor());
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, border);
    RoundRect(hdc, overlay.left, overlay.top, overlay.right, overlay.bottom, cornerRadius, cornerRadius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(fill);
    DeleteObject(border);

    int y = overlay.top + padding;
    RECT titleRect{overlay.left + padding, y, overlay.right - padding, y + titleHeight};
    y = titleRect.bottom + lineGap;
    RECT monitorRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = monitorRect.bottom + lineGap;
    RECT positionRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = positionRect.bottom + lineGap;
    RECT scaleRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = scaleRect.bottom + lineGap;
    RECT hintRect{overlay.left + padding, y, overlay.right - padding, overlay.bottom - padding};

    DrawTextBlock(hdc, titleRect, titleText, renderer_.LabelFont(), AccentColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, monitorText, renderer_.SmallFont(), ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, scaleRect, scaleText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, hintText, renderer_.SmallFont(),
        MutedTextColor(), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

int DashboardApp::Run() {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK DashboardApp::WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<DashboardApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DashboardApp::WndProcThunk));
        app->hwnd_ = hwnd;
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK DashboardApp::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app != nullptr ? app->HandleMessage(message, wParam, lParam)
                          : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        currentDpi_ = CurrentWindowDpi();
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        if (!InitializeFonts()) {
            return -1;
        }
        StartPlacementWatch();
        ApplyConfigPlacement();
        SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
        CreateTrayIcon();
        return 0;
    case WM_TIMER:
        if (wParam == kMoveTimerId) {
            UpdateMoveTracking();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wParam == kPlacementTimerId) {
            RetryConfigPlacementIfPending();
            return 0;
        }
        telemetry_->UpdateSnapshot();
        if (diagnostics_ != nullptr &&
            std::chrono::steady_clock::now() - lastDiagnosticsOutput_ >= std::chrono::seconds(1)) {
            if (!WriteDiagnosticsOutputs()) {
                DestroyWindow(hwnd_);
                return 0;
            }
            lastDiagnosticsOutput_ = std::chrono::steady_clock::now();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetWindowRect(hwnd_, &rect);
            point.x = rect.left + 24;
            point.y = rect.top + 24;
        }
        if (isMoving_) {
            StopMoveMode();
        }
        ShowContextMenu(point);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (isEditingLayout_) {
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (hoveredEditableTextAnchor_.has_value()) {
                const auto region = renderer_.FindEditableTextRegion(*hoveredEditableTextAnchor_);
                if (region.has_value()) {
                    activeTextEditDrag_ = TextEditDragState{
                        region->key,
                        region->fontSize,
                        clientPoint.x};
                    hoveredEditableText_ = region->key;
                    hoveredEditableWidget_ = region->key.widget;
                    renderer_.SetHoveredEditableText(hoveredEditableText_);
                    renderer_.SetHoveredEditableWidget(hoveredEditableWidget_);
                    renderer_.SetActiveEditableText(region->key);
                    SetCapture(hwnd_);
                    return 0;
                }
            }
            size_t widgetGuideIndex = 0;
            const DashboardRenderer::WidgetEditGuide* widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
            if (widgetGuide != nullptr) {
                activeWidgetEditDrag_ = WidgetEditDragState{
                    *widgetGuide,
                    widgetGuide->value,
                    widgetGuide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y};
                renderer_.SetActiveWidgetEditGuide(std::optional<DashboardRenderer::WidgetEditGuide>(activeWidgetEditDrag_->guide));
                hoveredEditableWidget_ = widgetGuide->widget;
                hoveredWidgetEditGuideIndex_ = widgetGuideIndex;
                renderer_.SetHoveredEditableWidget(hoveredEditableWidget_);
                SetCapture(hwnd_);
                return 0;
            }
            size_t guideIndex = 0;
            const DashboardRenderer::LayoutEditGuide* guide = HitTestLayoutGuide(clientPoint, &guideIndex);
            if (guide != nullptr) {
                const LayoutNodeConfig* guideNode = FindGuideNode(config_, *guide);
                const std::vector<int> initialWeights = SeedLayoutGuideWeights(*guide, guideNode);
                activeLayoutDrag_ = LayoutDragState{
                    *guide,
                    initialWeights,
                    renderer_.CollectLayoutGuideSnapCandidates(*guide),
                    guide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y};
                renderer_.SetActiveLayoutEditGuide(std::optional<DashboardRenderer::LayoutEditGuide>(activeLayoutDrag_->guide));
                hoveredLayoutGuideIndex_ = guideIndex;
                SetCapture(hwnd_);
                return 0;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (isEditingLayout_) {
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (activeLayoutDrag_.has_value()) {
                UpdateLayoutDrag(clientPoint);
            } else if (activeTextEditDrag_.has_value()) {
                UpdateTextEditDrag(clientPoint);
            } else if (activeWidgetEditDrag_.has_value()) {
                UpdateWidgetEditDrag(clientPoint);
            } else {
                RefreshLayoutEditHover(clientPoint);
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (activeTextEditDrag_.has_value()) {
            activeTextEditDrag_.reset();
            renderer_.SetActiveEditableText(std::nullopt);
            ReleaseCapture();
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RefreshLayoutEditHover(clientPoint);
            return 0;
        }
        if (activeWidgetEditDrag_.has_value()) {
            activeWidgetEditDrag_.reset();
            renderer_.SetActiveWidgetEditGuide(std::nullopt);
            ReleaseCapture();
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RefreshLayoutEditHover(clientPoint);
            return 0;
        }
        if (activeLayoutDrag_.has_value()) {
            activeLayoutDrag_.reset();
            renderer_.SetActiveLayoutEditGuide(std::nullopt);
            ReleaseCapture();
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RefreshLayoutEditHover(clientPoint);
            return 0;
        }
        if (isMoving_) {
            StopMoveMode();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (isEditingLayout_) {
                StopLayoutEditMode();
                return 0;
            }
            if (isMoving_) {
                StopMoveMode();
                return 0;
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (activeTextEditDrag_.has_value() && reinterpret_cast<HWND>(lParam) != hwnd_) {
            activeTextEditDrag_.reset();
            renderer_.SetActiveEditableText(std::nullopt);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (activeWidgetEditDrag_.has_value() && reinterpret_cast<HWND>(lParam) != hwnd_) {
            activeWidgetEditDrag_.reset();
            renderer_.SetActiveWidgetEditGuide(std::nullopt);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (activeLayoutDrag_.has_value() && reinterpret_cast<HWND>(lParam) != hwnd_) {
            activeLayoutDrag_.reset();
            renderer_.SetActiveLayoutEditGuide(std::nullopt);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && isEditingLayout_) {
            if (activeTextEditDrag_.has_value()) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            } else if (activeWidgetEditDrag_.has_value()) {
                const auto& guide = activeWidgetEditDrag_->guide;
                SetCursor(LoadCursorW(nullptr,
                    guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
            } else if (activeLayoutDrag_.has_value()) {
                const auto& guide = activeLayoutDrag_->guide;
                SetCursor(LoadCursorW(nullptr,
                    guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
            } else {
                POINT cursor{};
                GetCursorPos(&cursor);
                ScreenToClient(hwnd_, &cursor);
                RefreshLayoutEditHover(cursor);
            }
            return TRUE;
        }
        break;
    case kTrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowContextMenu(point);
            return 0;
        }
        if (lParam == WM_LBUTTONDBLCLK) {
            BringOnTop();
            return 0;
        }
        break;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_DPICHANGED:
        if (!ApplyWindowDpi(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam))) {
            return -1;
        }
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DISPLAYCHANGE:
        StartPlacementWatch();
        RetryConfigPlacementIfPending();
        if (!ApplyWindowDpi(CurrentWindowDpi())) {
            return -1;
        }
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DEVICECHANGE:
    case WM_SETTINGCHANGE:
        StartPlacementWatch();
        RetryConfigPlacementIfPending();
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, kRefreshTimerId);
        KillTimer(hwnd_, kMoveTimerId);
        KillTimer(hwnd_, kPlacementTimerId);
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:ui_done");
        }
        RemoveTrayIcon();
        ReleaseFonts();
        {
            HICON largeIcon = appIconLarge_;
            HICON smallIcon = appIconSmall_;
            appIconLarge_ = nullptr;
            appIconSmall_ = nullptr;
            if (largeIcon != nullptr) {
                DestroyIcon(largeIcon);
            }
            if (smallIcon != nullptr && smallIcon != largeIcon) {
                DestroyIcon(smallIcon);
            }
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DashboardApp::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);

    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    HBRUSH background = CreateSolidBrush(BackgroundColor());
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);

    DrawLayout(memDc, telemetry_->Snapshot());
    if (isMoving_) {
        DrawMoveOverlay(memDc);
    }

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd_, &ps);
}

void DashboardApp::DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
    COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    const std::wstring wideText = WideFromUtf8(text);
    DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
}

void DashboardApp::DrawLayout(HDC hdc, const SystemSnapshot& snapshot) {
    renderer_.Draw(hdc, snapshot);
}
