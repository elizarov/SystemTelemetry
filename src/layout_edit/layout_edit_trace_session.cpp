#include "layout_edit/layout_edit_trace_session.h"

#include <iomanip>
#include <sstream>

namespace {

std::string FormatMilliseconds(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

double DurationMilliseconds(std::chrono::nanoseconds value) {
    return std::chrono::duration<double, std::milli>(value).count();
}

}  // namespace

void LayoutEditTraceSession::Begin(Trace& trace, const std::string& kind, const std::string& detail) {
    *this = {};
    active_ = true;
    kind_ = kind;
    detail_ = detail;
    startedAt_ = std::chrono::steady_clock::now();
    trace.Write("layout_edit_drag:start kind=\"" + kind + "\" detail=\"" + detail + "\"");
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

void LayoutEditTraceSession::End(Trace& trace, const std::string& reason) {
    if (!active_) {
        *this = {};
        return;
    }

    const auto appendAverage = [&](std::string& text, const char* name, const Stats& stats) {
        if (stats.samples == 0) {
            return;
        }
        const double averageMs = DurationMilliseconds(stats.total) / static_cast<double>(stats.samples);
        text += " avg_";
        text += name;
        text += "_ms=" + FormatMilliseconds(averageMs);
        text += " " + std::string(name) + "_samples=" + std::to_string(stats.samples);
    };

    const auto elapsed = std::chrono::steady_clock::now() - startedAt_;
    std::string summary =
        "layout_edit_drag:end kind=\"" + kind_ + "\" detail=\"" + detail_ + "\" reason=\"" + reason + "\" elapsed_ms=" +
        FormatMilliseconds(DurationMilliseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)));
    appendAverage(summary, "snap", snap_);
    appendAverage(summary, "apply", apply_);
    appendAverage(summary, "paint_total", paintTotal_);
    appendAverage(summary, "paint_draw", paintDraw_);
    trace.Write(summary);
    *this = {};
}
