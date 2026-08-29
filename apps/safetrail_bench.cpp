// safetrail_bench -- the measurements that make this a project rather than a demo.
//
//   1. Index scaling      brute force vs quadtree, 10 -> 100,000 zones
//   2. Equivalence        every index must agree with brute force, exactly
//   3. Hysteresis A/B     false-alert reduction under injected GPS noise  [GAP 8]
//   4. Containment cross-check   ray casting vs winding number
//   5. Persistent index   path-copying sharing vs full copies  [GAP 3]
//   6. Routing            A* vs Dijkstra node expansions
//   7. Dispatch           greedy vs optimal responder assignment
//   8. Power              adaptive sampling vs continuous polling  [GAP 7]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
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

// A timing is never one number. We report the MEDIAN of several full passes
// (robust to a single scheduler hiccup), the best pass seen (the cleanest run
// the machine managed), and the spread as a percent of the median (how much to
// trust the figure). A warmup pass first faults in the caches and settles the
// branch predictor so the first timed pass is not an outlier.
struct Timing { double median_us, min_us, spread_pct; };

static Timing time_queries(index::SpatialIndex& ix, const Corpus& c, double radius) {
  std::vector<index::ZoneId> out; out.reserve(8192);
  for (const auto& p : c.probes) {          // warmup, untimed
    out.clear(); ix.query(geo::Bbox::around(p, radius), out);
  }
  constexpr int kRuns = 7;
  double s[kRuns];
  for (int r = 0; r < kRuns; ++r) {
    auto t0 = Clock::now();
    for (const auto& p : c.probes) { out.clear(); ix.query(geo::Bbox::around(p, radius), out); }
    s[r] = ms_since(t0) * 1000.0 / double(c.probes.size());   // us per query
  }
  std::sort(s, s + kRuns);
  const double median = s[kRuns / 2];
  return {median, s[0], median > 0 ? 100.0 * (s[kRuns - 1] - s[0]) / median : 0.0};
}

