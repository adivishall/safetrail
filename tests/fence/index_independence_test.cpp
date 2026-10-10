// The central claim, tested end to end: the spatial index changes how fast the
// engine runs and NOTHING about what it says.
//
// The unit-level equivalence tests compare each index's candidate SET with brute
// force. That is necessary and not sufficient: the engine consumes candidates in
// the order the index emits them, keeps per-zone state across ticks, filters by
// validity window and reconciles zones that leave the candidate window. Any of
// those can make two correct indexes produce different event streams. So this
// runs whole workloads under all four indexes and compares every event, field
// by field, bit for bit.
//
//   A. The real simulation (OSM zones + synthetic padding), through alerts,
//      correlation into incidents, and dispatch.
//   B. A hostile evaluator workload: concave zones with holes, validity windows
//      that open and close, tourists that teleport, go silent, and return --
//      the inputs that exercise the temporal filter and state reconciliation.
#include "../test_harness.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "safetrail/fence/evaluator.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/spatial_index.hpp"
#include "safetrail/sim/simulator.hpp"

using namespace safetrail;

namespace {

const index::IndexKind kKinds[] = {index::IndexKind::BruteForce, index::IndexKind::Quadtree,
                                   index::IndexKind::RTree, index::IndexKind::Geohash};

bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

bool same_event(const fence::Event& a, const fence::Event& b) {
  return a.kind == b.kind && a.tourist == b.tourist && a.zone == b.zone && a.t_ms == b.t_ms &&
         a.containment == b.containment && same_bits(a.depth_m, b.depth_m) &&
         same_bits(a.accuracy_m, b.accuracy_m) && same_bits(a.eta_s, b.eta_s);
}

size_t first_difference(const std::vector<fence::Event>& a, const std::vector<fence::Event>& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) if (!same_event(a[i], b[i])) return i;
  return a.size() == b.size() ? SIZE_MAX : n;
}

// ── B: a hostile workload driven straight through the evaluator ──────────────
struct Workload {
  fence::ZoneStore zones;
  std::vector<std::pair<ZoneId, geo::Bbox>> boxes;
};

Workload make_workload(uint64_t seed) {
  sim::Rng rng(seed);
  Workload w;
  for (int i = 0; i < 400; ++i) {
    const double clat = rng.range(25.50, 25.60), clon = rng.range(91.80, 91.92);
    const double r = rng.range(0.0005, 0.004);
    const int verts = 5 + int(rng.below(12));
    geo::Ring ring;
    for (int v = 0; v < verts; ++v) {                        // star-shaped => simple, often concave
      const double a = 6.283185307179586 * double(v) / double(verts);
      const double rr = r * rng.range(0.45, 1.0);
      ring.push_back({clat + rr * std::sin(a), clon + rr * std::cos(a)});
    }
    fence::Zone z;
    z.severity = uint8_t(1 + rng.below(5));
    z.shape = geo::Polygon(std::move(ring));
    if (rng.uniform() < 0.3) z.shape.add_hole({{clat - r * 0.1, clon - r * 0.1},
                                               {clat - r * 0.1, clon + r * 0.1},
                                               {clat + r * 0.1, clon + r * 0.1},
                                               {clat + r * 0.1, clon - r * 0.1}});
    if (z.shape.validate() != geo::Polygon::Validity::Ok) continue;
    if (rng.uniform() < 0.4) {                               // opens and closes during the run
      const int64_t from = int64_t(rng.below(450)) * 1000;
      z.validity = {from, from + 60000 + int64_t(rng.below(450)) * 1000};
    }
    const ZoneId id = w.zones.add(std::move(z));
    w.boxes.emplace_back(id, w.zones.get(id)->shape.bbox());
  }
  return w;
}

