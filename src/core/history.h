#pragma once

#include "core/metrics.h"

#include <array>
#include <cstddef>
#include <vector>

namespace vela {

enum class HistorySeries { Cpu, Gpu, Memory, Network };

// The last readings of the metrics the panel draws as graphs. Kept by the
// app from its first reading, so a graph is already full when the panel opens.
// The GPU only has readings while it is measured.
class History {
public:
    static constexpr size_t kCapacity = 60;

    void Push(const Snapshot& snapshot);
    // Oldest first, scaled to 0-1: percents against 100, network speed
    // (down plus up) against its own peak in the series.
    std::vector<float> Values(HistorySeries series) const;

private:
    struct Ring {
        std::array<float, kCapacity> values{};
        size_t next = 0;
        size_t count = 0;
    };
    std::array<Ring, 4> rings_{};
};

}  // namespace vela
