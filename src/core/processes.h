#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vela {

// Usage of all processes that share an executable name, like Task Manager's
// app groups: twelve browser processes count as one line.
struct ProcessUsage {
    std::wstring name;
    int count = 0;
    double cpuPercent = 0;  // Share of the whole CPU, 0-100
    uint64_t memory = 0;    // Private working set, as Task Manager shows
    uint64_t ioRate = 0;    // File and device reads plus writes, bytes per second
};

enum class ProcessMetric { Cpu, Memory, Io };

// The `count` largest by `metric`, largest first; ties keep name order.
std::vector<ProcessUsage> TopProcesses(std::vector<ProcessUsage> processes, ProcessMetric metric, size_t count);

// Raw per-process counters, as one system call returns them for every process.
struct ProcessCounters {
    uint32_t pid = 0;
    int64_t createTime = 0;
    std::wstring name;
    uint64_t cpuTime = 0;  // Kernel plus user, 100 ns units
    uint64_t memory = 0;
    uint64_t ioBytes = 0;
};

// Turns two readings into grouped usage. Kept apart from the system call so
// it can be tested.
class ProcessUsageCalculator {
public:
    std::vector<ProcessUsage> Update(const std::vector<ProcessCounters>& counters, uint64_t elapsedMs, unsigned processors);
    void Reset();

private:
    struct Previous {
        int64_t createTime;
        uint64_t cpuTime;
        uint64_t ioBytes;
    };
    std::unordered_map<uint32_t, Previous> previous_;
};

// Reads every process at once with NtQuerySystemInformation: no handle to
// each process is needed, so it also sees processes Vela cannot open.
// Only used while the panel is open.
class ProcessSampler {
public:
    std::vector<ProcessUsage> Sample();
    void Reset();

private:
    ProcessUsageCalculator calculator_;
    uint64_t lastTick_ = 0;
};

}  // namespace vela
