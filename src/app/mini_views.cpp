#include "app/mini_views.h"

#include "app/tray_icon.h"

#include <windowsx.h>

#include <algorithm>

namespace vela {

namespace {

constexpr wchar_t kFloatingClass[] = L"VelaFloating";
constexpr wchar_t kTaskbarClass[] = L"VelaTaskbar";

int Scale(UINT dpi, int value) { return MulDiv(value, static_cast<int>(dpi), 96); }

SIZE MeasureLines(HFONT font, const MiniLines& lines) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE usage{}, network{};
    GetTextExtentPoint32W(dc, lines.usage.c_str(), static_cast<int>(lines.usage.size()), &usage);
    GetTextExtentPoint32W(dc, lines.network.c_str(), static_cast<int>(lines.network.size()), &network);
    SelectObject(dc, previous);
    ReleaseDC(nullptr, dc);
    return {std::max(usage.cx, network.cx), usage.cy};
}

ATOM RegisterClassOnce(HINSTANCE instance, const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = name;
    return RegisterClassExW(&wc);
}

template <typename View>
View* Attach(HWND window, UINT message, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* view = static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }
    return reinterpret_cast<View*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

}  // namespace

// --- Floating widget ---------------------------------------------------------------

int FloatingWidget::Scale(int value) const { return vela::Scale(GetDpiForWindow(window_), value); }

void FloatingWidget::RefreshStyle() {
    theme_ = Theme::Current();
    if (font_) DeleteObject(font_);
    font_ = CreateUiFont(GetDpiForWindow(window_), 9, FW_SEMIBOLD);
    ApplyWindowStyle(window_, theme_);
    const SIZE text = MeasureLines(font_, WidestMiniText());
    RECT bounds;
    GetWindowRect(window_, &bounds);
    SetWindowPos(window_, nullptr, 0, 0, text.cx + 2 * Scale(12), 2 * text.cy + 2 * Scale(7),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, FALSE);
}

