// Trajectory stats and anomaly detection over the ping history.
#include <cmath>
#include "safetrail/track/anomaly.hpp"
#include "safetrail/track/trajectory.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::track;

namespace {
Ping ping(double lat, double lon, int64_t t_ms) {
  Ping p; p.fix.pos = {lat, lon}; p.fix.accuracy_m = 5.0; p.fix.t_ms = t_ms; return p;
}
}  // namespace

int main() {
  // ── Trajectory stats on a known straight walk ───────────────────────────────
  {
    Tourist t;
    // Walk east ~ four ~100 m steps, one per minute (pushed oldest-first).
    for (int i = 0; i <= 4; ++i) t.pings.push(ping(25.5, 91.8 + i * 0.001, i * 60'000));
    const double straight = displacement_m(t);
    t::ok(std::fabs(path_length_m(t) - straight) < 1.0, "straight walk: path == displacement");
    t::near(tortuosity(t), 1.0, 0.02, "straight walk has tortuosity ~1");
    t::ok(displacement_m(t) > 350.0 && displacement_m(t) < 420.0, "displacement ~400 m");
  }

  // ── Wandering has high tortuosity ───────────────────────────────────────────
  {
    Tourist t;
    // Out and back: lots of path, ~zero net displacement.
    t.pings.push(ping(25.5, 91.800, 0));
    t.pings.push(ping(25.5, 91.805, 60'000));
    t.pings.push(ping(25.5, 91.800, 120'000));   // newest = back at start
    t::ok(tortuosity(t) > 5.0, "out-and-back has high tortuosity");
  }

  // ── Signal loss: gap since newest fix exceeds the threshold ─────────────────
  {
    Tourist t;
    t.pings.push(ping(25.5, 91.8, 0));
    auto r = detect(t, 200'000);   // 200 s later, gap > 120 s
    t::ok(r.kind == AnomalyKind::SignalLost, "old newest fix -> signal lost");
    // Empty history is also signal loss.
    Tourist empty;
    t::ok(detect(empty, 0).kind == AnomalyKind::SignalLost, "no pings -> signal lost");
  }

  // ── Stationary: barely moved across a 5-min window ──────────────────────────
  {
    Tourist t;
    for (int i = 0; i <= 6; ++i)                       // 7 pings, one per minute
      t.pings.push(ping(25.5 + (i % 2) * 0.00002, 91.8, int64_t(i) * 60'000));  // ~2 m jitter
    auto r = detect(t, 6 * 60'000 + 1000);
    t::ok(r.kind == AnomalyKind::Stationary, "jitter-only over 5+ min -> stationary");
  }

  // ── A moving tourist is NOT flagged stationary ──────────────────────────────
  {
    Tourist t;
    for (int i = 0; i <= 6; ++i) t.pings.push(ping(25.5, 91.8 + i * 0.001, int64_t(i) * 60'000));
    t::ok(detect(t, 6 * 60'000 + 1000).kind == AnomalyKind::None, "steady walk -> no anomaly");
  }

  // ── Route deviation: far from the planned line ──────────────────────────────
  {
    Tourist t;
    t.planned_route = {{25.5, 91.8}, {25.5, 91.9}};    // a straight east-west route
    t.pings.push(ping(25.5, 91.85, 0));                // on-route
    t::ok(detect(t, 500).kind == AnomalyKind::None, "on the route -> no deviation");
    Tourist off;
    off.planned_route = t.planned_route;
    off.pings.push(ping(25.52, 91.85, 0));             // ~2 km north of the route
    t::ok(detect(off, 500).kind == AnomalyKind::RouteDeviation, "far from route -> deviation");
  }

  return t::report("track/anomaly");
}
