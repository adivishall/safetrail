// safetrail_headless -- run a scenario, print what the engine sees.
//
// Loads real OpenStreetMap zones, simulates a population of tourists with a GPS
// error model, runs the geofencing engine tick by tick, and prints the event
// stream and the engine's counters. `--export-html` also writes the
// self-contained dashboard. `make demo` and `make dashboard` are the two usual
// invocations.
//
//   --index brute|quadtree|rtree|geohash   spatial index (default quadtree);
//                                          the event stream is identical for all
//                                          four (tests/fence/index_independence_test)
//   --tourists N  --hours H  --seed S  --synthetic N  --zones FILE  --show N
//   --no-hysteresis                        the naive baseline, for the A/B
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "safetrail/sim/simulator.hpp"
#include "safetrail/viz/html_export.hpp"
#include "safetrail/evidence/merkle_log.hpp"

using namespace safetrail;

static const char* kind_str(fence::EventKind k) {
  switch (k) {
    case fence::EventKind::ZoneEnter: return "ENTER      ";
    case fence::EventKind::ZoneExit: return "EXIT       ";
    case fence::EventKind::ZoneUncertain: return "UNCERTAIN  ";
    case fence::EventKind::ZoneApproaching: return "APPROACHING";
    case fence::EventKind::DwellExceeded: return "DWELL      ";
  }
  return "?";
}
static std::string hhmmss(int64_t ms) {
  // Sized for the widest int64 hour count, so snprintf can never truncate
  // (g++ -Wformat-truncation proved the old 16 bytes could).
  char b[48];
  snprintf(b, sizeof b, "%02lld:%02lld:%02lld", (long long)(ms / 3600000),
           (long long)(ms / 60000 % 60), (long long)(ms / 1000 % 60));
  return b;
}

