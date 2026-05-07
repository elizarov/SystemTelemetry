#pragma once

#include <cstddef>
#include <pdh.h>
#include <string>
#include <vector>

class GpuRawCounterMap {
public:
    void Clear();
    void Reserve(std::size_t count);
    void Set(const wchar_t* instance, const PDH_RAW_COUNTER& raw);
    const PDH_RAW_COUNTER* Find(const wchar_t* instance) const;
    void Swap(GpuRawCounterMap& other);

private:
    struct Entry {
        std::wstring instance;
        PDH_RAW_COUNTER raw{};
        bool occupied = false;
    };

    void Insert(const wchar_t* instance, const PDH_RAW_COUNTER& raw);

    std::vector<Entry> entries_;
    std::size_t size_ = 0;
};
