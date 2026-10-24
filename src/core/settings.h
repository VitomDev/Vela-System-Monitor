#pragma once

#include <windows.h>

#include <array>
#include <optional>
#include <string>

namespace vela {

// Registry locations. The installer and the tests rely on these names.
inline constexpr wchar_t kSettingsKey[] = L"Software\\Vela System Monitor";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"Vela System Monitor";
inline constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

// What the tray icon draws.
enum class TrayIcon : DWORD { Logo = 0, CpuPercent = 1, MemoryPercent = 2 };
inline constexpr std::array kTrayIcons{TrayIcon::Logo, TrayIcon::CpuPercent, TrayIcon::MemoryPercent};
const wchar_t* TrayIconName(TrayIcon icon);

// How often the counters are read while the panel is closed.
inline constexpr std::array<DWORD, 3> kIntervals{1, 2, 5};

struct Settings {
    TrayIcon trayIcon = TrayIcon::Logo;
    DWORD intervalSeconds = 2;
    // The always-on-top mini widget and the view on the taskbar.
    bool floating = false;
    bool taskbar = false;
    // Where the widget was left; none until it is first moved.
    std::optional<POINT> floatingPosition;

    // `keyPath` is relative to HKEY_CURRENT_USER; tests use a scratch key.
    static Settings Load(const wchar_t* keyPath = kSettingsKey);
    bool Save(const wchar_t* keyPath = kSettingsKey) const;
};

// True only the first time it is called for a settings key, so Vela can
// introduce itself on its very first launch. Uninstalling removes the key.
bool ConsumeFirstRun(const wchar_t* keyPath = kSettingsKey);

// "Start with Windows" is the presence of Vela's value in the per-user Run
// key, so the menu always shows the real state.
bool IsAutostartEnabled(const wchar_t* valueName = kRunValue);
bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName = kRunValue);

std::wstring AutostartCommand(const std::wstring& exePath);

}  // namespace vela
