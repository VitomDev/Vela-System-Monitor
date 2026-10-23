#pragma once

#include "app/theme.h"
#include "core/metrics.h"

#include <windows.h>

#include <functional>
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
    void Show(HINSTANCE instance, const std::optional<POINT>& position, const MiniLines& lines);
    void Update(const MiniLines& lines);
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void RefreshStyle();
    int Scale(int value) const;

    HWND window_ = nullptr;
    MiniLines lines_;
    Theme theme_{};
    HFONT font_ = nullptr;
};

// The mini lines drawn on the taskbar, just left of the notification area.
// Windows 11 has no API for taskbar widgets, so this is a transparent window
// placed inside the taskbar, the way other taskbar monitors do it.
class TaskbarView {
public:
    std::function<void(POINT)> onMenu;
    std::function<void()> onOpenPanel;

    bool IsOpen() const { return window_ != nullptr; }
    // False when the taskbar could not be found.
    bool Show(HINSTANCE instance, const MiniLines& lines);
    void Update(const MiniLines& lines);
    // After a theme change, which child windows are not told about.
    void Redraw() {
        if (window_) Render();
    }
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    // Follows the notification area as it grows or the taskbar moves.
    void Place();
    void Render();

    HWND window_ = nullptr;
    HWND taskbar_ = nullptr;
    MiniLines lines_;
    SIZE size_{};
    UINT dpi_ = 0;
    SIZE textSize_{};
};

}  // namespace vela
