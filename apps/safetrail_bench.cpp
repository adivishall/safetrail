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

static void bench_scaling(FILE* csv) {
  printf("\n\033[1m1. INDEX SCALING\033[0m   query = 450 m box, 2000 probes each\n");
  printf("  %8s  %14s  %14s  %10s  %9s  %9s\n",
         "zones", "brute (us/q)", "quad (us/q)", "speedup", "cands BF", "cands QT");
  printf("  ────────────────────────────────────────────────────────────────────────────\n");
  if (csv) fprintf(csv, "zones,brute_us,quad_us,speedup,cand_brute,cand_quad,nodes,depth\n");

  for (size_t n : {10u, 100u, 1000u, 5000u, 20000u, 50000u, 100000u}) {
    Corpus c = make_corpus(n, 2000, 99);
    index::BruteForceIndex bf; bf.build(c.boxes);
    index::Quadtree qt;        qt.build(c.boxes);

    std::vector<index::ZoneId> out; out.reserve(4096);

    auto t0 = Clock::now();
    for (const auto& p : c.probes) { out.clear(); bf.query(geo::Bbox::around(p, 450), out); }
    const double bf_ms = ms_since(t0);

    t0 = Clock::now();
    for (const auto& p : c.probes) { out.clear(); qt.query(geo::Bbox::around(p, 450), out); }
    const double qt_ms = ms_since(t0);

    const auto bs = bf.stats(), qs = qt.stats();
    const double bf_us = bf_ms * 1000.0 / double(c.probes.size());
    const double qt_us = qt_ms * 1000.0 / double(c.probes.size());
    printf("  %8zu  %14.2f  %14.2f  %9.1fx  %9.2f  %9.2f\n", n, bf_us, qt_us,
           qt_us > 0 ? bf_us / qt_us : 0.0, bs.avg_candidates(), qs.avg_candidates());
    if (csv) fprintf(csv, "%zu,%.4f,%.4f,%.2f,%.2f,%.2f,%zu,%zu\n", n, bf_us, qt_us,
                     qt_us > 0 ? bf_us / qt_us : 0.0, bs.avg_candidates(),
                     qs.avg_candidates(), qs.node_count, qs.max_depth);
  }
}

static bool bench_equivalence() {
  printf("\n\033[1m2. EQUIVALENCE\033[0m   quadtree must return exactly what brute force returns\n");
  bool all_ok = true;
  for (size_t n : {50u, 500u, 5000u}) {
    Corpus c = make_corpus(n, 3000, 7);
    index::BruteForceIndex bf; bf.build(c.boxes);
    index::Quadtree qt;        qt.build(c.boxes);
    size_t mismatches = 0, total = 0;
    std::vector<index::ZoneId> a, b;
    for (const auto& p : c.probes)
      for (double r : {80.0, 400.0, 2000.0}) {
        a.clear(); b.clear();
        const geo::Bbox q = geo::Bbox::around(p, r);
        bf.query(q, a); qt.query(q, b);
        std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
        total += a.size();
        if (a != b) ++mismatches;
      }
    printf("  %6zu zones   %6zu queries   %8zu hits   mismatches: %s%zu\033[0m\n",
           n, c.probes.size() * 3, total, mismatches ? "\033[31m" : "\033[32m", mismatches);
    if (mismatches) all_ok = false;
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

  printf("\n═════════════════════════════════════════════════════════════════════════════\n");
  printf("  correctness gates: equivalence %s   containment %s\n",
         eq ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m",
         cc ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m");
  if (!out.empty()) printf("  csv written to %s/\n", out.c_str());
  printf("\n");
  return (eq && cc) ? 0 : 1;
}
