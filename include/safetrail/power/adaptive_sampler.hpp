#pragma once
//
// Risk-adaptive GPS sampling.  [GAP 7]
//
// Measured: continuous GPS polling costs 8-12% of battery per hour. A tourist on
// a full-day trek whose phone dies at 3pm is less safe than one who was never
// tracked at all — so a monitoring system that flattens the battery is actively
// counterproductive, and every existing implementation polls at a fixed interval.
//
// The insight is obvious once stated: sampling need is a function of PROXIMITY TO
// RISK, and we already compute that distance for containment.
//
//   > 5 km from any zone     →  every 5 min      (near-zero cost)
//   1-5 km                   →  every 60 s
//   200 m - 1 km             →  every 15 s
//   < 200 m                  →  every 2 s        (full rate)
//   inside a zone            →  every 2 s
//
// Distance comes free from SpatialIndex::nearest() plus signed_distance_m(), both
// of which the evaluator already calls.
//
// The reportable result: projected battery life and total fix count against
// fixed-interval polling on the same scenario, at identical alert recall. If
// recall drops you have gone too far, and showing that trade-off curve is the
// interesting part.
//
#include <cstdint>
#include <vector>

namespace safetrail::power {

struct SamplingTier {
  double  max_distance_m;
  int64_t interval_ms;
};

struct SamplerConfig {
  // Ordered nearest-first. Evaluated top to bottom, first match wins.
  std::vector<SamplingTier> tiers = {
      {   200.0,   2000},
      {  1000.0,  15000},
      {  5000.0,  60000},
      {1e12,      300000},
  };

  // Emergency override: an open alert on this tourist forces full rate
  // regardless of distance.
  int64_t alert_active_interval_ms = 1000;

  // Speed override: someone moving fast covers ground between fixes, so distance
  // alone under-samples them. Above this, shrink the interval proportionally.
  double  fast_movement_mps = 5.0;

  bool enabled = true;   // off = fixed full-rate polling, the A/B baseline
};

// Per-tourist rate controller. One instance each; keep it small.
class AdaptiveSampler {
 public:
  explicit AdaptiveSampler(SamplerConfig cfg = {});

  // Called every tick. Cheap — no geometry, just a tier lookup against the
  // distance the evaluator already has.
  bool should_sample(int64_t now_ms, double nearest_zone_m, double speed_mps,
                     bool alert_active);

  void note_sampled(int64_t now_ms) { last_sample_ms_ = now_ms; }
  int64_t current_interval_ms() const { return current_interval_ms_; }

  // Energy accounting for the report. Model: one fix costs a fixed charge
  // increment; battery percent per hour follows from fix rate.
  struct Energy {
    uint64_t fixes_taken   = 0;
    uint64_t fixes_skipped = 0;
    double   estimated_pct_per_hour = 0.0;
    double   savings_vs_continuous() const {
      const double kContinuousPctPerHour = 10.0;   // midpoint of measured 8-12%
      return kContinuousPctPerHour > 0.0
                 ? 1.0 - estimated_pct_per_hour / kContinuousPctPerHour
                 : 0.0;
    }
  };
  Energy energy() const { return energy_; }

 private:
  SamplerConfig cfg_;
  int64_t last_sample_ms_     = 0;
  int64_t current_interval_ms_= 0;
  Energy  energy_{};
};

}  // namespace safetrail::power
