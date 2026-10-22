// Incident ranking is a pure function of the incident set.
//
// The simulator ranks open incidents (most people, then highest severity) to
// pick which ones enter the capped dispatch contest, and the dashboard ranks
// them the same way for its incident cards. Until 2026-10-22 both ranked with a
// bare std::sort and no tie-break. Among incidents with equal (people,
// severity) -- most of them: one person, one severity level -- std::sort leaves
// the order unspecified, and libc++ (Apple clang) and libstdc++ (g++) leave
// DIFFERENT orders. The cap cut through a tie group, so the same commit admitted
// different incidents to the dispatch contest on macOS and on Linux: the greedy
// and optimal travel totals and the incident cards disagreed between the two
// builds while the other 1.6 MB of the dashboard matched byte for byte.
//
// alert::outranks adds the id as the final key, making the order total. This
// test pins (1) the order itself and its independence from the input
// permutation, (2) that the simulator's dispatch plan is computed from exactly
// this ranking, and (3) that the dashboard's incident cards follow it.
#include "../test_harness.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "safetrail/alert/correlator.hpp"
#include "safetrail/dispatch/assigner.hpp"
#include "safetrail/sim/simulator.hpp"
#include "safetrail/viz/html_export.hpp"
using namespace safetrail;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
};
// Fisher-Yates with our own generator: std::shuffle's output is
// implementation-defined too, and this test is about exactly that.
template <class T> void shuffle(std::vector<T>& v, Rng& r) {
  for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[size_t(r.next() % i)]);
}
alert::Incident make(IncidentId id, size_t people, uint8_t sev) {
  alert::Incident inc{};
  inc.id = id;
  inc.max_severity = sev;
  for (size_t k = 0; k < people; ++k) inc.affected.push_back(TouristId(size_t(id) * 1000 + k));
  return inc;
}
std::vector<IncidentId> ids(const std::vector<const alert::Incident*>& v) {
  std::vector<IncidentId> out;
  for (const auto* i : v) out.push_back(i->id);
  return out;
}
std::string join(const std::vector<IncidentId>& v) {
  std::string s;
  for (auto id : v) s += (s.empty() ? "" : " ") + std::to_string(id);
  return s;
}
std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

