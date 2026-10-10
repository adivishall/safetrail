// GAP 8: the hysteresis A/B, gated on ground truth.
//
// Comparing filter-off with filter-on under noise only shows the filter reports
// fewer transitions; it cannot tell a suppressed flap from a suppressed real
// crossing (DEFECT_LOG pass 6). The check that matters is against the same
// trajectories with noise-free fixes and the same policy -- the "target" in
// `make bench` section 10: under realistic drift the filtered noisy run must
// report about what perfect GPS would. White noise is not held to the same band:
// losing a third of the target there is the documented price of suppressing
// ~20,000 flaps, and the bench reports it rather than this test hiding it.
#include "../test_harness.hpp"
#include <string>
#include "safetrail/sim/simulator.hpp"
using namespace safetrail;

static uint64_t transitions(bool hysteresis, double rho, bool noisy) {
  sim::SimConfig cfg;
  cfg.tourists = 60; cfg.groups = 6; cfg.seed = 20260817;
  cfg.duration_ms = 1800000; cfg.tick_ms = 1000;
  cfg.gps.correlation = rho;
  if (!noisy) { cfg.gps.open_sky_m = 0.01; cfg.gps.multipath_m = 0.01; }
  cfg.eval.hysteresis.enabled = hysteresis;
  sim::Simulator s(cfg);
  std::string err;
  if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) return 0;
  s.spawn_tourists();
  s.run();
  return s.summary().enters + s.summary().exits;
}

int main() {
  for (double rho : {0.0, 0.9}) {
    const std::string tag = " (rho=" + std::to_string(rho) + ")";
    const uint64_t off = transitions(false, rho, true);
    const uint64_t on  = transitions(true,  rho, true);
    t::ok(off > 0, "baseline produced transitions" + tag);
    t::ok(on < off, "hysteresis reduces transitions" + tag);
    const double removed = 100.0 * (1.0 - double(on) / double(off));
    t::ok(removed > 60.0, "filter reports <40% of the unfiltered run's transitions" + tag +
          ", got " + std::to_string(100.0 - removed) + "%");

    const uint64_t target = transitions(true, rho, false);   // noise-free, same policy
    t::ok(target > 0, "noise-free target produced transitions" + tag);
    const double vs_target = 100.0 * double(on) / double(target);
    t::ok(vs_target <= 115.0, "filtered noisy run does not exceed the noise-free target by >15%" +
          tag + ", got " + std::to_string(vs_target) + "%");
    if (rho > 0.5)
      t::ok(vs_target >= 85.0, "under realistic drift the filter keeps >=85% of the noise-free "
            "target, got " + std::to_string(vs_target) + "%");
  }
  return t::report("golden/hysteresis_ab");
}
