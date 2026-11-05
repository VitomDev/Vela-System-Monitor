#include "app/settings_dialog.h"

#include "app/resource.h"
#include "app/theme.h"
#include "version.h"

#include <commctrl.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace vela {

namespace {

constexpr wchar_t kClassName[] = L"VelaSettings";

enum ControlId {
    kTrayIconButton = 100,
    kIntervalButton,
    kFloatingToggle,
    kTaskbarToggle,
    kTaskbarSpotButton,
    kAutostartToggle,
    kAlertsToggle,
    kCancel = IDCANCEL,
    kSave = IDOK,
    // One chip per metric, in kChips order.
    kPanelChipFirst = 200,
    kMiniChipFirst = 220,
};

struct Chip {
    uint32_t metric;
    const wchar_t* label;
};
constexpr Chip kChips[] = {{metric::kCpu, L"CPU"},
                           {metric::kGpu, L"GPU"},
                           {metric::kMemory, L"Memory"},
                           {metric::kDisk, L"Disk"},
                           {metric::kNetwork, L"Network"},
                           {metric::kBattery, L"Battery"}};
constexpr int kChipCount = static_cast<int>(std::size(kChips));

// Layout in 96-DPI pixels.
constexpr int kWidth = 500;
constexpr int kPadding = 24;
constexpr int kRowHeight = 36;
constexpr int kFieldWidth = 200;
constexpr int kFieldHeight = 32;
constexpr int kSectionGap = 18;
constexpr int kRadius = 4;

enum class TextStyle { Title, Caption, Label, Hint };

struct TextItem {
    RECT rect;
    std::wstring text;
    TextStyle style;
};

struct State {
    HWND window = nullptr;
    Settings settings;
    ApplySettings apply;
    bool autostart = false;
    std::wstring status;
    bool done = false;