void FloatingWidget::Show(HINSTANCE instance, const std::optional<POINT>& position, const MiniLines& lines) {
    if (window_) return;
    static const ATOM windowClass = RegisterClassOnce(instance, kFloatingClass, WindowProc);
    if (!windowClass) return;
    lines_ = lines;

    // Back where it was left, or near the bottom-right corner of the main
    // screen, away from title bar buttons.
    POINT origin{0, 0};
    if (position) origin = *position;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(origin, position ? MONITOR_DEFAULTTONEAREST : MONITOR_DEFAULTTOPRIMARY), &monitor);
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kFloatingClass, L"Vela", WS_POPUP,
                              monitor.rcWork.left, monitor.rcWork.top, 1, 1, nullptr, nullptr, instance, this);
    if (!window_) return;
    RefreshStyle();

    RECT bounds;
    GetWindowRect(window_, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const RECT& work = monitor.rcWork;
    if (!position) origin = {work.right - width - Scale(24), work.bottom - height - Scale(24)};
    // Always fully on screen, even if a monitor was removed since.
    origin.x = std::clamp<LONG>(origin.x, work.left, work.right - width);
    origin.y = std::clamp<LONG>(origin.y, work.top, work.bottom - height);
    SetWindowPos(window_, HWND_TOPMOST, origin.x, origin.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void FloatingWidget::Update(const MiniLines& lines) {
    if (!window_ || lines == lines_) return;
    lines_ = lines;
    InvalidateRect(window_, nullptr, FALSE);
}

void FloatingWidget::Close() {
    if (!window_) return;
    DestroyWindow(window_);
    window_ = nullptr;
    if (font_) DeleteObject(font_);
    font_ = nullptr;
}

LRESULT CALLBACK FloatingWidget::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* widget = Attach<FloatingWidget>(window, message, lParam);
    if (message == WM_NCCREATE && widget) widget->window_ = window;
    if (!widget || widget->window_ != window) return DefWindowProcW(window, message, wParam, lParam);
    return widget->HandleMessage(message, wParam, lParam);
}

LRESULT FloatingWidget::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window_, &paint);
            RECT client;
            GetClientRect(window_, &client);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ previousBitmap = SelectObject(memory, bitmap);
            HBRUSH background = CreateSolidBrush(theme_.background);
            FillRect(memory, &client, background);
            DeleteObject(background);
            SetBkMode(memory, TRANSPARENT);
            HGDIOBJ previousFont = SelectObject(memory, font_);
            const int padding = Scale(12);
            RECT top{padding, 0, client.right - padding, client.bottom / 2};
            RECT bottom{padding, client.bottom / 2, client.right - padding, client.bottom};
            SetTextColor(memory, theme_.text);
            DrawTextW(memory, lines_.usage.c_str(), -1, &top, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(memory, theme_.secondaryText);
            DrawTextW(memory, lines_.network.c_str(), -1, &bottom, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previousFont);
            SelectObject(memory, previousBitmap);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            // The whole widget is a handle to drag it.
            return HTCAPTION;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCLBUTTONDBLCLK:
            if (onOpenPanel) onOpenPanel();
            return 0;
        case WM_NCRBUTTONUP:
            if (onMenu) onMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_EXITSIZEMOVE: {
            RECT bounds;
            GetWindowRect(window_, &bounds);
            if (onMoved) onMoved({bounds.left, bounds.top});
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_DPICHANGED:
            RefreshStyle();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

// --- Taskbar view ------------------------------------------------------------------

bool TaskbarView::Show(HINSTANCE instance, const MiniLines& lines) {
    if (window_) return true;
    static const ATOM windowClass = RegisterClassOnce(instance, kTaskbarClass, WindowProc);
    taskbar_ = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!windowClass || !taskbar_) return false;
    lines_ = lines;
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kTaskbarClass, L"Vela", WS_POPUP, 0, 0, 1,
                              1, nullptr, nullptr, instance, this);
    if (!window_) return false;
    // Inside the taskbar it stays above it and moves with it.
    SetWindowLongPtrW(window_, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
    if (!SetParent(window_, taskbar_)) {
        Close();
        return false;
    }
    Place();
    Render();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    return true;
}

void TaskbarView::Place() {
    RECT taskbar, notify;
    GetWindowRect(taskbar_, &taskbar);
    HWND notifyWindow = FindWindowExW(taskbar_, nullptr, L"TrayNotifyWnd", nullptr);
    if (!notifyWindow || !GetWindowRect(notifyWindow, &notify)) notify = {taskbar.right, taskbar.top, taskbar.right, taskbar.bottom};

    const UINT dpi = GetDpiForWindow(taskbar_);
    if (dpi != dpi_) {
        // Measured once per DPI: this runs on every update.
        HFONT font = CreateUiFont(dpi, 9);
        textSize_ = MeasureLines(font, WidestMiniText());
        DeleteObject(font);
        dpi_ = dpi;
    }
    const SIZE text = textSize_;
    SIZE size{text.cx + Scale(dpi, 8), 2 * text.cy + Scale(dpi, 4)};
    POINT position;
    if (taskbar.right - taskbar.left >= taskbar.bottom - taskbar.top) {
        size.cy = std::max<LONG>(size.cy, taskbar.bottom - taskbar.top);
        position = {notify.left - taskbar.left - size.cx - Scale(dpi, 4), 0};
    } else {
        // Vertical taskbar: above the notification area.
        size.cx = taskbar.right - taskbar.left;
        position = {0, notify.top - taskbar.top - size.cy};
    }
    RECT current;
    GetWindowRect(window_, &current);
    MapWindowPoints(nullptr, taskbar_, reinterpret_cast<POINT*>(&current), 2);
    if (size.cx == size_.cx && size.cy == size_.cy && current.left == position.x && current.top == position.y) return;
    size_ = size;
    SetWindowPos(window_, HWND_TOP, position.x, position.y, size.cx, size.cy, SWP_NOACTIVATE);
}

void TaskbarView::Render() {
    if (size_.cx <= 0 || size_.cy <= 0) return;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size_.cx, -size_.cy, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);

    // White text on black, then its brightness becomes the alpha of the
    // taskbar text color: per-pixel transparency over any taskbar material.
    LOGFONTW logFont{};
    logFont.lfHeight = -MulDiv(9, static_cast<int>(GetDpiForWindow(taskbar_)), 72);
    logFont.lfWeight = FW_NORMAL;
    logFont.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(logFont.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&logFont);
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT top{0, 0, size_.cx, size_.cy / 2};
    RECT bottom{0, size_.cy / 2, size_.cx, size_.cy};
    DrawTextW(dc, lines_.usage.c_str(), -1, &top, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(dc, lines_.network.c_str(), -1, &bottom, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();

    const int color = IsTaskbarLight() ? 0 : 255;
    auto* pixels = static_cast<uint32_t*>(bits);
    for (int i = 0; i < size_.cx * size_.cy; ++i) {
        const uint32_t p = pixels[i];
        // Alpha 1 instead of 0 keeps the whole area clickable but invisible.
        const int alpha = std::max<int>(1, std::max({p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF}));
        const uint32_t c = static_cast<uint32_t>(color * alpha / 255);
        pixels[i] = (static_cast<uint32_t>(alpha) << 24) | (c << 16) | (c << 8) | c;
    }

    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window_, screen, nullptr, &size_, dc, &source, 0, &blend, ULW_ALPHA);
    SelectObject(dc, previousFont);
    SelectObject(dc, previousBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}

void TaskbarView::Update(const MiniLines& lines) {
    if (!window_) return;
    if (!IsWindow(taskbar_)) {
        Close();  // Explorer restarted; the app shows it again on TaskbarCreated.
        return;
    }
    const SIZE previous = size_;
    Place();
    if (lines == lines_ && previous.cx == size_.cx && previous.cy == size_.cy) return;
    lines_ = lines;
    Render();
}

void TaskbarView::Close() {
    if (!window_) return;
    HWND window = window_;
    window_ = nullptr;
    DestroyWindow(window);
}

LRESULT CALLBACK TaskbarView::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* view = Attach<TaskbarView>(window, message, lParam);
    if (message == WM_NCCREATE && view) view->window_ = window;
    if (!view || view->window_ != window) return DefWindowProcW(window, message, wParam, lParam);
    return view->HandleMessage(message, wParam, lParam);
}

LRESULT TaskbarView::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONUP:
            if (onOpenPanel) onOpenPanel();
            return 0;
        case WM_RBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window_, &point);
            if (onMenu) onMenu(point);
            return 0;
        }
        case WM_SETTINGCHANGE:
            Render();  // Light or dark taskbar
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace vela
