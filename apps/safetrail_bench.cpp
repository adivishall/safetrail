// safetrail_bench -- the measurements that make this a project rather than a demo.
//
//   1. Index scaling      brute force vs quadtree, 10 -> 100,000 zones
//   2. Equivalence        every index must agree with brute force, exactly
//   3. Hysteresis A/B     false-alert reduction under injected GPS noise  [GAP 8]
//   4. Containment cross-check   ray casting vs winding number
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/sim/simulator.hpp"

using namespace safetrail;
using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

struct Corpus {
  std::vector<std::pair<index::ZoneId, geo::Bbox>> boxes;
  std::vector<geo::LatLon> probes;
};

static Corpus make_corpus(size_t n, size_t probes, uint64_t seed) {
  sim::Rng rng(seed);
  Corpus c;
  const geo::Bbox area{25.40, 91.70, 25.75, 92.05};
  for (size_t i = 0; i < n; ++i) {
    const double clat = rng.range(area.min_lat, area.max_lat);
    const double clon = rng.range(area.min_lon, area.max_lon);
    const double r = rng.range(0.0003, 0.0022);
    geo::Bbox b{clat - r, clon - r, clat + r, clon + r};
    c.boxes.emplace_back(index::ZoneId(i), b);
  }
  for (size_t i = 0; i < probes; ++i)
    c.probes.push_back({rng.range(area.min_lat, area.max_lat),
                        rng.range(area.min_lon, area.max_lon)});
  return c;
}

static double time_queries(index::SpatialIndex& ix, const Corpus& c, double radius) {
  std::vector<index::ZoneId> out; out.reserve(8192);
  auto t0 = Clock::now();
  for (const auto& p : c.probes) { out.clear(); ix.query(geo::Bbox::around(p, radius), out); }
  return ms_since(t0) * 1000.0 / double(c.probes.size());     // us per query
}

static void bench_scaling(FILE* csv) {
  printf("\n\033[1m1. INDEX SCALING\033[0m   450 m query box, 2000 probes per row, us/query\n");
  printf("  %8s  %11s  %11s  %11s  %9s  %9s  %8s\n",
         "zones", "brute", "quadtree", "r-tree", "QT gain", "RT gain", "cands");
  printf("  ─────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,brute_us,quad_us,rtree_us,quad_speedup,rtree_speedup,"
                        "candidates,quad_nodes,quad_depth,rtree_nodes,rtree_depth\n");

  for (size_t n : {10u, 100u, 1000u, 5000u, 20000u, 50000u, 100000u}) {
    Corpus c = make_corpus(n, 2000, 99);
    index::BruteForceIndex bf; bf.build(c.boxes);
    index::Quadtree qt;        qt.build(c.boxes);
    index::RTree rt;           rt.build(c.boxes);

    const double bf_us = time_queries(bf, c, 450);
    const double qt_us = time_queries(qt, c, 450);
    const double rt_us = time_queries(rt, c, 450);
    const auto bs = bf.stats(), qs = qt.stats(), rs = rt.stats();

    printf("  %8zu  %11.2f  %11.2f  %11.2f  %8.1fx  %8.1fx  %8.2f\n", n, bf_us, qt_us, rt_us,
           qt_us > 0 ? bf_us / qt_us : 0.0, rt_us > 0 ? bf_us / rt_us : 0.0,
           bs.avg_candidates());
    if (csv) fprintf(csv, "%zu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%zu,%zu,%zu,%zu\n",
                     n, bf_us, qt_us, rt_us, qt_us > 0 ? bf_us / qt_us : 0.0,
                     rt_us > 0 ? bf_us / rt_us : 0.0, bs.avg_candidates(),
                     qs.node_count, qs.max_depth, rs.node_count, rs.max_depth);
  }
  printf("\n  Note: candidates returned is IDENTICAL across all three -- they are true\n");
  printf("  positives. The speedup ceiling is output size k, exactly as O(log n + k) says.\n");
}

static bool bench_equivalence() {
  printf("\n\033[1m2. EQUIVALENCE\033[0m   every index must return EXACTLY what brute force returns\n");
  bool all_ok = true;
  for (size_t n : {50u, 500u, 5000u}) {
    Corpus c = make_corpus(n, 2000, 7);
    index::BruteForceIndex bf; bf.build(c.boxes);
    index::Quadtree qt;        qt.build(c.boxes);
    index::RTree rt;           rt.build(c.boxes);

    size_t mq = 0, mr = 0, total = 0, queries = 0;
    std::vector<index::ZoneId> a, b, d;
    for (const auto& p : c.probes)
      for (double r : {80.0, 400.0, 2000.0}) {
        a.clear(); b.clear(); d.clear();
        const geo::Bbox q = geo::Bbox::around(p, r);
        bf.query(q, a); qt.query(q, b); rt.query(q, d);
        std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end()); std::sort(d.begin(), d.end());
        total += a.size(); ++queries;
        if (a != b) ++mq;
        if (a != d) ++mr;
      }
    printf("  %6zu zones  %6zu queries  %8zu hits   quadtree: %s%zu\033[0m   r-tree: %s%zu\033[0m\n",
           n, queries, total, mq ? "\033[31m" : "\033[32m", mq,
           mr ? "\033[31m" : "\033[32m", mr);
    if (mq || mr) all_ok = false;
  }
  return all_ok;
}

