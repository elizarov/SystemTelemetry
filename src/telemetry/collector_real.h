#pragma once

#include <memory>

#include "telemetry.h"

std::unique_ptr<TelemetryCollector> CreateRealTelemetryCollector();