    Theme theme{};
    HBRUSH background = nullptr;
    HFONT titleFont = nullptr;
    HFONT captionFont = nullptr;
    HFONT textFont = nullptr;
    HFONT iconFont = nullptr;
    std::vector<TextItem> texts;
    RECT statusRect{};
};

HWND openWindow = nullptr;

int Scale(HWND window, int value) { return MulDiv(value, GetDpiForWindow(window), 96); }

const wchar_t* TaskbarSpotName(TaskbarSpot spot) {
    switch (spot) {
        case TaskbarSpot::Clock: return L"Next to the clock";
        case TaskbarSpot::Left: return L"At the left";
        case TaskbarSpot::Custom: return L"Where I dragged it";
    }
    return L"";
}

std::wstring IntervalName(DWORD seconds) { return seconds == 1 ? L"Every second" : L"Every " + std::to_wstring(seconds) + L" seconds"; }

// --- Style and layout ------------------------------------------------------------

void RefreshStyle(State& state) {
    state.theme = Theme::Current();
    if (state.background) DeleteObject(state.background);
    state.background = CreateSolidBrush(state.theme.background);
    for (HFONT font : {state.titleFont, state.captionFont, state.textFont, state.iconFont}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(state.window);
    state.titleFont = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    state.captionFont = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    state.textFont = CreateUiFont(dpi, 10);
    state.iconFont = CreateIconFont(dpi, 9);
    ApplyWindowStyle(state.window, state.theme);
}

void Layout(State& state) {
    HWND window = state.window;
    const auto scale = [&](int value) { return Scale(window, value); };
    const int width = scale(kWidth);
    const int padding = scale(kPadding);
    const int row = scale(kRowHeight);
    const int fieldWidth = scale(kFieldWidth);
    const int fieldHeight = scale(kFieldHeight);
    const auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(GetDlgItem(window, id), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    state.texts.clear();
    const auto text = [&](int x, int y, int w, int h, std::wstring value, TextStyle style) {
        state.texts.push_back({{x, y, x + w, y + h}, std::move(value), style});
    };
    const auto labelRow = [&](int y, const wchar_t* label, int controlId, int controlWidth, int controlHeight) {
        text(padding, y, width - 2 * padding - controlWidth, row, label, TextStyle::Label);
        place(controlId, width - padding - controlWidth, y + (row - controlHeight) / 2, controlWidth, controlHeight);
    };

    int y = padding;
    text(padding, y, width - 2 * padding, scale(32), L"Settings", TextStyle::Title);
    y += scale(32) + scale(kSectionGap) - scale(6);

    text(padding, y, width - 2 * padding, scale(24), L"Monitoring", TextStyle::Caption);
    y += scale(24);
    labelRow(y, L"Tray icon shows", kTrayIconButton, fieldWidth, fieldHeight);
    y += row;
    labelRow(y, L"Update", kIntervalButton, fieldWidth, fieldHeight);
    y += row;
    y += scale(kSectionGap);

    // A row of chips, each one a metric to show or hide.
    const auto chipRow = [&](int y, int firstId) {
        int x = padding;
        for (int i = 0; i < kChipCount; ++i) {
            const int chipWidth = TextWidth(window, state.textFont, kChips[i].label) + scale(24);
            place(firstId + i, x, y, chipWidth, scale(30));
            x += chipWidth + scale(8);
        }
    };

    text(padding, y, width - 2 * padding, scale(24), L"What to show", TextStyle::Caption);
    y += scale(24);
    text(padding, y, width - 2 * padding, scale(28), L"In the panel", TextStyle::Label);
    y += scale(28);
    chipRow(y, kPanelChipFirst);
    y += scale(30) + scale(10);
    text(padding, y, width - 2 * padding, scale(28), L"On the taskbar and in the floating widget", TextStyle::Label);
    y += scale(28);
    chipRow(y, kMiniChipFirst);
    y += scale(30) + scale(kSectionGap);

    text(padding, y, width - 2 * padding, scale(24), L"Always visible", TextStyle::Caption);
    y += scale(24);
    labelRow(y, L"Floating widget above other windows", kFloatingToggle, scale(44), scale(20));
    y += row;
    labelRow(y, L"Show on the taskbar", kTaskbarToggle, scale(44), scale(20));
    y += row;
    labelRow(y, L"Position on the taskbar", kTaskbarSpotButton, fieldWidth, fieldHeight);
    y += row;
    y += scale(kSectionGap);

    text(padding, y, width - 2 * padding, scale(24), L"General", TextStyle::Caption);
    y += scale(24);
    labelRow(y, L"Start with Windows", kAutostartToggle, scale(44), scale(20));
    y += row;
    labelRow(y, L"Warn when CPU, GPU or memory stay at 90%", kAlertsToggle, scale(44), scale(20));
    y += row + scale(12);

    state.statusRect = {padding, y, width - padding, y + scale(20)};
    y += scale(20) + scale(16);

    const int buttonWidth = scale(96);
    text(padding, y, width - 2 * padding - 2 * buttonWidth - scale(16), fieldHeight,
         L"Vela " VELA_VERSION_STRING L" \u00B7 VitomDev", TextStyle::Hint);
    place(kSave, width - padding - buttonWidth, y, buttonWidth, fieldHeight);
    place(kCancel, width - padding - 2 * buttonWidth - scale(8), y, buttonWidth, fieldHeight);
    y += fieldHeight + padding;

    // Grow the window around the client area.
    RECT frame{0, 0, width, y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window, GWL_STYLE), FALSE, GetWindowLongW(window, GWL_EXSTYLE),
                             GetDpiForWindow(window));
    SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window, nullptr, TRUE);
}

// --- Painting --------------------------------------------------------------------

void Paint(State& state) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(state.window, &paint);
    RECT client;
    GetClientRect(state.window, &client);
    FillRect(dc, &client, state.background);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, state.textFont);
    for (auto& item : state.texts) {
        COLORREF color = state.theme.text;
        HFONT font = state.textFont;
        switch (item.style) {
            case TextStyle::Title: font = state.titleFont; break;
            case TextStyle::Caption: font = state.captionFont; break;
            case TextStyle::Label: break;
            case TextStyle::Hint: color = state.theme.secondaryText; break;
        }
        SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, item.text.c_str(), -1, &item.rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (!state.status.empty()) {
        SelectObject(dc, state.textFont);
        SetTextColor(dc, RGB(232, 72, 85));
        DrawTextW(dc, state.status.c_str(), -1, &state.statusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, previous);
    EndPaint(state.window, &paint);
}

void DrawToggle(State& state, const DRAWITEMSTRUCT& item, bool on) {
    // Switch: a pill with a knob, accent when on.
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const Theme& theme = state.theme;
    FillRect(dc, &rect, state.background);
    const int height = rect.bottom - rect.top;
    if (on) {
        FillRounded(dc, rect, theme.accent, height / 2);
    } else {
        if (IsHot(item.hwndItem)) FillRounded(dc, rect, theme.hover, height / 2);
        OutlineRounded(dc, rect, theme.secondaryText, height / 2);
    }
    const int knob = Scale(state.window, 12);
    const int knobX = on ? rect.right - Scale(state.window, 4) - knob : rect.left + Scale(state.window, 4);
    RECT knobRect{knobX, rect.top + (height - knob) / 2, knobX + knob, rect.top + (height + knob) / 2};
    FillRounded(dc, knobRect, on ? theme.accentText : theme.secondaryText, knob / 2);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, 2, 2);
        DrawFocusRect(dc, &focus);
    }
}

