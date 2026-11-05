#include "core/gpu.h"

#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cwchar>
#include <map>

namespace vela {

int GpuInfo::MemoryPercent() const {
    return memoryTotal ? static_cast<int>((memoryUsed * 100 + memoryTotal / 2) / memoryTotal) : 0;
}

GpuEngineUsage SumGpuEngines(const std::vector<std::pair<std::wstring, double>>& instances) {
    // Each engine adds up the processes using it; the GPU is as busy as its
    // busiest engine, and so is each process.
    std::map<std::wstring, double> engines;
    std::map<std::pair<uint32_t, std::wstring>, double> processEngines;
    for (const auto& [name, value] : instances) {
        if (name.rfind(L"pid_", 0) != 0 || value <= 0) continue;
        const size_t luid = name.find(L"_luid_");
        if (luid == std::wstring::npos) continue;
        const auto pid = static_cast<uint32_t>(wcstoul(name.c_str() + 4, nullptr, 10));
        const std::wstring engine = name.substr(luid + 1);
        engines[engine] += value;
        processEngines[{pid, engine}] += value;
    }
    GpuEngineUsage usage;
    for (const auto& [engine, value] : engines) usage.total = std::max(usage.total, value);
    usage.total = std::min(usage.total, 100.0);
    for (const auto& [key, value] : processEngines) {
        double& process = usage.byProcess[key.first];
        process = std::min(std::max(process, value), 100.0);
    }
    return usage;
}

namespace {

// The hardware GPU with the most dedicated memory, as its counter instance
// prefix ("luid_0x..._0x...") and memory size. DXGI knows both.
bool FindMainGpu(std::wstring& luidName, uint64_t& memory) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
    luidName.clear();
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            (luidName.empty() || desc.DedicatedVideoMemory > memory)) {
            wchar_t luid[48];
            swprintf_s(luid, L"luid_0x%08X_0x%08X", static_cast<unsigned>(desc.AdapterLuid.HighPart),
                       static_cast<unsigned>(desc.AdapterLuid.LowPart));
            luidName = luid;
            memory = desc.DedicatedVideoMemory;
        }
        adapter->Release();
    }
    factory->Release();
    return !luidName.empty();
}

}  // namespace

bool HasGpu() {
    static const bool has = [] {
        std::wstring luid;
        uint64_t memory = 0;
        return FindMainGpu(luid, memory);
    }();
    return has;
}

GpuSampler::~GpuSampler() { Close(); }

bool GpuSampler::Open() {
    if (failed_) return false;
    FindMainGpu(mainLuid_, mainMemory_);
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engines = nullptr, memory = nullptr;
    if (mainLuid_.empty() || PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        failed_ = true;
        return false;
    }
    if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engines) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        failed_ = true;  // Windows 10 before 1709 has no GPU counters.
        return false;
    }
    PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &memory);
    query_ = query;
    engines_ = engines;
    memory_ = memory;
    PdhCollectQueryData(query);
    return true;
}

void GpuSampler::Close() {
    if (query_) PdhCloseQuery(static_cast<PDH_HQUERY>(query_));
    query_ = engines_ = memory_ = nullptr;
    std::vector<unsigned char>().swap(buffer_);
}

GpuInfo GpuSampler::Sample(std::unordered_map<uint32_t, double>* byProcess) {
    GpuInfo info;
    const bool fresh = !query_;
    if (!query_ && !Open()) return info;
    info.present = true;
    info.memoryTotal = mainMemory_;
    if (fresh) return info;  // Utilization is measured from the next reading.
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(query_)) != ERROR_SUCCESS) return info;

    // Every instance of a wildcard counter, as (name, value) pairs.
    const auto read = [this](void* counter) {
        std::vector<std::pair<std::wstring, double>> values;
        DWORD size = 0, count = 0;
        auto status = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                   &size, &count, nullptr);
        if (status != PDH_MORE_DATA) return values;
        buffer_.resize(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer_.data());
        status = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size,
                                              &count, items);
        if (status != ERROR_SUCCESS) return values;
        values.reserve(count);
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
                values.emplace_back(items[i].szName, items[i].FmtValue.doubleValue);
            }
        }
        return values;
    };

    GpuEngineUsage usage = SumGpuEngines(read(engines_));
    info.measured = true;
    info.percent = usage.total;
    if (byProcess) *byProcess = std::move(usage.byProcess);
    if (memory_) {
        for (const auto& [name, value] : read(memory_)) {
            if (name.rfind(mainLuid_, 0) == 0) info.memoryUsed += static_cast<uint64_t>(value);
        }
    }
    return info;
}

}  // namespace vela
