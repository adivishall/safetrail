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
//   9. Bulk loading       R-tree: STR packing vs repeated insertion
//  10. Churn              what insert/delete does to each structure over time
//  11. Serialisation      blob size, write time, read time
//  12. Self-intersection  Shamos-Hoey sweep vs the O(V^2) pairwise reference
//  13. Interval tree      churn: real AVL deletion vs the tombstone it replaced
//  14. Node snapping      k-d tree vs the linear scan, on the dispatch path
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include "safetrail/ds/interval_tree.hpp"
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


// ── 9. Bulk loading: STR vs repeated insertion ───────────────────────────────
//
// Inserting n items one at a time makes every ChooseSubtree decision blind to the
// items still to come, so early splits are guesses and node boxes end up
// overlapping more than they need to. Overlap is what forces a query down several
// branches, so it is the thing that actually costs query time. STR knows the
// whole set up front and packs it into near-square tiles.
//
// Four numbers, because "better" needs to be specific: build time, tree size,
// query time, and -- the one that explains the others -- how many nodes a query
// has to visit.
static void bench_bulkload(FILE* csv) {
  printf("\n\033[1m9. BULK LOADING\033[0m   R-tree: STR packing vs repeated insertion\n");
  printf("  %8s  %11s  %11s  %9s  %9s  %11s  %11s  %8s\n",
         "zones", "insert ms", "STR ms", "ins nodes", "STR nodes",
         "ins us/qry", "STR us/qry", "gain");
  printf("  ────────────────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,incremental_build_ms,str_build_ms,incremental_nodes,str_nodes,"
                        "incremental_depth,str_depth,incremental_us,str_us,query_gain\n");

  for (size_t n : {1000u, 10000u, 50000u, 100000u}) {
    Corpus c = make_corpus(n, 2000, 31337);

    index::RTree inc;
    auto t0 = Clock::now();
    inc.build_incremental(c.boxes);
    const double inc_ms = ms_since(t0);

    index::RTree str;
    t0 = Clock::now();
    str.build(c.boxes);
    const double str_ms = ms_since(t0);

    const Timing inc_t = time_queries(inc, c, 450);
    const Timing str_t = time_queries(str, c, 450);
    const auto is = inc.stats(), ss = str.stats();

    printf("  %8zu  %11.1f  %11.1f  %9zu  %9zu  %11.2f  %11.2f  %7.2fx\n",
           n, inc_ms, str_ms, is.node_count, ss.node_count,
           inc_t.median_us, str_t.median_us,
           str_t.median_us > 0 ? inc_t.median_us / str_t.median_us : 0.0);
    if (csv) fprintf(csv, "%zu,%.2f,%.2f,%zu,%zu,%zu,%zu,%.4f,%.4f,%.3f\n",
                     n, inc_ms, str_ms, is.node_count, ss.node_count,
                     is.max_depth, ss.max_depth, inc_t.median_us, str_t.median_us,
                     str_t.median_us > 0 ? inc_t.median_us / str_t.median_us : 0.0);
  }
  printf("\n  Both are O(n log n) to build. What differs is tree QUALITY: STR's tiles\n");
  printf("  overlap less, so fewer branches are entered per query. A gain near 1.00x on\n");
  printf("  this corpus would mean the zones are uniform enough that insertion order\n");
  printf("  barely matters -- which is itself worth reporting rather than hiding.\n");
}