int main(int argc, char** argv) {
  sim::SimConfig cfg;
  cfg.tourists = 40;
  cfg.groups = 6;
  cfg.roads_path = "data/osm/roads.txt";   // real OSM roads if present, else synthetic grid
  cfg.duration_ms = 3600000;
  cfg.tick_ms = 1000;
  std::string zones = "data/zones/shillong_osm.geojson";
  std::string html;
  size_t synthetic = 0, show = 18;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--tourists") && i + 1 < argc) cfg.tourists = size_t(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--synthetic") && i + 1 < argc) synthetic = size_t(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--hours") && i + 1 < argc) cfg.duration_ms = atoll(argv[++i]) * 3600000;
    else if (!strcmp(argv[i], "--no-hysteresis")) cfg.eval.hysteresis.enabled = false;
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) cfg.seed = uint64_t(atoll(argv[++i]));
    else if (!strcmp(argv[i], "--index") && i + 1 < argc) {
      const std::string k = argv[++i];
      if (k == "brute") cfg.index = index::IndexKind::BruteForce;
      else if (k == "quadtree") cfg.index = index::IndexKind::Quadtree;
      else if (k == "rtree") cfg.index = index::IndexKind::RTree;
      else if (k == "geohash") cfg.index = index::IndexKind::Geohash;
      else { printf("unknown index '%s' (brute|quadtree|rtree|geohash)\n", k.c_str()); return 2; }
    }
    else if (!strcmp(argv[i], "--brute")) cfg.index = index::IndexKind::BruteForce;
    else if (!strcmp(argv[i], "--zones") && i + 1 < argc) zones = argv[++i];
    else if (!strcmp(argv[i], "--show") && i + 1 < argc) show = size_t(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--rtree")) cfg.index = index::IndexKind::RTree;
    else if (!strcmp(argv[i], "--export-html") && i + 1 < argc) html = argv[++i];
  }

  sim::Simulator s(cfg);
  std::string err;
  if (!s.load_zones(zones, &err)) { printf("zone load failed: %s\n", err.c_str()); return 1; }
  if (synthetic) s.add_synthetic_zones(synthetic);
  s.spawn_tourists();

  printf("\n\033[1msafetrail\033[0m  geofencing engine\n");
  printf("─────────────────────────────────────────────────────────────────────\n");
  printf("  zones loaded      %zu  (%zu authored", s.zones().size(), s.zones().size() - synthetic);
  if (synthetic) printf(" + %zu synthetic", synthetic);
  printf(")\n");
  printf("  tourists          %zu in %zu groups\n", cfg.tourists, cfg.groups);
  printf("  spatial index     %s\n", s.index().name());
  printf("  hysteresis        %s\n", cfg.eval.hysteresis.enabled ? "on" : "OFF (naive baseline)");
  printf("  GPS error model   %.0f m open sky / %.0f m multipath (%.0f%% of fixes)\n",
         cfg.gps.open_sky_m, cfg.gps.multipath_m, cfg.gps.multipath_probability * 100);
  printf("  simulated span    %lld h at %lld ms/tick\n\n",
         (long long)(cfg.duration_ms / 3600000), (long long)cfg.tick_ms);

  printf("\033[1mzones\033[0m\n");
  for (index::ZoneId id : s.zones().all_ids()) {
    const auto* z = s.zones().get(id);
    if (z->name.rfind("synthetic", 0) == 0) continue;
    const char* k = z->kind == fence::ZoneKind::Restricted ? "\033[31mrestricted\033[0m"
                  : z->kind == fence::ZoneKind::Caution ? "\033[33mcaution   \033[0m"
                  : z->kind == fence::ZoneKind::Safe ? "\033[32msafe      \033[0m" : "advisory  ";
    printf("  %2u  %s  sev %u  %2zu verts  %6.0f m perimeter  %s\n", id, k, z->severity,
           z->shape.vertex_count(), z->shape.perimeter_m(), z->name.c_str());
  }

  viz::TraceRecorder rec;
  if (!html.empty()) {
    while (!s.done()) { s.step(); rec.capture(s); }
    s.finalize();
  } else {
    s.run();
  }

  printf("\n\033[1mevent stream\033[0m  (first %zu of %zu)\n", show, s.events().size());
  printf("  %-8s  %-11s  %-8s  %-28s %s\n", "time", "event", "tourist", "zone", "detail");
  size_t n = 0;
  for (const auto& e : s.events()) {
    if (n++ >= show) break;
    const auto* z = s.zones().get(e.zone);
    char detail[96];
    if (e.kind == fence::EventKind::ZoneApproaching)
      snprintf(detail, sizeof detail, "ETA %.0fs, %.0fm out", e.eta_s, e.depth_m);
    else if (e.kind == fence::EventKind::ZoneUncertain)
      snprintf(detail, sizeof detail, "±%.0fm accuracy, %.0fm from edge", e.accuracy_m, e.depth_m);
    else
      snprintf(detail, sizeof detail, "%.0fm deep, ±%.0fm", -e.depth_m, e.accuracy_m);
    printf("  %-8s  %s  TID-%05u  %-28s %s\n", hhmmss(e.t_ms).c_str(), kind_str(e.kind),
           e.tourist, z ? z->name.substr(0, 28).c_str() : "?", detail);
  }

  const auto sum = s.summary();
  const auto c = s.counters();
  const auto ist = s.index().stats();
  const auto cst = s.correlator().stats();

  printf("\n\033[1mevents by kind\033[0m\n");
  printf("  zone entries         %6llu\n", (unsigned long long)sum.enters);
  printf("  zone exits           %6llu\n", (unsigned long long)sum.exits);
  printf("  uncertain            %6llu   position too noisy to call\n", (unsigned long long)sum.uncertain);
  printf("  approaching          %6llu   predicted before crossing\n", (unsigned long long)sum.approaching);
  printf("  dwell exceeded       %6llu\n", (unsigned long long)sum.dwell);
  printf("  cohesion             %6llu   group splits / stragglers\n", (unsigned long long)sum.cohesion_events);
  printf("  anomalies            %6llu   stationary / signal-lost / off-route\n", (unsigned long long)sum.anomalies);

  printf("\n\033[1mengine counters\033[0m\n");
  printf("  ticks                     %10llu\n", (unsigned long long)c.ticks);
  printf("  evaluations               %10llu\n", (unsigned long long)c.evaluations);
  printf("  index queries             %10zu\n", ist.queries);
  printf("  candidates returned       %10zu   (%.2f avg per query)\n",
         ist.candidates_returned, ist.avg_candidates());
  printf("  exact geometry tests      %10llu\n", (unsigned long long)c.exact_tests_run);
  printf("  fixes rejected as noise   %10llu   accuracy worse than %.0fm\n",
         (unsigned long long)c.fixes_rejected_noise, geo::UncertainPoint::UNUSABLE_ACCURACY_M);
  printf("  \033[1mflaps suppressed          %10llu\033[0m   drift-induced false transitions\n",
         (unsigned long long)c.flaps_suppressed);
  printf("  out-of-window observations%10llu   open state, zone no longer a candidate\n",
         (unsigned long long)c.out_of_window_observations);
  printf("  states closed             %10llu   zone went out of force\n",
         (unsigned long long)c.states_closed);

  printf("\n\033[1mindex\033[0m %s\n", s.index().name());
  printf("  nodes  %zu   max depth  %zu   memory  %.1f KB\n",
         ist.node_count, ist.max_depth, double(ist.bytes) / 1024.0);
  const size_t naive = s.zones().size();
  printf("  pruning: %zu zones -> %.2f candidates per query  (%.0fx reduction)\n",
         naive, ist.avg_candidates(),
         ist.avg_candidates() > 0 ? double(naive) / ist.avg_candidates() : 0.0);

  printf("\n\033[1malert correlation\033[0m\n");
  printf("  alerts raised        %6llu\n", (unsigned long long)cst.alerts_ingested);
  printf("  incidents opened     %6llu\n", (unsigned long long)cst.incidents_opened);
  printf("  alerts absorbed      %6llu   operator cards NOT shown\n",
         (unsigned long long)cst.alerts_absorbed);
  printf("  compression ratio    %6.2f alerts per incident\n", cst.compression_ratio());

  // ── Phase 8: responder dispatch over the road network, greedy vs optimal ───
  printf("\n\033[1mdispatch\033[0m  greedy vs optimal responder assignment\n");
  printf("  incidents covered    %6llu   (of %llu open)\n",
         (unsigned long long)sum.dispatched,
         (unsigned long long)(sum.dispatched + sum.unassigned));
  printf("  greedy total         %8.0f m\n", sum.greedy_response_m);
  printf("  optimal total        %8.0f m", sum.optimal_response_m);
  if (sum.greedy_response_m > sum.optimal_response_m + 1.0)
    printf("   \033[32m%.0f m saved by Hungarian\033[0m",
           sum.greedy_response_m - sum.optimal_response_m);
  printf("\n");

  // ── GAP 9: tamper-evident evidence log of this run's events ────────────────
  // Every event becomes an append-only log entry. The Merkle root commits to the
  // whole stream; an inclusion proof lets a responder verify any single event
  // offline against just the root. Rewriting any past event changes the root.
  {
    evidence::MerkleLog evlog;
    for (const auto& e : s.events()) {
      char rec_line[128];
      snprintf(rec_line, sizeof rec_line, "%lld|%d|T%u|Z%u",
               (long long)e.t_ms, int(e.kind), e.tourist, e.zone);
      evlog.append(std::string(rec_line));
    }
    printf("\n\033[1mevidence log\033[0m  tamper-evident, offline-verifiable\n");
    printf("  events committed     %6llu\n", (unsigned long long)evlog.size());
    printf("  merkle root          %s\n", evidence::to_hex(evlog.root()).substr(0, 32).c_str());
    if (evlog.size() > 0) {
      const uint64_t idx = evlog.size() / 2;
      auto proof = evlog.prove(idx);
      std::vector<uint8_t> entry; evlog.get(idx, entry);
      auto leaf = evidence::leaf_hash(entry.data(), entry.size());
      const bool ok = proof.verify(leaf, evlog.root());
      printf("  inclusion proof #%llu   %zu hashes, verifies: %s\n",
             (unsigned long long)idx, proof.path.size(), ok ? "yes" : "NO");
    }
  }

  if (!html.empty()) {
    if (rec.write_html(s, html))
      printf("\n\033[1mdashboard\033[0m  %s  (%zu frames, self-contained -- just open it)\n",
             html.c_str(), rec.frames());
    else
      printf("\n  failed to write %s\n", html.c_str());
  }
  printf("\n");
  return 0;
}
