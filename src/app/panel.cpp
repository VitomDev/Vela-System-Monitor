#include "app/panel.h"

#include <windowsx.h>

#include <algorithm>

namespace vela {

namespace {

constexpr wchar_t kClassName[] = L"VelaPanel";

// Layout in 96-DPI pixels.
constexpr int kWidth = 340;
constexpr int kPadding = 16;
constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 60;
constexpr int kBarHeight = 4;
constexpr int kMargin = 12;  // From the taskbar and screen edges

constexpr int kHighPercent = 90;
constexpr COLORREF kWarning = RGB(232, 72, 85);

std::wstring Percent(int value) { return std::to_wstring(value) + L"%"; }

}  // namespace

std::vector<PanelRow> BuildPanelRows(const Snapshot& s, unsigned processors, unsigned long long uptimeMs) {
    std::vector<PanelRow> rows;
    const int cpu = static_cast<int>(s.cpuPercent + 0.5);
    rows.push_back({icons::kCpu, L"CPU", Percent(cpu),
                    std::to_wstring(processors) + L" logical processors · up " +
                        FormatDuration(static_cast<long long>(uptimeMs / 1000)),
                    cpu, cpu >= kHighPercent});

    rows.push_back({icons::kMemory, L"Memory", Percent(s.MemoryPercent()),
                    FormatBytes(s.memoryUsed) + L" of " + FormatBytes(s.memoryTotal), s.MemoryPercent(),
                    s.MemoryPercent() >= kHighPercent});

    std::wstring disk = FormatBytes(s.diskUsed) + L" of " + FormatBytes(s.diskTotal);
    if (s.diskRatesKnown) disk += L" · R " + FormatRate(s.diskRead) + L" · W " + FormatRate(s.diskWrite);
    rows.push_back({icons::kDisk, L"Disk (" + std::wstring(1, s.diskLetter) + L":)", Percent(s.DiskPercent()), disk,
                    s.DiskPercent(), s.DiskPercent() >= kHighPercent});

    rows.push_back({icons::kNetwork, L"Network", L"",
                    L"↓ " + FormatRate(s.networkDown) + L"   ↑ " + FormatRate(s.networkUp), -1, false});

    if (s.battery.present) {
        std::wstring state;
        if (s.battery.charging) {
            state = L"Charging";
        } else if (s.battery.pluggedIn) {
            state = L"Plugged in";
        } else if (s.battery.secondsLeft >= 0) {
            state = FormatDuration(s.battery.secondsLeft) + L" left";
        } else {
            state = L"On battery";
        }
        rows.push_back({icons::kBattery, L"Battery", Percent(s.battery.percent), state, s.battery.percent,
                        !s.battery.pluggedIn && s.battery.percent <= 10});
    }
    return rows;
}

int Panel::Scale(int value) const { return MulDiv(value, static_cast<int>(GetDpiForWindow(window_)), 96); }

SIZE Panel::Measure() const {
    return {Scale(kWidth), Scale(kHeaderHeight) + Scale(kRowHeight) * static_cast<int>(rows_.size()) + Scale(kPadding)};
}

RECT Panel::RowRect(size_t index) const {
    const int top = Scale(kHeaderHeight) + Scale(kRowHeight) * static_cast<int>(index);
    return {0, top, Scale(kWidth), top + Scale(kRowHeight)};
}

RECT Panel::SettingsRect() const {
    const int size = Scale(32);
    const int right = Scale(kWidth) - Scale(kPadding) + Scale(8);
    return {right - size, Scale(4), right, Scale(4) + size};
}

void Panel::RefreshStyle() {
    theme_ = Theme::Current();
    for (HFONT font : {titleFont_, labelFont_, valueFont_, detailFont_, iconFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    titleFont_ = CreateUiFont(dpi, 11, FW_SEMIBOLD);
    labelFont_ = CreateUiFont(dpi, 10);
    valueFont_ = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    detailFont_ = CreateUiFont(dpi, 9);
    iconFont_ = CreateIconFont(dpi, 11);
    ApplyWindowStyle(window_, theme_);
}

void Panel::Show(HINSTANCE instance, const RECT& anchor, const Snapshot& snapshot) {
    if (window_) {
        SetForegroundWindow(window_);
        return;
    }
    static const ATOM windowClass = [instance] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!windowClass) return;

    // Created on the monitor of the tray icon so it gets that monitor's DPI.
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"Vela", WS_POPUP, work.left, work.top, 1, 1,
                              nullptr, nullptr, instance, this);
    if (!window_) return;

    processors_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    rows_ = BuildPanelRows(snapshot, processors_, GetTickCount64());
    RefreshStyle();

    // Next to the tray icon, inside the work area, like the system flyouts.
    const SIZE size = Measure();
    const int margin = Scale(kMargin);
    const int center = (anchor.left + anchor.right) / 2;
    const int x = std::clamp<int>(center - size.cx / 2, work.left + margin, work.right - margin - size.cx);
    const bool taskbarAtTop = anchor.bottom <= work.top + (work.bottom - work.top) / 2;
    const int y = taskbarAtTop ? work.top + margin : work.bottom - margin - size.cy;
    SetWindowPos(window_, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
}

void Panel::Update(const Snapshot& snapshot) {
    if (!window_) return;
    std::vector<PanelRow> rows = BuildPanelRows(snapshot, processors_, GetTickCount64());
    if (rows.size() != rows_.size()) {
        // A battery appeared or went away: resize from the bottom edge.
        rows_ = std::move(rows);
        RECT bounds;
        GetWindowRect(window_, &bounds);
        const SIZE size = Measure();
        SetWindowPos(window_, nullptr, bounds.left, bounds.bottom - size.cy, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    // Only the rows whose text or bar changed are painted again.
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i] == rows_[i]) continue;
        rows_[i] = std::move(rows[i]);
        const RECT rect = RowRect(i);
        InvalidateRect(window_, &rect, FALSE);
    }
}

void Panel::Close() {
    if (!window_) return;
    HWND window = window_;
    window_ = nullptr;
    closedAt_ = GetTickCount64();
    activated_ = settingsHot_ = false;
    DestroyWindow(window);
    for (HFONT* font : {&titleFont_, &labelFont_, &valueFont_, &detailFont_, &iconFont_}) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
    rows_.clear();
    rows_.shrink_to_fit();
    if (onClosed) onClosed();
}

// --- Painting ---------------------------------------------------------------------

void Panel::PaintRow(HDC dc, const PanelRow& row, const RECT& rect) {
    const int padding = Scale(kPadding);
    const int left = rect.left + padding;
    const int right = rect.right - padding;
    const int lineHeight = Scale(22);

    SelectObject(dc, iconFont_);
    SetTextColor(dc, row.high ? kWarning : theme_.accent);
    RECT iconRect{left, rect.top, left + Scale(20), rect.top + lineHeight};
    DrawTextW(dc, row.icon, -1, &iconRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const int textLeft = left + Scale(28);
    SelectObject(dc, labelFont_);
    SetTextColor(dc, theme_.text);
    RECT labelRect{textLeft, rect.top, right, rect.top + lineHeight};
    DrawTextW(dc, row.label.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, valueFont_);
    SetTextColor(dc, row.high ? kWarning : theme_.text);
    DrawTextW(dc, row.value.c_str(), -1, &labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, detailFont_);
    SetTextColor(dc, theme_.secondaryText);
    RECT detailRect{textLeft, rect.top + lineHeight, right, rect.top + lineHeight + Scale(18)};
    DrawTextW(dc, row.detail.c_str(), -1, &detailRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (row.barPercent >= 0) {
        const int barTop = detailRect.bottom + Scale(6);
        const int barHeight = Scale(kBarHeight);
        const RECT track{textLeft, barTop, right, barTop + barHeight};
        FillRounded(dc, track, theme_.divider, barHeight / 2);
        const int fill = (track.right - track.left) * std::clamp(row.barPercent, 0, 100) / 100;
        if (fill > 0) {
            const RECT level{track.left, track.top, track.left + std::max(fill, barHeight), track.bottom};
            FillRounded(dc, level, row.high ? kWarning : theme_.accent, barHeight / 2);
        }
    }
}

void Panel::Paint(HDC dc, const RECT& client) {
    HBRUSH background = CreateSolidBrush(theme_.background);
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, titleFont_);

    const int padding = Scale(kPadding);
    SetTextColor(dc, theme_.text);
    RECT title{padding, 0, Scale(kWidth) - padding, Scale(kHeaderHeight)};
    DrawTextW(dc, L"System", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const RECT settings = SettingsRect();
    if (settingsHot_) FillRounded(dc, settings, theme_.hover, Scale(4));
    SelectObject(dc, iconFont_);
    SetTextColor(dc, theme_.secondaryText);
    RECT settingsText = settings;
    DrawTextW(dc, icons::kSettings, -1, &settingsText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (size_t i = 0; i < rows_.size(); ++i) {
        const RECT rect = RowRect(i);
        RECT overlap;
        if (IntersectRect(&overlap, &rect, &client)) PaintRow(dc, rows_[i], rect);
    }
    SelectObject(dc, previous);
}

// --- Window procedure -------------------------------------------------------------

LRESULT CALLBACK Panel::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* panel = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        panel->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!panel || panel->window_ != window) return DefWindowProcW(window, message, wParam, lParam);
    return panel->HandleMessage(message, wParam, lParam);
}

LRESULT Panel::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window_, &paint);
            RECT client;
            GetClientRect(window_, &client);
            // Drawn off screen first so updates never flicker.
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ previous = SelectObject(memory, bitmap);
            Paint(memory, paint.rcPaint);
            BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left,
                   paint.rcPaint.bottom - paint.rcPaint.top, memory, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
            SelectObject(memory, previous);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const RECT settings = SettingsRect();
            const bool hot = PtInRect(&settings, point) != FALSE;
            if (hot != settingsHot_) {
                settingsHot_ = hot;
                InvalidateRect(window_, &settings, FALSE);
                if (hot) {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
                    TrackMouseEvent(&track);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (settingsHot_) {
                settingsHot_ = false;
                const RECT settings = SettingsRect();
                InvalidateRect(window_, &settings, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const RECT settings = SettingsRect();
            if (PtInRect(&settings, point)) {
                Close();
                if (onSettings) onSettings();
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) Close();
            return 0;
        case WM_ACTIVATE:
            // Closes when clicking anywhere else, like the system flyouts. When
            // Windows refused it the foreground it was never active: it stays.
            if (LOWORD(wParam) != WA_INACTIVE) {
                activated_ = true;
            } else if (activated_) {
                PostMessageW(window_, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_CLOSE:
            Close();
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            RefreshStyle();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
            Close();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace vela
