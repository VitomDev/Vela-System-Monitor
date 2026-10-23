#include "app/tray_icon.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vela {

namespace {

constexpr int kHighPercent = 90;
constexpr COLORREF kWarning = RGB(255, 99, 71);

struct BarLayout {
    int margin, gap, width, height;
};

BarLayout Bars(int size) {
    const int margin = std::max(1, size / 8);
    const int gap = std::max(2, size / 8);
    return {margin, gap, (size - 2 * margin - gap) / 2, size - 2 * margin};
}

uint32_t Pixel(COLORREF color, int alpha) {
    return (static_cast<uint32_t>(alpha) << 24) | (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
}

void FillBox(std::vector<uint32_t>& pixels, int size, int left, int top, int right, int bottom, uint32_t value) {
    for (int y = std::max(0, top); y < std::min(size, bottom); ++y) {
        for (int x = std::max(0, left); x < std::min(size, right); ++x) pixels[y * size + x] = value;
    }
}

void DrawBar(std::vector<uint32_t>& pixels, int size, int left, int fill, bool high, COLORREF foreground) {
    const BarLayout bars = Bars(size);
    const int top = bars.margin, bottom = bars.margin + bars.height, right = left + bars.width;
    // A faint frame with the level filled from the bottom.
    const uint32_t frame = Pixel(foreground, 110);
    FillBox(pixels, size, left, top, right, top + 1, frame);
    FillBox(pixels, size, left, bottom - 1, right, bottom, frame);
    FillBox(pixels, size, left, top, left + 1, bottom, frame);
    FillBox(pixels, size, right - 1, top, right, bottom, frame);
    FillBox(pixels, size, left + 1, bottom - 1 - fill, right - 1, bottom - 1, Pixel(high ? kWarning : foreground, 255));
}

// White text on black through GDI, then its brightness becomes the alpha.
void DrawNumber(std::vector<uint32_t>& pixels, int size, int value, bool high, COLORREF foreground) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    const std::wstring text = std::to_wstring(value);
    // Three digits need a narrower font to fit.
    LOGFONTW font{};
    font.lfHeight = -(text.size() > 2 ? size * 9 / 16 : size * 13 / 16);
    font.lfWeight = FW_SEMIBOLD;
    font.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    HFONT handle = CreateFontIndirectW(&font);
    HGDIOBJ previousFont = SelectObject(dc, handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rect{-1, 0, size + 1, size};
    DrawTextW(dc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    const auto* source = static_cast<const uint32_t*>(bits);
    const COLORREF color = high ? kWarning : foreground;
    for (int i = 0; i < size * size; ++i) {
        const int alpha = (source[i] >> 8) & 0xFF;
        if (alpha) pixels[i] = Pixel(color, alpha);
    }
    SelectObject(dc, previousFont);
    SelectObject(dc, previousBitmap);
    DeleteObject(handle);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

}  // namespace

TrayIconState MakeTrayIconState(TrayIcon style, int size, bool lightTaskbar, double cpuPercent, int memoryPercent) {
    TrayIconState state;
    state.style = style;
    state.size = size;
    state.lightTaskbar = lightTaskbar;
    const int cpu = std::clamp(static_cast<int>(std::lround(cpuPercent)), 0, 100);
    const int memory = std::clamp(memoryPercent, 0, 100);
    state.cpuHigh = style != TrayIcon::MemoryPercent && cpu >= kHighPercent;
    state.memoryHigh = style != TrayIcon::CpuPercent && memory >= kHighPercent;
    switch (style) {
        case TrayIcon::Bars: {
            const int inner = Bars(size).height - 2;
            // At least one pixel so an idle bar still reads as a bar.
            state.cpu = std::max(1, (cpu * inner + 50) / 100);
            state.memory = std::max(1, (memory * inner + 50) / 100);
            break;
        }
        case TrayIcon::CpuPercent: state.cpu = cpu; break;
        case TrayIcon::MemoryPercent: state.memory = memory; break;
    }
    return state;
}

HICON CreateTrayIcon(const TrayIconState& state) {
    const int size = state.size;
    if (size <= 0) return nullptr;
    const COLORREF foreground = state.lightTaskbar ? RGB(0, 0, 0) : RGB(255, 255, 255);
    std::vector<uint32_t> pixels(static_cast<size_t>(size) * size, 0);
    switch (state.style) {
        case TrayIcon::Bars: {
            const BarLayout bars = Bars(size);
            DrawBar(pixels, size, bars.margin, state.cpu, state.cpuHigh, foreground);
            DrawBar(pixels, size, bars.margin + bars.width + bars.gap, state.memory, state.memoryHigh, foreground);
            break;
        }
        case TrayIcon::CpuPercent: DrawNumber(pixels, size, state.cpu, state.cpuHigh, foreground); break;
        case TrayIcon::MemoryPercent: DrawNumber(pixels, size, state.memory, state.memoryHigh, foreground); break;
    }

    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    HICON icon = nullptr;
    if (color && mask) {
        std::copy(pixels.begin(), pixels.end(), static_cast<uint32_t*>(bits));
        ICONINFO iconInfo{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&iconInfo);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}

bool IsTaskbarLight() {
    DWORD light = 0;
    DWORD size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light != 0;
}

}  // namespace vela
