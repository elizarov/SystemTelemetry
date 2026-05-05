#include "telemetry/impl/retained_history.h"

#include "util/numeric_safety.h"

namespace {

constexpr size_t kRecentHistorySamples = 60;
constexpr size_t kMaxCachedHistoryIndex = 0xffffu - 1u;

size_t HistoryKeyIndex(RetainedHistoryKey key) {
    return static_cast<size_t>(key);
}

RetainedHistorySeries CreateRetainedHistorySeries(std::string_view seriesRef) {
    RetainedHistorySeries history;
    history.seriesRef = std::string(seriesRef);
    history.samples.assign(kRecentHistorySamples, 0.0);
    return history;
}

void PushHistorySample(std::vector<double>& history, double value) {
    if (history.empty()) {
        return;
    }
    history.erase(history.begin());
    history.push_back(FiniteOr(value, 0.0));
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
    size_t index = 0;
    if (!TryGetCachedHistoryIndex(snapshot, key, seriesRef, index)) {
        if (!FindHistoryIndex(snapshot, seriesRef, index)) {
            index = snapshot.retainedHistories.size();
            snapshot.retainedHistories.push_back(CreateRetainedHistorySeries(seriesRef));
        }
        CacheHistoryIndex(snapshot, key, index);
    }
    PushHistorySample(snapshot.retainedHistories[index].samples, value);
}

void RetainedHistoryStore::PushSample(SystemSnapshot& snapshot, const std::string& seriesRef, double value) const {
    for (auto& history : snapshot.retainedHistories) {
        if (history.seriesRef == seriesRef) {
            PushHistorySample(history.samples, value);
            return;
        }
    }
    snapshot.retainedHistories.push_back(CreateRetainedHistorySeries(seriesRef));
    PushHistorySample(snapshot.retainedHistories.back().samples, value);
}

void RetainedHistoryStore::PushBoardMetricSamples(SystemSnapshot& snapshot) const {
    for (const auto& metric : snapshot.boardTemperatures) {
        PushSample(snapshot, "board.temp." + metric.name, metric.metric.value.value_or(0.0));
    }
    for (const auto& metric : snapshot.boardFans) {
        PushSample(snapshot, "board.fan." + metric.name, metric.metric.value.value_or(0.0));
    }
}