// Input-like field with a chevron that opens a menu of choices.
void DrawChoice(State& state, const DRAWITEMSTRUCT& item, const std::wstring& label) {
    const Theme& theme = state.theme;
    const int radius = Scale(state.window, kRadius);
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    FillRect(dc, &rect, state.background);
    FillRounded(dc, rect, IsHot(item.hwndItem) ? theme.hover : theme.pressed, radius);
    OutlineRounded(dc, rect, theme.divider, radius);
    SetBkMode(dc, TRANSPARENT);
    RECT inner{rect.left + Scale(state.window, 12), rect.top, rect.right - Scale(state.window, 12), rect.bottom};
    HGDIOBJ previous = SelectObject(dc, state.textFont);
    SetTextColor(dc, theme.text);
    DrawTextW(dc, label.c_str(), -1, &inner, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, state.iconFont);
    SetTextColor(dc, theme.secondaryText);
    DrawTextW(dc, icons::kChevronDown, -1, &inner, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previous);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(dc, &focus);
    }
}

// A pill that is accent-filled with a check while its metric is shown.
void DrawChip(State& state, const DRAWITEMSTRUCT& item, const wchar_t* label, bool on) {
    const Theme& theme = state.theme;
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const int radius = (rect.bottom - rect.top) / 2;
    FillRect(dc, &rect, state.background);
    if (on) {
        COLORREF fill = theme.accent;
        if (IsHot(item.hwndItem)) fill = RGB(GetRValue(fill) * 9 / 10, GetGValue(fill) * 9 / 10, GetBValue(fill) * 9 / 10);
        FillRounded(dc, rect, fill, radius);
    } else {
        FillRounded(dc, rect, IsHot(item.hwndItem) ? theme.hover : theme.pressed, radius);
        OutlineRounded(dc, rect, theme.divider, radius);
    }
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, state.textFont);
    SetTextColor(dc, on ? theme.accentText : theme.text);
    RECT text = rect;
    DrawTextW(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(dc, &focus);
    }
}

// The metric set a chip id belongs to, and its index; null when not a chip.
uint32_t* ChipMetrics(State& state, int id, int& index) {
    if (id >= kPanelChipFirst && id < kPanelChipFirst + kChipCount) {
        index = id - kPanelChipFirst;
        return &state.settings.panelMetrics;
    }
    if (id >= kMiniChipFirst && id < kMiniChipFirst + kChipCount) {
        index = id - kMiniChipFirst;
        return &state.settings.miniMetrics;
    }
    return nullptr;
}

