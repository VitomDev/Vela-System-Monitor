#pragma once

#include "app/mini_views.h"
#include "app/panel.h"
#include "app/tray_icon.h"
#include "core/alerts.h"
#include "core/gpu.h"
#include "core/history.h"
#include "core/sampler.h"
#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <unordered_map>

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
    // A warning opens the top list it is about when clicked.
    void Notify(const std::wstring& message, bool warning = false);
    void TogglePanel();
    void ShowPanel(PanelView view = PanelView::Overview);

    // Reads the counters and updates only what changed.
    void Sample();
    // The system counters, plus the GPU only while something shows it.
    void ReadSnapshot(bool panelOpen);
    bool NeedsGpu(bool panelOpen) const;
    void CheckAlerts();
    void UpdateTrayIcon(bool force);
    void RestartTimer();
    // Opens or closes the floating widget and the taskbar view as set.
    void ApplyMiniViews();
    uint32_t MiniMetrics() const;

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
    GpuSampler gpuSampler_;
    std::unordered_map<uint32_t, double> gpuByProcess_;
    History history_;
    UsageAlerts alerts_;
    PanelView alertView_ = PanelView::Overview;  // What a click on the last warning opens
    ProcessSampler processSampler_;
    Snapshot snapshot_;
    TrayIconState iconState_;
    HICON currentIcon_ = nullptr;
    std::wstring tooltip_;
    Panel panel_;
    FloatingWidget floating_;
    TaskbarView taskbarView_;
};

}  // namespace vela
