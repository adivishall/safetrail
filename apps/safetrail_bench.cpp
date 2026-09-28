// safetrail_bench -- every number the documentation quotes is printed here and
// written to bench/results/*.csv by `make bench`.
//
// CORE -- the project's claim: hand-built indexes make repeated geofencing
// faster without changing the answer.
//   1. Scaling, fixed area       brute force vs quadtree vs R-tree; k grows with n
//   2. Scaling, fixed density    the same, with the answer size k held near 1
//   3. End to end                "which zones contain this point?": filter + exact
//                                geometry, against a truly naive baseline, by
//                                polygon complexity
//   4. Build, memory, updates    what each index costs to build, hold and change
//   5. Equivalence               every index == brute force, over mixed radii
//   6. Containment cross-check   ray casting vs winding number
//   7. R-tree bulk loading       STR packing vs repeated insertion
//   8. Interval tree             stabbing vs a linear scan; AVL height under churn
//   9. Persistent quadtree       path copying vs full copies; cost per mutation
// EXTENSIONS -- built on the core, not part of the headline claim
//  10. Hysteresis A/B    11. Routing    12. Dispatch    13. Adaptive sampling
//  14. Index churn       15. Serialisation    16. Self-intersection
//  17. Node snapping
//
// ── Measurement protocol (applies to every "median" in the core sections) ────
//
//   * Work is precomputed. Query boxes are built before timing, so a query time
//     is index traversal, not the trigonometry of Bbox::around.
//   * One SAMPLE runs the whole probe set enough times to last >= 20 ms,
//     calibrated per contender after an untimed warm-up pass. Microsecond-scale
//     timings of single ~2 ms passes were what made the old table jump +/-40%.
//   * 11 ROUNDS. In each round every contender takes one sample, in an order
//     rotated round to round, so drift (thermal, frequency, core migration)
//     lands on all of them rather than on whichever ran last.
//   * Reported: median, and the interquartile range as a percentage of it.
//     Speedups are PAIRED: the median over rounds of (baseline / contender)
//     measured back to back in the same round, which cancels slow rounds.
//   * Every pass returns a checksum of its results (order-independent). The
//     checksums of contenders doing the same job must be equal -- that is both
//     a correctness gate and what stops the optimiser deleting the work.
//   * On macOS the process asks for the performance cores (QoS
//     user-interactive); Apple Silicon otherwise runs default-QoS work on
//     efficiency cores at will, which roughly doubled absolute times in testing.
//
// Absolute times are specific to one machine and compiler; bench/results/
// environment.txt records which. Ratios and candidate counts are the portable
// part, and the docs quote them that way.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif
#include "safetrail/ds/interval_tree.hpp"
#include "safetrail/geo/containment.hpp"
#include "safetrail/geo/polygon.hpp"
#include "safetrail/geo/sweep_line.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/graph/dijkstra.hpp"
#include "safetrail/graph/astar.hpp"
#include "safetrail/dispatch/assigner.hpp"
#include "safetrail/dispatch/responder.hpp"
#include "safetrail/power/adaptive_sampler.hpp"
#include "safetrail/ds/hash_table.hpp"
#include "safetrail/sim/simulator.hpp"

using namespace safetrail;
using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// ── Harness ──────────────────────────────────────────────────────────────────

// Everything measured is folded into this, so no result is dead code.
static volatile uint64_t g_sink = 0;

struct Stat {
  double median = 0, p25 = 0, p75 = 0, min = 0;
  double iqr_pct() const { return median > 0 ? 100.0 * (p75 - p25) / median : 0.0; }
};

static Stat summarize(std::vector<double> v) {
  if (v.empty()) return {};
  std::sort(v.begin(), v.end());
  auto at = [&](double q) { return v[size_t(q * double(v.size() - 1) + 0.5)]; };
  return {at(0.5), at(0.25), at(0.75), v.front()};
}

constexpr int kRounds = 11;
constexpr double kMinSampleMs = 20.0;

struct Contender {
  std::string name;
  std::function<uint64_t()> pass;    // one full pass over the probe set -> checksum
  size_t ops_per_pass;
};

struct Measured {
  std::vector<double> us;            // microseconds per op, one entry per round
  Stat stat;
  uint64_t checksum = 0;             // of ONE pass
};

static std::vector<Measured> measure(const std::vector<Contender>& cs, int rounds = kRounds) {
  const size_t k = cs.size();
  std::vector<Measured> m(k);
  std::vector<int> reps(k, 1);
  for (size_t i = 0; i < k; ++i) {
    m[i].checksum = cs[i].pass();                      // warm-up; records the checksum
    const auto t0 = Clock::now();
    g_sink = g_sink + cs[i].pass();
    const double one = ms_since(t0);
    reps[i] = std::max(1, int(std::ceil(kMinSampleMs / std::max(one, 1e-3))));
  }
  for (int r = 0; r < rounds; ++r)
    for (size_t j = 0; j < k; ++j) {
      const size_t i = (j + size_t(r)) % k;            // rotate the order each round
      uint64_t s = 0;
      const auto t0 = Clock::now();
      for (int q = 0; q < reps[i]; ++q) s += cs[i].pass();
      const double ms = ms_since(t0);
      g_sink = g_sink + s;
      m[i].us.push_back(ms * 1000.0 / (double(reps[i]) * double(cs[i].ops_per_pass)));
    }
  for (auto& x : m) x.stat = summarize(x.us);
  return m;
}

// Median over rounds of base/fast, each pair measured back to back.
static double paired_speedup(const Measured& base, const Measured& fast) {
  std::vector<double> r;
  for (size_t i = 0; i < base.us.size() && i < fast.us.size(); ++i)
    if (fast.us[i] > 0) r.push_back(base.us[i] / fast.us[i]);
  return summarize(r).median;
}

// Median wall time of `reps` runs of f, in ms.
static double median_ms(int reps, const std::function<void()>& f) {
  std::vector<double> t;
  for (int i = 0; i < reps; ++i) {
    const auto t0 = Clock::now();
    f();
    t.push_back(ms_since(t0));
  }
  return summarize(t).median;
}

// Order-independent fold of a result set: equal sets give equal checksums
// however the index happened to order them.
static uint64_t fold(const std::vector<index::ZoneId>& ids) {
  uint64_t s = ids.size();
  for (index::ZoneId id : ids) s += (uint64_t(id) + 1) * 0x9E3779B97F4A7C15ull;
  return s;
}

// ── Corpora ──────────────────────────────────────────────────────────────────
using Items = std::vector<std::pair<index::ZoneId, geo::Bbox>>;
static const geo::Bbox kDistrict{25.40, 91.70, 25.75, 92.05};   // ~39 x 35 km, Shillong

struct Corpus {
  Items boxes;
  std::vector<geo::LatLon> probes;
};

// Zones are boxes of half-width 0.0003..0.0022 deg (~30-240 m), scattered
// uniformly over `area`; probes are uniform over the same area.
static Corpus make_corpus_in(const geo::Bbox& area, size_t n, size_t probes, uint64_t seed) {
  sim::Rng rng(seed);
  Corpus c;
  c.boxes.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const double clat = rng.range(area.min_lat, area.max_lat);
    const double clon = rng.range(area.min_lon, area.max_lon);
    const double r = rng.range(0.0003, 0.0022);
    c.boxes.emplace_back(index::ZoneId(i), geo::Bbox{clat - r, clon - r, clat + r, clon + r});
  }
  for (size_t i = 0; i < probes; ++i)
    c.probes.push_back({rng.range(area.min_lat, area.max_lat), rng.range(area.min_lon, area.max_lon)});
  return c;
}
static Corpus make_corpus(size_t n, size_t probes, uint64_t seed) {
  return make_corpus_in(kDistrict, n, probes, seed);
}

static std::vector<geo::Bbox> query_boxes(const std::vector<geo::LatLon>& probes, double radius_m) {
  std::vector<geo::Bbox> q;
  q.reserve(probes.size());
  for (const auto& p : probes) q.push_back(geo::Bbox::around(p, radius_m));
  return q;
}

static Contender index_queries(const std::string& name, const index::SpatialIndex& ix,
                               const std::vector<geo::Bbox>& qs) {
  auto out = std::make_shared<std::vector<index::ZoneId>>();
  out->reserve(1 << 14);
  return {name,
          [&ix, &qs, out]() {
            uint64_t s = 0;
            for (const auto& q : qs) {
              out->clear();
              ix.query(q, *out);
              s += fold(*out);
            }
            return s;
          },
          qs.size()};
}

static double avg_results(const index::SpatialIndex& ix, const std::vector<geo::Bbox>& qs) {
  std::vector<index::ZoneId> out;
  size_t total = 0;
  for (const auto& q : qs) { out.clear(); ix.query(q, out); total += out.size(); }
  return qs.empty() ? 0.0 : double(total) / double(qs.size());
}

static const char* ok_str(bool ok) { return ok ? "\033[32mok\033[0m" : "\033[31mMISMATCH\033[0m"; }

