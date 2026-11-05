#include "app/panel.h"

#include <windowsx.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>

namespace vela {

namespace {

constexpr wchar_t kClassName[] = L"VelaPanel";

// Layout in 96-DPI pixels.
constexpr int kWidth = 340;
constexpr int kPadding = 16;
constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 64;
constexpr int kLineHeight = 30;
constexpr int kFooterHeight = 28;
constexpr int kBarHeight = 4;
constexpr int kMargin = 12;  // From the taskbar and screen edges

constexpr int kHighPercent = 90;
constexpr COLORREF kWarning = RGB(232, 72, 85);

// Hot spots besides the overview rows, which use their index.
constexpr int kHotSettings = 100;
constexpr int kHotBack = 101;

std::wstring Percent(int value) { return std::to_wstring(value) + L"%"; }

const wchar_t* ViewTitle(PanelView view) {
    switch (view) {
        case PanelView::Cpu: return L"CPU \u00B7 top processes";
        case PanelView::Gpu: return L"GPU \u00B7 top processes";
        case PanelView::Memory: return L"Memory \u00B7 top processes";
        case PanelView::Disk: return L"Disk activity \u00B7 top processes";
        case PanelView::Overview: break;
    }
    return L"System";
}

}  // namespace

std::vector<PanelRow> BuildPanelRows(const Snapshot& s, unsigned processors, unsigned long long uptimeMs,
                                     uint32_t metrics, const History* history) {
    std::vector<PanelRow> rows;
    const auto graph = [history](HistorySeries series) { return history ? history->Values(series) : std::vector<float>{}; };

    if (metrics & metric::kCpu) {
        const int cpu = static_cast<int>(s.cpuPercent + 0.5);
        rows.push_back({icons::kCpu, L"CPU", Percent(cpu),
                        std::to_wstring(processors) + L" logical processors \u00B7 up " +
                            FormatDuration(static_cast<long long>(uptimeMs / 1000)),
                        cpu, cpu >= kHighPercent, PanelView::Cpu, graph(HistorySeries::Cpu)});
    }
    if ((metrics & metric::kGpu) && s.gpu.present) {
        const int gpu = static_cast<int>(s.gpu.percent + 0.5);
        const std::wstring detail = s.gpu.memoryTotal ? L"Video memory " + FormatBytes(s.gpu.memoryUsed) + L" of " +
                                                            FormatBytes(s.gpu.memoryTotal)
                                                      : L"Busiest engine";
        rows.push_back({icons::kGpu, L"GPU", Percent(gpu), detail, gpu, gpu >= kHighPercent, PanelView::Gpu,
                        graph(HistorySeries::Gpu)});
    }
    if (metrics & metric::kMemory) {
        rows.push_back({icons::kMemory, L"Memory", Percent(s.MemoryPercent()),
                        FormatBytes(s.memoryUsed) + L" of " + FormatBytes(s.memoryTotal), s.MemoryPercent(),
                        s.MemoryPercent() >= kHighPercent, PanelView::Memory, graph(HistorySeries::Memory)});
    }
    if (metrics & metric::kDisk) {
        std::wstring disk = FormatBytes(s.diskUsed) + L" of " + FormatBytes(s.diskTotal);
        if (s.diskRatesKnown) disk += L" \u00B7 R " + FormatRate(s.diskRead) + L" \u00B7 W " + FormatRate(s.diskWrite);
        rows.push_back({icons::kDisk, L"Disk (" + std::wstring(1, s.diskLetter) + L":)", Percent(s.DiskPercent()), disk,
                        s.DiskPercent(), s.DiskPercent() >= kHighPercent, PanelView::Disk});
    }
    if (metrics & metric::kNetwork) {
        rows.push_back({icons::kNetwork, L"Network", L"",
                        L"\u2193 " + FormatRate(s.networkDown) + L"   \u2191 " + FormatRate(s.networkUp), -1, false,
                        PanelView::Overview, graph(HistorySeries::Network)});
    }

    if ((metrics & metric::kBattery) && s.battery.present) {
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
    // Nothing could be shown (a battery or GPU this PC does not have): CPU at least.
    if (rows.empty() && metrics != metric::kCpu) return BuildPanelRows(s, processors, uptimeMs, metric::kCpu, history);
    return rows;
}

std::vector<ProcessLine> BuildProcessLines(const std::vector<ProcessUsage>& processes, PanelView view, uint64_t memoryTotal) {
    ProcessMetric metric = ProcessMetric::Cpu;
    if (view == PanelView::Memory) metric = ProcessMetric::Memory;
    if (view == PanelView::Disk) metric = ProcessMetric::Io;
    if (view == PanelView::Gpu) metric = ProcessMetric::Gpu;
    const std::vector<ProcessUsage> top = TopProcesses(processes, metric, kTopProcesses);
    const uint64_t busiest = top.empty() ? 0 : top.front().ioRate;

    std::vector<ProcessLine> lines;
    for (const ProcessUsage& process : top) {
        ProcessLine line;
        line.name = process.count > 1 ? process.name + L" (" + std::to_wstring(process.count) + L")" : process.name;
        line.exe = process.name;
        line.pids = process.pids;
        switch (metric) {
            case ProcessMetric::Cpu:
            case ProcessMetric::Gpu: {
                // Tenths below 10%, where most processes are.
                const double percent = metric == ProcessMetric::Cpu ? process.cpuPercent : process.gpuPercent;
                wchar_t text[16];
                swprintf_s(text, percent < 9.95 ? L"%.1f%%" : L"%.0f%%", percent);
                line.value = text;
                line.barPercent = static_cast<int>(std::lround(percent));
                break;
            }
            case ProcessMetric::Memory:
                line.value = FormatBytes(process.memory);
                line.barPercent = memoryTotal ? static_cast<int>(process.memory * 100 / memoryTotal) : 0;
                break;
            case ProcessMetric::Io:
                line.value = FormatRate(process.ioRate);
                line.barPercent = busiest ? static_cast<int>(process.ioRate * 100 / busiest) : 0;
                break;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

int Panel::Scale(int value) const { return MulDiv(value, static_cast<int>(GetDpiForWindow(window_)), 96); }

SIZE Panel::Measure() const {
    if (view_ == PanelView::Overview) {
        return {Scale(kWidth), Scale(kHeaderHeight) + Scale(kRowHeight) * static_cast<int>(rows_.size()) + Scale(kPadding)};
    }
    // Always room for the full list, so the panel does not jump as it fills.
    return {Scale(kWidth), Scale(kHeaderHeight) + Scale(kLineHeight) * static_cast<int>(kTopProcesses) + Scale(kFooterHeight) +
                               Scale(kPadding) / 2};
}

RECT Panel::RowRect(size_t index) const {
    const int top = Scale(kHeaderHeight) + Scale(kRowHeight) * static_cast<int>(index);
    return {0, top, Scale(kWidth), top + Scale(kRowHeight)};
}

RECT Panel::LineRect(size_t index) const {
    const int top = Scale(kHeaderHeight) + Scale(kLineHeight) * static_cast<int>(index);
    return {0, top, Scale(kWidth), top + Scale(kLineHeight)};
}

RECT Panel::SettingsRect() const {
    const int size = Scale(32);
    const int right = Scale(kWidth) - Scale(kPadding) + Scale(8);
    return {right - size, Scale(4), right, Scale(4) + size};
}

RECT Panel::BackRect() const { return {Scale(8), Scale(4), Scale(kWidth) - Scale(48), Scale(36)}; }

int Panel::HitTest(POINT point) const {
    const RECT settings = SettingsRect();
    if (PtInRect(&settings, point)) return kHotSettings;
    if (view_ != PanelView::Overview) {
        const RECT back = BackRect();
        return PtInRect(&back, point) ? kHotBack : -1;
    }
    for (size_t i = 0; i < rows_.size(); ++i) {
        const RECT row = RowRect(i);
        if (rows_[i].opens != PanelView::Overview && PtInRect(&row, point)) return static_cast<int>(i);
    }
    return -1;
}

void Panel::SetHot(int hot) {
    if (hot == hot_) return;
    const auto invalidate = [this](int spot) {
        if (spot < 0) return;
        const RECT rect = spot == kHotSettings ? SettingsRect() : spot == kHotBack ? BackRect() : RowRect(spot);
        InvalidateRect(window_, &rect, FALSE);
    };
    invalidate(hot_);
    hot_ = hot;
    invalidate(hot_);
    SetCursor(LoadCursorW(nullptr, hot_ >= 0 ? IDC_HAND : IDC_ARROW));
    if (hot_ >= 0) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        TrackMouseEvent(&track);
    }
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

void Panel::Show(HINSTANCE instance, const RECT& anchor, const Snapshot& snapshot, uint32_t metrics, const History* history,
                 PanelView view) {
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

    view_ = view;
    status_.clear();
    processors_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    memoryTotal_ = snapshot.memoryTotal;
    metrics_ = metrics;
    history_ = history;
    rows_ = BuildPanelRows(snapshot, processors_, GetTickCount64(), metrics_, history_);
    RefreshStyle();

    // Next to the tray icon, inside the work area, like the system flyouts.
    const SIZE size = Measure();
    const int margin = Scale(kMargin);
    const int center = (anchor.left + anchor.right) / 2;
    const int x = std::clamp<int>(center - size.cx / 2, work.left + margin, work.right - margin - size.cx);
    anchoredTop_ = anchor.bottom <= work.top + (work.bottom - work.top) / 2;
    const int y = anchoredTop_ ? work.top + margin : work.bottom - margin - size.cy;
    SetWindowPos(window_, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
}

void Panel::Relayout() {
    RECT bounds;
    GetWindowRect(window_, &bounds);
    const SIZE size = Measure();
    const int y = anchoredTop_ ? bounds.top : bounds.bottom - size.cy;
    SetWindowPos(window_, nullptr, bounds.left, y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, FALSE);
}

void Panel::SetView(PanelView view) {
    view_ = view;
    hot_ = -1;
    status_.clear();
    lines_ = view == PanelView::Overview ? std::vector<ProcessLine>{} : BuildProcessLines(processes_, view, memoryTotal_);
    Relayout();
}

void Panel::Update(const Snapshot& snapshot, const std::vector<ProcessUsage>& processes) {
    if (!window_) return;
    memoryTotal_ = snapshot.memoryTotal;
    processes_ = processes;
    std::vector<PanelRow> rows = BuildPanelRows(snapshot, processors_, GetTickCount64(), metrics_, history_);

    if (view_ != PanelView::Overview) {
        rows_ = std::move(rows);
        std::vector<ProcessLine> lines = BuildProcessLines(processes_, view_, memoryTotal_);
        for (size_t i = 0; i < std::max(lines.size(), lines_.size()); ++i) {
            if (i < lines.size() && i < lines_.size() && lines[i] == lines_[i]) continue;
            const RECT rect = LineRect(i);
            InvalidateRect(window_, &rect, FALSE);
        }
        if (lines_.empty() != lines.empty()) InvalidateRect(window_, nullptr, FALSE);
        lines_ = std::move(lines);
        return;
    }
    if (rows.size() != rows_.size()) {
        // A battery appeared or went away.
        rows_ = std::move(rows);
        Relayout();
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
    activated_ = false;
    hot_ = -1;
    DestroyWindow(window);
    for (HFONT* font : {&titleFont_, &labelFont_, &valueFont_, &detailFont_, &iconFont_}) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
    std::vector<PanelRow>().swap(rows_);
    std::vector<ProcessUsage>().swap(processes_);
    std::vector<ProcessLine>().swap(lines_);
    if (onClosed) onClosed();
}

// --- Painting ---------------------------------------------------------------------

void Panel::PaintRow(HDC dc, const PanelRow& row, const RECT& rect, bool hot, bool clickable) {
    const int padding = Scale(kPadding);
    const int left = rect.left + padding;
    const int right = rect.right - padding;
    const int lineHeight = Scale(22);

    if (hot) {
        RECT highlight{rect.left + Scale(6), rect.top + Scale(2), rect.right - Scale(6), rect.bottom - Scale(2)};
        FillRounded(dc, highlight, theme_.hover, Scale(4));
    }

    SelectObject(dc, iconFont_);
    SetTextColor(dc, row.high ? kWarning : theme_.accent);
    RECT iconRect{left, rect.top, left + Scale(20), rect.top + lineHeight};
    DrawTextW(dc, row.icon, -1, &iconRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const int textLeft = left + Scale(28);
    SelectObject(dc, labelFont_);
    SetTextColor(dc, theme_.text);
    RECT labelRect{textLeft, rect.top, right, rect.top + lineHeight};
    DrawTextW(dc, row.label.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (clickable) {
        // A chevron hints that the row opens its top processes.
        SelectObject(dc, detailFont_);
        SetTextColor(dc, theme_.secondaryText);
        HFONT chevron = CreateIconFont(GetDpiForWindow(window_), 7);
        SelectObject(dc, chevron);
        DrawTextW(dc, icons::kChevronRight, -1, &labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, detailFont_);
        DeleteObject(chevron);
        labelRect.right -= Scale(16);
    }
    SelectObject(dc, valueFont_);
    SetTextColor(dc, row.high ? kWarning : theme_.text);
    DrawTextW(dc, row.value.c_str(), -1, &labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, detailFont_);
    SetTextColor(dc, theme_.secondaryText);
    RECT detailRect{textLeft, rect.top + lineHeight, right, rect.top + lineHeight + Scale(18)};
    DrawTextW(dc, row.detail.c_str(), -1, &detailRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (!row.graph.empty()) {
        const RECT area{textLeft, detailRect.bottom + Scale(3), right, rect.bottom - Scale(5)};
        PaintGraph(dc, row.graph, area, row.high ? kWarning : theme_.accent);
    } else if (row.barPercent >= 0) {
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

// An area chart of the last readings, newest at the right, over a faint
// baseline. Values are 0-1.
void Panel::PaintGraph(HDC dc, const std::vector<float>& values, const RECT& rect, COLORREF color) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HPEN baseline = CreatePen(PS_SOLID, 1, theme_.divider);
    HGDIOBJ previousPen = SelectObject(dc, baseline);
    MoveToEx(dc, rect.left, rect.bottom - 1, nullptr);
    LineTo(dc, rect.right, rect.bottom - 1);
    SelectObject(dc, previousPen);
    DeleteObject(baseline);
    if (values.size() < 2) return;

    // Always the full width for a full history, so the graph scrolls.
    const double step = static_cast<double>(width) / (History::kCapacity - 1);
    const double start = rect.right - step * static_cast<double>(values.size() - 1);
    std::vector<POINT> points;
    points.reserve(values.size() + 2);
    for (size_t i = 0; i < values.size(); ++i) {
        points.push_back({static_cast<LONG>(std::lround(start + step * static_cast<double>(i))),
                          rect.bottom - 1 - static_cast<LONG>(std::lround(values[i] * (height - 1)))});
    }
    // The area in a mix of the color and the background, then the line.
    const auto mix = [&](int shift) {
        return static_cast<int>((((color >> shift) & 0xFF) * 3 + ((theme_.background >> shift) & 0xFF) * 7) / 10);
    };
    std::vector<POINT> area = points;
    area.push_back({points.back().x, rect.bottom});
    area.push_back({points.front().x, rect.bottom});
    HBRUSH fill = CreateSolidBrush(RGB(mix(0), mix(8), mix(16)));
    HGDIOBJ previousBrush = SelectObject(dc, fill);
    previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, area.data(), static_cast<int>(area.size()));
    HPEN line = CreatePen(PS_SOLID, std::max(1, Scale(1)), color);
    SelectObject(dc, line);
    Polyline(dc, points.data(), static_cast<int>(points.size()));
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(line);
    DeleteObject(fill);
}

void Panel::SetStatus(std::wstring status) {
    status_ = std::move(status);
    InvalidateRect(window_, nullptr, FALSE);
}

namespace {

// Ends every process of a group; the count that could not be ended.
size_t EndProcesses(const std::vector<uint32_t>& pids) {
    size_t failed = 0;
    for (uint32_t pid : pids) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!process || !TerminateProcess(process, 1)) ++failed;
        if (process) CloseHandle(process);
    }
    return failed;
}

bool OpenFileLocation(const std::vector<uint32_t>& pids) {
    for (uint32_t pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD size = static_cast<DWORD>(std::size(path));
        const bool found = QueryFullProcessImageNameW(process, 0, path, &size) != 0;
        CloseHandle(process);
        if (!found) continue;
        const std::wstring arguments = L"/select,\"" + std::wstring(path) + L"\"";
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, nullptr, L"explorer.exe", arguments.c_str(), nullptr,
                                                       SW_SHOWNORMAL)) > 32;
    }
    return false;
}

}  // namespace

void Panel::ShowProcessMenu(POINT point) {
    size_t index = lines_.size();
    for (size_t i = 0; i < lines_.size(); ++i) {
        const RECT rect = LineRect(i);
        if (PtInRect(&rect, point)) index = i;
    }
    if (index == lines_.size()) return;
    // Copied: the list may be refreshed while the menu is open.
    const ProcessLine line = lines_[index];
    enum { kEnd = 1, kLocation };
    HMENU menu = CreatePopupMenu();
    const std::wstring end = L"End " + line.exe;
    AppendMenuW(menu, MF_STRING, kEnd, end.c_str());
    AppendMenuW(menu, MF_STRING, kLocation, L"Open file location");
    POINT screen = point;
    ClientToScreen(window_, &screen);
    const int command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, window_, nullptr);
    DestroyMenu(menu);
    if (!window_) return;
    if (command == kEnd) {
        const size_t failed = EndProcesses(line.pids);
        SetStatus(failed == 0 ? L"Ended " + line.exe + L"."
                              : L"Could not end " + line.exe + L": it may need administrator rights.");
    } else if (command == kLocation) {
        if (OpenFileLocation(line.pids)) {
            Close();
        } else {
            SetStatus(L"Windows does not let Vela see where " + line.exe + L" is.");
        }
    }
}

void Panel::PaintLine(HDC dc, const ProcessLine& line, const RECT& rect) {
    const int left = rect.left + Scale(kPadding);
    const int right = rect.right - Scale(kPadding);
    const int barHeight = Scale(3);
    RECT text{left, rect.top, right, rect.bottom - barHeight - Scale(3)};

    SelectObject(dc, valueFont_);
    SetTextColor(dc, theme_.text);
    SIZE valueSize{};
    GetTextExtentPoint32W(dc, line.value.c_str(), static_cast<int>(line.value.size()), &valueSize);
    DrawTextW(dc, line.value.c_str(), -1, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, labelFont_);
    RECT name{text.left, text.top, text.right - valueSize.cx - Scale(12), text.bottom};
    DrawTextW(dc, line.name.c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    const RECT track{left, text.bottom, right, text.bottom + barHeight};
    FillRounded(dc, track, theme_.divider, 1);
    const int fill = (track.right - track.left) * std::clamp(line.barPercent, 0, 100) / 100;
    if (fill > 0) FillRounded(dc, {track.left, track.top, track.left + std::max(fill, barHeight), track.bottom}, theme_.accent, 1);
}

void Panel::Paint(HDC dc, const RECT& client) {
    HBRUSH background = CreateSolidBrush(theme_.background);
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, titleFont_);

    const int padding = Scale(kPadding);
    RECT title{padding, 0, Scale(kWidth) - padding, Scale(kHeaderHeight)};
    if (view_ != PanelView::Overview) {
        // The whole title is the back button.
        const RECT back = BackRect();
        if (hot_ == kHotBack) FillRounded(dc, back, theme_.hover, Scale(4));
        SelectObject(dc, iconFont_);
        SetTextColor(dc, theme_.secondaryText);
        DrawTextW(dc, icons::kBack, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        title.left += Scale(26);
        SelectObject(dc, titleFont_);
    }
    SetTextColor(dc, theme_.text);
    DrawTextW(dc, ViewTitle(view_), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const RECT settings = SettingsRect();
    if (hot_ == kHotSettings) FillRounded(dc, settings, theme_.hover, Scale(4));
    SelectObject(dc, iconFont_);
    SetTextColor(dc, theme_.secondaryText);
    RECT settingsText = settings;
    DrawTextW(dc, icons::kSettings, -1, &settingsText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT overlap;
    if (view_ == PanelView::Overview) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            const RECT rect = RowRect(i);
            if (IntersectRect(&overlap, &rect, &client)) PaintRow(dc, rows_[i], rect, static_cast<int>(i) == hot_, rows_[i].opens != PanelView::Overview);
        }
    } else {
        for (size_t i = 0; i < lines_.size(); ++i) {
            const RECT rect = LineRect(i);
            if (IntersectRect(&overlap, &rect, &client)) PaintLine(dc, lines_[i], rect);
        }
        SelectObject(dc, detailFont_);
        SetTextColor(dc, theme_.secondaryText);
        if (lines_.empty()) {
            RECT measuring = LineRect(0);
            measuring.left += padding;
            DrawTextW(dc, L"Measuring\u2026", -1, &measuring, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        const RECT last = LineRect(kTopProcesses - 1);
        RECT footer{padding, last.bottom, Scale(kWidth) - padding, last.bottom + Scale(kFooterHeight)};
        const wchar_t* note = view_ == PanelView::Disk ? L"File and device reads plus writes \u00B7 right-click for options"
                                                       : L"Same names are added up \u00B7 right-click for options";
        DrawTextW(dc, status_.empty() ? note : status_.c_str(), -1, &footer,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, hot_ >= 0 ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE:
            SetHot(HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
            return 0;
        case WM_MOUSELEAVE:
            SetHot(-1);
            return 0;
        case WM_LBUTTONUP: {
            const int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hit == kHotSettings) {
                Close();
                if (onSettings) onSettings();
            } else if (hit == kHotBack) {
                SetView(PanelView::Overview);
            } else if (hit >= 0) {
                SetView(rows_[hit].opens);
            }
            return 0;
        }
        case WM_RBUTTONUP:
            if (view_ != PanelView::Overview) ShowProcessMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (view_ != PanelView::Overview) {
                    SetView(PanelView::Overview);
                } else {
                    Close();
                }
            } else if (wParam == VK_BACK && view_ != PanelView::Overview) {
                SetView(PanelView::Overview);
            }
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
