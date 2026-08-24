#pragma once
// Anomaly detection over the ping history.
//
// Not every emergency trips a geofence. Three patterns in a tourist's own track
// signal trouble on their own, and each is cheap to read off the recent pings:
//
//   - SignalLost  -- no fix for too long. The device died, or they walked into a
//                    canyon/cave. Either way the operator has lost them.
//   - Stationary  -- barely moved for a long window. Possible injury or collapse
//                    (distinct from "resting", which the timeout is tuned for).
//   - RouteDeviation -- strayed too far from the planned route.
//
// These map to Alert kinds (SignalLost, Stationary, RouteDeviation). Detection is
// O(window) over the circular buffer plus O(route length) for the deviation test.
#include <cstdint>
#include "safetrail/track/tourist.hpp"

namespace safetrail::track {

enum class AnomalyKind { None, SignalLost, Stationary, RouteDeviation };
const char* to_string(AnomalyKind k);

struct AnomalyConfig {
  int64_t signal_gap_ms       = 120'000;   // no fix for 2 min -> signal lost
  double  stationary_radius_m = 15.0;       // moved less than this...
  int64_t stationary_window_ms = 300'000;   // ...across 5 min of history -> stationary
  double  route_deviation_m   = 200.0;      // farther than this from the route -> deviation
};

struct AnomalyResult {
  AnomalyKind kind  = AnomalyKind::None;
  double      value = 0.0;   // the measured quantity (gap ms, radius m, or deviation m)
};

// Evaluate the tourist's newest state against their history and planned route.
// Returns the single most serious anomaly (signal loss first -- no data trumps
// everything -- then stationarity, then route deviation), or None.
AnomalyResult detect(const Tourist& t, int64_t now_ms, const AnomalyConfig& cfg = {});

}  // namespace safetrail::track
