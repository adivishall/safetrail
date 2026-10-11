// Per-zone state must be observed on every fix, not only when the index happens
// to return the zone.
//
// The bug this pins: the evaluator advanced a (tourist, zone) state only for
// zones in this tick's candidate set. A tourist confirmed Inside who then left
// the zone's candidate window in one step (GPS gap, vehicle ride, phone off) was
// never observed against it again -- no ZoneExit, state stuck at Inside, and a
// later genuine re-entry produced NO ZoneEnter because the state already said
// Inside. A zone whose validity window closed while someone was inside did the
// same. Both are silent missed alerts in a safety system.
#include "../test_harness.hpp"

#include <string>
#include <vector>

#include "safetrail/fence/evaluator.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/quadtree.hpp"

using namespace safetrail;
using namespace safetrail::fence;

namespace {

geo::Ring square(double lat, double lon, double half) {
  return {{lat - half, lon - half}, {lat - half, lon + half},
          {lat + half, lon + half}, {lat + half, lon - half}};
}

struct World {
  ZoneStore zones;
  index::Quadtree qt;
  explicit World(std::vector<Zone> zs) {
    std::vector<std::pair<ZoneId, geo::Bbox>> items;
    for (auto& z : zs) {
      const ZoneId id = zones.add(z);
      items.emplace_back(id, zones.get(id)->shape.bbox());
    }
    qt.build(items);
  }
};

Zone zone_at(double lat, double lon, double half) {
  Zone z;
  z.shape = geo::Polygon(square(lat, lon, half));
  return z;
}

size_t count(const std::vector<Event>& ev, EventKind k, ZoneId zone) {
  size_t n = 0;
  for (const auto& e : ev) n += (e.kind == k && e.zone == zone);
  return n;
}

// One fix per second at `p`, for `secs` seconds.
void walk(Evaluator& ev, track::Tourist& t, geo::LatLon p, int secs, int64_t& now,
          std::vector<Event>& out) {
  for (int i = 0; i < secs; ++i, now += 1000) {
    t.last_fix = {p, 5.0, now};
    ev.evaluate(t, now, out);
  }
}

const geo::LatLon kInside{25.51, 91.81};
const geo::LatLon kFar{25.80, 91.81};          // ~32 km north: far outside any query box

}  // namespace

