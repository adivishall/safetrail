#include "safetrail/power/adaptive_sampler.hpp"
#include <utility>

namespace safetrail::power {

AdaptiveSampler::AdaptiveSampler(SamplerConfig cfg) : cfg_(std::move(cfg)) {}

// GAP 7. Sampling need is a function of proximity to risk, and we already compute
// that distance for containment -- so this is nearly free.
bool AdaptiveSampler::should_sample(int64_t now_ms, double nearest_zone_m,
                                    double speed_mps, bool alert_active) {
  if (!cfg_.enabled) { ++energy_.fixes_taken; last_sample_ms_ = now_ms;
                       current_interval_ms_ = 0; return true; }

  int64_t interval = cfg_.tiers.empty() ? 1000 : cfg_.tiers.back().interval_ms;
  for (const auto& t : cfg_.tiers)
    if (nearest_zone_m <= t.max_distance_m) { interval = t.interval_ms; break; }

  if (alert_active) interval = cfg_.alert_active_interval_ms;

  // Someone moving fast covers ground between fixes, so distance alone
  // under-samples them. Shrink proportionally above the threshold.
  if (speed_mps > cfg_.fast_movement_mps && speed_mps > 0.0)
    interval = int64_t(double(interval) * cfg_.fast_movement_mps / speed_mps);
  if (interval < 200) interval = 200;

  current_interval_ms_ = interval;
  if (now_ms - last_sample_ms_ >= interval) {
    last_sample_ms_ = now_ms;
    ++energy_.fixes_taken;
    // Model: a fix costs a fixed charge increment. 10%/h at 1 Hz continuous is
    // the midpoint of the measured 8-12% range, so per-fix cost is 10/3600 %.
    energy_.estimated_pct_per_hour = 0.0;   // recomputed by the caller from rate
    return true;
  }
  ++energy_.fixes_skipped;
  return false;
}

}  // namespace safetrail::power
