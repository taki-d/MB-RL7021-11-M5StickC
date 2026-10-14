#pragma once

#include <stdint.h>

// Compile-time polling policy for the B-route metrics (rebuild to change).
//
// The RL7023-11 transport is intentionally serialized: only one ECHONET Lite
// request is outstanding at a time. These switches control which requests are
// placed on that queue and which Prometheus metrics are exposed.
namespace metrics_config {

constexpr bool kEnableInstantaneousPower = true;   // EPC 0xE7
// Keep current off to avoid its slower E7+E8 transaction during power polling.
constexpr bool kEnableInstantaneousCurrent = false; // EPC 0xE8
constexpr bool kEnableCumulativeEnergyImport = true; // EPC 0xE0
constexpr bool kEnableCumulativeEnergyExport = true; // EPC 0xE3
// If current is enabled, fetch E7+E8 in one transaction when it is due.
// SNA/timeout falls back
// to separate requests for the remainder of this boot.
constexpr bool kCombinePowerAndCurrent = true;
// Age since reception and the previous received-sample interval, for detecting
// gaps without confusing an unchanged watt value with a missed response.
constexpr bool kEnablePowerTimingMetrics = true;

// Minimum start-to-start interval, not a guaranteed sample period. Measured
// E7 round trips take about 2-2.4 s here; a late response delays the next send.
constexpr uint32_t kInstantaneousPowerIntervalMs = 1UL * 1000UL;
constexpr uint32_t kInstantaneousCurrentIntervalMs = 30UL * 1000UL;
constexpr uint32_t kCumulativeEnergyIntervalMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kEnergyUnitRetryIntervalMs = 60UL * 1000UL;

} // namespace metrics_config
