#pragma once

#include "core/settings.h"

#include <windows.h>

#include <functional>
#include <string>

namespace vela {

// Applies confirmed settings (saves them, sets autostart). Returns false with
// a message when they cannot be used.
using ApplySettings = std::function<bool(const Settings& settings, bool autostart, std::wstring& error)>;

// The small Settings window. Modal; a second request focuses the open one.
void ShowSettingsDialog(HINSTANCE instance, HWND owner, const Settings& current, const ApplySettings& apply);

}  // namespace vela
