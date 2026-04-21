#pragma once

#include <memory>

#include "telemetry/telemetry.h"

std::unique_ptr<TelemetryCollector> CreateRealTelemetryCollector();
