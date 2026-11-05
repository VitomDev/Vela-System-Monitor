#include "core/metrics.h"

#include <cmath>
#include <cwchar>
#include <iterator>
#include <vector>

namespace vela {

namespace {

int Percent(uint64_t part, uint64_t total) {
    if (total == 0) return 0;
    return static_cast<int>(std::lround(static_cast<double>(part) * 100.0 / static_cast<double>(total)));
}

}  // namespace

int Snapshot::MemoryPercent() const { return Percent(memoryUsed, memoryTotal); }
int Snapshot::DiskPercent() const { return Percent(diskUsed, diskTotal); }

double CpuPercent(const CpuTimes& before, const CpuTimes& after) {
    if (after.idle < before.idle || after.kernel < before.kernel || after.user < before.user) return 0;
    const uint64_t idle = after.idle - before.idle;
    const uint64_t total = (after.kernel - before.kernel) + (after.user - before.user);
    if (total == 0 || idle >= total) return 0;
    return static_cast<double>(total - idle) * 100.0 / static_cast<double>(total);
}

uint64_t RatePerSecond(uint64_t before, uint64_t after, uint64_t elapsedMs) {
    if (after < before || elapsedMs == 0) return 0;
    return (after - before) * 1000 / elapsedMs;
}

BatteryInfo BatteryFromPowerStatus(const SYSTEM_POWER_STATUS& status) {
    BatteryInfo battery;
    // 128: no system battery; 255: unknown status.
    if (status.BatteryFlag & 128 || status.BatteryFlag == 255 || status.BatteryLifePercent > 100) return battery;
    battery.present = true;
    battery.percent = status.BatteryLifePercent;
    battery.pluggedIn = status.ACLineStatus == 1;
    battery.charging = (status.BatteryFlag & 8) != 0;
    if (!battery.pluggedIn && status.BatteryLifeTime != static_cast<DWORD>(-1)) battery.secondsLeft = status.BatteryLifeTime;
    return battery;
}

std::wstring FormatBytes(uint64_t bytes) {
    static constexpr const wchar_t* kUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1000 && unit + 1 < std::size(kUnits)) {
        value /= 1024;
        ++unit;
    }
    wchar_t text[32];
    if (unit == 0) {
        swprintf_s(text, L"%llu B", static_cast<unsigned long long>(bytes));
    } else {
        // Three significant digits at most: 1.5 KB, 15.2 GB, 476 GB.
        swprintf_s(text, value < 100 ? L"%.1f %s" : L"%.0f %s", value, kUnits[unit]);
    }
    return text;
}

std::wstring FormatRate(uint64_t bytesPerSecond) { return FormatBytes(bytesPerSecond) + L"/s"; }

std::wstring FormatDuration(long long seconds) {
    if (seconds < 0) return L"";
    const long long minutes = (seconds + 30) / 60;
    wchar_t text[32];
    if (minutes < 60) {
        swprintf_s(text, L"%lld min", minutes);
    } else if (minutes >= 24 * 60) {
        swprintf_s(text, L"%lld d %lld h", minutes / (24 * 60), minutes / 60 % 24);
    } else if (minutes % 60 == 0) {
        swprintf_s(text, L"%lld h", minutes / 60);
    } else {
        swprintf_s(text, L"%lld h %lld min", minutes / 60, minutes % 60);
    }
    return text;
}

std::wstring FormatPercent(double percent) { return std::to_wstring(std::lround(percent)) + L"%"; }

std::wstring TooltipText(const Snapshot& s) {
    std::wstring text = L"CPU " + FormatPercent(s.cpuPercent);
    if (s.gpu.present) text += L" \u00B7 GPU " + FormatPercent(s.gpu.percent);
    text += L" \u00B7 RAM " + std::to_wstring(s.MemoryPercent()) + L"%";
    text += L"\nDisk " + std::wstring(1, s.diskLetter) + L": " + std::to_wstring(s.DiskPercent()) + L"%";
    text += L"\n\u2193 " + FormatRate(s.networkDown) + L"  \u2191 " + FormatRate(s.networkUp);
    if (s.battery.present) {
        text += L"\nBattery " + std::to_wstring(s.battery.percent) + L"%";
        if (s.battery.charging) {
            text += L", charging";
        } else if (s.battery.pluggedIn) {
            text += L", plugged in";
        } else if (s.battery.secondsLeft >= 0) {
            text += L", " + FormatDuration(s.battery.secondsLeft) + L" left";
        }
    }
    if (text.size() > 127) text.resize(127);
    return text;
}

MiniLines MiniText(const Snapshot& s, uint32_t metrics) {
    std::vector<std::wstring> items;
    if (metrics & metric::kCpu) items.push_back(L"CPU " + FormatPercent(s.cpuPercent));
    if ((metrics & metric::kGpu) && s.gpu.present) items.push_back(L"GPU " + FormatPercent(s.gpu.percent));
    if (metrics & metric::kMemory) items.push_back(L"RAM " + std::to_wstring(s.MemoryPercent()) + L"%");
    if (metrics & metric::kDisk) {
        items.push_back(s.diskRatesKnown ? L"Disk " + FormatRate(s.diskRead + s.diskWrite)
                                         : L"Disk " + std::to_wstring(s.DiskPercent()) + L"%");
    }
    if (metrics & metric::kNetwork) items.push_back(L"\u2193 " + FormatRate(s.networkDown) + L"  \u2191 " + FormatRate(s.networkUp));
    if ((metrics & metric::kBattery) && s.battery.present) items.push_back(L"Battery " + std::to_wstring(s.battery.percent) + L"%");

    // The first line takes the larger half.
    MiniLines lines;
    const size_t split = (items.size() + 1) / 2;
    for (size_t i = 0; i < items.size(); ++i) {
        std::wstring& line = i < split ? lines.first : lines.second;
        if (!line.empty()) line += L"  ";
        line += items[i];
    }
    return lines;
}

MiniLines WidestMiniText(uint32_t metrics) {
    Snapshot widest;
    widest.cpuPercent = 100;
    widest.memoryUsed = widest.memoryTotal = 1;
    widest.diskRatesKnown = true;
    widest.diskRead = 888ull << 20;
    widest.networkDown = widest.networkUp = 888ull << 20;
    widest.battery = {true, 100, false, false, -1};
    widest.gpu = {true, true, 100, 0, 0};
    return MiniText(widest, metrics);
}

}  // namespace vela
