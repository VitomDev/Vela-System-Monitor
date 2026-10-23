#pragma once

#include "core/metrics.h"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace vela {

// Reads the system counters. Rates are measured between two calls to
// Sample(), so the first snapshot reports them as zero.
class Sampler {
public:
    Sampler();
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Snapshot Sample();

private:
    void FindNetworkInterfaces();
    bool ReadNetwork(uint64_t& received, uint64_t& sent);
    bool ReadDiskCounters(uint64_t& read, uint64_t& written);

    bool first_ = true;
    uint64_t lastTick_ = 0;
    CpuTimes cpu_;
    uint64_t received_ = 0;
    uint64_t sent_ = 0;
    uint64_t diskRead_ = 0;
    uint64_t diskWritten_ = 0;

    // Physical adapters only, so virtual and filter interfaces are not counted twice.
    std::vector<uint32_t> interfaces_;
    uint64_t interfacesFoundAt_ = 0;

    wchar_t diskRoot_[4] = L"C:\\";
    HANDLE volume_ = INVALID_HANDLE_VALUE;
};

}  // namespace vela