int main() {
  // ── 1. A strict total order, with the id breaking every tie ────────────────
  {
    // Twelve incidents in four (people, severity) groups. Ids deliberately not in
    // rank order, so a sort that merely preserves its input order fails too.
    std::vector<alert::Incident> store;
    store.push_back(make(7, 1, 3));  store.push_back(make(2, 2, 4));  store.push_back(make(11, 1, 3));
    store.push_back(make(0, 1, 3));  store.push_back(make(5, 3, 5));  store.push_back(make(9, 2, 4));
    store.push_back(make(3, 1, 3));  store.push_back(make(8, 2, 3));  store.push_back(make(1, 1, 3));
    store.push_back(make(10, 2, 4)); store.push_back(make(4, 2, 3));  store.push_back(make(6, 1, 3));
    // (3 people, sev 5): 5 | (2, 4): 2 9 10 | (2, 3): 4 8 | (1, 3): 0 1 3 6 7 11
    const std::vector<IncidentId> expected = {5, 2, 9, 10, 4, 8, 0, 1, 3, 6, 7, 11};

    std::vector<const alert::Incident*> v;
    for (const auto& i : store) v.push_back(&i);

    bool total = true;
    for (const auto* a : v)
      for (const auto* b : v) {
        const bool ab = alert::outranks(*a, *b), ba = alert::outranks(*b, *a);
        if (a == b ? (ab || ba) : (ab == ba)) total = false;
      }
    t::ok(total, "outranks is a strict total order: exactly one direction holds for distinct incidents");

    alert::rank_by_seriousness(v);
    t::ok(ids(v) == expected,
          "ranked: most people, then highest severity, then lowest id  (got " + join(ids(v)) + ")");

    Rng rng(0x5eed);
    const int kShuffles = 50;
    int same = 0;
    for (int k = 0; k < kShuffles; ++k) {
      shuffle(v, rng);
      alert::rank_by_seriousness(v);
      if (ids(v) == expected) ++same;
    }
    t::ok(same == kShuffles,
          "every one of " + std::to_string(kShuffles) + " input permutations ranks to the same sequence (" +
              std::to_string(same) + " did)");
  }

  // ── 2. The simulator's dispatch plan comes from exactly this ranking ───────
  //
  // Run the engine, rank its open incidents here, replay the dispatch (cap,
  // snap, greedy, Hungarian) and require the totals to equal summary() exactly.
  // If the simulator ranked any other way -- the old bare std::sort included --
  // a different subset of the tie group at the cap would be dispatched and the
  // totals would differ.
  sim::SimConfig cfg;
  cfg.tourists = 40;
  cfg.groups = 5;
  cfg.seed = 20261022;
  cfg.duration_ms = 1800000;            // 30 simulated minutes
  cfg.tick_ms = 1000;
  cfg.responders = 6;
  cfg.roads_path.clear();               // synthetic grid: no file dependency
  sim::Simulator s(cfg);
  std::string err;
  if (!s.load_zones("data/zones/shillong_osm.geojson", &err)) {
    t::ok(false, "zones load: " + err);
    return t::report("alert/incident_ranking");
  }
  s.spawn_tourists();
  viz::TraceRecorder rec;
  while (!s.done()) { s.step(); rec.capture(s); }
  s.finalize();

  auto open = s.correlator().open_incidents();
  alert::rank_by_seriousness(open);
  const size_t cap = cfg.responders * 3 + 1;   // the simulator's dispatch cap
  auto key = [](const alert::Incident* i) { return std::make_pair(i->people(), int(i->max_severity)); };
  // Non-vacuity: the tie group at the cap must straddle it, or the ranking could
  // not have changed which incidents were dispatched.
  const bool straddles = open.size() > cap && key(open[cap - 1]) == key(open[cap]);
  t::ok(straddles, "more open incidents than the cap, with a tie group straddling the cap (" +
                       std::to_string(open.size()) + " open, cap " + std::to_string(cap) + ")");

  std::vector<dispatch::Incident> incidents;
  for (size_t i = 0; i < open.size() && i < cap; ++i)
    incidents.push_back({open[i]->id, open[i]->centroid, graph::kNoNode});
  dispatch::snap_incidents(incidents, s.roads());
  const auto greedy  = dispatch::assign_greedy(s.responders(), incidents, s.roads());
  const auto optimal = dispatch::assign_optimal(s.responders(), incidents, s.roads());
  const auto sum = s.summary();
  t::ok(greedy.total_m == sum.greedy_response_m,
        "greedy total replayed from the ranked incidents equals the simulator's (" +
            std::to_string(greedy.total_m) + " vs " + std::to_string(sum.greedy_response_m) + ")");
  t::ok(optimal.total_m == sum.optimal_response_m,
        "optimal total replayed from the ranked incidents equals the simulator's (" +
            std::to_string(optimal.total_m) + " vs " + std::to_string(sum.optimal_response_m) + ")");
  t::ok(optimal.dispatches.size() == sum.dispatched, "same number of dispatches");

  // ── 3. The dashboard's incident cards follow the same ranking ──────────────
  {
    std::filesystem::create_directories("build");
    const std::string path = "build/incident_ranking_test.html";
    t::ok(rec.write_html(s, path), "dashboard written");
    const std::string html = read_file(path);
    const size_t from = html.find("\"incidents_top\":[");
    const size_t to = from == std::string::npos ? from : html.find("],\"responders\"", from);
    t::ok(from != std::string::npos && to != std::string::npos, "dashboard has an incidents_top array");
    if (from != std::string::npos && to != std::string::npos) {
      const std::string cards = html.substr(from, to - from);
      // Expected cards: ranked open incidents with >= 2 people, at most 8, each
      // identified by the fields the export prints before the zone label.
      std::vector<std::string> expect;
      for (const auto* i : open) {
        if (i->people() < 2) continue;
        if (expect.size() >= 8) break;
        char b[160];
        snprintf(b, sizeof b, "{\"people\":%zu,\"sev\":%d,\"alerts\":%zu,\"lat\":%.6f,\"lon\":%.6f,",
                 i->people(), int(i->max_severity), i->alerts.size(), i->centroid.lat, i->centroid.lon);
        expect.emplace_back(b);
      }
      t::ok(expect.size() >= 2, "the run produced at least two incident cards (" +
                                    std::to_string(expect.size()) + ")");
      size_t pos = 0, in_order = 0, n_cards = 0;
      for (size_t at = cards.find("{\"people\":"); at != std::string::npos;
           at = cards.find("{\"people\":", at + 1)) ++n_cards;
      for (const auto& e : expect) {
        const size_t at = cards.find(e, pos);
        if (at == std::string::npos) break;
        ++in_order;
        pos = at + 1;
      }
      t::ok(n_cards == expect.size(), "card count matches the ranked incidents with >= 2 people (" +
                                          std::to_string(n_cards) + " vs " + std::to_string(expect.size()) + ")");
      t::ok(in_order == expect.size(), "cards appear in ranking order (" + std::to_string(in_order) +
                                           " of " + std::to_string(expect.size()) + " in sequence)");
    }
  }
  return t::report("alert/incident_ranking");
}