int main() {
  // ── 1. Leaving the candidate window in one step ────────────────────────────
  {
    World w({zone_at(25.51, 91.81, 0.01)});
    Evaluator ev(w.qt, w.zones);
    track::Tourist t; t.id = 1;
    std::vector<Event> out;
    int64_t now = 0;
    walk(ev, t, kInside, 20, now, out);
    t::ok(count(out, EventKind::ZoneEnter, 0) == 1, "entered once");

    walk(ev, t, kFar, 10, now, out);
    t::ok(count(out, EventKind::ZoneExit, 0) == 1,
          "a jump out of the candidate window still produces exactly one ZoneExit");
    t::ok(t.peek_state(0) == nullptr, "and the settled state is dropped (bounded state)");
    t::ok(ev.counters().out_of_window_observations >= 3,
          "the exit came from out-of-window observations, not from a lucky candidate");

    walk(ev, t, kInside, 20, now, out);
    t::ok(count(out, EventKind::ZoneEnter, 0) == 2,
          "re-entering after the jump raises a fresh ZoneEnter (it used to be silent)");
  }

  // ── 2. One wild fix must not force an exit: hysteresis still applies ───────
  {
    World w({zone_at(25.51, 91.81, 0.01)});
    Evaluator ev(w.qt, w.zones);
    track::Tourist t; t.id = 2;
    std::vector<Event> out;
    int64_t now = 0;
    walk(ev, t, kInside, 20, now, out);
    walk(ev, t, kFar, 1, now, out);                  // a single multipath outlier
    walk(ev, t, kInside, 20, now, out);
    t::ok(count(out, EventKind::ZoneEnter, 0) == 1 && count(out, EventKind::ZoneExit, 0) == 0,
          "a single out-of-window fix is filtered like any other flap: no exit, no re-entry");
  }

  // ── 3. The zone goes out of force while the tourist is inside ──────────────
  {
    Zone z = zone_at(25.51, 91.81, 0.01);
    z.validity = {0, 60000};                         // in force for the first minute
    World w({z});
    Evaluator ev(w.qt, w.zones);
    track::Tourist t; t.id = 3;
    std::vector<Event> out;
    int64_t now = 0;
    walk(ev, t, kInside, 70, now, out);
    t::ok(count(out, EventKind::ZoneEnter, 0) == 1, "entered while in force");
    bool exit_at_expiry = false;
    for (const auto& e : out)
      if (e.kind == EventKind::ZoneExit && e.t_ms == 60000) exit_at_expiry = true;
    t::ok(exit_at_expiry, "ZoneExit at the instant the validity window closes (t=60 s)");
    t::ok(t.peek_state(0) == nullptr, "and the state is closed");

    // The rule comes back into force with the tourist still standing inside.
    w.zones.get_mut(0)->validity = {100000, kForever};
    walk(ev, t, kInside, 60, now, out);
    t::ok(count(out, EventKind::ZoneEnter, 0) == 2,
          "reactivation with the tourist inside raises a fresh ZoneEnter");
  }

  // ── 4. The zone is deleted from the store while the tourist is inside ──────
  {
    World w({zone_at(25.51, 91.81, 0.01), zone_at(25.60, 91.90, 0.01)});
    Evaluator ev(w.qt, w.zones);
    track::Tourist t; t.id = 4;
    std::vector<Event> out;
    int64_t now = 0;
    walk(ev, t, kInside, 20, now, out);
    w.zones.remove(0);                               // index still returns it
    walk(ev, t, kInside, 5, now, out);
    t::ok(count(out, EventKind::ZoneExit, 0) == 1, "a deleted zone closes with a ZoneExit");
    t::ok(ev.counters().states_closed >= 1, "counted as a closed state");
  }

  // ── 5. State stays bounded by the candidate window ─────────────────────────
  {
    std::vector<Zone> zs;
    for (int i = 0; i < 60; ++i) zs.push_back(zone_at(25.50, 91.80 + 0.01 * i, 0.002));
    World w(zs);
    Evaluator ev(w.qt, w.zones);
    track::Tourist t; t.id = 5;
    std::vector<Event> out;
    int64_t now = 0;
    size_t peak = 0;
    for (int i = 0; i < 60; ++i) {                   // visit every zone in turn
      walk(ev, t, {25.50, 91.80 + 0.01 * i}, 15, now, out);
      peak = std::max(peak, t.zone_states.size());
    }
    t::ok(count(out, EventKind::ZoneEnter, 59) == 1, "the walk really entered the last zone");
    t::ok(peak <= 6, "zone_states never grows beyond the neighbourhood (peak " +
                         std::to_string(peak) + " of 60 zones visited)");
    size_t exits = 0;
    for (const auto& e : out) exits += e.kind == EventKind::ZoneExit;
    t::ok(exits == 59, "every zone left behind got its ZoneExit (" + std::to_string(exits) + ")");
  }

  // ── 6. The capped policy must not reconcile what it merely skipped ─────────
  //
  // Under NearestFirstCapped a candidate beyond the cap was RETURNED by the
  // index (it may contain the fix) but not evaluated. Treating it as a certain
  // Outside would fabricate an exit, so its state is left untouched.
  {
    std::vector<Zone> zs;
    for (int i = 0; i < 5; ++i) zs.push_back(zone_at(25.51, 91.81, 0.01 + 0.001 * i));
    World w(zs);                                     // five nested squares
    EvaluatorConfig cfg;
    cfg.max_candidates = 5;
    cfg.candidate_policy = CandidatePolicy::NearestFirstCapped;
    Evaluator ev(w.qt, w.zones, cfg);
    track::Tourist t; t.id = 6;
    std::vector<Event> out;
    int64_t now = 0;
    walk(ev, t, kInside, 20, now, out);
    size_t enters = 0;
    for (const auto& e : out) enters += e.kind == EventKind::ZoneEnter;
    t::ok(enters == 5, "all five nested zones entered while under the cap");

    // A sixth, more severe zone, equally close, outranks them and pushes one real
    // container (zone 4, the highest id among equal ranks) past the cap.
    ZoneStore& zones = w.zones;
    Zone extra = zone_at(25.51, 91.81, 0.0095);
    extra.severity = 5;
    const ZoneId xid = zones.add(extra);
    w.qt.insert(xid, zones.get(xid)->shape.bbox());
    walk(ev, t, kInside, 10, now, out);
    size_t exits = 0;
    for (const auto& e : out) exits += e.kind == EventKind::ZoneExit;
    t::ok(exits == 0, "a candidate dropped by the cap is not given a fabricated ZoneExit");
    t::ok(ev.counters().candidates_dropped > 0, "and the drop is still counted");
  }

  return t::report("fence/state_reconciliation");
}
