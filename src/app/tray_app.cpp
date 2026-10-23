#include "app/tray_app.h"

#include "app/resource.h"
#include "app/settings_dialog.h"
#include "app/theme.h"
#include "core/paths.h"

#include <commctrl.h>
#include <windowsx.h>

namespace vela {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr UINT WM_APP_OPEN_PANEL = WM_APP + 14;

constexpr UINT_PTR kSampleTimer = 1;
constexpr UINT kPanelIntervalMs = 1000;

enum MenuId {
    kMenuOpen = 1,
    kMenuFloating,
    kMenuTaskbar,
    kMenuSettings,
    kMenuAutostart,
    kMenuExit,
};

// Gives memory used by a window back to the system once Vela is idle again.
void TrimWorkingSet() { SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); }

}  // namespace

bool TrayApp::Initialize(HINSTANCE instance, bool startedAtSignIn) {
    instance_ = instance;
    settings_ = Settings::Load();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // A hidden top-level window rather than a message-only one: it must
    // receive the TaskbarCreated broadcast, and the installer finds it by class.
    window_ = CreateWindowExW(0, kTrayWindowClass, L"Vela System Monitor", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) return false;

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    // No sampling while the screen is off: nobody can see it.
    displayNotification_ = RegisterPowerSettingNotification(window_, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    EnableDarkMenus();
    panel_.onClosed = [this] {
        RestartTimer();
        TrimWorkingSet();
    };
    panel_.onSettings = [this] { PostMessageW(window_, WM_COMMAND, kMenuSettings, 0); };

    floating_.onMoved = [this](POINT position) {
        settings_.floatingPosition = position;
        settings_.Save();
    };
    floating_.onMenu = taskbarView_.onMenu = [this](POINT point) { ShowTrayMenu(point); };
    floating_.onOpenPanel = taskbarView_.onOpenPanel = [this] { TogglePanel(); };

    snapshot_ = sampler_.Sample();
    AddTrayIcon();
    ApplyMiniViews();
    RestartTimer();

    if (ConsumeFirstRun()) {
        PostMessageW(window_, WM_APP_OPEN_PANEL, 0, 0);
    } else if (!startedAtSignIn) {
        Notify(L"Vela is running in the notification area. Click its icon to see your system at a glance.");
    }
    TrimWorkingSet();
    return true;
}

TrayApp::~TrayApp() {
    if (currentIcon_) DestroyIcon(currentIcon_);
}

// --- Tray icon ---------------------------------------------------------------

void TrayApp::AddTrayIcon() {
    icon_ = {sizeof(icon_)};
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    icon_.uCallbackMessage = WM_APP_TRAY;
    tooltip_ = TooltipText(snapshot_);
    wcsncpy_s(icon_.szTip, tooltip_.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &icon_);
    icon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon_);
    UpdateTrayIcon(true);
}

void TrayApp::UpdateTrayIcon(bool force) {
    const int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(window_));
    const TrayIconState state =
        MakeTrayIconState(settings_.trayIcon, size, IsTaskbarLight(), snapshot_.cpuPercent, snapshot_.MemoryPercent());
    if (!force && state == iconState_) return;
    HICON icon = CreateTrayIcon(state);
    if (!icon) return;
    iconState_ = state;
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = window_;
    data.uID = icon_.uID;
    data.uFlags = NIF_ICON;
    data.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &data);
    if (currentIcon_) DestroyIcon(currentIcon_);
    currentIcon_ = icon;
    icon_.hIcon = icon;
}

void TrayApp::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open Vela");
    AppendMenuW(menu, MF_STRING | (settings_.floating ? MF_CHECKED : 0), kMenuFloating, L"Floating widget");
    AppendMenuW(menu, MF_STRING | (settings_.taskbar ? MF_CHECKED : 0), kMenuTaskbar, L"Show on taskbar");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit Vela");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);

    // Required so the menu closes when clicking elsewhere.
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, window_, nullptr);
    DestroyMenu(menu);
    switch (command) {
        case kMenuOpen: ShowPanel(); break;
        case kMenuFloating:
        case kMenuTaskbar: {
            Settings settings = settings_;
            bool& option = command == kMenuFloating ? settings.floating : settings.taskbar;
            option = !option;
            std::wstring error;
            if (!ApplySettings(settings, IsAutostartEnabled(), error)) Notify(error);
            break;
        }
        case kMenuSettings: OpenSettings(); break;
        case kMenuAutostart: ToggleAutostart(); break;
        case kMenuExit: Exit(); break;
    }
}

