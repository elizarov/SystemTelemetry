#pragma once

class Trace;
struct BoardVendorTelemetrySample;

BoardVendorTelemetrySample CaptureBoardSensorsServiceSample(Trace& trace);