void DrawControl(State& state, const DRAWITEMSTRUCT& item) {
    const ButtonFonts fonts{state.textFont, state.iconFont};
    const int radius = Scale(state.window, kRadius);
    int chip = 0;
    if (const uint32_t* metrics = ChipMetrics(state, static_cast<int>(item.CtlID), chip)) {
        DrawChip(state, item, kChips[chip].label, (*metrics & kChips[chip].metric) != 0);
        return;
    }
    switch (static_cast<int>(item.CtlID)) {
        case kTrayIconButton: DrawChoice(state, item, TrayIconName(state.settings.trayIcon)); break;
        case kIntervalButton: DrawChoice(state, item, IntervalName(state.settings.intervalSeconds)); break;
        case kFloatingToggle: DrawToggle(state, item, state.settings.floating); break;
        case kTaskbarToggle: DrawToggle(state, item, state.settings.taskbar); break;
        case kTaskbarSpotButton: DrawChoice(state, item, TaskbarSpotName(state.settings.taskbarSpot)); break;
        case kAutostartToggle: DrawToggle(state, item, state.autostart); break;
        case kAlertsToggle: DrawToggle(state, item, state.settings.alerts); break;
        case kSave: DrawButton(item, state.theme, fonts, ButtonStyle::Primary, nullptr, L"Save", radius); break;
        case kCancel: DrawButton(item, state.theme, fonts, ButtonStyle::Standard, nullptr, L"Cancel", radius); break;
    }
}

// --- Actions ---------------------------------------------------------------------

void Save(State& state) {
    std::wstring error;
    if (!state.apply(state.settings, state.autostart, error)) {
        state.status = error;
        InvalidateRect(state.window, &state.statusRect, TRUE);
        return;
    }
    state.done = true;
}

// Shows `labels` under a field; returns the chosen index or -1.
int ShowChoiceMenu(State& state, int fieldId, const std::vector<std::wstring>& labels, size_t checked) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < labels.size(); ++i) {
        AppendMenuW(menu, MF_STRING | (i == checked ? MF_CHECKED : 0), i + 1, labels[i].c_str());
    }
    RECT field;
    GetWindowRect(GetDlgItem(state.window, fieldId), &field);
    const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, field.left, field.bottom,
                                        state.window, nullptr);
    DestroyMenu(menu);
    InvalidateRect(GetDlgItem(state.window, fieldId), nullptr, FALSE);
    return chosen - 1;
}

void ChooseTrayIcon(State& state) {
    std::vector<std::wstring> labels;
    size_t checked = 0;
    for (size_t i = 0; i < kTrayIcons.size(); ++i) {
        labels.push_back(TrayIconName(kTrayIcons[i]));
        if (kTrayIcons[i] == state.settings.trayIcon) checked = i;
    }
    const int chosen = ShowChoiceMenu(state, kTrayIconButton, labels, checked);
    if (chosen >= 0) state.settings.trayIcon = kTrayIcons[chosen];
}

void ChooseInterval(State& state) {
    std::vector<std::wstring> labels;
    size_t checked = 0;
    for (size_t i = 0; i < kIntervals.size(); ++i) {
        labels.push_back(IntervalName(kIntervals[i]));
        if (kIntervals[i] == state.settings.intervalSeconds) checked = i;
    }
    const int chosen = ShowChoiceMenu(state, kIntervalButton, labels, checked);
    if (chosen >= 0) state.settings.intervalSeconds = kIntervals[chosen];
}

