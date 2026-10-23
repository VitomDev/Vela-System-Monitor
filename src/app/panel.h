#pragma once

#include "app/theme.h"
#include "core/metrics.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace vela {

// One line of the panel: a metric with its value, details and level bar.
struct PanelRow {
    const wchar_t* icon = nullptr;
    std::wstring label;
    std::wstring value;
    std::wstring detail;
    int barPercent = -1;  // -1: no bar
    bool high = false;

    bool operator==(const PanelRow&) const = default;
};

std::vector<PanelRow> BuildPanelRows(const Snapshot& snapshot, unsigned processors, unsigned long long uptimeMs);

// The small flyout opened from the tray icon, in the style of the Windows 11
// quick settings. It only exists while open, so it costs nothing when closed.
class Panel {
public:
    std::function<void()> onClosed;
    std::function<void()> onSettings;

    bool IsOpen() const { return window_ != nullptr; }
    HWND window() const { return window_; }
    // True right after the panel closed by losing focus, so the tray click
    // that closed it does not open it again.
    bool JustClosed() const { return GetTickCount64() - closedAt_ < 300; }

    void Show(HINSTANCE instance, const RECT& anchor, const Snapshot& snapshot);
    void Update(const Snapshot& snapshot);
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void RefreshStyle();
    int Scale(int value) const;
    SIZE Measure() const;
    RECT RowRect(size_t index) const;
    RECT SettingsRect() const;
    void Paint(HDC dc, const RECT& client);
    void PaintRow(HDC dc, const PanelRow& row, const RECT& rect);

    HWND window_ = nullptr;
    ULONGLONG closedAt_ = 0;
    std::vector<PanelRow> rows_;
    unsigned processors_ = 0;
    bool settingsHot_ = false;
    bool activated_ = false;

    Theme theme_{};
    HFONT titleFont_ = nullptr;
    HFONT labelFont_ = nullptr;
    HFONT valueFont_ = nullptr;
    HFONT detailFont_ = nullptr;
    HFONT iconFont_ = nullptr;
};

}  // namespace vela
