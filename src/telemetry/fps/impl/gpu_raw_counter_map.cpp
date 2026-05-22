#include "telemetry/fps/impl/gpu_raw_counter_map.h"

#include <utility>

namespace {

std::size_t HashGpuEngineInstance(const char* value) {
    std::size_t hash = 1469598103934665603ull;
    if (value == nullptr) {
        return hash;
    }
    while (*value != '\0') {
        hash ^= static_cast<std::size_t>(static_cast<unsigned char>(*value));
        hash *= 1099511628211ull;
        ++value;
    }
    return hash;
}

}  // namespace

void GpuRawCounterMap::Clear() {
    for (Entry& entry : entries_) {
        if (entry.occupied) {
            entry.instance.clear();
            entry.occupied = false;
        }
    }
    size_ = 0;
}

void GpuRawCounterMap::Reserve(std::size_t count) {
    std::size_t target = 16;
    while (target < count * 2) {
        target *= 2;
    }
    if (entries_.size() < target) {
        entries_.clear();
        entries_.resize(target);
        size_ = 0;
    }
}

void GpuRawCounterMap::Set(const char* instance, const PDH_RAW_COUNTER& raw) {
    if (entries_.empty() || (size_ + 1) * 2 > entries_.size()) {
        Reserve(entries_.empty() ? 16 : entries_.size());
    }
    Insert(instance, raw);
}

const PDH_RAW_COUNTER* GpuRawCounterMap::Find(const char* instance) const {
    if (entries_.empty() || instance == nullptr) {
        return nullptr;
    }
    const std::size_t mask = entries_.size() - 1;
    std::size_t slot = HashGpuEngineInstance(instance) & mask;
    for (std::size_t probes = 0; probes < entries_.size(); ++probes) {
        const Entry& entry = entries_[slot];
        if (!entry.occupied) {
            return nullptr;
        }
        if (entry.instance == instance) {
            return &entry.raw;
        }
        slot = (slot + 1) & mask;
    }
    return nullptr;
}

void GpuRawCounterMap::Swap(GpuRawCounterMap& other) {
    entries_.swap(other.entries_);
    std::swap(size_, other.size_);
}

void GpuRawCounterMap::Insert(const char* instance, const PDH_RAW_COUNTER& raw) {
    const std::size_t mask = entries_.size() - 1;
    std::size_t slot = HashGpuEngineInstance(instance) & mask;
    for (;;) {
        Entry& entry = entries_[slot];
        if (!entry.occupied) {
            entry.instance = instance != nullptr ? instance : std::string{};
            entry.raw = raw;
            entry.occupied = true;
            ++size_;
            return;
        }
        if (entry.instance == instance) {
            entry.raw = raw;
            return;
        }
        slot = (slot + 1) & mask;
    }
}
