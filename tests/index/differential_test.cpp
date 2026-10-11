// Randomised differential testing of every spatial index against the brute-force
// oracle, under hostile workloads, with structural audits after every operation.
//
// tests/index/equivalence_test.cpp checks well-behaved random boxes in one
// district. That is the easy case. This file generates workloads from seeds in
// seven profiles chosen for where spatial indexes actually break:
//
//   uniform      the easy case, as a control
//   clustered    many zones with IDENTICAL boxes, plus nested ones -- the
//                quadtree's degenerate case (no split separates them)
//   degenerate   zero-area points and zero-width lines, touching edges, point
//                queries exactly on box boundaries
//   mixed-scale  district-sized boxes among 10 m ones -- large boxes settle at
//                the quadtree root and inflate R-tree envelopes
//   hemispheres  negative latitudes and longitudes, boxes straddling 0/0
//   extremes     near the poles and the antimeridian, and beyond +/-180
//   far-inserts  build fitted to one area, then insert far outside it -- the
//                quadtree's root-expansion path
//
// Each profile runs a mixed sequence -- bulk build, inserts, removals of present
// AND absent ids, rebuilds -- and after every operation:
//   * every index returns exactly brute force's id set for random queries
//     (including whole-world, empty-result, and point queries on edges)
//   * no index returns an id twice
//   * size() and remove()'s return value agree with brute force
//   * Quadtree::check_invariants() and RTree::check_invariants() hold
//
// Finally, the same checker is pointed at deliberately broken indexes and must
// report them: a harness that cannot fail proves nothing.
#include "../test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;
using namespace safetrail::index;

