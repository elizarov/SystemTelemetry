#include "telemetry/impl/retained_history.h"

#include "util/numeric_safety.h"

namespace {

constexpr size_t kRecentHistorySamples = 60;

RetainedHistorySeries CreateRetainedHistorySeries(const std::string& seriesRef) {
    RetainedHistorySeries history;
    history.seriesRef = seriesRef;
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

}  // namespace

void RebuildRetainedHistoryIndex(SystemSnapshot& snapshot) {
    snapshot.retainedHistoryIndexByRef.clear();
    snapshot.retainedHistoryIndexByRef.reserve(snapshot.retainedHistories.size());
    for (size_t i = 0; i < snapshot.retainedHistories.size(); ++i) {
        snapshot.retainedHistoryIndexByRef[snapshot.retainedHistories[i].seriesRef] = i;
    }
}

void RetainedHistoryStore::Reset(SystemSnapshot& snapshot) const {
    snapshot.retainedHistories.clear();
    snapshot.retainedHistoryIndexByRef.clear();
}

void RetainedHistoryStore::PushSample(SystemSnapshot& snapshot, const std::string& seriesRef, double value) const {
    auto it = snapshot.retainedHistoryIndexByRef.find(seriesRef);
    if (it == snapshot.retainedHistoryIndexByRef.end()) {
        const size_t index = snapshot.retainedHistories.size();
        snapshot.retainedHistories.push_back(CreateRetainedHistorySeries(seriesRef));
        snapshot.retainedHistoryIndexByRef.emplace(seriesRef, index);
        it = snapshot.retainedHistoryIndexByRef.find(seriesRef);
    }
    PushHistorySample(snapshot.retainedHistories[it->second].samples, value);
}

void RetainedHistoryStore::PushBoardMetricSamples(SystemSnapshot& snapshot) const {
    for (const auto& metric : snapshot.boardTemperatures) {
        PushSample(snapshot, "board.temp." + metric.name, metric.metric.value.value_or(0.0));
    }
    for (const auto& metric : snapshot.boardFans) {
        PushSample(snapshot, "board.fan." + metric.name, metric.metric.value.value_or(0.0));
    }
}
