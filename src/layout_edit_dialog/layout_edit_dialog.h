#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/config.h"
#include "layout_edit/layout_edit_controller.h"
#include "widget/layout_edit_types.h"

struct LayoutEditDialogState;

class LayoutEditDialogHost {
public:
    virtual ~LayoutEditDialogHost() = default;

    virtual HINSTANCE LayoutEditDialogInstance() const = 0;
    virtual HWND LayoutEditDialogAnchorWindow() const = 0;
    virtual UINT LayoutEditDialogAnchorDpi() const = 0;

    virtual AppConfig BuildLayoutEditOriginalConfig() const = 0;
    virtual const AppConfig& CurrentConfig() const = 0;

    virtual bool ApplyParameterPreview(LayoutEditParameter parameter, double value) = 0;
    virtual bool ApplyFontPreview(LayoutEditParameter parameter, const UiFontConfig& value) = 0;
    virtual bool ApplyFontFamilyPreview(const std::string& family) = 0;
    virtual bool ApplyFontSetPreview(const UiFontSetConfig& fonts) = 0;
    virtual bool ApplyColorPreview(LayoutEditParameter parameter, unsigned int value) = 0;
    virtual bool ApplyMetricPreview(const LayoutMetricEditKey& key,
        const std::optional<double>& scale,
        const std::string& unit,
        const std::string& label,
        const std::optional<std::string>& binding) = 0;
    virtual bool ApplyCardTitlePreview(const LayoutCardTitleEditKey& key, const std::string& title) = 0;
    virtual bool ApplyMetricListOrderPreview(
        const LayoutMetricListOrderEditKey& key, const std::vector<std::string>& metricRefs) = 0;
    virtual bool ApplyDateTimeFormatPreview(const LayoutDateTimeFormatEditKey& key, const std::string& format) = 0;
    virtual bool ApplyWeightPreview(const LayoutWeightEditKey& key, int firstWeight, int secondWeight) = 0;

    virtual std::vector<std::string> AvailableBoardMetricSensorBindings(const LayoutMetricEditKey& key) const = 0;
    virtual void UpdateLayoutEditSelectionHighlight(const std::optional<LayoutEditSelectionHighlight>& highlight) = 0;
    virtual void RestackLayoutEditDialogAnchor(HWND dialogHwnd) = 0;
    virtual void TraceLayoutEditDialogEvent(const std::string& event, const std::string& details = {}) const = 0;
    virtual void OnLayoutEditDialogCloseRequested() = 0;
};

class LayoutEditDialog {
public:
    explicit LayoutEditDialog(LayoutEditDialogHost& host);
    ~LayoutEditDialog();

    LayoutEditDialogHost& Host();
    const LayoutEditDialogHost& Host() const;

    bool HandleDialogMessage(MSG* msg) const;
    bool Ensure(const std::optional<LayoutEditFocusKey>& focusKey = std::nullopt, bool bringToFront = false);
    void Refresh(const std::optional<LayoutEditFocusKey>& preferredFocus = std::nullopt);
    void RefreshSelection();
    void RestackAnchor();
    bool SyncSelection(const std::optional<LayoutEditController::TooltipTarget>& target, bool bringToFront);
    bool ShouldDashboardIgnoreMouse(POINT screenPoint) const;
    void SetSelectionHighlightVisible(bool visible);
    void UpdateSelectionHighlight(const std::optional<LayoutEditSelectionHighlight>& highlight);
    void Close();

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool IsForegroundWindow() const;
    void BringToFront();
    void PositionWindow(HWND hwnd) const;
    void ApplySelectionHighlightVisibility();
    void ClearSelectionHighlight();
    void RememberWindowPlacement(HWND hwnd);
    void HandleDestroyed(HWND hwnd);

    LayoutEditDialogHost& host_;
    HWND hwnd_ = nullptr;
    std::unique_ptr<LayoutEditDialogState> state_;
    std::optional<LayoutEditSelectionHighlight> selectionHighlight_;
    std::optional<RECT> savedWindowRect_;
    bool selectionHighlightVisible_ = false;
};
