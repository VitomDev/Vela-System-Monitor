#pragma once

#include "core/metrics.h"

#include <array>
#include <cstdint>

namespace vela {

// Decides when to warn that CPU, GPU or memory stayed high: at 90% or more
// for 30 seconds. After a warning that metric has to drop below 80% and
// 10 minutes have to pass before it can warn again.
class UsageAlerts {
public:
    static constexpr double kHigh = 90;
    static constexpr double kLow = 80;
    static constexpr uint64_t kSustainMs = 30'000;
    static constexpr uint64_t kCooldownMs = 10 * 60'000;

    // The metric:: bit to warn about now, or 0.
    uint32_t Update(const Snapshot& snapshot, uint64_t nowMs);
    void Reset() { states_ = {}; }

private:
    struct State {
        uint64_t highSince = 0;  // 0: not high
        uint64_t warnedAt = 0;   // 0: never
        bool armed = true;
    };
    std::array<State, 3> states_{};
};

}  // namespace vela
