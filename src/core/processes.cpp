#include "core/processes.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace vela {

std::vector<ProcessUsage> TopProcesses(std::vector<ProcessUsage> processes, ProcessMetric metric, size_t count) {
    const auto value = [metric](const ProcessUsage& p) {
        switch (metric) {
            case ProcessMetric::Cpu: return p.cpuPercent;
            case ProcessMetric::Memory: return static_cast<double>(p.memory);
            case ProcessMetric::Io: return static_cast<double>(p.ioRate);
        }
        return 0.0;
    };
    std::stable_sort(processes.begin(), processes.end(),
                     [&](const ProcessUsage& a, const ProcessUsage& b) { return value(a) > value(b); });
    if (processes.size() > count) processes.resize(count);
    return processes;
}

std::vector<ProcessUsage> ProcessUsageCalculator::Update(const std::vector<ProcessCounters>& counters, uint64_t elapsedMs,
                                                         unsigned processors) {
    // Grouped case-insensitively by name, in name order.
    std::map<std::wstring, ProcessUsage> groups;
    std::unordered_map<uint32_t, Previous> current;
    const double cpuCapacity = static_cast<double>(elapsedMs) * 10'000.0 * std::max(1u, processors);
    for (const ProcessCounters& process : counters) {
        std::wstring key = process.name;
        for (wchar_t& c : key) c = static_cast<wchar_t>(std::towlower(c));
        ProcessUsage& group = groups[key];
        if (group.name.empty()) group.name = process.name;
        ++group.count;
        group.memory += process.memory;

        // Only a process seen last time, and not a new one reusing its id, has a rate.
        const auto found = previous_.find(process.pid);
        if (found != previous_.end() && found->second.createTime == process.createTime && elapsedMs > 0) {
            const Previous& before = found->second;
            if (process.cpuTime >= before.cpuTime && cpuCapacity > 0) {
                group.cpuPercent += static_cast<double>(process.cpuTime - before.cpuTime) * 100.0 / cpuCapacity;
            }
            if (process.ioBytes >= before.ioBytes) group.ioRate += (process.ioBytes - before.ioBytes) * 1000 / elapsedMs;
        }
        current[process.pid] = {process.createTime, process.cpuTime, process.ioBytes};
    }
    previous_ = std::move(current);

    std::vector<ProcessUsage> usage;
    usage.reserve(groups.size());
    for (auto& [key, group] : groups) {
        group.cpuPercent = std::min(group.cpuPercent, 100.0);
        usage.push_back(std::move(group));
    }
    return usage;
}

void ProcessUsageCalculator::Reset() {
    previous_.clear();
    std::unordered_map<uint32_t, Previous>().swap(previous_);
}

namespace {

// SYSTEM_PROCESS_INFORMATION up to the counters Vela reads. winternl.h only
// declares a few of its fields; this layout has been stable since Windows 7.
struct UnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct SystemProcessInformation {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UnicodeString ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
};

constexpr ULONG kSystemProcessInformation = 5;
constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004);

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

std::vector<ProcessCounters> ReadCounters() {
    static const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (!query) return {};
    // Grows until every process fits; processes may start between calls.
    std::vector<BYTE> buffer(256 * 1024);
    LONG status;
    for (;;) {
        ULONG needed = 0;
        status = query(kSystemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &needed);
        if (status != kStatusInfoLengthMismatch) break;
        buffer.resize(std::max<size_t>(buffer.size() * 2, needed + 16 * 1024));
    }
    if (status < 0) return {};

    std::vector<ProcessCounters> counters;
    for (size_t offset = 0;;) {
        const auto* entry = reinterpret_cast<const SystemProcessInformation*>(buffer.data() + offset);
        const auto pid = static_cast<uint32_t>(reinterpret_cast<ULONG_PTR>(entry->UniqueProcessId));
        // Process 0 is the idle time, not a program.
        if (pid != 0) {
            ProcessCounters process;
            process.pid = pid;
            process.createTime = entry->CreateTime.QuadPart;
            process.name = entry->ImageName.Buffer ? std::wstring(entry->ImageName.Buffer, entry->ImageName.Length / sizeof(wchar_t))
                                                   : std::wstring(L"System");
            process.cpuTime = static_cast<uint64_t>(entry->UserTime.QuadPart + entry->KernelTime.QuadPart);
            process.memory = static_cast<uint64_t>(entry->WorkingSetPrivateSize.QuadPart);
            process.ioBytes = static_cast<uint64_t>(entry->ReadTransferCount.QuadPart + entry->WriteTransferCount.QuadPart);
            counters.push_back(std::move(process));
        }
        if (entry->NextEntryOffset == 0) break;
        offset += entry->NextEntryOffset;
    }
    return counters;
}

}  // namespace

std::vector<ProcessUsage> ProcessSampler::Sample() {
    const uint64_t now = GetTickCount64();
    const uint64_t elapsed = lastTick_ ? now - lastTick_ : 0;
    lastTick_ = now;
    return calculator_.Update(ReadCounters(), elapsed, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
}

void ProcessSampler::Reset() {
    calculator_.Reset();
    lastTick_ = 0;
}

}  // namespace vela
