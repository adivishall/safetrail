// Sweep-line intersection detection vs an O(n^2) all-pairs oracle, and vs
// Polygon::validate() for the self-intersection verdict.
#include <cmath>
#include <vector>
#include "safetrail/geo/polygon.hpp"
#include "safetrail/geo/sweep_line.hpp"
#include "safetrail/sim/mobility.hpp"
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
  int poly_agree = 0, poly_trials = 1000;
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

  // ── The equivalence that now carries weight ────────────────────────────────
  //
  // validate() dispatches to the sweep at kSweepThresholdVertices and to the
  // pairwise scan below it, so the two must agree on BOTH sides of that line --
  // otherwise the same polygon is valid or invalid depending on how many vertices
  // it happens to have. Rings are built with enough spread to produce a healthy
  // mix of verdicts; the counts are asserted so a change that made every ring
  // trivially simple (or trivially crossed) could not pass silently.
  {
    const size_t sizes[] = {8, 16, kSweepThresholdVertices - 1,
                            kSweepThresholdVertices, kSweepThresholdVertices + 1,
                            64, 200, 500};
    for (size_t n : sizes) {
      int agree_n = 0, crossed = 0;
      const int trials_n = 120;
      for (int trial = 0; trial < trials_n; ++trial) {
        Ring r;
        for (size_t i = 0; i < n; ++i)
          r.push_back({rng.range(25.0, 26.0), rng.range(91.0, 92.0)});
        const bool sweep_says = ring_self_intersects_sweep(r);
        const bool pair_says = ring_self_intersects_pairwise(r);
        if (sweep_says == pair_says) ++agree_n;
        if (pair_says) ++crossed;
        // ...and the dispatcher must return that same verdict whichever branch
        // it picks for this size.
        if (ring_self_intersects(r) != pair_says) agree_n = -1;
      }
      t::ok(agree_n == trials_n,
            "sweep == pairwise == dispatch on " + std::to_string(n) +
                "-vertex rings (" + std::to_string(agree_n) + "/" +
                std::to_string(trials_n) + ")");
      t::ok(crossed > 0, "random " + std::to_string(n) +
                             "-vertex rings do self-intersect, so the test has teeth");
    }
  }

  // A simple ring stays simple at any size -- the case where a sweep bug that
  // reports spurious crossings would otherwise hide behind random data that
  // genuinely crosses. A convex polygon on a circle is simple by construction.
  {
    for (size_t n : {8u, 33u, 129u, 1025u}) {
      Ring r;
      for (size_t i = 0; i < n; ++i) {
        const double a = 6.283185307179586 * double(i) / double(n);
        r.push_back({25.5 + 0.3 * std::sin(a), 91.5 + 0.3 * std::cos(a)});
      }
      t::ok(!ring_self_intersects_sweep(r),
            "a convex " + std::to_string(n) + "-gon is simple (sweep)");
      t::ok(!ring_self_intersects_pairwise(r),
            "a convex " + std::to_string(n) + "-gon is simple (pairwise)");
      // Now break it: swap two vertices far apart, which must introduce a crossing.
      std::swap(r[1], r[n / 2]);
      t::ok(ring_self_intersects_sweep(r),
            "swapping two vertices of the " + std::to_string(n) +
                "-gon creates a crossing (sweep)");
      t::ok(ring_self_intersects_pairwise(r),
            "...and the pairwise oracle agrees");
    }
  }

  // A clean convex square is not self-intersecting; a bowtie is.
  {
    Polygon square(Ring{{25.0, 91.0}, {25.0, 91.1}, {25.1, 91.1}, {25.1, 91.0}});
    t::ok(!polygon_self_intersects(square), "square is simple");
    Polygon bowtie(Ring{{25.0, 91.0}, {25.1, 91.1}, {25.0, 91.1}, {25.1, 91.0}});
    t::ok(polygon_self_intersects(bowtie), "bowtie self-intersects");
  }

  // ── Regression: degenerate rings the sweep used to call simple ─────────────
  //
  // The randomised checks above use points on a jittered circle, which never
  // produce touching edges, shared vertices or collinear overlaps -- exactly the
  // inputs a sweep line is fragile on. Fuzzing on a coarse lattice found 94
  // disagreements in 800,000 small rings, all of one shape: the pairwise oracle
  // said "intersecting", the sweep said "simple". The cause was at INSERTION: a
  // new segment was tested only against its first non-exempt neighbour on each
  // side, so a touching segment one step further out was never compared (see
  // kReach in sweep_line.cpp). The first two cases below are from the original
  // failures; the rest are rings that fail with exactly that insertion rule and
  // nothing else changed, so the test pins the fix itself. Lattice units
  // (lat, lon) x 0.001 around (25, 91).
  {
    auto lattice = [](std::initializer_list<std::pair<int, int>> pts) {
      Ring r;
      for (const auto& p : pts) r.push_back({25.0 + 0.001 * p.first, 91.0 + 0.001 * p.second});
      return r;
    };
    const Ring cases[] = {
        lattice({{2, 0}, {2, 1}, {2, 2}, {1, 1}, {3, 3}, {3, 0}}),      // fold-back through a vertex
        lattice({{1, 2}, {2, 1}, {0, 0}, {1, 3}, {1, 1}}),              // vertex on a non-adjacent edge
        lattice({{4, 2}, {4, 2}, {0, 2}, {1, 0}, {3, 0}, {5, 3}}),      // duplicated vertex
        lattice({{2, 2}, {1, 2}, {2, 1}, {3, 2}, {2, 2}}),
        lattice({{1, 6}, {0, 6}, {6, 3}, {2, 6}, {1, 6}}),
        lattice({{0, 3}, {2, 3}, {1, 3}, {3, 3}, {1, 0}}),              // collinear overlap
        lattice({{1, 2}, {1, 2}, {0, 2}, {2, 0}, {2, 3}}),
        lattice({{0, 3}, {2, 3}, {1, 3}, {3, 3}, {3, 2}}),
    };
    int k = 0;
    for (const Ring& r : cases) {
      t::ok(ring_self_intersects_pairwise(r), "degenerate case " + std::to_string(k) +
                                                  ": the pairwise oracle says intersecting");
      t::ok(ring_self_intersects_sweep(r), "degenerate case " + std::to_string(k) +
                                               ": and so does the sweep");
      ++k;
    }
  }

  // ── Fuzz: sweep == pairwise on lattice rings full of degeneracies ──────────
  //
  // Tiny random lattice rings (almost all intersecting, many degenerately), and
  // large star-shaped lattice rings -- above the dispatch threshold, where
  // validate() really runs the sweep -- left simple or given one injected
  // degeneracy: a vertex moved onto a non-adjacent edge, a duplicated vertex,
  // or a collinear spike. tools/ has no separate fuzzer; this is it, scaled to
  // run in a second. The 800,000-ring version used to find the bugs above is
  // the same loop with a larger count.
  {
    safetrail::sim::Rng frng(12345);
    auto L = [](double y, double x) { return LatLon{25.0 + 0.001 * y, 91.0 + 0.001 * x}; };
    size_t disagree = 0, rings = 0, intersecting = 0;
    for (int it = 0; it < 6000 * t::stress(); ++it) {
      {
        const size_t n = 4 + frng.below(9);
        const uint32_t g = frng.uniform() < 0.5 ? 4 : 8;
        Ring r;
        for (size_t i = 0; i < n; ++i) r.push_back(L(double(frng.below(g)), double(frng.below(g))));
        const bool a = ring_self_intersects_pairwise(r);
        disagree += a != ring_self_intersects_sweep(r);
        intersecting += a;
        ++rings;
      }
      {
        const size_t n = 20 + frng.below(130);
        Ring r;
        for (size_t i = 0; i < n; ++i) {
          const double ang = 6.283185307179586 * (double(i) + 0.5) / double(n);
          const double rad = 20 + double(frng.below(20));
          r.push_back(L(std::round(rad * std::sin(ang)), std::round(rad * std::cos(ang))));
        }
        const size_t k = frng.below(uint32_t(n - 1));
        switch (frng.below(5)) {
          case 1: {                                            // vertex onto an edge midpoint
            const size_t j = (k + 2 + frng.below(uint32_t(n - 4))) % n;
            r[k] = {(r[j].lat + r[(j + 1) % n].lat) / 2, (r[j].lon + r[(j + 1) % n].lon) / 2};
            break;
          }
          case 2: r.insert(r.begin() + long(k), r[k]); break;  // consecutive duplicate
          case 3: r[k] = r[(k + 3) % n]; break;        // non-consecutive duplicate
          case 4: {                                            // collinear spike
            const LatLon a = r[k], b = r[k + 1];
            const LatLon mid{(a.lat + b.lat) / 2, (a.lon + b.lon) / 2};
            r.insert(r.begin() + long(k) + 2, mid);
            r.insert(r.begin() + long(k) + 2, b);
            break;
          }
          default: break;                                      // left simple (mostly)
        }
        const bool a = ring_self_intersects_pairwise(r);
        disagree += a != ring_self_intersects_sweep(r);
        intersecting += a;
        ++rings;
      }
    }
    t::ok(disagree == 0, "sweep == pairwise on " + std::to_string(rings) +
                             " degenerate lattice rings (" + std::to_string(intersecting) +
                             " intersecting, " + std::to_string(disagree) + " disagreements)");
  }

  return t::report("geo/sweep_line");
}
