#include "app/tray_icon.h"

#include "app/resource.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vela {

namespace {

constexpr int kHighPercent = 90;
constexpr COLORREF kWarning = RGB(255, 99, 71);

uint32_t Pixel(COLORREF color, int alpha) {
    return (static_cast<uint32_t>(alpha) << 24) | (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
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
    state.cpuHigh = style == TrayIcon::CpuPercent && cpu >= kHighPercent;
    state.memoryHigh = style == TrayIcon::MemoryPercent && memory >= kHighPercent;
    switch (style) {
        case TrayIcon::Logo: break;
        case TrayIcon::CpuPercent: state.cpu = cpu; break;
        case TrayIcon::MemoryPercent: state.memory = memory; break;
    }
    return state;
}

HICON CreateTrayIcon(const TrayIconState& state) {
    const int size = state.size;
    if (size <= 0) return nullptr;
    if (state.style == TrayIcon::Logo) {
        return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_VELA), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
    }
    const COLORREF foreground = state.lightTaskbar ? RGB(0, 0, 0) : RGB(255, 255, 255);
    std::vector<uint32_t> pixels(static_cast<size_t>(size) * size, 0);
    switch (state.style) {
        case TrayIcon::Logo: break;
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