// ── 1 & 2. Scaling ───────────────────────────────────────────────────────────
//
// Two regimes, because "how does it scale?" has two honest answers:
//   fixed area     more zones in the same district, so the answer size k grows in
//                  proportion to n. O(log n + k) is dominated by k, and the
//                  speedup over brute force levels off -- no index returns
//                  fewer results than exist.
//   fixed density  the area grows with n, so k stays near 1. Now the log n term
//                  is what is left, and the speedup keeps growing with n.
static bool scaling_table(FILE* csv, bool fixed_density, const std::vector<size_t>& sizes) {
  if (csv) fprintf(csv, "zones,brute_us,quad_us,rtree_us,quad_speedup,rtree_speedup,candidates,"
                        "brute_iqr_pct,quad_iqr_pct,rtree_iqr_pct,"
                        "brute_build_ms,quad_build_ms,rtree_build_ms,"
                        "brute_bytes,quad_bytes,rtree_bytes,quad_nodes,quad_depth,"
                        "rtree_nodes,rtree_depth,area_deg\n");
  printf("  %8s  %10s  %10s  %10s  %8s  %8s  %8s  %6s  %6s\n", "zones", "brute us",
         "quadtree", "r-tree", "QT x", "RT x", "k/query", "IQR", "equal");
  printf("  ──────────────────────────────────────────────────────────────────────────────────────\n");
  bool all_ok = true;
  for (size_t n : sizes) {
    const double side = fixed_density ? 0.35 * std::sqrt(double(n) / 1000.0) : 0.35;
    const geo::Bbox area{kDistrict.min_lat, kDistrict.min_lon, kDistrict.min_lat + side,
                         kDistrict.min_lon + side};
    const Corpus c = make_corpus_in(area, n, 2000, 99);
    const auto qs = query_boxes(c.probes, 450);

    index::BruteForceIndex bf;
    index::Quadtree qt;
    index::RTree rt;
    const double bf_b = median_ms(5, [&] { bf.build(c.boxes); });
    const double qt_b = median_ms(5, [&] { qt.build(c.boxes); });
    const double rt_b = median_ms(5, [&] { rt.build(c.boxes); });

    const auto m = measure({index_queries("brute", bf, qs), index_queries("quadtree", qt, qs),
                            index_queries("r-tree", rt, qs)});
    const bool eq = m[0].checksum == m[1].checksum && m[0].checksum == m[2].checksum;
    all_ok = all_ok && eq;
    const double qx = paired_speedup(m[0], m[1]), rx = paired_speedup(m[0], m[2]);
    const double k = avg_results(bf, qs);
    const double iqr = std::max({m[0].stat.iqr_pct(), m[1].stat.iqr_pct(), m[2].stat.iqr_pct()});
    const auto bs = bf.stats(), qst = qt.stats(), rs = rt.stats();

    printf("  %8zu  %10.3f  %10.3f  %10.3f  %7.1fx  %7.1fx  %8.2f  %5.1f%%  %6s\n", n,
           m[0].stat.median, m[1].stat.median, m[2].stat.median, qx, rx, k, iqr, ok_str(eq));
    if (csv)
      fprintf(csv, "%zu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,"
                   "%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.3f\n",
              n, m[0].stat.median, m[1].stat.median, m[2].stat.median, qx, rx, k,
              m[0].stat.iqr_pct(), m[1].stat.iqr_pct(), m[2].stat.iqr_pct(), bf_b, qt_b, rt_b,
              bs.bytes, qst.bytes, rs.bytes, qst.node_count, qst.max_depth, rs.node_count,
              rs.max_depth, side);
  }
  return all_ok;
}

static bool bench_scaling(FILE* csv) {
  printf("\n\033[1m1. SCALING, FIXED AREA\033[0m   one 39 x 35 km district, 450 m query boxes, "
         "2000 probes; us/query\n");
  const bool ok = scaling_table(csv, false, {100, 1000, 5000, 10000, 20000, 50000, 100000});
  printf("\n  k grows in proportion to n here: at 100,000 zones about 99 genuinely overlap\n");
  printf("  each query box, and every index must return all of them. That is the O(k)\n");
  printf("  term, and it caps the speedup. The index removes the O(n) scan, not the answer.\n");
  return ok;
}

static bool bench_density(FILE* csv) {
  printf("\n\033[1m2. SCALING, FIXED DENSITY\033[0m   the area grows with n, so k stays ~1; "
         "us/query\n");
  const bool ok = scaling_table(csv, true, {1000, 10000, 100000});
  printf("\n  With k held constant, brute force stays linear while the trees grow with the\n");
  printf("  depth of the tree, so the speedup keeps rising with n. Same code, same queries\n");
  printf("  per zone; only the answer size differs from section 1.\n");
  return ok;
}

// ── 3. End to end: filter + exact geometry ───────────────────────────────────
//
// The question the engine really asks per fix: which zones CONTAIN this point?
// Four ways to answer it, all required to return the same set:
//   naive       ray-cast every polygon, no filter at all                  O(n V)
//   bbox scan   BruteForceIndex (test every zone's box), exact on hits    O(n + k V)
//   quadtree    tree filter, exact on candidates                  O(log n + k + k V)
//   r-tree      likewise
// Polygon complexity V is the variable: the index only removes the O(n) part, so
// as V grows the shared O(k V) refinement is what is left.
namespace {
// The textbook even-odd test with no bounding-box early-out -- the "naive" row.
// It deliberately does not share code with geo::contains(): it is the baseline,
// and its answers are checked against the engine's below.
bool naive_contains(const geo::Ring& r, const geo::LatLon& p) {
  bool in = false;
  const size_t n = r.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    if ((r[i].lat > p.lat) != (r[j].lat > p.lat) &&
        p.lon < r[i].lon + (p.lat - r[i].lat) / (r[j].lat - r[i].lat) * (r[j].lon - r[i].lon))
      in = !in;
  return in;
}
}  // namespace

static bool bench_end_to_end(FILE* csv) {
  printf("\n\033[1m3. END TO END\033[0m   \"which zones contain this point?\" -- filter + exact "
         "geometry;\n   5,000 zones in the district, 300 probe points, us/query\n");
  printf("  %6s  %10s  %10s  %10s  %10s  %9s  %9s  %8s  %6s\n", "V", "naive", "bbox scan",
         "quadtree", "r-tree", "QT/naive", "QT/scan", "cand/q", "equal");
  printf("  ──────────────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "vertices,naive_us,scan_us,quad_us,rtree_us,quad_vs_naive,quad_vs_scan,"
                        "rtree_vs_scan,candidates,hits,naive_iqr_pct,scan_iqr_pct,quad_iqr_pct,"
                        "rtree_iqr_pct\n");
  bool all_ok = true;
  for (size_t V : {8u, 32u, 128u, 512u}) {
    sim::Rng rng(0xE2E0 + V);
    std::vector<geo::Polygon> polys;
    Items boxes;
    while (polys.size() < 5000) {
      const double clat = rng.range(kDistrict.min_lat, kDistrict.max_lat);
      const double clon = rng.range(kDistrict.min_lon, kDistrict.max_lon);
      const double r = rng.range(0.0003, 0.0022);
      geo::Ring ring;
      for (size_t v = 0; v < V; ++v) {                 // star-shaped, hence simple
        const double a = 6.283185307179586 * (double(v) + rng.range(0.0, 0.8)) / double(V);
        const double rr = r * rng.range(0.6, 1.0);
        ring.push_back({clat + rr * std::sin(a), clon + rr * std::cos(a)});
      }
      geo::Polygon p(std::move(ring));
      if (p.validate() != geo::Polygon::Validity::Ok) continue;
      boxes.emplace_back(index::ZoneId(polys.size()), p.bbox());
      polys.push_back(std::move(p));
    }
    std::vector<geo::LatLon> probes;
    for (int i = 0; i < 300; ++i)
      probes.push_back({rng.range(kDistrict.min_lat, kDistrict.max_lat),
                        rng.range(kDistrict.min_lon, kDistrict.max_lon)});

    index::BruteForceIndex bf; bf.build(boxes);
    index::Quadtree qt;        qt.build(boxes);
    index::RTree rt;           rt.build(boxes);

    auto buf = std::make_shared<std::vector<index::ZoneId>>();
    auto hits = std::make_shared<std::vector<index::ZoneId>>();
    auto filtered = [&, buf, hits](const index::SpatialIndex& ix) {
      return [&ix, &probes, &polys, buf, hits]() {
        uint64_t s = 0;
        for (const auto& p : probes) {
          buf->clear(); hits->clear();
          ix.query(geo::Bbox{p.lat, p.lon, p.lat, p.lon}, *buf);
          for (index::ZoneId id : *buf) if (geo::contains(polys[id], p)) hits->push_back(id);
          s += fold(*hits);
        }
        return s;
      };
    };
    std::vector<Contender> cs = {
        {"naive",
         [&probes, &polys, hits]() {
           uint64_t s = 0;
           for (const auto& p : probes) {
             hits->clear();
             for (size_t z = 0; z < polys.size(); ++z)
               if (naive_contains(polys[z].outer(), p)) hits->push_back(index::ZoneId(z));
             s += fold(*hits);
           }
           return s;
         },
         probes.size()},
        {"bbox scan", filtered(bf), probes.size()},
        {"quadtree", filtered(qt), probes.size()},
        {"r-tree", filtered(rt), probes.size()},
    };
    const auto m = measure(cs, 7);
    const bool eq = m[0].checksum == m[1].checksum && m[1].checksum == m[2].checksum &&
                    m[2].checksum == m[3].checksum;
    all_ok = all_ok && eq;

    size_t cand = 0, hit = 0;
    for (const auto& p : probes) {
      std::vector<index::ZoneId> out;
      bf.query(geo::Bbox{p.lat, p.lon, p.lat, p.lon}, out);
      cand += out.size();
      for (index::ZoneId id : out) hit += geo::contains(polys[id], p);
    }
    const double cq = double(cand) / double(probes.size()), hq = double(hit) / double(probes.size());
    const double q_naive = paired_speedup(m[0], m[2]), q_scan = paired_speedup(m[1], m[2]);
    const double r_scan = paired_speedup(m[1], m[3]);
    printf("  %6zu  %10.2f  %10.3f  %10.3f  %10.3f  %8.0fx  %8.1fx  %8.2f  %6s\n", V,
           m[0].stat.median, m[1].stat.median, m[2].stat.median, m[3].stat.median, q_naive,
           q_scan, cq, ok_str(eq));
    if (csv)
      fprintf(csv, "%zu,%.4f,%.4f,%.4f,%.4f,%.1f,%.2f,%.2f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f\n", V,
              m[0].stat.median, m[1].stat.median, m[2].stat.median, m[3].stat.median, q_naive,
              q_scan, r_scan, cq, hq, m[0].stat.iqr_pct(), m[1].stat.iqr_pct(),
              m[2].stat.iqr_pct(), m[3].stat.iqr_pct());
  }
  printf("\n  The naive column is what \"check every zone\" really costs: O(n V), which is\n");
  printf("  where the large factors come from. A plain bounding-box scan already removes\n");
  printf("  the V; the tree then removes the n. The exact test on the few candidates is\n");
  printf("  identical in every column, which is why QT/scan shrinks as V grows.\n");
  return all_ok;
}

