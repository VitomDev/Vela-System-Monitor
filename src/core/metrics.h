#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace vela {

// Cumulative system CPU times, as GetSystemTimes reports them (kernel includes idle).
struct CpuTimes {
    uint64_t idle = 0;
    uint64_t kernel = 0;
    uint64_t user = 0;
};

struct BatteryInfo {
    bool present = false;
    int percent = 0;          // 0-100
    bool pluggedIn = false;
    bool charging = false;
    long long secondsLeft = -1;  // -1 when Windows does not know
};

// One reading of everything Vela shows. Rates are bytes per second.
struct Snapshot {
    double cpuPercent = 0;
    uint64_t memoryUsed = 0;
    uint64_t memoryTotal = 0;
    uint64_t networkDown = 0;
    uint64_t networkUp = 0;
    wchar_t diskLetter = L'C';
    uint64_t diskUsed = 0;
    uint64_t diskTotal = 0;
    bool diskRatesKnown = false;
    uint64_t diskRead = 0;
    uint64_t diskWrite = 0;
    BatteryInfo battery;

    int MemoryPercent() const;
    int DiskPercent() const;
};

// Busy share of the CPU between two readings, 0-100.
double CpuPercent(const CpuTimes& before, const CpuTimes& after);

// Per-second rate of a cumulative counter. A counter that went back (an
// adapter was reset or removed) counts as no traffic.
uint64_t RatePerSecond(uint64_t before, uint64_t after, uint64_t elapsedMs);

BatteryInfo BatteryFromPowerStatus(const SYSTEM_POWER_STATUS& status);

// Readable sizes and rates: "512 B", "1.5 KB", "15.2 GB", "1.2 MB/s".
std::wstring FormatBytes(uint64_t bytes);
std::wstring FormatRate(uint64_t bytesPerSecond);
// "45 min", "2 h 5 min", "6 d 8 h".
std::wstring FormatDuration(long long seconds);
std::wstring FormatPercent(double percent);

// The tray tooltip, which Windows limits to 127 characters.
std::wstring TooltipText(const Snapshot& snapshot);

}  // namespace vela
