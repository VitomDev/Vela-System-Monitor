#pragma once

#include "app/mini_views.h"
#include "app/panel.h"
#include "app/tray_icon.h"
#include "core/sampler.h"
#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace vela {

inline constexpr wchar_t kTrayWindowClass[] = L"VelaSystemMonitorTray";
inline constexpr UINT WM_APP_ALREADY_RUNNING = WM_APP + 12;

// The background application: samples the system on a slow timer, keeps the
// tray icon and tooltip current and opens the panel.
class TrayApp {
public:
    bool Initialize(HINSTANCE instance, bool startedAtSignIn);
    ~TrayApp();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void AddTrayIcon();
    void ShowTrayMenu(POINT point);
    void Notify(const std::wstring& message);
    void TogglePanel();
    void ShowPanel();

    // Reads the counters and updates only what changed.
    void Sample();
    void UpdateTrayIcon(bool force);
    void RestartTimer();
    // Opens or closes the floating widget and the taskbar view as set.
    void ApplyMiniViews();

    bool ApplySettings(const Settings& settings, bool autostart, std::wstring& error);
    void OpenSettings();
    void ToggleAutostart();
    void Exit();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    HPOWERNOTIFY displayNotification_ = nullptr;
    NOTIFYICONDATAW icon_{};
    Settings settings_;
    bool displayOn_ = true;
    bool exiting_ = false;

    Sampler sampler_;
    Snapshot snapshot_;
    TrayIconState iconState_;
    HICON currentIcon_ = nullptr;
    std::wstring tooltip_;
    Panel panel_;
    FloatingWidget floating_;
    TaskbarView taskbarView_;
};

}  // namespace vela