// ── 4. Build, memory, updates ────────────────────────────────────────────────
//
// Memory is IndexStats::bytes: nodes plus the capacity of every vector they own,
// allocator overhead excluded. Update cost: in each round every index is freshly
// built (untimed), then 1,000 inserts are timed, then removing those 1,000.
static void bench_costs(FILE* csv) {
  printf("\n\033[1m4. BUILD, MEMORY, UPDATES\033[0m   per index; build = median of 5, "
         "insert/remove = median of 7 rounds of 1,000\n");
  printf("  %8s  %-12s  %10s  %11s  %9s  %11s  %11s\n", "zones", "index", "build ms",
         "memory KB", "B/zone", "insert us", "remove us");
  printf("  ──────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,index,build_ms,bytes,bytes_per_zone,insert_us,remove_us\n");
  for (size_t n : {10000u, 100000u}) {
    const Corpus c = make_corpus(n, 0, 1234);
    sim::Rng rng(5678);
    Items extra;
    for (size_t i = 0; i < 1000; ++i) {
      const double clat = rng.range(kDistrict.min_lat, kDistrict.max_lat);
      const double clon = rng.range(kDistrict.min_lon, kDistrict.max_lon);
      const double r = rng.range(0.0003, 0.0022);
      extra.emplace_back(index::ZoneId(n + i), geo::Bbox{clat - r, clon - r, clat + r, clon + r});
    }
    std::vector<index::ZoneId> order;
    for (const auto& e : extra) order.push_back(e.first);
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[rng.below(uint32_t(i))]);

    const index::IndexKind kinds[] = {index::IndexKind::BruteForce, index::IndexKind::Quadtree,
                                      index::IndexKind::RTree};
    for (index::IndexKind kind : kinds) {
      auto ix = index::make_index(kind);
      const double build = median_ms(5, [&] { ix->build(c.boxes); });
      const size_t bytes = ix->stats().bytes;
      std::vector<double> ins, rem;
      for (int r = 0; r < 7; ++r) {
        ix->build(c.boxes);
        auto t0 = Clock::now();
        for (const auto& e : extra) ix->insert(e.first, e.second);
        ins.push_back(ms_since(t0) * 1000.0 / double(extra.size()));
        t0 = Clock::now();
        for (index::ZoneId id : order) ix->remove(id);
        rem.push_back(ms_since(t0) * 1000.0 / double(order.size()));
      }
      const double iu = summarize(ins).median, ru = summarize(rem).median;
      printf("  %8zu  %-12s  %10.2f  %11.1f  %9.1f  %11.3f  %11.3f\n", n, ix->name(), build,
             double(bytes) / 1024.0, double(bytes) / double(n), iu, ru);
      if (csv) fprintf(csv, "%zu,%s,%.3f,%zu,%.1f,%.4f,%.4f\n", n, ix->name(), build, bytes,
                       double(bytes) / double(n), iu, ru);
    }
  }
  printf("\n  Removal is O(n) in all three: none keeps an id -> node map, so the entry is\n");
  printf("  found by search. The trees then pay again to collapse or condense. That is a\n");
  printf("  deliberate simplification for a read-heavy workload, measured here rather\n");
  printf("  than hidden; an id -> leaf hash map is the fix if zones churn in production.\n");
}

// ── 5. Equivalence ───────────────────────────────────────────────────────────
static bool bench_equivalence() {
  printf("\n\033[1m5. EQUIVALENCE\033[0m   every index must return EXACTLY brute force's set "
         "(radii 80 m, 400 m, 2 km)\n");
  bool all_ok = true;
  for (size_t n : {50u, 500u, 5000u}) {
    Corpus c = make_corpus(n, 2000, 7);
    index::BruteForceIndex bf; bf.build(c.boxes);
    index::Quadtree qt;        qt.build(c.boxes);
    index::RTree rt;           rt.build(c.boxes);
    index::Geohash gh;         gh.build(c.boxes);

    size_t mq = 0, mr = 0, mg = 0, total = 0, queries = 0;
    std::vector<index::ZoneId> a, b, d, g;
    for (const auto& p : c.probes)
      for (double r : {80.0, 400.0, 2000.0}) {
        a.clear(); b.clear(); d.clear(); g.clear();
        const geo::Bbox q = geo::Bbox::around(p, r);
        bf.query(q, a); qt.query(q, b); rt.query(q, d); gh.query(q, g);
        std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
        std::sort(d.begin(), d.end()); std::sort(g.begin(), g.end());
        total += a.size(); ++queries;
        mq += a != b; mr += a != d; mg += a != g;
      }
    printf("  %6zu zones  %6zu queries  %8zu hits   quadtree %zu   r-tree %zu   geohash %zu "
           "mismatches\n", n, queries, total, mq, mr, mg);
    if (mq || mr || mg) all_ok = false;
  }
  printf("  (tests/index/differential_test.cpp runs ~275,000 more, over seven hostile\n");
  printf("   workload profiles, with structural invariants audited after every operation)\n");
  return all_ok;
}

// ── 7. R-tree bulk loading ───────────────────────────────────────────────────
//
// Inserting n items one at a time makes every ChooseSubtree decision blind to the
// items still to come, so node boxes overlap more than they need to; overlap is
// what forces a query down several branches. STR sees the whole set and packs it
// into near-square tiles.
static void bench_bulkload(FILE* csv) {
  printf("\n\033[1m7. R-TREE BULK LOADING\033[0m   STR packing vs repeated insertion, same data "
         "and queries\n");
  printf("  %8s  %10s  %10s  %9s  %9s  %11s  %11s  %8s\n", "zones", "insert ms", "STR ms",
         "ins nodes", "STR nodes", "ins us/qry", "STR us/qry", "gain");
  printf("  ────────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,incremental_build_ms,str_build_ms,incremental_nodes,str_nodes,"
                        "incremental_depth,str_depth,incremental_us,str_us,query_gain\n");
  for (size_t n : {1000u, 10000u, 100000u}) {
    const Corpus c = make_corpus(n, 2000, 31337);
    const auto qs = query_boxes(c.probes, 450);
    index::RTree inc, str;
    const double inc_ms = median_ms(5, [&] { inc.build_incremental(c.boxes); });
    const double str_ms = median_ms(5, [&] { str.build(c.boxes); });
    const auto m = measure({index_queries("incremental", inc, qs), index_queries("str", str, qs)});
    const auto is = inc.stats(), ss = str.stats();
    const double gain = paired_speedup(m[0], m[1]);
    printf("  %8zu  %10.2f  %10.2f  %9zu  %9zu  %11.3f  %11.3f  %7.2fx\n", n, inc_ms, str_ms,
           is.node_count, ss.node_count, m[0].stat.median, m[1].stat.median, gain);
    if (csv) fprintf(csv, "%zu,%.3f,%.3f,%zu,%zu,%zu,%zu,%.4f,%.4f,%.3f\n", n, inc_ms, str_ms,
                     is.node_count, ss.node_count, is.max_depth, ss.max_depth,
                     m[0].stat.median, m[1].stat.median, gain);
  }
  printf("\n  Both builds are O(n log n). STR's tree is smaller and its sibling boxes overlap\n");
  printf("  less, so a query enters fewer branches -- the structure, not the machine.\n");
}