void TrayApp::Notify(const std::wstring& message) {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = window_;
    data.uID = icon_.uID;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(data.szInfoTitle, L"Vela System Monitor");
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayApp::TogglePanel() {
    if (panel_.IsOpen()) {
        panel_.Close();
    } else if (!panel_.JustClosed()) {
        // Otherwise the click on the icon took focus from the panel and closed it already.
        ShowPanel();
    }
}

void TrayApp::ShowPanel() {
    if (exiting_) return;
    if (panel_.IsOpen()) {
        SetForegroundWindow(panel_.window());
        return;
    }
    NOTIFYICONIDENTIFIER id{sizeof(id), window_, icon_.uID};
    RECT anchor;
    if (FAILED(Shell_NotifyIconGetRect(&id, &anchor))) {
        POINT cursor;
        GetCursorPos(&cursor);
        anchor = {cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    }
    snapshot_ = sampler_.Sample();
    panel_.Show(instance_, anchor, snapshot_);
    RestartTimer();
}

// --- Sampling ------------------------------------------------------------------

void TrayApp::RestartTimer() {
    KillTimer(window_, kSampleTimer);
    if (!displayOn_ || exiting_) return;
    const UINT interval = panel_.IsOpen() ? kPanelIntervalMs : settings_.intervalSeconds * 1000;
    // A generous tolerance lets Windows batch the wake-up with other timers.
    SetCoalescableTimer(window_, kSampleTimer, interval, nullptr, interval / 4);
}

void TrayApp::Sample() {
    snapshot_ = sampler_.Sample();
    UpdateTrayIcon(false);
    std::wstring tooltip = TooltipText(snapshot_);
    if (tooltip != tooltip_) {
        tooltip_ = std::move(tooltip);
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = window_;
        data.uID = icon_.uID;
        data.uFlags = NIF_TIP | NIF_SHOWTIP;
        wcsncpy_s(data.szTip, tooltip_.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
    panel_.Update(snapshot_);
    if (floating_.IsOpen() || taskbarView_.IsOpen()) {
        const MiniLines lines = MiniText(snapshot_);
        floating_.Update(lines);
        taskbarView_.Update(lines);
    }
}

void TrayApp::ApplyMiniViews() {
    if (exiting_) return;
    if (settings_.floating) {
        floating_.Show(instance_, settings_.floatingPosition, MiniText(snapshot_));
    } else {
        floating_.Close();
    }
    if (!settings_.taskbar) {
        taskbarView_.Close();
    } else if (!taskbarView_.Show(instance_, MiniText(snapshot_))) {
        Notify(L"Vela could not find the taskbar to show its numbers there.");
    }
}

// --- Settings ----------------------------------------------------------------

bool TrayApp::ApplySettings(const Settings& settings, bool autostart, std::wstring& error) {
    settings_ = settings;
    if (!settings_.Save()) {
        error = L"Could not save the settings.";
        return false;
    }
    if (autostart != IsAutostartEnabled() && !SetAutostartEnabled(autostart, ExecutablePath().wstring())) {
        error = L"Could not change Start with Windows.";
        return false;
    }
    UpdateTrayIcon(true);
    ApplyMiniViews();
    RestartTimer();
    return true;
}

void TrayApp::OpenSettings() {
    panel_.Close();
    ShowSettingsDialog(instance_, window_, settings_,
                       [this](const Settings& settings, bool autostart, std::wstring& error) {
                           return ApplySettings(settings, autostart, error);
                       });
    TrimWorkingSet();
}

void TrayApp::ToggleAutostart() {
    if (!SetAutostartEnabled(!IsAutostartEnabled(), ExecutablePath().wstring())) {
        Notify(L"Could not change Start with Windows.");
    }
}

void TrayApp::Exit() {
    exiting_ = true;
    panel_.onClosed = nullptr;
    panel_.Close();
    floating_.Close();
    taskbarView_.Close();
    KillTimer(window_, kSampleTimer);
    if (displayNotification_) UnregisterPowerSettingNotification(displayNotification_);
    displayNotification_ = nullptr;
    Shell_NotifyIconW(NIM_DELETE, &icon_);
    DestroyWindow(window_);
}

// --- Window procedure ----------------------------------------------------------

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<TrayApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        // Explorer restarted: a new taskbar, so the taskbar view moves into it.
        AddTrayIcon();
        taskbarView_.Close();
        ApplyMiniViews();
        return 0;
    }
    switch (message) {
        case WM_TIMER:
            if (wParam == kSampleTimer) Sample();
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    TogglePanel();
                    break;
                case WM_CONTEXTMENU:
                    ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                    break;
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuSettings) OpenSettings();
            return 0;
        case WM_APP_OPEN_PANEL:
        case WM_APP_ALREADY_RUNNING:
            ShowPanel();
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_POWERSETTINGCHANGE) {
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                if (setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE && setting->DataLength >= sizeof(DWORD)) {
                    const bool on = *reinterpret_cast<const DWORD*>(setting->Data) != 0;
                    if (on != displayOn_) {
                        displayOn_ = on;
                        if (on) Sample();
                        RestartTimer();
                    }
                }
            }
            return TRUE;
        case WM_SETTINGCHANGE:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            // Light or dark taskbar, or a new icon size.
            UpdateTrayIcon(true);
            taskbarView_.Redraw();
            return 0;
        case WM_CLOSE:  // Sent by the installer before upgrading or uninstalling.
        case WM_ENDSESSION:
            if (message == WM_CLOSE || wParam) Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace vela
