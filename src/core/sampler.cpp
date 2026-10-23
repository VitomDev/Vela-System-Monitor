#include "core/sampler.h"

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <winioctl.h>

namespace vela {

namespace {

constexpr uint64_t kInterfaceRefreshMs = 30'000;

uint64_t ToUint64(const FILETIME& time) { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

}  // namespace

Sampler::Sampler() {
    wchar_t windows[MAX_PATH];
    if (GetSystemWindowsDirectoryW(windows, MAX_PATH) > 0 && windows[1] == L':') diskRoot_[0] = windows[0];
    // No access rights are needed to query a volume's performance counters.
    const wchar_t device[] = {L'\\', L'\\', L'.', L'\\', diskRoot_[0], L':', L'\0'};
    volume_ = CreateFileW(device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

Sampler::~Sampler() {
    if (volume_ != INVALID_HANDLE_VALUE) CloseHandle(volume_);
}

void Sampler::FindNetworkInterfaces() {
    interfaces_.clear();
    interfacesFoundAt_ = GetTickCount64();
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.InterfaceAndOperStatusFlags.HardwareInterface && !row.InterfaceAndOperStatusFlags.FilterInterface &&
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK) {
            interfaces_.push_back(row.InterfaceIndex);
        }
    }
    FreeMibTable(table);
}

bool Sampler::ReadNetwork(uint64_t& received, uint64_t& sent) {
    if (interfaces_.empty() || GetTickCount64() - interfacesFoundAt_ > kInterfaceRefreshMs) FindNetworkInterfaces();
    received = sent = 0;
    for (uint32_t index : interfaces_) {
        MIB_IF_ROW2 row{};
        row.InterfaceIndex = index;
        if (GetIfEntry2(&row) != NO_ERROR) {
            // An adapter went away: look again next time.
            interfacesFoundAt_ = 0;
            return false;
        }
        received += row.InOctets;
        sent += row.OutOctets;
    }
    return true;
}

bool Sampler::ReadDiskCounters(uint64_t& read, uint64_t& written) {
    if (volume_ == INVALID_HANDLE_VALUE) return false;
    DISK_PERFORMANCE performance{};
    DWORD size = 0;
    if (!DeviceIoControl(volume_, IOCTL_DISK_PERFORMANCE, nullptr, 0, &performance, sizeof(performance), &size, nullptr)) {
        return false;
    }
    read = static_cast<uint64_t>(performance.BytesRead.QuadPart);
    written = static_cast<uint64_t>(performance.BytesWritten.QuadPart);
    return true;
}

Snapshot Sampler::Sample() {
    Snapshot snapshot;
    const uint64_t now = GetTickCount64();
    const uint64_t elapsed = first_ ? 0 : now - lastTick_;

    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const CpuTimes cpu{ToUint64(idle), ToUint64(kernel), ToUint64(user)};
        if (!first_) snapshot.cpuPercent = CpuPercent(cpu_, cpu);
        cpu_ = cpu;
    }

    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) {
        snapshot.memoryTotal = memory.ullTotalPhys;
        snapshot.memoryUsed = memory.ullTotalPhys - memory.ullAvailPhys;
    }

    uint64_t received = 0, sent = 0;
    if (ReadNetwork(received, sent)) {
        if (!first_) {
            snapshot.networkDown = RatePerSecond(received_, received, elapsed);
            snapshot.networkUp = RatePerSecond(sent_, sent, elapsed);
        }
        received_ = received;
        sent_ = sent;
    } else {
        // The adapter set changed: start counting again from the next reading.
        received_ = sent_ = UINT64_MAX;
    }

    snapshot.diskLetter = diskRoot_[0];
    ULARGE_INTEGER free{}, total{};
    if (GetDiskFreeSpaceExW(diskRoot_, nullptr, &total, &free)) {
        snapshot.diskTotal = total.QuadPart;
        snapshot.diskUsed = total.QuadPart - free.QuadPart;
    }
    uint64_t read = 0, written = 0;
    if (ReadDiskCounters(read, written)) {
        snapshot.diskRatesKnown = true;
        if (!first_) {
            snapshot.diskRead = RatePerSecond(diskRead_, read, elapsed);
            snapshot.diskWrite = RatePerSecond(diskWritten_, written, elapsed);
        }
        diskRead_ = read;
        diskWritten_ = written;
    }

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power)) snapshot.battery = BatteryFromPowerStatus(power);

    first_ = false;
    lastTick_ = now;
    return snapshot;
}

}  // namespace vela