// ── 8. Interval tree ─────────────────────────────────────────────────────────
//
// "Which validity windows contain instant t?" -- stabbing the AVL interval tree
// vs scanning every window, in two regimes, because the answer size k decides
// the outcome exactly as it does for the spatial indexes:
//   selective  windows of 1-30 minutes spread over 30 days: few contain any t
//   dense      a tenth as many start times as windows, lasting up to 4 hours:
//              hundreds contain any t, and the tree must report every one
// Starts are shared heavily in both (many closures begin at the same moment).
// Then churn: ten rounds of deleting and reinserting a tenth of the set, to show
// the height tracks the LIVE size.
static bool bench_interval(FILE* csv) {
  printf("\n\033[1m8. INTERVAL TREE\033[0m   stab at a random instant: AVL tree vs linear scan\n");
  printf("  %-9s  %8s  %10s  %10s  %8s  %8s  %6s  %7s  %7s  %9s\n", "regime", "windows",
         "scan us", "tree us", "speedup", "k/stab", "height", "AVL max", "churned", "delete us");
  printf("  ─────────────────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "regime,intervals,scan_us,tree_us,speedup,hits_per_stab,height,avl_bound,"
                        "height_after_churn,delete_us,scan_iqr_pct,tree_iqr_pct\n");
  bool all_ok = true;
  for (int regime = 0; regime < 2; ++regime)
  for (size_t n : {1000u, 10000u, 100000u}) {
    const bool dense = regime == 1;
    sim::Rng rng(0xC4147 + n + size_t(regime));
    struct W { Timestamp lo, hi; int id; };
    std::vector<W> ws;
    ds::IntervalTree<int> tree;
    const Timestamp month = Timestamp(30) * 86400000;
    auto make = [&](int id) {
      if (dense) {
        const Timestamp lo = Timestamp(rng.below(uint32_t(n / 10 + 1))) * 60000;
        return W{lo, lo + 60000 + Timestamp(rng.below(4 * 3600)) * 1000, id};
      }
      const Timestamp lo = Timestamp(rng.below(uint32_t(n / 10 + 1))) * (month / Timestamp(n / 10 + 1));
      return W{lo, lo + 60000 + Timestamp(rng.below(29 * 60)) * 1000, id};
    };
    for (size_t i = 0; i < n; ++i) {
      ws.push_back(make(int(i)));
      tree.insert(ws.back().lo, ws.back().hi, ws.back().id);
    }
    const size_t h_fresh = tree.height();
    Timestamp span = 0;
    for (const auto& w : ws) span = std::max(span, w.hi);
    std::vector<Timestamp> probes;
    for (int i = 0; i < 2000; ++i)
      probes.push_back(Timestamp(rng.below(uint32_t(span / 1000))) * 1000);

    auto out = std::make_shared<std::vector<int>>();
    auto fold_ints = [](const std::vector<int>& v) {
      uint64_t s = v.size();
      for (int id : v) s += (uint64_t(id) + 1) * 0x9E3779B97F4A7C15ull;
      return s;
    };
    const auto m = measure({
        {"scan", [&ws, &probes, out, fold_ints]() {
           uint64_t s = 0;
           for (Timestamp t : probes) {
             out->clear();
             for (const auto& w : ws) if (w.lo <= t && t < w.hi) out->push_back(w.id);
             s += fold_ints(*out);
           }
           return s;
         }, probes.size()},
        {"tree", [&tree, &probes, out, fold_ints]() {
           uint64_t s = 0;
           for (Timestamp t : probes) {
             out->clear();
             tree.stabbing(t, *out);
             s += fold_ints(*out);
           }
           return s;
         }, probes.size()}});
    const bool eq = m[0].checksum == m[1].checksum;
    all_ok = all_ok && eq;
    size_t hits = 0;
    for (Timestamp t : probes) { out->clear(); tree.stabbing(t, *out); hits += out->size(); }

    const size_t batch = n / 10;
    std::vector<double> del_us;
    int next_id = int(n);
    for (int round = 0; round < 10; ++round) {
      const auto t0 = Clock::now();
      for (size_t i = 0; i < batch; ++i) {
        const W w = ws[size_t(round) * batch + i];
        tree.remove(w.lo, w.hi, w.id);
      }
      del_us.push_back(ms_since(t0) * 1000.0 / double(batch));
      for (size_t i = 0; i < batch; ++i) {
        ws.push_back(make(next_id++));
        tree.insert(ws.back().lo, ws.back().hi, ws.back().id);
      }
    }
    const double bound = 1.4405 * std::log2(double(n) + 2.0) - 0.3277;
    const double sx = paired_speedup(m[0], m[1]);
    const double k = double(hits) / double(probes.size());
    printf("  %-9s  %8zu  %10.3f  %10.3f  %7.1fx  %8.1f  %6zu  %7.1f  %7zu  %9.3f  %s\n",
           dense ? "dense" : "selective", n, m[0].stat.median, m[1].stat.median, sx, k, h_fresh,
           bound, tree.height(), summarize(del_us).median, ok_str(eq));
    if (csv) fprintf(csv, "%s,%zu,%.4f,%.4f,%.2f,%.2f,%zu,%.2f,%zu,%.4f,%.1f,%.1f\n",
                     dense ? "dense" : "selective", n, m[0].stat.median, m[1].stat.median, sx, k,
                     h_fresh, bound, tree.height(), summarize(del_us).median,
                     m[0].stat.iqr_pct(), m[1].stat.iqr_pct());
    if (!tree.check_invariants() || !tree.balanced()) all_ok = false;
  }
  printf("\n  Selective windows are where the augmentation earns its keep: the max_high\n");
  printf("  bound prunes whole subtrees, and the scan's cost is all waste. With hundreds\n");
  printf("  of windows containing every instant, the tree must still visit and report\n");
  printf("  each one and a sequential scan is as fast -- the same O(k) ceiling as the\n");
  printf("  spatial indexes. Height stays under the AVL bound for the LIVE size after\n");
  printf("  churn, and each delete is one root-to-node descent despite shared starts.\n");
  return all_ok;
}

