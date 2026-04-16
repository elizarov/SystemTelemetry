#pragma once

// Private network collector module marker header. The TelemetryCollector::Impl
// member declarations stay in collector_internal.h; this header exists so the
// translation unit keeps an explicit private module boundary and passes the
// header/implementation ownership lint checks.