namespace {

using Item = std::pair<ZoneId, geo::Bbox>;

enum Profile { kUniform, kClustered, kDegenerate, kMixedScale, kHemispheres, kExtremes,
               kFarInserts, kProfiles };
const char* kProfileName[] = {"uniform", "clustered", "degenerate", "mixed-scale",
                              "hemispheres", "extremes", "far-inserts"};

geo::Bbox box_around(double lat, double lon, double hlat, double hlon) {
  return {lat - hlat, lon - hlon, lat + hlat, lon + hlon};
}

// A box drawn from the profile. `phase` lets far-inserts switch regions after
// the initial build.
geo::Bbox gen_box(sim::Rng& rng, Profile p, int phase) {
  switch (p) {
    case kUniform:
      return box_around(rng.range(25.40, 25.80), rng.range(91.70, 92.10),
                        rng.range(0.0002, 0.003), rng.range(0.0002, 0.003));
    case kClustered: {
      // A few hot spots; most boxes exactly identical to their hot spot's box.
      const int h = int(rng.below(3));
      const double lat = 25.5 + 0.01 * h, lon = 91.9 + 0.01 * h;
      if (rng.uniform() < 0.7) return box_around(lat, lon, 0.001, 0.001);
      const double s = rng.range(0.0001, 0.002);             // nested around it
      return box_around(lat, lon, s, s);
    }
    case kDegenerate: {
      // Snap to a coarse lattice so edges coincide and points sit on edges.
      const double lat = 25.5 + 0.001 * double(rng.below(20));
      const double lon = 91.9 + 0.001 * double(rng.below(20));
      switch (rng.below(4)) {
        case 0: return {lat, lon, lat, lon};                   // a point
        case 1: return {lat, lon, lat, lon + 0.001 * double(1 + rng.below(3))};  // horizontal line
        case 2: return {lat, lon, lat + 0.001 * double(1 + rng.below(3)), lon};  // vertical line
        default: return {lat, lon, lat + 0.001, lon + 0.001};  // a lattice cell
      }
    }
    case kMixedScale:
      if (rng.uniform() < 0.1)
        return box_around(rng.range(25.4, 25.8), rng.range(91.7, 92.1), rng.range(0.05, 0.3),
                          rng.range(0.05, 0.3));
      return box_around(rng.range(25.4, 25.8), rng.range(91.7, 92.1), 0.0001, 0.0001);
    case kHemispheres:
      return box_around(rng.range(-0.05, 0.05), rng.range(-0.05, 0.05),
                        rng.range(0.0001, 0.01), rng.range(0.0001, 0.01));
    case kExtremes: {
      switch (rng.below(4)) {
        case 0: return box_around(rng.range(89.0, 89.99), rng.range(-179.0, 179.0), 0.005, 0.5);
        case 1: return box_around(rng.range(-89.99, -89.0), rng.range(-179.0, 179.0), 0.005, 0.5);
        case 2: return box_around(rng.range(-10.0, 10.0), rng.range(179.5, 179.99), 0.01, 0.01);
        default: return box_around(rng.range(-10.0, 10.0), rng.range(180.1, 185.0), 0.01, 0.01);
      }
    }
    case kFarInserts:
    default:
      if (phase == 0)
        return box_around(rng.range(25.55, 25.57), rng.range(91.87, 91.89), 0.0005, 0.0005);
      return box_around(rng.range(20.0, 30.0), rng.range(85.0, 100.0), rng.range(0.0005, 0.01),
                        rng.range(0.0005, 0.01));
  }
}

geo::Bbox gen_query(sim::Rng& rng, Profile p, const std::vector<Item>& live) {
  const double u = rng.uniform();
  if (u < 0.05) return {-90.0, -180.0, 90.0, 180.0};            // whole world
  if (u < 0.10) return {-60.0, 10.0, -59.0, 11.0};              // nowhere near anything
  if (u < 0.35 && !live.empty()) {                              // a point on a live box's edge/corner
    const geo::Bbox& b = live[rng.below(uint32_t(live.size()))].second;
    const double lat = rng.uniform() < 0.5 ? b.min_lat : b.max_lat;
    const double lon = rng.uniform() < 0.5 ? b.min_lon : b.max_lon;
    return {lat, lon, lat, lon};
  }
  const geo::Bbox b = gen_box(rng, p, 1);
  const double grow = rng.range(0.0, 0.01);
  return {b.min_lat - grow, b.min_lon - grow, b.max_lat + grow, b.max_lon + grow};
}

std::vector<ZoneId> sorted(std::vector<ZoneId> v) { std::sort(v.begin(), v.end()); return v; }

struct Tally {
  size_t wrong_results = 0, duplicates = 0, size_mismatch = 0, remove_mismatch = 0,
         audit_fail = 0, queries = 0, ops = 0;
};

// One index checked against the oracle for one query.
void compare(const SpatialIndex& ix, const BruteForceIndex& bf, const geo::Bbox& q, Tally& t) {
  std::vector<ZoneId> want, got;
  bf.query(q, want);
  ix.query(q, got);
  const auto sg = sorted(got);
  if (std::adjacent_find(sg.begin(), sg.end()) != sg.end()) ++t.duplicates;
  if (sg != sorted(want)) ++t.wrong_results;
  ++t.queries;
}

bool audit(const SpatialIndex& ix) {
  if (auto* q = dynamic_cast<const Quadtree*>(&ix)) return q->check_invariants();
  if (auto* r = dynamic_cast<const RTree*>(&ix)) return r->check_invariants();
  return true;
}

// Drive `ix` and the oracle through one seeded workload.
void run_workload(SpatialIndex& ix, Profile p, uint64_t seed, Tally& t) {
  sim::Rng rng(seed);
  BruteForceIndex bf;
  std::vector<Item> live;
  ZoneId next = 0;

  const size_t n0 = 30 + rng.below(250);
  for (size_t i = 0; i < n0; ++i) live.emplace_back(next++, gen_box(rng, p, 0));
  ix.build(live);
  bf.build(live);
  if (!audit(ix)) ++t.audit_fail;

  for (int op = 0; op < 220; ++op, ++t.ops) {
    const double u = rng.uniform();
    if (u < 0.35) {                                            // insert
      const Item it{next++, gen_box(rng, p, 1)};
      ix.insert(it.first, it.second);
      bf.insert(it.first, it.second);
      live.push_back(it);
    } else if (u < 0.60 && !live.empty()) {                    // remove a present id
      const size_t k = rng.below(uint32_t(live.size()));
      const ZoneId id = live[k].first;
      live.erase(live.begin() + long(k));
      if (ix.remove(id) != bf.remove(id)) ++t.remove_mismatch;
    } else if (u < 0.65) {                                     // remove an absent id
      const ZoneId id = next + 1000 + ZoneId(rng.below(1000));
      if (ix.remove(id) || bf.remove(id)) ++t.remove_mismatch;
    } else if (u < 0.67) {                                     // rebuild from the live set
      ix.build(live);
      bf.build(live);
    } else if (u < 0.69 && !live.empty()) {                    // drain to empty, then refill
      for (const auto& it : live) { ix.remove(it.first); bf.remove(it.first); }
      live.clear();
    }
    if (ix.size() != bf.size()) ++t.size_mismatch;
    if (!audit(ix)) ++t.audit_fail;
    for (int q = 0; q < 6; ++q) compare(ix, bf, gen_query(rng, p, live), t);
  }
}

std::unique_ptr<SpatialIndex> make(int k) {
  switch (k) {
    case 0: return std::make_unique<Quadtree>();
    case 1: return std::make_unique<Quadtree>(/*node_capacity=*/1, /*max_depth=*/30);  // deep tree
    case 2: return std::make_unique<RTree>();
    case 3: return std::make_unique<RTree>(/*max_entries=*/4);                          // tall tree
    default: return std::make_unique<Geohash>();
  }
}
const char* kIndexName[] = {"quadtree", "quadtree(cap=1,depth=30)", "r-tree", "r-tree(M=4)",
                            "geohash"};

// ── Deliberately broken indexes, for the harness's own test ──────────────────
// Each wraps a correct brute force and injects one classic bug.
class Broken : public SpatialIndex {
 public:
  enum Bug { StrictEdges, DropsOne, Duplicates, StaleRemove };
  explicit Broken(Bug b) : bug_(b) {}
  const char* name() const override { return "broken"; }
  void build(const std::vector<Item>& items) override { inner_.build(items); items_ = items; }
  void insert(ZoneId id, const geo::Bbox& b) override { inner_.insert(id, b); items_.emplace_back(id, b); }
  bool remove(ZoneId id) override {
    if (bug_ == StaleRemove) return true;                       // reports success, removes nothing
    return inner_.remove(id);
  }
  void query(const geo::Bbox& q, std::vector<ZoneId>& out) const override {
    if (bug_ == StrictEdges) {                                  // open intervals: touching != intersecting
      for (const auto& it : items_)
        if (inner_has(it.first) && q.min_lat < it.second.max_lat && it.second.min_lat < q.max_lat &&
            q.min_lon < it.second.max_lon && it.second.min_lon < q.max_lon)
          out.push_back(it.first);
      return;
    }
    const size_t before = out.size();
    inner_.query(q, out);
    if (bug_ == DropsOne && out.size() - before >= 3) out.pop_back();
    if (bug_ == Duplicates && out.size() - before >= 2) out.push_back(out[before]);
  }
  size_t size() const override { return inner_.size(); }
  IndexStats stats() const override { return inner_.stats(); }
  void reset_counters() override {}

