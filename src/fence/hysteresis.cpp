#include "safetrail/fence/hysteresis.hpp"

namespace safetrail::fence {

// GAP 8. A Schmitt trigger over the containment signal. Three mechanisms compose:
// asymmetric thresholds create a dead band where drift lives harmlessly,
// N-consecutive confirmation trades latency for precision, and the dwell minimum
// discards corner-clipping transits.
geo::Containment HysteresisState::update(geo::Containment raw, double sd_m,
                                         int64_t t_ms, const HysteresisConfig& cfg) {
  suppressed_ = false;
  if (!cfg.enabled) {                    // the naive baseline, for the A/B
    if (raw == geo::Containment::Inside && phase_ != Phase::Inside) {
      phase_ = Phase::Inside; inside_since_ms_ = t_ms;
    } else if (raw != geo::Containment::Inside) {
      phase_ = Phase::Outside;
    }
    return raw;
  }

  // Uncertain never drives a transition -- it is reported, not acted on.
  if (raw == geo::Containment::Uncertain) {
    agree_count_ = 0;
    return phase_ == Phase::Inside ? geo::Containment::Inside : geo::Containment::Uncertain;
  }

  const bool deep_in  = sd_m < -cfg.enter_margin_m;   // sd is negative inside
  const bool clear_out = sd_m >  cfg.exit_margin_m;

  switch (phase_) {
    case Phase::Outside:
      if (deep_in) {
        phase_ = Phase::EnteringPending; agree_count_ = 1; pending_since_ms_ = t_ms;
      }
      return geo::Containment::Outside;

    case Phase::EnteringPending:
      if (deep_in) {
        if (++agree_count_ >= cfg.confirm_samples &&
            t_ms - pending_since_ms_ >= cfg.min_dwell_ms) {
          phase_ = Phase::Inside; inside_since_ms_ = pending_since_ms_; agree_count_ = 0;
          return geo::Containment::Inside;
        }
        return geo::Containment::Outside;      // still unconfirmed
      }
      phase_ = Phase::Outside; agree_count_ = 0; suppressed_ = true;   // ← a flap
      return geo::Containment::Outside;

    case Phase::Inside:
      if (clear_out) {
        phase_ = Phase::ExitingPending; agree_count_ = 1;
      }
      return geo::Containment::Inside;

    case Phase::ExitingPending:
      if (clear_out) {
        if (++agree_count_ >= cfg.confirm_samples) {
          phase_ = Phase::Outside; agree_count_ = 0;
          return geo::Containment::Outside;
        }
        return geo::Containment::Inside;
      }
      phase_ = Phase::Inside; agree_count_ = 0; suppressed_ = true;    // ← a flap
      return geo::Containment::Inside;
  }
  return raw;
}

}  // namespace safetrail::fence