// ── 9. Persistent quadtree ───────────────────────────────────────────────────
static void bench_versioned(FILE* csv, FILE* csv_ops) {
  printf("\n\033[1m9. PERSISTENT QUADTREE\033[0m   path copying vs copying the whole tree "
         "per version\n");
  printf("  %8s  %10s  %12s  %8s  %11s  %11s  %8s\n", "versions", "allocated", "full copies",
         "sharing", "query@past", "query@now", "past/now");
  printf("  ────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "versions,allocated,full_copies,sharing_ratio,query_past_us,"
                        "query_now_us,past_over_now\n");
  for (size_t n : {50u, 200u, 1000u, 5000u}) {
    sim::Rng rng(4321);
    index::VersionedIndex ix;
    std::vector<geo::LatLon> probes;
    for (size_t i = 0; i < n; ++i) {
      const double lat = rng.range(25.50, 25.62), lon = rng.range(91.80, 91.96);
      const double r = rng.range(0.0004, 0.0025);
      ix.add_zone(index::ZoneId(i), geo::Bbox{lat - r, lon - r, lat + r, lon + r},
                  index::Validity{0, kForever}, Timestamp(1000 * (i + 1)));
    }
    for (int i = 0; i < 500; ++i) probes.push_back({rng.range(25.49, 25.63), rng.range(91.79, 91.97)});
    const auto qs = query_boxes(probes, 450);
    const auto st = ix.share_stats();
    const Timestamp mid = Timestamp(1000 * (n / 2));

    auto out = std::make_shared<std::vector<index::ZoneId>>();
    const auto m = measure({
        {"past", [&ix, &qs, mid, out]() {
           uint64_t s = 0;
           for (const auto& q : qs) { out->clear(); ix.query_at(mid, q, *out); s += fold(*out); }
           return s;
         }, qs.size()},
        {"now", [&ix, &qs, out]() {
           uint64_t s = 0;
           for (const auto& q : qs) { out->clear(); ix.query_now(q, *out); s += fold(*out); }
           return s;
         }, qs.size()}});
    const double ratio = paired_speedup(m[1], m[0]);   // now-time / past-time
    printf("  %8zu  %10zu  %12zu  %7.1fx  %11.3f  %11.3f  %7.2fx\n", ix.version_count(),
           st.total_nodes_allocated, st.nodes_if_full_copies, st.sharing_ratio(),
           m[0].stat.median, m[1].stat.median, 1.0 / ratio);
    if (csv) fprintf(csv, "%zu,%zu,%zu,%.2f,%.4f,%.4f,%.3f\n", ix.version_count(),
                     st.total_nodes_allocated, st.nodes_if_full_copies, st.sharing_ratio(),
                     m[0].stat.median, m[1].stat.median, 1.0 / ratio);
  }

  // Cost of each kind of mutation, in nodes allocated, on a 5,000-zone index.
  {
    sim::Rng rng(8765);
    index::VersionedIndex ix;
    Timestamp at = 1;
    for (index::ZoneId i = 0; i < 5000; ++i) {
      const double lat = rng.range(25.50, 25.62), lon = rng.range(91.80, 91.96);
      const double r = rng.range(0.0004, 0.0025);
      ix.add_zone(i, {lat - r, lon - r, lat + r, lon + r}, {0, kForever}, at++);
    }
    struct Op { const char* name; std::function<void(index::ZoneId)> run; };
    const Op ops[] = {
        {"add (new zone)", [&](index::ZoneId id) {
           const double lat = rng.range(25.50, 25.62), lon = rng.range(91.80, 91.96);
           ix.add_zone(10000 + id, {lat, lon, lat + 0.001, lon + 0.001}, {0, kForever}, at++);
         }},
        {"replace (moved zone)", [&](index::ZoneId id) {
           const double lat = rng.range(25.50, 25.62), lon = rng.range(91.80, 91.96);
           ix.add_zone(id, {lat, lon, lat + 0.001, lon + 0.001}, {0, kForever}, at++);
         }},
        {"validity change", [&](index::ZoneId id) {
           ix.update_validity(id, {0, Timestamp(1000000 + id)}, at++);
         }},
        {"remove", [&](index::ZoneId id) { ix.remove_zone(id + 2000, at++); }},
    };
    printf("\n  nodes allocated per mutation (5,000-zone index, 500 of each):\n");
    if (csv_ops) fprintf(csv_ops, "mutation,avg_nodes_allocated,max_nodes_allocated\n");
    for (const auto& op : ops) {
      size_t total = 0, worst = 0;
      for (index::ZoneId i = 0; i < 500; ++i) {
        const size_t before = ix.share_stats().total_nodes_allocated;
        op.run(i);
        const size_t d = ix.share_stats().total_nodes_allocated - before;
        total += d; worst = std::max(worst, d);
      }
      printf("    %-22s  avg %6.1f   max %4zu\n", op.name, double(total) / 500.0, worst);
      if (csv_ops) fprintf(csv_ops, "%s,%.2f,%zu\n", op.name, double(total) / 500.0, worst);
    }
  }
  printf("\n  Sharing grows with history, a query against the past costs what one against\n");
  printf("  the present does (a different root pointer, no replay), and every mutation\n");
  printf("  copies one root-to-node path: a validity change copies nothing.\n");
}

// Old-style single-index timing, used by the churn extension. Same protocol as
// measure(), one contender.
struct Timing { double median_us, min_us, spread_pct; };
static Timing time_queries(index::SpatialIndex& ix, const Corpus& c, double radius) {
  const auto qs = query_boxes(c.probes, radius);
  const auto m = measure({index_queries(ix.name(), ix, qs)});
  return {m[0].stat.median, m[0].stat.min, m[0].stat.iqr_pct()};
}

// ── Extensions (and the containment cross-check) ────────────────────────────

static bool bench_containment() {
  printf("\n\033[1m6. CONTAINMENT CROSS-CHECK\033[0m   ray casting vs winding number, two independent implementations\n");
  sim::Rng rng(4242);
  size_t disagree = 0, inside = 0, tested = 0;
  for (int poly = 0; poly < 200; ++poly) {
    const double clat = 25.5, clon = 91.9;
    const int verts = 5 + int(rng.below(14));
    geo::Ring ring;
    for (int v = 0; v < verts; ++v) {
      const double a = 6.283185307 * double(v) / double(verts);
      const double r = rng.range(0.004, 0.012);        // irregular -> concave
      ring.push_back({clat + r * std::sin(a), clon + r * std::cos(a)});
    }
    geo::Polygon p(std::move(ring));
    if (p.validate() != geo::Polygon::Validity::Ok) continue;
    for (int k = 0; k < 500; ++k) {
      const geo::LatLon q{rng.range(clat - 0.015, clat + 0.015),
                          rng.range(clon - 0.015, clon + 0.015)};
      const bool r1 = geo::contains(p, q), r2 = geo::contains_winding(p, q);
      ++tested; if (r1) ++inside;
      if (r1 != r2) ++disagree;
    }
  }
  printf("  %zu points against 200 polygons (%zu inside)   disagreements: %s%zu\033[0m\n",
         tested, inside, disagree ? "\033[31m" : "\033[32m", disagree);
  return disagree == 0;
}


// ── 10. Hysteresis A/B ───────────────────────────────────────────────────────
//
// The filter's job is to make noisy fixes produce the transitions that PERFECT
// fixes would have produced under the same policy. So each noise model runs four
// simulations over identical trajectories (the GPS model draws the same random
// numbers whatever its sigma, so movement is unchanged):
//   noise-free, filter off   the raw truth: every boundary crossing
//   noise-free, filter on    the TARGET: truth under the dwell/confirmation policy
//   noisy, filter off        what a naive geofence reports
//   noisy, filter on         what this engine reports
// "Excess removed" is the share of the naive run's excess over the target that
// the filter eliminates. Transitions = confirmed ZoneEnter + ZoneExit events.
// An earlier version compared only the two noisy runs and called the difference
// "false transitions removed", which cannot tell a removed flap from a
// suppressed real crossing.
static void bench_hysteresis(FILE* csv) {
  printf("\n\033[1m10. HYSTERESIS A/B\033[0m  [extension]   60 tourists x 30 min on the OSM zones; "
         "enter + exit events\n");
  printf("  %-26s %10s  %10s  %10s  %10s  %9s\n", "noise model", "truth raw", "target",
         "naive", "filtered", "excess cut");
  printf("  %-26s %10s  %10s  %10s  %10s  %9s\n", "", "(no noise)", "(no noise,", "(noisy,",
         "(noisy,", "");
  printf("  %-26s %10s  %10s  %10s  %10s  %9s\n", "", "", "filter on)", "filter off)",
         "filter on)", "");
  printf("  ──────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "noise_model,correlation,truth_raw,target,naive,filtered,excess_removed_pct,"
                        "filtered_vs_target_pct\n");

  const struct { const char* name; double rho; } models[] = {
      {"white noise (rho=0)", 0.0}, {"realistic drift (rho=0.9)", 0.9}};

  auto run = [](double rho, bool noisy, bool filter) -> uint64_t {
    sim::SimConfig cfg;
    cfg.tourists = 60; cfg.groups = 6; cfg.seed = 20260817;
    cfg.duration_ms = 1800000; cfg.tick_ms = 1000;
    cfg.gps.correlation = rho;
    if (!noisy) { cfg.gps.open_sky_m = 0.01; cfg.gps.multipath_m = 0.01; }
    cfg.eval.hysteresis.enabled = filter;
    sim::Simulator s(cfg);
    std::string err;
    if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) return 0;
    s.spawn_tourists();
    s.run();
    return s.summary().enters + s.summary().exits;
  };

  for (const auto& nm : models) {
    const uint64_t raw = run(nm.rho, false, false), target = run(nm.rho, false, true);
    const uint64_t naive = run(nm.rho, true, false), filtered = run(nm.rho, true, true);
    const double excess = naive > target ? double(naive - target) : 0.0;
    const double removed = excess > 0 ? 100.0 * (double(naive) - double(filtered)) / excess : 0.0;
    const double vs_target = target ? 100.0 * double(filtered) / double(target) : 0.0;
    printf("  %-26s %10llu  %10llu  %10llu  %10llu  %8.1f%%\n", nm.name,
           (unsigned long long)raw, (unsigned long long)target, (unsigned long long)naive,
           (unsigned long long)filtered, removed);
    if (csv) fprintf(csv, "%s,%.1f,%llu,%llu,%llu,%llu,%.1f,%.1f\n", nm.name, nm.rho,
                     (unsigned long long)raw, (unsigned long long)target,
                     (unsigned long long)naive, (unsigned long long)filtered, removed, vs_target);
  }
  printf("\n  Filtered vs target is the check that matters: close to 100%% means the filter\n");
  printf("  recovers what perfect GPS would have reported. Counts are a first-order check;\n");
  printf("  they do not match individual events to individual crossings.\n");
}


