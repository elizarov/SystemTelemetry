#include "telemetry/impl/retained_history.h"

#include "telemetry/timing.h"
#include "util/numeric_safety.h"
#include "util/text_format.h"

namespace {

constexpr size_t kMaxCachedHistoryIndex = 0xffffu - 1u;

size_t HistoryKeyIndex(RetainedHistoryKey key) {
    return static_cast<size_t>(key);
}

RetainedHistorySeries CreateRetainedHistorySeries(std::string_view seriesRef, bool throughput) {
    RetainedHistorySeries history;
    history.seriesRef = std::string(seriesRef);
    history.samples.assign(throughput ? kRetainedThroughputHistorySamples : kRetainedScalarHistorySamples, 0.0);
    if (throughput) {
        history.throughputLiveSamples.assign(kThroughputHistorySmoothingSamples, 0.0);
    }
    return history;
}

void PushHistorySample(std::vector<double>& history, double value) {
    if (history.empty()) {
        return;
    }
    history.erase(history.begin());
    history.push_back(FiniteOr(value, 0.0));
}

void NormalizeThroughputState(RetainedHistorySeries& history) {
    if (history.samples.size() != kRetainedThroughputHistorySamples) {
        history.samples.assign(kRetainedThroughputHistorySamples, 0.0);
    }
    while (history.throughputLiveSamples.size() < kThroughputHistorySmoothingSamples) {
        history.throughputLiveSamples.insert(history.throughputLiveSamples.begin(), 0.0);
    }
    if (history.throughputLiveSamples.size() > kThroughputHistorySmoothingSamples) {
        history.throughputLiveSamples.erase(
            history.throughputLiveSamples.begin(),
            history.throughputLiveSamples.begin() +
                static_cast<std::ptrdiff_t>(history.throughputLiveSamples.size() - kThroughputHistorySmoothingSamples));
    }
    if (history.throughputBucketSampleCount > kThroughputHistorySmoothingSamples) {
        history.throughputBucketTotal = 0.0;
        history.throughputBucketSampleCount = 0;
    }
}

void PushThroughputSample(RetainedHistorySeries& history, double value) {
    NormalizeThroughputState(history);
    const double sanitized = FiniteNonNegativeOr(value);

    history.throughputLiveSamples.erase(history.throughputLiveSamples.begin());
    history.throughputLiveSamples.push_back(sanitized);

    history.throughputBucketTotal += sanitized;
    ++history.throughputBucketSampleCount;
    if (history.throughputBucketSampleCount >= kThroughputHistorySmoothingSamples) {
        PushHistorySample(
            history.samples, history.throughputBucketTotal / static_cast<double>(history.throughputBucketSampleCount));
        history.throughputBucketTotal = 0.0;
        history.throughputBucketSampleCount = 0;
    }
}

bool IsThroughputSeriesRef(std::string_view seriesRef) {
    RetainedHistoryKey key = RetainedHistoryKey::Count;
    return TryRetainedHistoryKey(seriesRef, key) && IsThroughputRetainedHistoryKey(key);
}

bool TryGetCachedHistoryIndex(
    const SystemSnapshot& snapshot, RetainedHistoryKey key, std::string_view seriesRef, size_t& index) {
    const uint16_t encodedIndex = snapshot.retainedHistoryIndexByKey[HistoryKeyIndex(key)];
    if (encodedIndex == 0) {
        return false;
    }
    index = encodedIndex - 1u;
    return index < snapshot.retainedHistories.size() && snapshot.retainedHistories[index].seriesRef == seriesRef;
}

void CacheHistoryIndex(SystemSnapshot& snapshot, RetainedHistoryKey key, size_t index) {
    if (index <= kMaxCachedHistoryIndex) {
        snapshot.retainedHistoryIndexByKey[HistoryKeyIndex(key)] = static_cast<uint16_t>(index + 1u);
    }
}

void ClearHistoryKeyIndexes(SystemSnapshot& snapshot) {
    for (uint16_t& encodedIndex : snapshot.retainedHistoryIndexByKey) {
        encodedIndex = 0;
    }
}

bool FindHistoryIndex(const SystemSnapshot& snapshot, std::string_view seriesRef, size_t& index) {
    for (size_t i = 0; i < snapshot.retainedHistories.size(); ++i) {
        if (snapshot.retainedHistories[i].seriesRef == seriesRef) {
            index = i;
            return true;
        }
    }
    return false;
}

}  // namespace

void RetainedHistoryStore::Reset(SystemSnapshot& snapshot) const {
    snapshot.retainedHistories.clear();
    ClearHistoryKeyIndexes(snapshot);
}

void RetainedHistoryStore::PushSample(SystemSnapshot& snapshot, RetainedHistoryKey key, double value) const {
    const std::string_view seriesRef = RetainedHistorySeriesRef(key);
    const bool throughput = IsThroughputRetainedHistoryKey(key);
    size_t index = 0;
    if (!TryGetCachedHistoryIndex(snapshot, key, seriesRef, index)) {
        if (!FindHistoryIndex(snapshot, seriesRef, index)) {
            index = snapshot.retainedHistories.size();
            snapshot.retainedHistories.push_back(CreateRetainedHistorySeries(seriesRef, throughput));
        }
        CacheHistoryIndex(snapshot, key, index);
    }
    if (throughput) {
        PushThroughputSample(snapshot.retainedHistories[index], value);
    } else {
        PushHistorySample(snapshot.retainedHistories[index].samples, value);
    }
}

void RetainedHistoryStore::PushSample(SystemSnapshot& snapshot, const std::string& seriesRef, double value) const {
    const bool throughput = IsThroughputSeriesRef(seriesRef);
    for (auto& history : snapshot.retainedHistories) {
        if (history.seriesRef == seriesRef) {
            if (throughput) {
                PushThroughputSample(history, value);
            } else {
                PushHistorySample(history.samples, value);
            }
            return;
        }
    }
    snapshot.retainedHistories.push_back(CreateRetainedHistorySeries(seriesRef, throughput));
    if (throughput) {
        PushThroughputSample(snapshot.retainedHistories.back(), value);
    } else {
        PushHistorySample(snapshot.retainedHistories.back().samples, value);
    }
}

void RetainedHistoryStore::PushBoardMetricSamples(SystemSnapshot& snapshot) const {
    for (const auto& metric : snapshot.boardTemperatures) {
        PushSample(snapshot, FormatText("board.temp.%s", metric.name.c_str()), metric.metric.value.value_or(0.0));
    }
    for (const auto& metric : snapshot.boardFans) {
        PushSample(snapshot, FormatText("board.fan.%s", metric.name.c_str()), metric.metric.value.value_or(0.0));
    }
}
