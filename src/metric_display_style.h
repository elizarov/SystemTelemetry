#pragma once

#include "enum_string.h"

#define SYSTEM_TELEMETRY_METRIC_DISPLAY_STYLE_ITEMS(X)                                                                 \
    X(Scalar, "scalar")                                                                                                \
    X(Percent, "percent")                                                                                              \
    X(Memory, "memory")                                                                                                \
    X(Throughput, "throughput")                                                                                        \
    X(SizeAuto, "size_auto")                                                                                           \
    X(LabelOnly, "label_only")

ENUM_STRING_DECLARE(MetricDisplayStyle, SYSTEM_TELEMETRY_METRIC_DISPLAY_STYLE_ITEMS);

#undef SYSTEM_TELEMETRY_METRIC_DISPLAY_STYLE_ITEMS