// ── 6. Routing: A* vs Dijkstra, node expansions on the road grid ──────────────
static void bench_routing(FILE* csv) {
  printf("\n\033[1m11. ROUTING  A* vs DIJKSTRA\033[0m  [extension]   single-pair queries on a road grid\n");
  printf("  %8s  %8s  %12s  %12s  %9s  %10s  %10s\n", "nodes", "queries",
         "dijkstra exp", "A* expanded", "less work", "dijkstra us", "A* us");
  printf("  ────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "nodes,queries,dijkstra_expanded,astar_expanded,work_reduction_pct,"
                        "dijkstra_us,astar_us\n");

  const geo::Bbox area{25.40, 91.70, 25.75, 92.05};
  for (int side : {8, 16, 24, 32}) {
    graph::RoadGraph g = graph::RoadGraph::grid(area, side, side, /*seed=*/7);
    sim::Rng rng(123);
    const int Q = 400;
    unsigned long long dij_exp = 0, ast_exp = 0;
    double dij_ms = 0, ast_ms = 0;
    int done = 0;
    for (int q = 0; q < Q; ++q) {
      const graph::NodeId s = graph::NodeId(rng.below(uint32_t(g.node_count())));
      const graph::NodeId d = graph::NodeId(rng.below(uint32_t(g.node_count())));
      auto t0 = Clock::now();
      const auto sp = graph::dijkstra(g, s, d);
      dij_ms += ms_since(t0);
      t0 = Clock::now();
      const auto as = graph::astar(g, s, d);
      ast_ms += ms_since(t0);
      if (!sp.reachable(d) || !as.found) continue;
      dij_exp += sp.nodes_expanded;
      ast_exp += as.nodes_expanded;
      ++done;
    }
    const double reduction = dij_exp ? 100.0 * (1.0 - double(ast_exp) / double(dij_exp)) : 0.0;
    printf("  %8zu  %8d  %12.1f  %12.1f  %8.1f%%  %10.2f  %10.2f\n", g.node_count(), done,
           done ? double(dij_exp) / done : 0.0, done ? double(ast_exp) / done : 0.0, reduction,
           dij_ms * 1000.0 / Q, ast_ms * 1000.0 / Q);
    if (csv) fprintf(csv, "%zu,%d,%.2f,%.2f,%.1f,%.4f,%.4f\n", g.node_count(), done,
                     done ? double(dij_exp) / done : 0.0, done ? double(ast_exp) / done : 0.0,
                     reduction, dij_ms * 1000.0 / Q, ast_ms * 1000.0 / Q);
  }
  printf("\n  A* returns the SAME shortest path as Dijkstra but settles fewer nodes --\n");
  printf("  the map-distance heuristic steers the search toward the target.\n");
}