 private:
  bool inner_has(ZoneId id) const {
    std::vector<ZoneId> all;
    inner_.query({-1e9, -1e9, 1e9, 1e9}, all);
    return std::find(all.begin(), all.end(), id) != all.end();
  }
  Bug bug_;
  BruteForceIndex inner_;
  std::vector<Item> items_;
};

}  // namespace

int main() {
  // ── Every index, every profile, several seeds ──────────────────────────────
  for (int k = 0; k < 5; ++k) {
    Tally total;
    for (int p = 0; p < kProfiles; ++p) {
      Tally t;
      for (uint64_t seed = 1; seed <= uint64_t(6 * t::stress()); ++seed) {
        auto ix = make(k);
        run_workload(*ix, Profile(p), 0x9E3779B97F4A7C15ull * seed + uint64_t(p), t);
      }
      const std::string ctx = std::string(kIndexName[k]) + " / " + kProfileName[p];
      t::ok(t.wrong_results == 0 && t.duplicates == 0,
            ctx + ": " + std::to_string(t.queries) + " queries == brute force, no duplicates (" +
                std::to_string(t.wrong_results) + " wrong)");
      t::ok(t.size_mismatch == 0 && t.remove_mismatch == 0,
            ctx + ": size() and remove() agree with brute force across " +
                std::to_string(t.ops) + " ops");
      t::ok(t.audit_fail == 0, ctx + ": structural invariants hold after every op");
      total.queries += t.queries;
    }
    std::printf("       %-26s %zu differential queries across %d profiles\n", kIndexName[k],
                total.queries, int(kProfiles));
  }

  // ── The harness must catch broken implementations ──────────────────────────
  const struct { Broken::Bug bug; const char* what; Profile profile; } mutants[] = {
      {Broken::StrictEdges, "open-interval intersects (misses touching boxes)", kDegenerate},
      {Broken::DropsOne, "drops the last candidate", kClustered},
      {Broken::Duplicates, "returns an id twice", kUniform},
      {Broken::StaleRemove, "remove() that does not remove", kUniform},
  };
  for (const auto& m : mutants) {
    Tally t;
    Broken ix(m.bug);
    run_workload(ix, m.profile, 42, t);
    t::ok(t.wrong_results + t.duplicates + t.size_mismatch + t.remove_mismatch > 0,
          std::string("harness detects a broken index: ") + m.what);
  }

  return t::report("index/differential");
}
