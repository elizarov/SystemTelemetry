#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "tools/impl/tools_progress.h"

constexpr size_t kToolAutoConcurrency = 0;

bool ParseToolConcurrency(
    std::string_view value,
    size_t& concurrency,
    std::string& error,
    std::string_view optionName = "--concurrency"
);
size_t DefaultToolConcurrency();
size_t EffectiveToolConcurrency(size_t workSize, size_t requestedConcurrency);

template <typename Worker>
void RunToolParallelFor(size_t workSize, size_t requestedConcurrency, ToolFileProgress* progress, Worker&& worker) {
    if (workSize == 0) {
        if (progress != nullptr) {
            progress->Finish(0);
        }
        return;
    }

    const size_t workerCount = EffectiveToolConcurrency(workSize, requestedConcurrency);
    if (workerCount <= 1) {
        for (size_t index = 0; index < workSize; ++index) {
            worker(index);
            if (progress != nullptr) {
                progress->Update(index + 1);
            }
        }
        if (progress != nullptr) {
            progress->Finish(workSize);
        }
        return;
    }

    std::atomic<size_t> nextIndex = 0;
    std::atomic<size_t> completedCount = 0;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        workers.emplace_back([&]() {
            for (;;) {
                const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (index >= workSize) {
                    return;
                }
                worker(index);
                completedCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    while (completedCount.load(std::memory_order_relaxed) < workSize) {
        if (progress != nullptr) {
            progress->Update(completedCount.load(std::memory_order_relaxed));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
    if (progress != nullptr) {
        progress->Finish(completedCount.load(std::memory_order_relaxed));
    }
}