// ── 7. Dispatch: greedy vs optimal (Hungarian) responder assignment ───────────
static void bench_dispatch(FILE* csv) {
  printf("\n\033[1m12. DISPATCH  GREEDY vs OPTIMAL\033[0m  [extension]   total responder travel, averaged over 200 layouts\n");
  printf("  %10s  %12s  %12s  %10s  %10s\n", "responders", "greedy m", "optimal m",
         "saved", "greedy>=opt");
  printf("  ──────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "size,greedy_avg_m,optimal_avg_m,saved_pct,optimal_never_worse\n");

  const geo::Bbox area{25.40, 91.70, 25.75, 92.05};
  graph::RoadGraph g = graph::RoadGraph::grid(area, 16, 16, /*seed=*/9);
  for (int size : {5, 10, 20, 40}) {
    sim::Rng rng(555);
    double sum_greedy = 0, sum_opt = 0;
    int trials = 200, never_worse = 0;
    for (int tr = 0; tr < trials; ++tr) {
      dispatch::ResponderPool pool;
      for (int i = 0; i < size; ++i) {
        dispatch::Responder r;
        r.pos = g.pos(graph::NodeId(rng.below(uint32_t(g.node_count()))));
        pool.add(r);
      }
      pool.snap_all(g);
      std::vector<dispatch::Incident> inc;
      for (int i = 0; i < size; ++i)
        inc.push_back({IncidentId(i), g.pos(graph::NodeId(rng.below(uint32_t(g.node_count())))),
                       graph::kNoNode});
      dispatch::snap_incidents(inc, g);
      const auto gr = dispatch::assign_greedy(pool, inc, g);
      const auto op = dispatch::assign_optimal(pool, inc, g);
      sum_greedy += gr.total_m;
      sum_opt += op.total_m;
      if (op.total_m <= gr.total_m + 1.0) ++never_worse;
    }
    const double saved = sum_greedy > 0 ? 100.0 * (1.0 - sum_opt / sum_greedy) : 0.0;
    printf("  %10d  %12.0f  %12.0f  %9.1f%%  %6d/%d\n", size, sum_greedy / trials,
           sum_opt / trials, saved, never_worse, trials);
    if (csv) fprintf(csv, "%d,%.1f,%.1f,%.1f,%d\n", size, sum_greedy / trials,
                     sum_opt / trials, saved, never_worse == trials ? 1 : 0);
  }
  printf("\n  Hungarian is never worse than greedy and typically cheaper -- greedy's\n");
  printf("  early cheap pick can strand a later incident with only a distant responder.\n");
}


// ── 8. Power: adaptive sampling vs continuous polling, at matched recall ──────
static void bench_power(FILE* csv) {
  printf("\n\033[1m13. ADAPTIVE SAMPLING\033[0m  [extension]   risk-adaptive GPS sampling vs continuous 1 Hz\n");
  printf("  %-22s %11s  %11s  %10s  %12s\n", "day profile", "cont. fixes", "adaptive",
         "battery", "near-zone");
  printf("  %-22s %11s  %11s  %10s  %12s\n", "", "(1 Hz)", "fixes", "saved", "recall");
  printf("  ──────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "profile,continuous_fixes,adaptive_fixes,battery_saved_pct,near_zone_recall_pct\n");

  // An 8-hour trek: mostly far from any hazard, with a few approach episodes where
  // the nearest zone ramps from 5 km down to 30 m and back (~12 min each).
  const int64_t secs = 8 * 3600;
  auto dist_at = [&](int64_t t) -> double {
    const int64_t period = 90 * 60;          // one approach episode every 90 min
    const int64_t phase = t % period;
    if (phase > 12 * 60) return 8000.0;       // far the rest of the time
    const double u = double(phase) / double(12 * 60);       // 0..1 across the episode
    const double tri = 1.0 - std::fabs(2.0 * u - 1.0);      // 0 -> 1 -> 0
    return 5000.0 - tri * (5000.0 - 30.0);                  // 5 km -> 30 m -> 5 km
  };

  power::AdaptiveSampler sampler;   // default tiers
  uint64_t adaptive = 0, continuous = 0;
  uint64_t near_secs = 0, near_covered = 0;
  int64_t last_fix = -100000;
  for (int64_t t = 0; t < secs; ++t) {
    const int64_t now = t * 1000;
    const double d = dist_at(t);
    ++continuous;                                            // 1 Hz baseline
    if (sampler.should_sample(now, d, /*speed*/1.4, /*alert*/false)) { ++adaptive; last_fix = now; }
    if (d < 200.0) {                                         // "near a zone": recall matters
      ++near_secs;
      if (now - last_fix <= 4000) ++near_covered;            // a fix within the last few seconds
    }
  }
  const double saved = 100.0 * (1.0 - double(adaptive) / double(continuous));
  const double recall = near_secs ? 100.0 * double(near_covered) / double(near_secs) : 100.0;
  printf("  %-22s %11llu  %11llu  %9.1f%%  %10.1f%%\n", "8h trek, 5 approaches",
         (unsigned long long)continuous, (unsigned long long)adaptive, saved, recall);
  if (csv) fprintf(csv, "8h_trek,%llu,%llu,%.1f,%.1f\n",
                   (unsigned long long)continuous, (unsigned long long)adaptive, saved, recall);
  printf("\n  Battery saved by sampling on proximity to risk -- while still catching\n");
  printf("  essentially every second the tourist is near a hazard (recall stays high).\n");
}


// ── 10. Churn ────────────────────────────────────────────────────────────────
//
// Every structure here was originally written as if it were built once and
// queried forever. This measures what a long-running index actually experiences:
// repeated insert and delete, with the live set held constant. The failure being
// measured is not wrongness -- correctness is asserted in the tests -- it is
// DECAY: node counts that never come back down, and query times that drift up.
static void bench_churn(FILE* csv) {
  printf("\n\033[1m14. INDEX CHURN\033[0m   insert/delete cycles at a constant live-set size\n");
  if (csv) fprintf(csv, "structure,metric,fresh,after_churn,ratio\n");

  Corpus c = make_corpus(2000, 1500, 5150);
  sim::Rng rng(24680);

  auto churn_index = [&](index::SpatialIndex& ix, const char* name) {
    ix.build(c.boxes);
    const auto fresh_stats = ix.stats();
    const Timing fresh = time_queries(ix, c, 450);

    // 20 rounds of "add 2000 new zones, then remove them again". The live set
    // returns to exactly the original 2000 every round.
    index::ZoneId next = index::ZoneId(c.boxes.size());
    for (int round = 0; round < 20; ++round) {
      std::vector<index::ZoneId> added;
      for (size_t i = 0; i < c.boxes.size(); ++i) {
        const double clat = rng.range(25.40, 25.75), clon = rng.range(91.70, 92.05);
        const double r = rng.range(0.0003, 0.0022);
        ix.insert(next, {clat - r, clon - r, clat + r, clon + r});
        added.push_back(next);
        ++next;
      }
      for (index::ZoneId id : added) ix.remove(id);
    }

    ix.reset_counters();
    const auto churned_stats = ix.stats();
    const Timing after = time_queries(ix, c, 450);

    printf("  %-10s  live %5zu   nodes %6zu -> %6zu (%.2fx)   %7.2f -> %7.2f us/query (%.2fx)\n",
           name, ix.size(), fresh_stats.node_count, churned_stats.node_count,
           fresh_stats.node_count ? double(churned_stats.node_count) / double(fresh_stats.node_count) : 0.0,
           fresh.median_us, after.median_us,
           fresh.median_us > 0 ? after.median_us / fresh.median_us : 0.0);
    if (csv) {
      fprintf(csv, "%s,nodes,%zu,%zu,%.3f\n", name, fresh_stats.node_count,
              churned_stats.node_count,
              fresh_stats.node_count ? double(churned_stats.node_count) / double(fresh_stats.node_count) : 0.0);
      fprintf(csv, "%s,query_us,%.4f,%.4f,%.3f\n", name, fresh.median_us, after.median_us,
              fresh.median_us > 0 ? after.median_us / fresh.median_us : 0.0);
      fprintf(csv, "%s,bytes,%zu,%zu,%.3f\n", name, fresh_stats.bytes, churned_stats.bytes,
              fresh_stats.bytes ? double(churned_stats.bytes) / double(fresh_stats.bytes) : 0.0);
    }
  };

  { index::Quadtree qt; churn_index(qt, "quadtree"); }
  { index::RTree rt;    churn_index(rt, "r-tree"); }
  { index::Geohash gh;  churn_index(gh, "geohash"); }

  // The hash table's failure mode is different: tombstones, not nodes. A table
  // holding a constant live set must not grow without bound while churning.
  {
    ds::HashMap<uint32_t, uint32_t> h;
    for (uint32_t i = 0; i < 2000; ++i) h.put(i, i);
    const size_t fresh_buckets = h.bucket_count();
    for (uint32_t round = 0; round < 200; ++round)
      for (uint32_t i = 0; i < 2000; ++i) {
        const uint32_t k = 100000 + round * 2000 + i;
        h.put(k, k);
        h.erase(k);
      }
    printf("  %-10s  live %5zu   buckets %6zu -> %6zu (%.2fx)   %zu same-size rebuilds, "
           "%zu growths\n",
           "hash table", h.size(), fresh_buckets, h.bucket_count(),
           double(h.bucket_count()) / double(fresh_buckets), h.rehashes(), h.growths());
    if (csv) fprintf(csv, "hash_table,buckets,%zu,%zu,%.3f\n", fresh_buckets,
                     h.bucket_count(), double(h.bucket_count()) / double(fresh_buckets));
  }

  printf("\n  A ratio near 1.00x means the structure returns to the shape its live set\n");
  printf("  deserves. Before compaction these grew without bound -- the quadtree kept the\n");
  printf("  subdivision of its high-water mark, the r-tree kept underfull nodes, and the\n");
  printf("  hash table doubled to make room for tombstones.\n");
  printf("  The r-tree does NOT return all the way to 1.00x, and that is expected rather\n");
  printf("  than a residual bug: condensing reinserts orphaned entries from the root, so\n");
  printf("  a churned tree is a validly-shaped but differently-grouped tree, not the one\n");
  printf("  a fresh build would produce. It is bounded, which is the property that\n");
  printf("  matters; the unbounded growth is gone. Guttman-style same-level reinsertion\n");
  printf("  would close the gap and is noted as a deliberate non-goal in the header.\n");
}


// ── 11. Serialisation  [GAP 6] ───────────────────────────────────────────────
//
// The offline claim is that a district's zones ship to a device as a compact blob
// and are queried locally with no server. That is a size and a latency, so both
// are measured rather than asserted.
static void bench_serialization(FILE* csv) {
  printf("\n\033[1m15. SERIALISATION\033[0m  [extension]   geohash blob: size and round-trip cost\n");
  printf("  %8s  %11s  %10s  %11s  %11s  %11s\n",
         "zones", "bytes", "bytes/zone", "write ms", "read ms", "MB/s read");
  printf("  ──────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,bytes,bytes_per_zone,write_ms,read_ms,read_mb_s\n");

  for (size_t n : {1000u, 10000u, 100000u}) {
    Corpus c = make_corpus(n, 1, 4242);
    index::Geohash gh;
    gh.build(c.boxes);

    std::vector<uint8_t> blob;
    gh.serialize(blob);                       // warmup
    constexpr int kRuns = 5;
    double w[kRuns], r[kRuns];
    for (int i = 0; i < kRuns; ++i) {
      auto t0 = Clock::now();
      gh.serialize(blob);
      w[i] = ms_since(t0);
      index::Geohash back;
      t0 = Clock::now();
      back.deserialize(blob);
      r[i] = ms_since(t0);
    }
    std::sort(w, w + kRuns);
    std::sort(r, r + kRuns);
    const double wm = w[kRuns / 2], rm = r[kRuns / 2];
    const double mb = double(blob.size()) / (1024.0 * 1024.0);

    printf("  %8zu  %11zu  %10.1f  %11.2f  %11.2f  %11.1f\n",
           n, blob.size(), double(blob.size()) / double(n), wm, rm,
           rm > 0 ? mb / (rm / 1000.0) : 0.0);
    if (csv) fprintf(csv, "%zu,%zu,%.2f,%.4f,%.4f,%.1f\n", n, blob.size(),
                     double(blob.size()) / double(n), wm, rm,
                     rm > 0 ? mb / (rm / 1000.0) : 0.0);
  }
  printf("\n  44 bytes per zone: a 48-bit Morton key, a 32-bit id and four doubles.\n");
  printf("  A whole district fits in a few hundred kB -- which is the actual offline\n");
  printf("  argument, and it is a number rather than an adjective.\n");
  printf("  Layout is explicitly little-endian (util/bytes.hpp), so the blob is portable\n");
  printf("  across hosts; deserialisation validates and REFUSES malformed input rather\n");
  printf("  than loading a prefix (tests/index/serialization_test.cpp).\n");
}


// ── 12. Self-intersection: sweep vs pairwise, and where they cross ───────────
//
// Polygon::validate() dispatches on ring size: the pairwise O(V^2) scan below
// geo::kSweepThresholdVertices, the Shamos-Hoey sweep at or above it. This is the
// measurement that fixes the threshold. Both are run on the SAME rings and their
// verdicts compared, so the table is also a correctness check -- a faster
// algorithm that answers a different question is not faster.
static void bench_selfintersect(FILE* csv) {
  printf("\n\033[1m16. SELF-INTERSECTION\033[0m  [validation]   Shamos-Hoey sweep vs the O(V^2) pairwise"
         " reference, median of 7\n");
  printf("  vertices    pairwise us      sweep us    speedup   verdicts agree\n");
  printf("  ─────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "vertices,pairwise_us,sweep_us,speedup,agree,rings\n");

  sim::Rng rng(0x5EEEP1);
  for (size_t n : {8u, 16u, 32u, 48u, 64u, 80u, 96u, 112u, 128u, 256u, 512u, 2048u}) {
    // A simple ring, which is the case that matters: a self-intersecting one
    // lets both implementations exit early, and validation's cost is dominated by
    // the polygons that pass. Points on a jittered circle stay simple.
    std::vector<geo::Ring> rings;
    const int kRings = n > 256 ? 20 : 200;
    for (int r = 0; r < kRings; ++r) {
      geo::Ring ring;
      for (size_t i = 0; i < n; ++i) {
        const double a = 6.283185307179586 * double(i) / double(n);
        const double rad = 0.30 + rng.range(-0.05, 0.05);
        ring.push_back({25.5 + rad * std::sin(a), 91.5 + rad * std::cos(a)});
      }
      rings.push_back(std::move(ring));
    }

    int agree = 0;
    for (const auto& r : rings)
      if (geo::ring_self_intersects_pairwise(r) == geo::ring_self_intersects_sweep(r)) ++agree;

    auto time_one = [&](bool sweep) {
      for (const auto& r : rings)                  // warmup
        (void)(sweep ? geo::ring_self_intersects_sweep(r)
                     : geo::ring_self_intersects_pairwise(r));
      constexpr int kRuns = 7;
      double t[kRuns];
      for (int k = 0; k < kRuns; ++k) {
        auto t0 = Clock::now();
        for (const auto& r : rings)
          (void)(sweep ? geo::ring_self_intersects_sweep(r)
                       : geo::ring_self_intersects_pairwise(r));
        t[k] = ms_since(t0) * 1000.0 / double(rings.size());
      }
      std::sort(t, t + kRuns);
      return t[kRuns / 2];
    };
    const double pair_us = time_one(false), sweep_us = time_one(true);
    printf("  %8zu   %12.3f  %12.3f   %8.2fx   %11s\n", n, pair_us, sweep_us,
           sweep_us > 0 ? pair_us / sweep_us : 0.0,
           agree == kRings ? "\033[32myes\033[0m" : "\033[31mNO\033[0m");
    if (csv) fprintf(csv, "%zu,%.4f,%.4f,%.3f,%d,%d\n", n, pair_us, sweep_us,
                     sweep_us > 0 ? pair_us / sweep_us : 0.0, agree, kRings);
  }
  printf("\n  The crossover is where speedup passes 1.00x. Polygon::validate() dispatches\n");
  printf("  at kSweepThresholdVertices = %zu, read off this table. Below it the sweep\n",
         geo::kSweepThresholdVertices);
  printf("  pays to sort 2V events and build a balanced tree in order to skip a few\n");
  printf("  dozen orientation tests, so the O(V^2) reference wins; above it O(V log V)\n");
  printf("  takes over and keeps widening the gap. Verdicts agree on every ring, which\n");
  printf("  is what makes dispatching between them safe -- a faster algorithm that\n");
  printf("  answered a slightly different question would not be faster, it would be a\n");
  printf("  second opinion. The reference is not deleted: it is the oracle.\n");
}


// ── 14. Node snapping: k-d tree vs the linear scan ───────────────────────────
//
// Every dispatch decision snaps a GPS fix to the nearest road junction, twice per
// (responder, incident) pair while building the cost matrix. This was an O(V)
// scan. Both are kept -- the scan is the oracle -- so this is the measurement
// that justifies which one the pipeline calls, and it re-checks that they return
// the SAME node, which is the only reason the swap is safe.
static void bench_snap(FILE* csv) {
  printf("\n\033[1m17. NODE SNAPPING\033[0m  [extension]   k-d tree vs linear scan, nearest road junction\n");
  printf("     nodes     linear us     k-d tree us      speedup   same node\n");
  printf("  ────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "nodes,linear_us,kdtree_us,speedup,agree,probes\n");

  const geo::Bbox area{25.50, 91.83, 25.62, 91.95};
  for (int side : {8, 16, 32, 64, 100}) {
    graph::RoadGraph g = graph::RoadGraph::grid(area, side, side, 7);
    sim::Rng rng(0x5AAA);
    std::vector<geo::LatLon> probes;
    for (int i = 0; i < 2000; ++i)
      probes.push_back({rng.range(area.min_lat, area.max_lat),
                        rng.range(area.min_lon, area.max_lon)});

    int agree = 0;
    for (const auto& p : probes)
      if (g.nearest_node(p) == g.nearest_node_linear(p)) ++agree;

    auto time_one = [&](bool kd) {
      for (const auto& p : probes) (void)(kd ? g.nearest_node(p) : g.nearest_node_linear(p));
      constexpr int kRuns = 7;
      double t[kRuns];
      for (int k = 0; k < kRuns; ++k) {
        auto t0 = Clock::now();
        for (const auto& p : probes) (void)(kd ? g.nearest_node(p) : g.nearest_node_linear(p));
        t[k] = ms_since(t0) * 1000.0 / double(probes.size());
      }
      std::sort(t, t + kRuns);
      return t[kRuns / 2];
    };
    const double lin_us = time_one(false), kd_us = time_one(true);
    printf("  %8zu   %11.4f   %13.4f   %10.2fx   %9s\n", g.node_count(), lin_us, kd_us,
           kd_us > 0 ? lin_us / kd_us : 0.0,
           agree == int(probes.size()) ? "\033[32myes\033[0m" : "\033[31mNO\033[0m");
    if (csv) fprintf(csv, "%zu,%.4f,%.4f,%.3f,%d,%zu\n", g.node_count(), lin_us, kd_us,
                     kd_us > 0 ? lin_us / kd_us : 0.0, agree, probes.size());
  }
  printf("\n  O(V) -> O(log V) expected. The build cost is paid once and amortised:\n");
  printf("  the tree is built lazily on the first snap and invalidated by add_node,\n");
  printf("  and the dispatch path snaps every responder and every incident per\n");
  printf("  assignment. Both implementations break ties on the lower NodeId, which\n");
  printf("  is why \"same node\" can be asserted rather than \"same distance\" -- a\n");
  printf("  different snap would change the whole dispatch plan.\n");
}


static FILE* open_csv(const std::string& dir, const char* name) {
  return dir.empty() ? nullptr : std::fopen((dir + "/" + name).c_str(), "w");
}
static void close_csv(FILE* f) { if (f) std::fclose(f); }

int main(int argc, char** argv) {
  // --out DIR      write CSVs there
  // --only 1,2,9   run just those sections (default: all). Useful for repeating
  //                one experiment to see its run-to-run variation.
  std::string out;
  uint32_t only = 0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!std::strcmp(argv[i], "--only") && i + 1 < argc)
      for (const char* p = argv[++i]; *p;) {
        char* end = nullptr;
        const long v = std::strtol(p, &end, 10);
        if (end == p) break;
        if (v > 0 && v < 32) only |= 1u << v;
        p = *end ? end + 1 : end;
      }
  }
  auto want = [only](int section) { return only == 0 || (only & (1u << section)) != 0; };

#if defined(__APPLE__)
  // A request for the performance cores, not a pin; see the protocol note at the
  // top. The paired ratios are what keep comparisons valid if it is ignored.
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

  printf("\n\033[1msafetrail benchmark\033[0m   medians of %d interleaved rounds, each sample "
         ">= %.0f ms; ratios are paired\n", kRounds, kMinSampleMs);
  printf("═════════════════════════════════════════════════════════════════════════════\n");

  FILE* f = nullptr;
  bool g1 = true, g2 = true, g3 = true, g5 = true, g6 = true, g8 = true;
  if (want(1)) { f = open_csv(out, "index_scaling.csv"); g1 = bench_scaling(f);    close_csv(f); }
  if (want(2)) { f = open_csv(out, "index_density.csv"); g2 = bench_density(f);    close_csv(f); }
  if (want(3)) { f = open_csv(out, "end_to_end.csv");    g3 = bench_end_to_end(f); close_csv(f); }
  if (want(4)) { f = open_csv(out, "index_costs.csv");   bench_costs(f);           close_csv(f); }
  if (want(5)) g5 = bench_equivalence();
  if (want(6)) g6 = bench_containment();
  if (want(7)) { f = open_csv(out, "index_build.csv");   bench_bulkload(f);        close_csv(f); }
  if (want(8)) { f = open_csv(out, "interval_tree.csv"); g8 = bench_interval(f);   close_csv(f); }
  if (want(9)) {
    f = open_csv(out, "versioned_index.csv");
    FILE* f2 = open_csv(out, "versioned_mutations.csv");
    bench_versioned(f, f2);
    close_csv(f); close_csv(f2);
  }

  if (want(10) || want(11) || want(12) || want(13) || want(14) || want(15) || want(16) || want(17))
    printf("\n\033[1m── extensions ──────────────────────────────────────────────────────────────\033[0m\n");
  if (want(10)) { f = open_csv(out, "hysteresis_ab.csv");     bench_hysteresis(f);    close_csv(f); }
  if (want(11)) { f = open_csv(out, "routing.csv");           bench_routing(f);       close_csv(f); }
  if (want(12)) { f = open_csv(out, "dispatch.csv");          bench_dispatch(f);      close_csv(f); }
  if (want(13)) { f = open_csv(out, "power.csv");             bench_power(f);         close_csv(f); }
  if (want(14)) { f = open_csv(out, "index_churn.csv");       bench_churn(f);         close_csv(f); }
  if (want(15)) { f = open_csv(out, "serialization.csv");     bench_serialization(f); close_csv(f); }
  if (want(16)) { f = open_csv(out, "self_intersection.csv"); bench_selfintersect(f); close_csv(f); }
  if (want(17)) { f = open_csv(out, "node_snap.csv");         bench_snap(f);          close_csv(f); }

  const bool all = g1 && g2 && g3 && g5 && g6 && g8;
  printf("\n═════════════════════════════════════════════════════════════════════════════\n");
  printf("  correctness gates: scaling %s  density %s  end-to-end %s  equivalence %s  "
         "containment %s  interval %s\n", ok_str(g1), ok_str(g2), ok_str(g3), ok_str(g5),
         ok_str(g6), ok_str(g8));
  if (!out.empty()) printf("  csv written to %s/\n", out.c_str());
  printf("\n");
  return all ? 0 : 1;
}