void ChooseTaskbarSpot(State& state) {
    // "Where I dragged it" is only offered once there is such a place.
    std::vector<TaskbarSpot> spots{TaskbarSpot::Clock, TaskbarSpot::Left};
    if (state.settings.taskbarSpot == TaskbarSpot::Custom) spots.push_back(TaskbarSpot::Custom);
    std::vector<std::wstring> labels;
    size_t checked = 0;
    for (size_t i = 0; i < spots.size(); ++i) {
        labels.push_back(TaskbarSpotName(spots[i]));
        if (spots[i] == state.settings.taskbarSpot) checked = i;
    }
    const int chosen = ShowChoiceMenu(state, kTaskbarSpotButton, labels, checked);
    if (chosen >= 0) state.settings.taskbarSpot = spots[chosen];
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_COMMAND: {
            int chip = 0;
            if (uint32_t* metrics = ChipMetrics(*state, LOWORD(wParam), chip)) {
                const uint32_t toggled = *metrics ^ kChips[chip].metric;
                if (toggled == 0) {
                    state->status = L"Keep at least one thing to show.";
                } else {
                    *metrics = toggled;
                    state->status.clear();
                }
                InvalidateRect(state->window, &state->statusRect, TRUE);
                InvalidateRect(GetDlgItem(window, LOWORD(wParam)), nullptr, FALSE);
                return 0;
            }
            switch (LOWORD(wParam)) {
                case kCancel: state->done = true; break;
                case kSave: Save(*state); break;
                case kTrayIconButton: ChooseTrayIcon(*state); break;
                case kIntervalButton: ChooseInterval(*state); break;
                case kTaskbarSpotButton: ChooseTaskbarSpot(*state); break;
                case kFloatingToggle:
                case kTaskbarToggle:
                case kAutostartToggle:
                case kAlertsToggle: {
                    const int id = LOWORD(wParam);
                    bool& option = id == kFloatingToggle ? state->settings.floating
                                   : id == kTaskbarToggle ? state->settings.taskbar
                                   : id == kAlertsToggle  ? state->settings.alerts
                                                          : state->autostart;
                    option = !option;
                    InvalidateRect(GetDlgItem(window, id), nullptr, FALSE);
                    break;
                }
            }
            return 0;
        }
        case WM_DRAWITEM:
            DrawControl(*state, *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT:
            Paint(*state);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETTINGCHANGE:
            RefreshStyle(*state);
            Layout(*state);
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshStyle(*state);
            Layout(*state);
            return 0;
        }
        case WM_CLOSE:
            state->done = true;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

void ShowSettingsDialog(HINSTANCE instance, HWND owner, const Settings& current, const ApplySettings& apply) {
    if (openWindow) {
        SetForegroundWindow(openWindow);
        return;
    }
    static const ATOM windowClass = [instance] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_VELA));
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!windowClass) return;

    State state;
    state.settings = current;
    state.apply = apply;
    state.autostart = IsAutostartEnabled();

    // Open on the monitor with the mouse, roughly centered.
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"Vela Settings",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, monitor.rcWork.left,
                                  monitor.rcWork.top, 100, 100, owner, nullptr, instance, &state);
    if (!window) return;
    state.window = window;
    openWindow = window;

    std::vector<int> ids{kTrayIconButton, kIntervalButton};
    for (int i = 0; i < kChipCount; ++i) ids.push_back(kPanelChipFirst + i);
    for (int i = 0; i < kChipCount; ++i) ids.push_back(kMiniChipFirst + i);
    ids.insert(ids.end(), {kFloatingToggle, kTaskbarToggle, kTaskbarSpotButton, kAutostartToggle, kAlertsToggle, kCancel, kSave});
    for (int id : ids) {
        HWND control = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        EnableHover(control);
    }

    RefreshStyle(state);
    Layout(state);
    RECT bounds;
    GetWindowRect(window, &bounds);
    const RECT& work = monitor.rcWork;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    SetWindowPos(window, HWND_TOP, work.left + (work.right - work.left - width) / 2,
                 std::max<int>(work.top, work.top + (work.bottom - work.top - height) / 2), 0, 0, SWP_NOSIZE);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    // No focus outline until the keyboard is used, like native dialogs.
    SendMessageW(window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

    // Modal loop; the window also closes if Vela exits meanwhile.
    MSG message;
    while (!state.done && IsWindow(window)) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    openWindow = nullptr;
    if (IsWindow(window)) DestroyWindow(window);
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(state.background), static_cast<HGDIOBJ>(state.titleFont),
                           static_cast<HGDIOBJ>(state.captionFont), static_cast<HGDIOBJ>(state.textFont),
                           static_cast<HGDIOBJ>(state.iconFont)}) {
        if (object) DeleteObject(object);
    }
}

}  // namespace vela