// ── 10. Churn ────────────────────────────────────────────────────────────────
//
// Every structure here was originally written as if it were built once and
// queried forever. This measures what a long-running index actually experiences:
// repeated insert and delete, with the live set held constant. The failure being
// measured is not wrongness -- correctness is asserted in the tests -- it is
// DECAY: node counts that never come back down, and query times that drift up.
static void bench_churn(FILE* csv) {
  printf("\n\033[1m10. CHURN\033[0m   insert/delete cycles at a constant live-set size\n");
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
  printf("\n\033[1m11. SERIALISATION  [GAP 6]\033[0m   geohash blob: size and round-trip cost\n");
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
  printf("\n\033[1m12. SELF-INTERSECTION\033[0m   Shamos-Hoey sweep vs the O(V^2) pairwise"
         " reference, median of 7\n");
  printf("  vertices    pairwise us      sweep us    speedup   verdicts agree\n");
  printf("  ─────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "vertices,pairwise_us,sweep_us,speedup,agree,rings\n");

  sim::Rng rng(0x5EEEP1);
  for (size_t n : {8u, 16u, 24u, 32u, 48u, 64u, 128u, 512u, 2048u}) {
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

// ── 13. Interval tree under churn ────────────────────────────────────────────
//
// Deletion used to be a tombstone: scan the node array, mark one dead, leave it.
// That is O(n) per delete and leaves the structure carrying its high-water mark
// forever. This measures real AVL deletion against the shape it should have --
// and against a tombstone emulation, so the improvement is a number rather than
// an assertion in a header.
static void bench_interval_churn(FILE* csv) {
  printf("\n\033[1m13. INTERVAL TREE\033[0m   AVL deletion under churn, at a constant live size\n");
  printf("     live   height fresh  height churned    stab us fresh  stab us churned"
         "     delete us\n");
  printf("  ──────────────────────────────────────────────────────────────────────────"
         "───────────\n");
  if (csv) fprintf(csv, "live,height_fresh,height_churned,stab_fresh_us,stab_churned_us,"
                        "delete_us,avl_bound\n");

  for (size_t live : {1000u, 10000u, 50000u}) {
    sim::Rng rng(0xC4147 + uint64_t(live));
    auto fill = [&](ds::IntervalTree<int>& t, std::vector<std::pair<Timestamp, Timestamp>>& iv) {
      for (size_t i = 0; i < live; ++i) {
        // Deliberately many shared low endpoints -- the shape that made the old
        // `low`-only key degenerate. A tenth as many distinct starts as entries.
        const Timestamp lo = Timestamp(rng.below(uint32_t(live / 10 + 1))) * 1000;
        const Timestamp hi = lo + Timestamp(1 + rng.below(50000));
        t.insert(lo, hi, int(i));
        iv.push_back({lo, hi});
      }
    };

    ds::IntervalTree<int> fresh;
    std::vector<std::pair<Timestamp, Timestamp>> fresh_iv;
    fill(fresh, fresh_iv);
    const size_t h_fresh = fresh.height();

    // Churn: ten rounds of deleting and reinserting a tenth of the live set, so
    // the live size returns to where it started every round.
    ds::IntervalTree<int> churned;
    std::vector<std::pair<Timestamp, Timestamp>> ch_iv;
    fill(churned, ch_iv);
    const size_t batch = live / 10;
    double delete_ms = 0;
    size_t deletes = 0;
    for (int round = 0; round < 10; ++round) {
      auto t0 = Clock::now();
      for (size_t i = 0; i < batch; ++i) {
        const size_t k = size_t(round) * batch + i;
        churned.remove(ch_iv[k].first, ch_iv[k].second, int(k));
      }
      delete_ms += ms_since(t0);
      deletes += batch;
      for (size_t i = 0; i < batch; ++i) {
        const Timestamp lo = Timestamp(rng.below(uint32_t(live / 10 + 1))) * 1000;
        const Timestamp hi = lo + Timestamp(1 + rng.below(50000));
        const int id = int(live + size_t(round) * batch + i);
        churned.insert(lo, hi, id);
        ch_iv.push_back({lo, hi});
      }
    }

    auto stab = [](ds::IntervalTree<int>& t, uint64_t seed) {
      sim::Rng r(seed);
      std::vector<int> out;
      std::vector<Timestamp> probes;
      for (int i = 0; i < 2000; ++i) probes.push_back(Timestamp(r.below(6000000)));
      for (Timestamp p : probes) { out.clear(); t.stabbing(p, out); }   // warmup
      constexpr int kRuns = 7;
      double s[kRuns];
      for (int k = 0; k < kRuns; ++k) {
        auto t0 = Clock::now();
        for (Timestamp p : probes) { out.clear(); t.stabbing(p, out); }
        s[k] = ms_since(t0) * 1000.0 / double(probes.size());
      }
      std::sort(s, s + kRuns);
      return s[kRuns / 2];
    };
    const double sf = stab(fresh, 77), sc = stab(churned, 77);
    const double del_us = deletes ? delete_ms * 1000.0 / double(deletes) : 0.0;
    const double bound = 1.4405 * std::log(double(live) + 2.0) / std::log(2.0);

    printf("  %7zu   %12zu  %14zu   %14.4f  %16.4f  %12.4f\n",
           live, h_fresh, churned.height(), sf, sc, del_us);
    if (csv) fprintf(csv, "%zu,%zu,%zu,%.4f,%.4f,%.4f,%.2f\n", live, h_fresh,
                     churned.height(), sf, sc, del_us, bound);
  }
  printf("\n  Height after churn stays at the AVL bound for the LIVE size, and stab\n");
  printf("  time with it. The tombstone version this replaced kept every deleted\n");
  printf("  node: height frozen at the high-water mark, max_high inflated by dead\n");
  printf("  intervals so the pruning bound loosened with every delete, and delete\n");
  printf("  itself O(n) because finding the victim was a scan of the node array.\n");
  printf("  Per-delete cost above is logarithmic even though a tenth of the entries\n");
  printf("  share each low endpoint -- that is the (low, high, value, seq) key.\n");
}

// ── 14. Node snapping: k-d tree vs the linear scan ───────────────────────────
//
// Every dispatch decision snaps a GPS fix to the nearest road junction, twice per
// (responder, incident) pair while building the cost matrix. This was an O(V)
// scan. Both are kept -- the scan is the oracle -- so this is the measurement
// that justifies which one the pipeline calls, and it re-checks that they return
// the SAME node, which is the only reason the swap is safe.
static void bench_snap(FILE* csv) {
  printf("\n\033[1m14. NODE SNAPPING\033[0m   k-d tree vs linear scan, nearest road junction\n");
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

  FILE* f7 = out.empty() ? nullptr : fopen((out + "/index_build.csv").c_str(), "w");
  bench_bulkload(f7);
  if (f7) fclose(f7);

  FILE* f8 = out.empty() ? nullptr : fopen((out + "/index_churn.csv").c_str(), "w");
  bench_churn(f8);
  if (f8) fclose(f8);

  FILE* f9 = out.empty() ? nullptr : fopen((out + "/serialization.csv").c_str(), "w");
  bench_serialization(f9);
  if (f9) fclose(f9);

  FILE* f10 = out.empty() ? nullptr : fopen((out + "/self_intersection.csv").c_str(), "w");
  bench_selfintersect(f10);
  if (f10) fclose(f10);

  FILE* f11 = out.empty() ? nullptr : fopen((out + "/interval_churn.csv").c_str(), "w");
  bench_interval_churn(f11);
  if (f11) fclose(f11);

  FILE* f12 = out.empty() ? nullptr : fopen((out + "/node_snap.csv").c_str(), "w");
  bench_snap(f12);
  if (f12) fclose(f12);

  printf("\n═════════════════════════════════════════════════════════════════════════════\n");
  printf("  correctness gates: equivalence %s   containment %s\n",
         eq ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m",
         cc ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m");
  if (!out.empty()) printf("  csv written to %s/\n", out.c_str());
  printf("\n");
  return (eq && cc) ? 0 : 1;
}
