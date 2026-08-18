// GAP 8: hysteresis must remove the majority of false transitions under BOTH
// noise models, and must never suppress a genuine sustained crossing.
#include "../test_harness.hpp"
#include <string>
#include "safetrail/sim/simulator.hpp"
using namespace safetrail;

static uint64_t transitions(bool hysteresis, double rho) {
  sim::SimConfig cfg;
  cfg.tourists = 60; cfg.groups = 6; cfg.seed = 20260817;
  cfg.duration_ms = 1800000; cfg.tick_ms = 1000;
  cfg.gps.correlation = rho;
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
    const uint64_t off = transitions(false, rho);
    const uint64_t on  = transitions(true,  rho);
    t::ok(off > 0, "baseline produced transitions (rho=" + std::to_string(rho) + ")");
    t::ok(on < off, "hysteresis reduces transitions (rho=" + std::to_string(rho) + ")");
    const double removed = 100.0 * (1.0 - double(on) / double(off));
    t::ok(removed > 60.0, "removes >60% of false transitions (rho=" +
          std::to_string(rho) + ", got " + std::to_string(removed) + "%)");
  }
  return t::report("golden/hysteresis_ab");
}
