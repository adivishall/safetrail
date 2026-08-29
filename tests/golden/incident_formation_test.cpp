// GAP 5, end to end. The unit tests exercised the correlator on a single hand-fed
// batch and all passed -- while the running product compressed barely 1.19:1,
// because the correlator never accumulated an incident across ticks and the
// scattered population never converged. This is the integration test that gap
// left open: drive the whole simulator and assert the marquee behaviour actually
// happens -- a scripted cohort collapses into ONE incident with many people, and
// a scattered run (scenario off) produces no such mass incident.
#include "../test_harness.hpp"
#include <algorithm>
#include <cstddef>
#include <string>
#include "safetrail/sim/simulator.hpp"
using namespace safetrail;

static size_t biggest_incident(const sim::Simulator& s) {
  size_t mp = 0;
  for (const auto* inc : s.correlator().open_incidents())
    mp = std::max(mp, inc->people());
  return mp;
}

int main() {
  sim::SimConfig cfg;
  cfg.tourists = 60; cfg.groups = 6; cfg.seed = 20260817;
  cfg.duration_ms = 3600000; cfg.tick_ms = 1000;

  sim::Simulator s(cfg);
  std::string err;
  if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) {
    t::ok(false, "zones load: " + err);
    return t::report("golden/incident_formation");
  }
  s.spawn_tourists();
  s.run();

  const size_t mass = biggest_incident(s);
  const double ratio = s.correlator().stats().compression_ratio();

  t::ok(!s.correlator().open_incidents().empty(), "the run opens at least one incident");
  t::ok(mass >= 15,
        "the scripted cohort collapses into ONE incident with many people (got " +
        std::to_string(mass) + ")");
  t::ok(ratio >= 20.0,
        "alert correlation compresses hard, not ~1:1 (got " +
        std::to_string(ratio) + " alerts/incident)");

  // Scenario off: the population is scattered, so no mass incident forms. This is
  // what proves the compression is the correlator working on a real convergence,
  // not an artefact of how incidents are counted.
  sim::SimConfig plain = cfg;
  plain.scenario.enabled = false;
  sim::Simulator s2(plain);
  if (!s2.load_zones("data/zones/shillong_osm.geojson", &err)) {
    t::ok(false, "zones load (plain): " + err);
    return t::report("golden/incident_formation");
  }
  s2.spawn_tourists();
  s2.run();
  const size_t plain_mass = biggest_incident(s2);

  t::ok(plain_mass < mass,
        "a scattered run forms a smaller mass incident than the scripted one (" +
        std::to_string(plain_mass) + " < " + std::to_string(mass) + ")");
  t::ok(plain_mass <= 10, "no accidental mass incident without the scenario (got " +
        std::to_string(plain_mass) + ")");

  return t::report("golden/incident_formation");
}