std::vector<fence::Event> run_hostile(const Workload& w, index::IndexKind kind, uint64_t seed,
                                      fence::Evaluator::Counters* counters) {
  auto ix = index::make_index(kind);
  ix->build(w.boxes);
  fence::Evaluator ev(*ix, w.zones);
  sim::Rng rng(seed ^ 0x5157);
  std::vector<track::Tourist> ts(20);
  std::vector<geo::LatLon> pos(ts.size());
  for (size_t i = 0; i < ts.size(); ++i) {
    ts[i].id = track::TouristId(i);
    pos[i] = {rng.range(25.50, 25.60), rng.range(91.80, 91.92)};
  }
  std::vector<fence::Event> out;
  for (int64_t now = 1000; now <= 900000; now += 1000) {
    for (size_t i = 0; i < ts.size(); ++i) {
      const double u = rng.uniform();
      if (u < 0.01) {                                        // teleport: vehicle ride / GPS gap
        pos[i] = {rng.range(25.50, 25.60), rng.range(91.80, 91.92)};
        // A tracker resets its motion estimate after a gap. Without this the jump
        // sits in the 64-ping speed window for a minute, the reach radius clamps
        // to 10 km, and every query returns every zone -- correct, and slow.
        ts[i].pings.clear();
      } else if (u < 0.05) {
        continue;                                            // no fix this tick
      } else {
        pos[i] = geo::offset(pos[i], rng.range(0.0, 360.0), rng.range(0.0, 3.0));
      }
      ts[i].last_fix = {pos[i], rng.range(3.0, 40.0), now};
      track::Ping p{};
      p.fix = ts[i].last_fix;
      ts[i].pings.push(p);
      ev.evaluate(ts[i], now, out);
    }
  }
  if (counters) *counters = ev.counters();
  return out;
}

}  // namespace

int main() {
  // ── A. The full simulation ─────────────────────────────────────────────────
  {
    struct Run {
      std::vector<fence::Event> events;
      sim::Simulator::Summary sum;
      size_t lines;
    };
    std::vector<Run> runs;
    for (index::IndexKind kind : kKinds) {
      sim::SimConfig cfg;
      cfg.tourists = 40; cfg.groups = 5; cfg.seed = 777;
      cfg.duration_ms = 900000; cfg.tick_ms = 1000;
      cfg.index = kind;
      sim::Simulator s(cfg);
      std::string err;
      if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) {
        t::ok(false, "load zones: " + err);
        return t::report("fence/index_independence");
      }
      s.add_synthetic_zones(3000);
      s.spawn_tourists();
      s.run();
      runs.push_back({s.events(), s.summary(), s.dispatch_lines().size()});
    }
    t::ok(runs[0].events.size() > 100, "the simulation produced a real event stream (" +
                                           std::to_string(runs[0].events.size()) + " events)");
    for (size_t k = 1; k < runs.size(); ++k) {
      const std::string name = index::to_string(kKinds[k]);
      const size_t d = first_difference(runs[0].events, runs[k].events);
      t::ok(d == SIZE_MAX, name + ": event stream identical to brute force, bit for bit" +
                               (d == SIZE_MAX ? "" : " (first difference at #" + std::to_string(d) + ")"));
      const auto& a = runs[0].sum;
      const auto& b = runs[k].sum;
      t::ok(a.alerts == b.alerts && a.incidents == b.incidents && a.enters == b.enters &&
                a.exits == b.exits && a.uncertain == b.uncertain &&
                a.approaching == b.approaching && a.dwell == b.dwell,
            name + ": alerts, incidents and every event counter identical");
      t::ok(same_bits(a.greedy_response_m, b.greedy_response_m) &&
                same_bits(a.optimal_response_m, b.optimal_response_m) &&
                runs[0].lines == runs[k].lines,
            name + ": dispatch plan identical");
    }
  }

  // ── B. Hostile evaluator workload ──────────────────────────────────────────
  for (uint64_t seed = 11; seed < 11 + uint64_t(3 * t::stress()); ++seed) {
    const Workload w = make_workload(seed);
    fence::Evaluator::Counters c{};
    const auto base = run_hostile(w, index::IndexKind::BruteForce, seed, &c);
    t::ok(!base.empty() && c.out_of_window_observations > 0 && c.states_closed > 0,
          "seed " + std::to_string(seed) + ": workload exercises reconciliation (" +
              std::to_string(c.out_of_window_observations) + " out-of-window observations, " +
              std::to_string(c.states_closed) + " states closed by validity) and emits " +
              std::to_string(base.size()) + " events");
    for (size_t k = 1; k < 4; ++k) {
      const auto got = run_hostile(w, kKinds[k], seed, nullptr);
      const size_t d = first_difference(base, got);
      t::ok(d == SIZE_MAX, "seed " + std::to_string(seed) + ", " + index::to_string(kKinds[k]) +
                               ": identical events under teleports, gaps and validity windows" +
                               (d == SIZE_MAX ? "" : " (first difference at #" + std::to_string(d) + ")"));
    }
  }

  return t::report("fence/index_independence");
}
