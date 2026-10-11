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

  // What EnteringPending reports while it waits: whatever was reported before
  // the entry attempt began, so an unconfirmed entry changes nothing visible.
  const geo::Containment before_entry = pending_from_ == Phase::Ambiguous
                                            ? geo::Containment::Uncertain
                                            : geo::Containment::Outside;

  // An Uncertain verdict is reported, never acted on: it cannot end Inside, and
  // it interrupts any confirmation run in progress.
  if (raw == geo::Containment::Uncertain) {
    agree_count_ = 0;
    switch (phase_) {
      case Phase::Inside:
        return geo::Containment::Inside;
      case Phase::ExitingPending:                 // the exit is not confirmed:
        phase_ = Phase::Inside;                   // cancel it, report no change
        return geo::Containment::Inside;
      case Phase::EnteringPending:                // interrupt the confirmation run, but
        pending_from_ = Phase::Ambiguous;         // keep the dwell clock: the entry can
        return geo::Containment::Uncertain;       // still confirm once fixes agree again
      case Phase::Outside:
      case Phase::Ambiguous:
        phase_ = Phase::Ambiguous;
        return geo::Containment::Uncertain;
    }
  }

  const bool deep_in  = sd_m < -cfg.enter_margin_m;   // sd is negative inside
  const bool clear_out = sd_m >  cfg.exit_margin_m;

  switch (phase_) {
    case Phase::Outside:
    case Phase::Ambiguous: {
      const Phase from = phase_;
      if (deep_in) {
        pending_from_ = from;
        phase_ = Phase::EnteringPending; agree_count_ = 1; pending_since_ms_ = t_ms;
        return from == Phase::Ambiguous ? geo::Containment::Uncertain
                                        : geo::Containment::Outside;
      }
      if (from == Phase::Outside) return geo::Containment::Outside;
      // Ambiguous clears like Inside does: only a run of clearly-outside fixes.
      if (clear_out) {
        if (++agree_count_ >= cfg.confirm_samples) {
          phase_ = Phase::Outside; agree_count_ = 0;
          return geo::Containment::Outside;
        }
      } else {
        agree_count_ = 0;
      }
      return geo::Containment::Uncertain;
    }

    case Phase::EnteringPending:
      if (deep_in) {
        if (++agree_count_ >= cfg.confirm_samples &&
            t_ms - pending_since_ms_ >= cfg.min_dwell_ms) {
          phase_ = Phase::Inside; inside_since_ms_ = pending_since_ms_; agree_count_ = 0;
          return geo::Containment::Inside;
        }
        return before_entry;                   // still unconfirmed
      }
      phase_ = pending_from_; agree_count_ = 0; suppressed_ = true;   // ← a flap
      return before_entry;

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
