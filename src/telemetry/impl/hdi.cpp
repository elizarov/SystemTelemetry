#include "telemetry/impl/hdi.h"

#include "telemetry/impl/hdi_default.h"

HdiFactory& ResolveHdiFactory(const HardwareDependencyInjection* injection) {
    return injection != nullptr && injection->factory != nullptr ? *injection->factory : DefaultHdiFactory();
}
