#include "util/scale.h"

#include <algorithm>
#include <cmath>

double ScaleFromDpi(UINT dpi) {
    return static_cast<double>(std::max(kDefaultDpi, dpi)) / static_cast<double>(kDefaultDpi);
}

bool HasExplicitDisplayScale(double scale) {
    return std::isfinite(scale) && scale > 0.0;
}

double ResolveDisplayScale(double configuredScale, UINT dpi) {
    return HasExplicitDisplayScale(configuredScale) ? configuredScale : ScaleFromDpi(dpi);
}

int ScaleLogicalToPhysical(int logicalValue, double scale) {
    if (logicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(logicalValue) * scale));
}

int ScaleLogicalToPhysical(int logicalValue, UINT dpi) {
    return ScaleLogicalToPhysical(logicalValue, ScaleFromDpi(dpi));
}

int ScalePhysicalToLogical(int physicalValue, double scale) {
    if (physicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(physicalValue) / scale));
}

int ScalePhysicalToLogical(int physicalValue, UINT dpi) {
    return ScalePhysicalToLogical(physicalValue, ScaleFromDpi(dpi));
}
