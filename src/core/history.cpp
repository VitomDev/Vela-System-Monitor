#include "core/history.h"

#include <algorithm>

namespace vela {

void History::Push(const Snapshot& s) {
    const auto push = [this](HistorySeries series, double value) {
        Ring& ring = rings_[static_cast<size_t>(series)];
        ring.values[ring.next] = static_cast<float>(value);
        ring.next = (ring.next + 1) % kCapacity;
        ring.count = std::min(ring.count + 1, kCapacity);
    };
    push(HistorySeries::Cpu, s.cpuPercent / 100);
    if (s.gpu.measured) push(HistorySeries::Gpu, s.gpu.percent / 100);
    push(HistorySeries::Memory, s.MemoryPercent() / 100.0);
    push(HistorySeries::Network, static_cast<double>(s.networkDown + s.networkUp));
}

std::vector<float> History::Values(HistorySeries series) const {
    const Ring& ring = rings_[static_cast<size_t>(series)];
    std::vector<float> values;
    values.reserve(ring.count);
    for (size_t i = 0; i < ring.count; ++i) values.push_back(ring.values[(ring.next + kCapacity - ring.count + i) % kCapacity]);
    if (series == HistorySeries::Network) {
        const float peak = values.empty() ? 0 : *std::max_element(values.begin(), values.end());
        for (float& value : values) value = peak > 0 ? value / peak : 0;
    }
    for (float& value : values) value = std::clamp(value, 0.0f, 1.0f);
    return values;
}

}  // namespace vela
