// Determinism: same seed -> byte-identical event stream. This is what makes the
// replay harness and every A/B comparison valid.
#include "../test_harness.hpp"
#include "safetrail/sim/simulator.hpp"
#include <string>
using namespace safetrail;

static std::string run_signature(uint64_t seed) {
  sim::SimConfig cfg;
  cfg.tourists = 30; cfg.groups = 4; cfg.seed = seed;
  cfg.duration_ms = 600000; cfg.tick_ms = 1000;
  sim::Simulator s(cfg);
  std::string err;
  if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) return "LOAD_FAIL:" + err;
  s.spawn_tourists();
  s.run();
  std::string sig;
  for (const auto& e : s.events())
    sig += std::to_string(int(e.kind)) + ":" + std::to_string(e.tourist) + ":" +
           std::to_string(e.zone) + ":" + std::to_string(e.t_ms) + ";";
  return sig;
}

int main() {
  const std::string a = run_signature(20260817);
  const std::string b = run_signature(20260817);
  t::ok(!a.empty() && a.rfind("LOAD_FAIL",0)!=0, "run produced events");
  t::ok(a == b, "same seed -> byte-identical event stream (determinism)");

  const std::string c = run_signature(42);
  t::ok(a != c, "different seed -> different event stream");
  return t::report("golden/scenario_replay");
}