static bool bench_containment() {
  printf("\n\033[1m4. CONTAINMENT CROSS-CHECK\033[0m   ray casting vs winding number\n");
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

static void bench_hysteresis(FILE* csv) {
  printf("\n\033[1m3. HYSTERESIS A/B  [GAP 8]\033[0m   identical seed and noise, filter on vs off\n");
  printf("  %-12s  %10s  %10s  %10s  %12s\n",
         "hysteresis", "enters", "exits", "transitions", "suppressed");
  printf("  ──────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "hysteresis,enters,exits,transitions,suppressed\n");

  uint64_t base_trans = 0;
  for (int mode = 0; mode < 2; ++mode) {
    sim::SimConfig cfg;
    cfg.tourists = 60; cfg.groups = 6; cfg.seed = 20260817;
    cfg.duration_ms = 1800000; cfg.tick_ms = 1000;
    cfg.eval.hysteresis.enabled = (mode == 1);
    sim::Simulator s(cfg);
    std::string err;
    if (!s.load_zones("data/zones/meghalaya.geojson", &err)) { printf("  %s\n", err.c_str()); return; }
    s.spawn_tourists();
    s.run();
    const auto sum = s.summary();
    const auto c = s.counters();
    const uint64_t trans = sum.enters + sum.exits;
    printf("  %-12s  %10llu  %10llu  %10llu  %12llu\n", mode ? "ON" : "OFF",
           (unsigned long long)sum.enters, (unsigned long long)sum.exits,
           (unsigned long long)trans, (unsigned long long)c.flaps_suppressed);
    if (csv) fprintf(csv, "%s,%llu,%llu,%llu,%llu\n", mode ? "on" : "off",
                     (unsigned long long)sum.enters, (unsigned long long)sum.exits,
                     (unsigned long long)trans, (unsigned long long)c.flaps_suppressed);
    if (mode == 0) base_trans = trans;
    else if (base_trans)
      printf("\n  \033[1mfalse transitions removed: %.1f%%\033[0m  (%llu -> %llu)\n",
             100.0 * (1.0 - double(trans) / double(base_trans)),
             (unsigned long long)base_trans, (unsigned long long)trans);
  }
}

static void bench_versioned(FILE* csv) {
  printf("\n\033[1m5. PERSISTENT INDEX  [GAP 3]\033[0m   path copying vs copying the whole tree\n");
  printf("  %8s  %10s  %12s  %14s  %9s  %11s\n", "versions", "allocated",
         "full copies", "sharing", "query@past", "query@now");
  printf("  ──────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "versions,allocated,full_copies,sharing_ratio,query_past_us,query_now_us\n");

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
    for (int i = 0; i < 500; ++i)
      probes.push_back({rng.range(25.49, 25.63), rng.range(91.79, 91.97)});

    const auto st = ix.share_stats();
    const Timestamp mid = Timestamp(1000 * (n / 2));

    std::vector<index::ZoneId> out;
    auto t0 = Clock::now();
    for (const auto& p : probes) { out.clear(); ix.query_at(mid, geo::Bbox::around(p, 450), out); }
    const double past_us = ms_since(t0) * 1000.0 / double(probes.size());

    t0 = Clock::now();
    for (const auto& p : probes) { out.clear(); ix.query_now(geo::Bbox::around(p, 450), out); }
    const double now_us = ms_since(t0) * 1000.0 / double(probes.size());

    printf("  %8zu  %10zu  %12zu  %12.1fx  %8.2f  %10.2f\n", ix.version_count(),
           st.total_nodes_allocated, st.nodes_if_full_copies, st.sharing_ratio(),
           past_us, now_us);
    if (csv) fprintf(csv, "%zu,%zu,%zu,%.2f,%.4f,%.4f\n", ix.version_count(),
                     st.total_nodes_allocated, st.nodes_if_full_copies,
                     st.sharing_ratio(), past_us, now_us);
  }
  printf("\n  Querying the past costs the same as querying the present -- there is no\n");
  printf("  replay or reconstruction, just a different root pointer.\n");
}

int main(int argc, char** argv) {
  std::string out;
  for (int i = 1; i < argc; ++i)
    if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];

  printf("\n\033[1msafetrail benchmark\033[0m\n");
  printf("═════════════════════════════════════════════════════════════════════════════\n");

  FILE* f1 = out.empty() ? nullptr : fopen((out + "/index_scaling.csv").c_str(), "w");
  bench_scaling(f1);
  if (f1) fclose(f1);

  const bool eq = bench_equivalence();

  FILE* f2 = out.empty() ? nullptr : fopen((out + "/hysteresis_ab.csv").c_str(), "w");
  bench_hysteresis(f2);
  if (f2) fclose(f2);

  const bool cc = bench_containment();

  FILE* f3 = out.empty() ? nullptr : fopen((out + "/versioned_index.csv").c_str(), "w");
  bench_versioned(f3);
  if (f3) fclose(f3);

  printf("\n═════════════════════════════════════════════════════════════════════════════\n");
  printf("  correctness gates: equivalence %s   containment %s\n",
         eq ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m",
         cc ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m");
  if (!out.empty()) printf("  csv written to %s/\n", out.c_str());
  printf("\n");
  return (eq && cc) ? 0 : 1;
}
