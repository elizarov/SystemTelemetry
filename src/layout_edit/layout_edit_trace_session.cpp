#include "layout_edit/layout_edit_trace_session.h"

#include "util/numeric_format.h"
#include "util/resource_strings.h"
#include "util/text_format.h"

namespace {

std::string FormatMilliseconds(double value) {
    return FormatDoubleFixed(value, 3);
}

double DurationMilliseconds(std::chrono::nanoseconds value) {
    return std::chrono::duration<double, std::milli>(value).count();
}

}  // namespace

void LayoutEditTraceSession::Begin(Trace& trace, ResourceStringId kind, const std::string& detail) {
    *this = {};
    active_ = true;
    kind_ = ResourceStringText(kind);
    detail_ = detail;
    startedAt_ = std::chrono::steady_clock::now();
    trace.WriteFmt(TracePrefix::Profile, RES_STR("start kind=\"%s\" detail=\"%s\""), kind_.c_str(), detail_.c_str());
}

void LayoutEditTraceSession::Record(LayoutEditHost::TracePhase phase, std::chrono::nanoseconds elapsed) {
    if (!active_) {
        return;
    }

    Stats* stats = nullptr;
    switch (phase) {
        case LayoutEditHost::TracePhase::Snap:
            stats = &snap_;
            break;
        case LayoutEditHost::TracePhase::Apply:
            stats = &apply_;
            break;
        case LayoutEditHost::TracePhase::PaintTotal:
            stats = &paintTotal_;
            break;
        case LayoutEditHost::TracePhase::PaintDraw:
            stats = &paintDraw_;
            break;
    }
    if (stats == nullptr) {
        return;
    }

    stats->total += elapsed;
    ++stats->samples;
}

void LayoutEditTraceSession::End(Trace& trace, ResourceStringId reason) {
    if (!active_) {
        *this = {};
        return;
    }

    const auto appendAverage = [&](std::string& text, const char* name, const Stats& stats) {
        if (stats.samples == 0) {
            return;
        }
        const double averageMs = DurationMilliseconds(stats.total) / static_cast<double>(stats.samples);
        AppendFormat(text,
            RES_STR(" avg_%s_ms=%s %s_samples=%zu"),
            name,
            FormatMilliseconds(averageMs).c_str(),
            name,
            stats.samples);
    };

    const auto elapsed = std::chrono::steady_clock::now() - startedAt_;
    std::string summary = FormatText(RES_STR("end kind=\"%s\" detail=\"%s\" reason=\"%s\" elapsed_ms=%s"),
        kind_.c_str(),
        detail_.c_str(),
        ResourceStringText(reason),
        FormatMilliseconds(DurationMilliseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)))
            .c_str());
    appendAverage(summary, "snap", snap_);
    appendAverage(summary, "apply", apply_);
    appendAverage(summary, "paint_total", paintTotal_);
    appendAverage(summary, "paint_draw", paintDraw_);
    trace.Write(TracePrefix::Profile, summary);
    *this = {};
}
