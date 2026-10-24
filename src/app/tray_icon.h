#pragma once

#include "core/settings.h"

#include <windows.h>

namespace vela {

// What a tray icon shows, quantized to what can actually be seen at its
// size: the icon is only redrawn when this changes.
struct TrayIconState {
    TrayIcon style = TrayIcon::Logo;
    int size = 0;
    bool lightTaskbar = false;
    int cpu = 0;     // The percent shown
    int memory = 0;
    bool cpuHigh = false;  // Drawn in a warning color
    bool memoryHigh = false;

    bool operator==(const TrayIconState&) const = default;
};

TrayIconState MakeTrayIconState(TrayIcon style, int size, bool lightTaskbar, double cpuPercent, int memoryPercent);

HICON CreateTrayIcon(const TrayIconState& state);

// The taskbar can be light while apps are dark, so it has its own setting.
bool IsTaskbarLight();

}  // namespace vela
