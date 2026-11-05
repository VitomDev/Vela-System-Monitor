#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vela {

struct GpuInfo {
    bool present = false;
    bool measured = false;    // False on the first reading, which has no utilization yet
    double percent = 0;       // The busiest engine of the busiest GPU, as Task Manager shows
    uint64_t memoryUsed = 0;  // Dedicated video memory of the main GPU
    uint64_t memoryTotal = 0;

    int MemoryPercent() const;
};

// Usage from the "GPU Engine" counter instances, named like
// "pid_1234_luid_0x00000000_0x0000D1C5_phys_0_eng_3_engtype_3D".
struct GpuEngineUsage {
    double total = 0;
    std::unordered_map<uint32_t, double> byProcess;
};
GpuEngineUsage SumGpuEngines(const std::vector<std::pair<std::wstring, double>>& instances);

// True when the PC has a hardware GPU (not only the software renderer).
// Checked once.
bool HasGpu();

// Reads the GPU counters Windows keeps for Task Manager. Opened on the first
// Sample() and released by Close(), so it costs nothing while no view shows
// the GPU. Utilization needs two readings: the first one reports zero.
class GpuSampler {
public:
    GpuSampler() = default;
    ~GpuSampler();
    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;

    // `byProcess`, when given, receives each process's share by id.
    GpuInfo Sample(std::unordered_map<uint32_t, double>* byProcess = nullptr);
    void Close();
    bool IsOpen() const { return query_ != nullptr; }

private:
    bool Open();

    void* query_ = nullptr;
    void* engines_ = nullptr;
    void* memory_ = nullptr;
    bool failed_ = false;
    // The GPU with the most dedicated memory: the one whose memory is shown.
    std::wstring mainLuid_;
    uint64_t mainMemory_ = 0;
    std::vector<unsigned char> buffer_;
};

}  // namespace vela