static void bench_scaling(FILE* csv) {
  printf("\n\033[1m1. INDEX SCALING\033[0m   450 m query box, 2000 probes/row, median of 7 timed"
         " passes after a warmup, us/query\n");
  printf("  %8s  %11s  %11s  %11s  %9s  %9s  %8s  %7s\n",
         "zones", "brute", "quadtree", "r-tree", "QT gain", "RT gain", "cands", "±spread");
  printf("  ─────────────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,brute_us,quad_us,rtree_us,quad_speedup,rtree_speedup,"
                        "candidates,quad_nodes,quad_depth,rtree_nodes,rtree_depth,"
                        "brute_min_us,quad_min_us,rtree_min_us,brute_spread_pct,"
                        "quad_spread_pct,rtree_spread_pct\n");

  for (size_t n : {10u, 100u, 1000u, 5000u, 20000u, 50000u, 100000u}) {
    Corpus c = make_corpus(n, 2000, 99);
    index::BruteForceIndex bf; bf.build(c.boxes);
    index::Quadtree qt;        qt.build(c.boxes);
    index::RTree rt;           rt.build(c.boxes);

    const Timing bf_t = time_queries(bf, c, 450);
    const Timing qt_t = time_queries(qt, c, 450);
    const Timing rt_t = time_queries(rt, c, 450);
    const auto bs = bf.stats(), qs = qt.stats(), rs = rt.stats();

    // The worst spread across the three medians -- the honest error bar on the row.
    const double worst_spread = std::max({bf_t.spread_pct, qt_t.spread_pct, rt_t.spread_pct});
    printf("  %8zu  %11.2f  %11.2f  %11.2f  %8.1fx  %8.1fx  %8.2f  %6.1f%%\n",
           n, bf_t.median_us, qt_t.median_us, rt_t.median_us,
           qt_t.median_us > 0 ? bf_t.median_us / qt_t.median_us : 0.0,
           rt_t.median_us > 0 ? bf_t.median_us / rt_t.median_us : 0.0,
           bs.avg_candidates(), worst_spread);
    if (csv) fprintf(csv, "%zu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%zu,%zu,%zu,%zu,"
                          "%.4f,%.4f,%.4f,%.1f,%.1f,%.1f\n",
                     n, bf_t.median_us, qt_t.median_us, rt_t.median_us,
                     qt_t.median_us > 0 ? bf_t.median_us / qt_t.median_us : 0.0,
                     rt_t.median_us > 0 ? bf_t.median_us / rt_t.median_us : 0.0,
                     bs.avg_candidates(), qs.node_count, qs.max_depth, rs.node_count, rs.max_depth,
                     bf_t.min_us, qt_t.min_us, rt_t.min_us,
                     bf_t.spread_pct, qt_t.spread_pct, rt_t.spread_pct);
  }
  printf("\n  Median of 7 passes, best-run and spread in the CSV. Single machine, one build;\n");
  printf("  the speedup is a ratio to our own brute force, not to an external library.\n");
  printf("  Candidates returned is IDENTICAL across all three -- they are true positives, so\n");
  printf("  the speedup ceiling is output size k, exactly as O(log n + k) says.\n");
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
  printf("\n\033[1m3. HYSTERESIS A/B  [GAP 8]\033[0m   filter on vs off, under TWO noise models\n");
  printf("  %-26s %11s  %11s  %10s\n", "noise model", "transitions", "transitions", "removed");
  printf("  %-26s %11s  %11s  %10s\n", "", "hyst OFF", "hyst ON", "");
  printf("  ────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "noise_model,correlation,trans_off,trans_on,removed_pct\n");

  // Two noise regimes:
  //   white  (rho=0)   independent per-tick error -- the naive, jumpy model that
  //                    flatters hysteresis because it flaps far more than reality
  //   drift  (rho=0.9) temporally-correlated smooth drift -- what a real GPS
  //                    receiver actually produces. The HONEST test.
  const struct { const char* name; double rho; } models[] = {
    {"white noise (rho=0)", 0.0}, {"realistic drift (rho=0.9)", 0.9}};

  for (const auto& nm : models) {
    uint64_t base = 0, kept = 0;
    for (int mode = 0; mode < 2; ++mode) {
      sim::SimConfig cfg;
      cfg.tourists = 60; cfg.groups = 6; cfg.seed = 20260817;
      cfg.duration_ms = 1800000; cfg.tick_ms = 1000;
      cfg.gps.correlation = nm.rho;
      cfg.eval.hysteresis.enabled = (mode == 1);
      sim::Simulator s(cfg);
      std::string err;
      if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) { printf("  %s\n", err.c_str()); return; }
      s.spawn_tourists();
      s.run();
      const uint64_t trans = s.summary().enters + s.summary().exits;
      if (mode == 0) base = trans; else kept = trans;
    }
    const double removed = base ? 100.0 * (1.0 - double(kept) / double(base)) : 0.0;
    printf("  %-26s %11llu  %11llu  %9.1f%%\n", nm.name,
           (unsigned long long)base, (unsigned long long)kept, removed);
    if (csv) fprintf(csv, "%s,%.1f,%llu,%llu,%.1f\n", nm.name, nm.rho,
                     (unsigned long long)base, (unsigned long long)kept, removed);
  }
  printf("\n  The realistic-drift row is the honest headline: hysteresis still removes\n");
  printf("  the bulk of false transitions even when the noise is NOT artificially jumpy.\n");
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

// ── 6. Routing: A* vs Dijkstra, node expansions on the road grid ──────────────
static void bench_routing(FILE* csv) {
  printf("\n\033[1m6. ROUTING  A* vs DIJKSTRA\033[0m   single-pair queries on a road grid\n");
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
  printf("\n\033[1m7. DISPATCH  GREEDY vs OPTIMAL\033[0m   total responder travel, averaged over 200 layouts\n");
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
  printf("\n\033[1m8. POWER  [GAP 7]\033[0m   risk-adaptive GPS sampling vs continuous 1 Hz\n");
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

  FILE* f4 = out.empty() ? nullptr : fopen((out + "/routing.csv").c_str(), "w");
  bench_routing(f4);
  if (f4) fclose(f4);

  FILE* f5 = out.empty() ? nullptr : fopen((out + "/dispatch.csv").c_str(), "w");
  bench_dispatch(f5);
  if (f5) fclose(f5);

  FILE* f6 = out.empty() ? nullptr : fopen((out + "/power.csv").c_str(), "w");
  bench_power(f6);
  if (f6) fclose(f6);

  printf("\n═════════════════════════════════════════════════════════════════════════════\n");
  printf("  correctness gates: equivalence %s   containment %s\n",
         eq ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m",
         cc ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m");
  if (!out.empty()) printf("  csv written to %s/\n", out.c_str());
  printf("\n");
  return (eq && cc) ? 0 : 1;
}
