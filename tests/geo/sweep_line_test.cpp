// Sweep-line intersection detection vs an O(n^2) all-pairs oracle, and vs
// Polygon::validate() for the self-intersection verdict.
#include <cmath>
#include <vector>
#include "safetrail/geo/polygon.hpp"
#include "safetrail/geo/sweep_line.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::geo;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double range(double lo, double hi) { return lo + (hi - lo) * (double(next() >> 11) * (1.0 / 9007199254740992.0)); }
};

int orient(double ax, double ay, double bx, double by, double cx, double cy) {
  const double v = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  return v > 1e-14 ? 1 : (v < -1e-14 ? -1 : 0);
}
bool on_seg(double ax, double ay, double bx, double by, double px, double py) {
  return std::fmin(ax, bx) - 1e-14 <= px && px <= std::fmax(ax, bx) + 1e-14 &&
         std::fmin(ay, by) - 1e-14 <= py && py <= std::fmax(ay, by) + 1e-14;
}
bool cross(const Segment& s, const Segment& t) {
  const double ax = s.a.lon, ay = s.a.lat, bx = s.b.lon, by = s.b.lat;
  const double cx = t.a.lon, cy = t.a.lat, dx = t.b.lon, dy = t.b.lat;
  const int o1 = orient(ax, ay, bx, by, cx, cy), o2 = orient(ax, ay, bx, by, dx, dy);
  const int o3 = orient(cx, cy, dx, dy, ax, ay), o4 = orient(cx, cy, dx, dy, bx, by);
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && on_seg(ax, ay, bx, by, cx, cy)) return true;
  if (o2 == 0 && on_seg(ax, ay, bx, by, dx, dy)) return true;
  if (o3 == 0 && on_seg(cx, cy, dx, dy, ax, ay)) return true;
  if (o4 == 0 && on_seg(cx, cy, dx, dy, bx, by)) return true;
  return false;
}
// O(n^2) oracle over a segment set.
bool oracle_any(const std::vector<Segment>& segs) {
  for (size_t i = 0; i < segs.size(); ++i)
    for (size_t j = i + 1; j < segs.size(); ++j)
      if (cross(segs[i], segs[j])) return true;
  return false;
}
}  // namespace

int main() {
  Rng rng(0x5EEBEE);

  // ── Random segment sets: sweep == O(n^2) oracle ─────────────────────────────
  // Coordinates are random floats -> general position, so no exact-tie ambiguity.
  int agree = 0, trials = 300;
  for (int trial = 0; trial < trials; ++trial) {
    const int n = 2 + int(rng.next() % 12);
    std::vector<Segment> segs;
    for (int i = 0; i < n; ++i)
      segs.push_back({{rng.range(0, 100), rng.range(0, 100)},
                      {rng.range(0, 100), rng.range(0, 100)}});
    if (any_intersection(segs) == oracle_any(segs)) ++agree;
  }
  t::ok(agree == trials, "sweep == O(n^2) oracle on random segment sets (" +
                          std::to_string(agree) + "/" + std::to_string(trials) + ")");

  // ── Hand-built cases ────────────────────────────────────────────────────────
  {
    std::vector<Segment> x = {{{0, 0}, {10, 10}}, {{0, 10}, {10, 0}}};   // an X
    t::ok(any_intersection(x), "crossing pair detected");
    std::vector<Segment> par = {{{0, 0}, {10, 0}}, {{0, 5}, {10, 5}}};   // parallel
    t::ok(!any_intersection(par), "parallel pair: no intersection");
    std::vector<Segment> tee = {{{0, 0}, {10, 0}}, {{5, 0}, {5, 5}}};    // T-touch
    t::ok(any_intersection(tee), "touching endpoint detected");
  }

  // ── Polygons: sweep verdict == Polygon::validate() self-intersection ────────
  int poly_agree = 0, poly_trials = 300;
  for (int trial = 0; trial < poly_trials; ++trial) {
    const int n = 3 + int(rng.next() % 8);
    Ring r;
    for (int i = 0; i < n; ++i) r.push_back({rng.range(25.0, 26.0), rng.range(91.0, 92.0)});
    Polygon poly(r);
    const bool validate_says = (poly.validate() == Polygon::Validity::SelfIntersecting);
    const bool sweep_says = polygon_self_intersects(poly);
    if (validate_says == sweep_says) ++poly_agree;
  }
  t::ok(poly_agree == poly_trials, "sweep self-intersection == validate() (" +
                                    std::to_string(poly_agree) + "/" +
                                    std::to_string(poly_trials) + ")");

  // A clean convex square is not self-intersecting; a bowtie is.
  {
    Polygon square(Ring{{25.0, 91.0}, {25.0, 91.1}, {25.1, 91.1}, {25.1, 91.0}});
    t::ok(!polygon_self_intersects(square), "square is simple");
    Polygon bowtie(Ring{{25.0, 91.0}, {25.1, 91.1}, {25.0, 91.1}, {25.1, 91.0}});
    t::ok(polygon_self_intersects(bowtie), "bowtie self-intersects");
  }

  return t::report("geo/sweep_line");
}
