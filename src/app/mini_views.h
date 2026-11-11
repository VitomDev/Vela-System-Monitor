#pragma once

#include "app/theme.h"
#include "core/metrics.h"
#include "core/taskbar_place.h"

#include <windows.h>

#include <functional>
#include <vector>
#include <optional>

namespace vela {

// A small always-on-top window with the two mini lines. Drag it anywhere;
// right-click for its menu, double-click for the panel.
class FloatingWidget {
public:
    std::function<void(POINT)> onMoved;  // Top-left corner after a drag
    std::function<void(POINT)> onMenu;   // Screen point of the right-click
    std::function<void()> onOpenPanel;

    bool IsOpen() const { return window_ != nullptr; }
    // `widest` sizes the widget for the chosen metrics.
    void Show(HINSTANCE instance, const std::optional<POINT>& position, const MiniLines& lines, const MiniLines& widest);
    void Update(const MiniLines& lines, const MiniLines& widest);
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void RefreshStyle();
    int Scale(int value) const;

    HWND window_ = nullptr;
    MiniLines lines_;
    MiniLines widest_;
    Theme theme_{};
    HFONT font_ = nullptr;
};

// The mini lines drawn on the taskbar, next to the clock, at the left or
// wherever they are dragged. Windows 11 has no API for taskbar widgets, so this
// is a transparent window placed inside the taskbar, the way other taskbar
// monitors do it.
// The taskbars of every screen: the main one first, then left to right.
std::vector<HWND> FindTaskbars();

class TaskbarView {
public:
    std::function<void(POINT)> onMenu;
    std::function<void()> onOpenPanel;
    // After a drag: the new spot and, for Custom, the 96-DPI offset.
    std::function<void(TaskbarSpot, int)> onPlaced;

    bool IsOpen() const { return window_ != nullptr; }
    // False when the taskbar could not be found.
    bool Show(HINSTANCE instance, HWND taskbar, const MiniLines& lines, const MiniLines& widest, TaskbarSpot spot,
              int offset);
    void SetSpot(TaskbarSpot spot, int offset);
    void Update(const MiniLines& lines, const MiniLines& widest);
    // After a theme change, which child windows are not told about.
    void Redraw() {
        if (window_) Render();
    }
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    RECT NotifyRect(const RECT& taskbar, UINT dpi) const;
    // Follows the notification area as it grows or the taskbar moves.
    void Place();
    void Render();
    TaskbarRoom Room(const RECT& taskbar, UINT dpi) const;
    bool IsHorizontal() const;

    HWND window_ = nullptr;
    TaskbarSpot spot_ = TaskbarSpot::Clock;
    int offset_ = 0;
    // Dragging: where the press was (screen x) and the view's left then.
    bool pressed_ = false;
    bool dragging_ = false;
    int pressX_ = 0;
    int pressLeft_ = 0;
    HWND taskbar_ = nullptr;
    MiniLines lines_;
    MiniLines widest_;
    SIZE size_{};
    UINT dpi_ = 0;
    SIZE textSize_{};
};

}  // namespace vela
