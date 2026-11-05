#pragma once

#include "app/theme.h"
#include "core/history.h"
#include "core/metrics.h"
#include "core/processes.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace vela {

// The overview, or the processes using the most of one resource.
enum class PanelView { Overview, Cpu, Gpu, Memory, Disk };

// One line of the panel: a metric with its value, details and level bar.
struct PanelRow {
    const wchar_t* icon = nullptr;
    std::wstring label;
    std::wstring value;
    std::wstring detail;
    int barPercent = -1;  // -1: no bar
    bool high = false;
    PanelView opens = PanelView::Overview;  // The top list a click opens, if any
    std::vector<float> graph;  // Recent values, 0-1, drawn instead of the bar

    bool operator==(const PanelRow&) const = default;
};

// Only the `metrics` chosen in Settings; CPU when none of them can be shown.
std::vector<PanelRow> BuildPanelRows(const Snapshot& snapshot, unsigned processors, unsigned long long uptimeMs,
                                     uint32_t metrics = metric::kAll, const History* history = nullptr);

// One process in a top list. The bar is relative to the whole system for
// CPU and memory, and to the busiest process for disk activity.
struct ProcessLine {
    std::wstring name;
    std::wstring value;
    int barPercent = 0;
    std::wstring exe;  // The group's executable name
    std::vector<uint32_t> pids;

    bool operator==(const ProcessLine&) const = default;
};

inline constexpr size_t kTopProcesses = 10;

std::vector<ProcessLine> BuildProcessLines(const std::vector<ProcessUsage>& processes, PanelView view, uint64_t memoryTotal);

// The small flyout opened from the tray icon, in the style of the Windows 11
// quick settings. It only exists while open, so it costs nothing when closed.
// CPU, GPU, memory and disk open the top processes for that resource, where a
// right-click on a process ends it or shows its file. The history the
// graphs draw belongs to the app, which keeps it while the panel is closed.
class Panel {
public:
    std::function<void()> onClosed;
    std::function<void()> onSettings;

    bool IsOpen() const { return window_ != nullptr; }
    HWND window() const { return window_; }
    // True right after the panel closed by losing focus, so the tray click
    // that closed it does not open it again.
    bool JustClosed() const { return GetTickCount64() - closedAt_ < 300; }

    void Show(HINSTANCE instance, const RECT& anchor, const Snapshot& snapshot, uint32_t metrics, const History* history,
              PanelView view = PanelView::Overview);
    void Update(const Snapshot& snapshot, const std::vector<ProcessUsage>& processes);
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void RefreshStyle();
    int Scale(int value) const;
    SIZE Measure() const;
    // Resizes for the current view, keeping the edge next to the taskbar.
    void Relayout();
    void SetView(PanelView view);
    RECT RowRect(size_t index) const;
    RECT LineRect(size_t index) const;
    RECT SettingsRect() const;
    RECT BackRect() const;
    // The row or back button under `point`: what a click there would open.
    int HitTest(POINT point) const;
    void SetHot(int hot);
    void Paint(HDC dc, const RECT& client);
    void PaintRow(HDC dc, const PanelRow& row, const RECT& rect, bool hot, bool clickable);
    void PaintLine(HDC dc, const ProcessLine& line, const RECT& rect);
    void PaintGraph(HDC dc, const std::vector<float>& values, const RECT& rect, COLORREF color);
    void ShowProcessMenu(POINT point);
    void SetStatus(std::wstring status);

    HWND window_ = nullptr;
    ULONGLONG closedAt_ = 0;
    PanelView view_ = PanelView::Overview;
    bool anchoredTop_ = false;
    std::vector<PanelRow> rows_;
    std::vector<ProcessUsage> processes_;
    std::vector<ProcessLine> lines_;
    uint64_t memoryTotal_ = 0;
    unsigned processors_ = 0;
    uint32_t metrics_ = metric::kAll;
    const History* history_ = nullptr;
    std::wstring status_;  // Shown in the footer of a top list
    int hot_ = -1;
    bool activated_ = false;

    Theme theme_{};
    HFONT titleFont_ = nullptr;
    HFONT labelFont_ = nullptr;
    HFONT valueFont_ = nullptr;
    HFONT detailFont_ = nullptr;
    HFONT iconFont_ = nullptr;
};

}  // namespace vela
