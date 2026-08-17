#pragma once
//
// Boundary flapping suppression.  [GAP 8]
//
// Documented reality: a tight geofence "will fire constantly if GPS drift nudges
// a tracker in and out of the boundary all day." Existing implementations list
// false positives as a limitation and move on. It is not an unavoidable property
// of geofencing — it is an unhandled one.
//
// Three mechanisms, all cheap:
//
//   1. ASYMMETRIC THRESHOLDS. Entering requires crossing `enter_margin_m` inside
//      the boundary; exiting requires crossing `exit_margin_m` outside. The dead
//      band between them is where drift lives harmlessly. This is a Schmitt
//      trigger, and citing it that way in the report is worth doing.
//
//   2. N-CONSECUTIVE CONFIRMATION. A transition needs `confirm_samples` fixes in
//      agreement before it is believed. Trades latency for precision.
//
//   3. DWELL MINIMUM. A tourist clipping a zone corner for 3 s is not an
//      incident. `min_dwell_ms` before an enter is reported at all.
//
// The measurable result, and the reason this module exists: run the same replay
// with injected GPS noise, hysteresis on and off, and report the false alert
// reduction. That is an experimental finding, not a feature.
//
#include <cstdint>
#include "safetrail/geo/containment.hpp"

namespace safetrail::fence {

struct HysteresisConfig {
  double  enter_margin_m   = 15.0;   // must be this far INSIDE to confirm entry
  double  exit_margin_m    = 25.0;   // must be this far OUTSIDE to confirm exit
  uint8_t confirm_samples  = 3;      // consecutive agreeing fixes required
  int64_t min_dwell_ms     = 5000;   // ignore transits shorter than this
  bool    enabled          = true;   // off = the naive baseline, for the A/B
};

// One instance per (tourist, zone) pair currently in play. Deliberately small —
// there can be tens of thousands live, so no virtuals and no heap.
class HysteresisState {
 public:
  enum class Phase : uint8_t { Outside, EnteringPending, Inside, ExitingPending };

  // Feed one observation. Returns the CONFIRMED containment, which may lag the
  // raw observation by up to confirm_samples ticks. `raw` is what the geometry
  // said; the return value is what we are willing to act on.
  geo::Containment update(geo::Containment raw, double signed_dist_m,
                          int64_t t_ms, const HysteresisConfig& cfg);

  Phase phase() const { return phase_; }
  int64_t inside_since_ms() const { return inside_since_ms_; }
  bool suppressed_last_update() const { return suppressed_; }

 private:
  Phase   phase_           = Phase::Outside;
  uint8_t agree_count_     = 0;
  int64_t inside_since_ms_ = 0;
  int64_t pending_since_ms_= 0;
  bool    suppressed_      = false;
};

}  // namespace safetrail::fence
