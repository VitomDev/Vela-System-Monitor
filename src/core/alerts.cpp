#include "core/alerts.h"

namespace vela {

uint32_t UsageAlerts::Update(const Snapshot& s, uint64_t nowMs) {
    const double values[] = {s.cpuPercent, s.gpu.present ? s.gpu.percent : 0, static_cast<double>(s.MemoryPercent())};
    constexpr uint32_t kMetrics[] = {metric::kCpu, metric::kGpu, metric::kMemory};
    uint32_t warn = 0;
    for (size_t i = 0; i < states_.size(); ++i) {
        State& state = states_[i];
        if (values[i] < kLow) state.armed = true;
        if (values[i] < kHigh) {
            state.highSince = 0;
            continue;
        }
        if (state.highSince == 0) state.highSince = nowMs;
        const bool cooled = state.warnedAt == 0 || nowMs - state.warnedAt >= kCooldownMs;
        if (!warn && state.armed && cooled && nowMs - state.highSince >= kSustainMs) {
            warn = kMetrics[i];
            state.warnedAt = nowMs;
            state.armed = false;
        }
    }
    return warn;
}

}  // namespace vela
