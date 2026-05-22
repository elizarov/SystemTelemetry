#pragma once

#include "util/enum_string.h"

#define CASEDASH_METRIC_DISPLAY_STYLE_ITEMS(X) \
    X(Scalar, "scalar") \
    X(Percent, "percent") \
    X(Memory, "memory") \
    X(Throughput, "throughput") \
    X(SizeAuto, "size_auto") \
    X(LabelOnly, "label_only")

ENUM_STRING_DECLARE(MetricDisplayStyle, CASEDASH_METRIC_DISPLAY_STYLE_ITEMS);

#undef CASEDASH_METRIC_DISPLAY_STYLE_ITEMS
