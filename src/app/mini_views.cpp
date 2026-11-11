#include "app/mini_views.h"

#include "app/tray_icon.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdlib>

namespace vela {

namespace {

constexpr wchar_t kFloatingClass[] = L"VelaFloating";
constexpr wchar_t kTaskbarClass[] = L"VelaTaskbar";

int Scale(UINT dpi, int value) { return MulDiv(value, static_cast<int>(dpi), 96); }

// Width of the wider line, and the height of one or both lines.
SIZE MeasureLines(HFONT font, const MiniLines& lines) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE first{}, second{};
    GetTextExtentPoint32W(dc, lines.first.c_str(), static_cast<int>(lines.first.size()), &first);
    GetTextExtentPoint32W(dc, lines.second.c_str(), static_cast<int>(lines.second.size()), &second);
    SelectObject(dc, previous);
    ReleaseDC(nullptr, dc);
    return {std::max(first.cx, second.cx), first.cy * (lines.second.empty() ? 1 : 2)};
}

// One line centered, or two lines meeting in the middle.
void DrawLines(HDC dc, const MiniLines& lines, const RECT& rect, UINT align) {
    const UINT flags = align | DT_SINGLELINE | DT_NOPREFIX;
    RECT area = rect;
    if (lines.second.empty()) {
        DrawTextW(dc, lines.first.c_str(), -1, &area, flags | DT_VCENTER);
        return;
    }
    RECT top{rect.left, rect.top, rect.right, (rect.top + rect.bottom) / 2};
    RECT bottom{rect.left, top.bottom, rect.right, rect.bottom};
    DrawTextW(dc, lines.first.c_str(), -1, &top, flags | DT_BOTTOM);
    DrawTextW(dc, lines.second.c_str(), -1, &bottom, flags | DT_TOP);
}

DWORD ReadExplorerOption(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", name,
                 RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}

// Windows 11 centers the taskbar icons and puts the Widgets button (weather)
// at the left; with left-aligned icons, or on Windows 10, Start is there.
// Both are drawn by Explorer itself, so their width is an estimate.
int LeftReserved(UINT dpi) {
    OSVERSIONINFOW version{sizeof(version)};
    using GetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static const auto getVersion =
        reinterpret_cast<GetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    const bool windows11 = getVersion && getVersion(&version) == 0 && version.dwBuildNumber >= 22000;
    if (!windows11 || ReadExplorerOption(L"TaskbarAl", 1) == 0) return Scale(dpi, 56);
    return Scale(dpi, ReadExplorerOption(L"TaskbarDa", 1) != 0 ? 170 : 8);
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
    const SIZE text = MeasureLines(font_, widest_);
    SetWindowPos(window_, nullptr, 0, 0, text.cx + 2 * Scale(12), text.cy + 2 * Scale(7),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, FALSE);
}

void FloatingWidget::Show(HINSTANCE instance, const std::optional<POINT>& position, const MiniLines& lines,
                          const MiniLines& widest) {
    if (window_) {
        Update(lines, widest);
        return;
    }
    static const ATOM windowClass = RegisterClassOnce(instance, kFloatingClass, WindowProc);
    if (!windowClass) return;
    lines_ = lines;
    widest_ = widest;

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

void FloatingWidget::Update(const MiniLines& lines, const MiniLines& widest) {
    if (!window_) return;
    if (!(widest == widest_)) {
        // Other metrics chosen: resize, keeping the top-left corner.
        widest_ = widest;
        lines_ = lines;
        RefreshStyle();
        return;
    }
    if (lines == lines_) return;
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
            SetTextColor(memory, theme_.text);
            DrawLines(memory, lines_, {padding, 0, client.right - padding, client.bottom}, DT_LEFT);
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

std::vector<HWND> FindTaskbars() {
    std::vector<HWND> taskbars;
    if (HWND main = FindWindowW(L"Shell_TrayWnd", nullptr)) taskbars.push_back(main);
    std::vector<HWND> others;
    for (HWND other = nullptr; (other = FindWindowExW(nullptr, other, L"Shell_SecondaryTrayWnd", nullptr));) {
        others.push_back(other);
    }
    std::sort(others.begin(), others.end(), [](HWND a, HWND b) {
        RECT ra, rb;
        GetWindowRect(a, &ra);
        GetWindowRect(b, &rb);
        return ra.left != rb.left ? ra.left < rb.left : ra.top < rb.top;
    });
    taskbars.insert(taskbars.end(), others.begin(), others.end());
    return taskbars;
}

bool TaskbarView::Show(HINSTANCE instance, HWND taskbar, const MiniLines& lines, const MiniLines& widest, TaskbarSpot spot,
                       int offset) {
    spot_ = spot;
    offset_ = offset;
    if (window_) {
        Update(lines, widest);
        return true;
    }
    widest_ = widest;
    dpi_ = 0;
    static const ATOM windowClass = RegisterClassOnce(instance, kTaskbarClass, WindowProc);
    taskbar_ = taskbar;
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

void TaskbarView::SetSpot(TaskbarSpot spot, int offset) {
    spot_ = spot;
    offset_ = offset;
    if (window_ && !pressed_) Place();
}

bool TaskbarView::IsHorizontal() const {
    RECT taskbar;
    GetWindowRect(taskbar_, &taskbar);
    return taskbar.right - taskbar.left >= taskbar.bottom - taskbar.top;
}

RECT TaskbarView::NotifyRect(const RECT& taskbar, UINT dpi) const {
    RECT notify;
    HWND notifyWindow = FindWindowExW(taskbar_, nullptr, L"TrayNotifyWnd", nullptr);
    if (notifyWindow && GetWindowRect(notifyWindow, &notify)) return notify;
    // Las barras secundarias no tienen esa ventana; el reloj ocupa la esquina.
    const bool horizontal = taskbar.right - taskbar.left >= taskbar.bottom - taskbar.top;
    if (horizontal) return {taskbar.right - Scale(dpi, 104), taskbar.top, taskbar.right, taskbar.bottom};
    return {taskbar.left, taskbar.bottom - Scale(dpi, 64), taskbar.right, taskbar.bottom};
}

TaskbarRoom TaskbarView::Room(const RECT& taskbar, UINT dpi) const {
    const RECT notify = NotifyRect(taskbar, dpi);
    const int end = notify.left - taskbar.left - Scale(dpi, 4);
    return {std::min(LeftReserved(dpi), end), end};
}

void TaskbarView::Place() {
    // While dragged, the mouse decides.
    if (pressed_ && dragging_) return;
    RECT taskbar;
    GetWindowRect(taskbar_, &taskbar);
    const UINT dpi = GetDpiForWindow(taskbar_);
    const RECT notify = NotifyRect(taskbar, dpi);
    if (dpi != dpi_) {
        // Measured once per DPI: this runs on every update.
        HFONT font = CreateUiFont(dpi, 9);
        textSize_ = MeasureLines(font, widest_);
        DeleteObject(font);
        dpi_ = dpi;
    }
    const SIZE text = textSize_;
    SIZE size{text.cx + Scale(dpi, 8), text.cy + Scale(dpi, 4)};
    POINT position;
    if (taskbar.right - taskbar.left >= taskbar.bottom - taskbar.top) {
        size.cy = std::max<LONG>(size.cy, taskbar.bottom - taskbar.top);
        position = {TaskbarViewLeft(spot_, Scale(dpi, offset_), size.cx, Room(taskbar, dpi)), 0};
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
    DrawLines(dc, lines_, {0, 0, size_.cx, size_.cy}, DT_RIGHT);
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

void TaskbarView::Update(const MiniLines& lines, const MiniLines& widest) {
    if (!window_) return;
    if (!IsWindow(taskbar_)) {
        Close();  // Explorer restarted; the app shows it again on TaskbarCreated.
        return;
    }
    if (!(widest == widest_)) {
        widest_ = widest;
        dpi_ = 0;  // Measure again.
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
        case WM_LBUTTONDOWN: {
            // A click opens the panel; a drag along the taskbar moves the view.
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window_, &point);
            RECT bounds;
            GetWindowRect(window_, &bounds);
            MapWindowPoints(nullptr, taskbar_, reinterpret_cast<POINT*>(&bounds), 2);
            pressed_ = true;
            dragging_ = false;
            pressX_ = point.x;
            pressLeft_ = bounds.left;
            SetCapture(window_);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!pressed_ || !IsHorizontal()) return 0;
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window_, &point);
            const int moved = point.x - pressX_;
            if (!dragging_ && std::abs(moved) < GetSystemMetrics(SM_CXDRAG)) return 0;
            dragging_ = true;
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            RECT taskbar;
            GetWindowRect(taskbar_, &taskbar);
            const TaskbarRoom room = Room(taskbar, GetDpiForWindow(taskbar_));
            const int left = TaskbarViewLeft(TaskbarSpot::Custom, pressLeft_ + moved, size_.cx, room);
            SetWindowPos(window_, HWND_TOP, left, 0, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }
        case WM_LBUTTONUP: {
            const bool dragged = dragging_;
            pressed_ = false;  // Before ReleaseCapture, which sends WM_CAPTURECHANGED
            dragging_ = false;
            ReleaseCapture();
            if (!dragged) {
                if (onOpenPanel) onOpenPanel();
                return 0;
            }
            RECT taskbar, bounds;
            GetWindowRect(taskbar_, &taskbar);
            GetWindowRect(window_, &bounds);
            const UINT dpi = GetDpiForWindow(taskbar_);
            const int left = bounds.left - taskbar.left;
            spot_ = SnapTaskbarDrop(left, size_.cx, Room(taskbar, dpi), Scale(dpi, 24));
            offset_ = MulDiv(left, 96, static_cast<int>(dpi));
            Place();
            if (onPlaced) onPlaced(spot_, offset_);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (pressed_) {
                // Lost the mouse mid-drag: back where it was.
                pressed_ = dragging_ = false;
                Place();
            }
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
