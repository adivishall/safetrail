// Spatial indexes under insert/delete churn.
//
// Every index in this project was written and tested as if it were built once and
// queried forever. Under churn three separate things went wrong, none of which a
// build-then-query test can see:
//
//   quadtree  deletion never collapsed a subdivision, so a tree that had held
//             1000 items kept its shape after 900 were removed
//   r-tree    deletion never condensed, so underfull nodes accumulated and
//             fan-out decayed
//   geohash   the query padding only ever grew, so removing the one large item
//             left every later query scanning a key range sized for it
//
// Each is measured here, not asserted: the test prints the node counts and the
// pruning numbers so the effect is visible, and gates on the properties that
// must hold. Correctness against the brute-force oracle is checked throughout,
// because a compaction bug that loses items is far worse than the leak it fixes.
#include "../test_harness.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;
using namespace safetrail::index;

static geo::Bbox box_at(double lat, double lon, double r) {
  return {lat - r, lon - r, lat + r, lon + r};
}

// Every index must return exactly what brute force returns, for every probe.
static size_t disagreements(const SpatialIndex& ix, const BruteForceIndex& bf,
                            const std::vector<geo::Bbox>& probes) {
  size_t bad = 0;
  for (const auto& q : probes) {
    std::vector<ZoneId> a, b;
    bf.query(q, a);
    ix.query(q, b);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) ++bad;
  }
  return bad;
}

int main() {
  sim::Rng rng(20260904);

  std::vector<geo::Bbox> probes;
  for (int i = 0; i < 60; ++i)
    probes.push_back(geo::Bbox::around(
        {rng.range(25.49, 25.63), rng.range(91.79, 91.97)}, rng.range(150, 2500)));

  // ── quadtree: collapse on delete ───────────────────────────────────────────
  //
  // Built with build(), not repeated insert(), so the root is FITTED to the data.
  // That matters for what this measures: a world-rooted quadtree over a district
  // spends its whole depth budget descending to the data and ends up with one
  // enormous leaf, so there is no subdivision to collapse and the test would pass
  // vacuously. The fitted root is also what the engine actually uses.
  {
    Quadtree qt;
    BruteForceIndex bf;
    std::vector<std::pair<ZoneId, geo::Bbox>> items;
    for (ZoneId i = 0; i < 1000; ++i)
      items.emplace_back(i, box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                                   rng.range(0.0002, 0.002)));
    qt.build(items);
    bf.build(items);

    const size_t nodes_at_1000 = qt.stats().node_count;
    t::ok(nodes_at_1000 > 100, "a fitted root really does subdivide (" +
                                   std::to_string(nodes_at_1000) + " nodes)");
    t::ok(disagreements(qt, bf, probes) == 0, "quadtree matches brute force at 1000 items");

    for (ZoneId i = 0; i < 900; ++i) { qt.remove(i); bf.remove(i); }
    t::ok(qt.size() == 100, "quadtree size is 100 after removing 900");
    t::ok(!qt.remove(0), "removing an id that is already gone reports false");
    t::ok(qt.size() == 100, "and does not change the count");

    const size_t nodes_at_100 = qt.stats().node_count;
    std::printf("       quadtree nodes: %zu at 1000 items -> %zu after deleting 900\n",
                nodes_at_1000, nodes_at_100);
    t::ok(nodes_at_100 < nodes_at_1000 / 2,
          "the subdivision collapses: node count more than halves (" +
              std::to_string(nodes_at_1000) + " -> " + std::to_string(nodes_at_100) + ")");
    t::ok(disagreements(qt, bf, probes) == 0, "still matches brute force after deletion");

    // Full churn cycles: the tree must not ratchet upward.
    size_t peak = nodes_at_100;
    ZoneId next = 1000;
    for (int cycle = 0; cycle < 4; ++cycle) {
      std::vector<ZoneId> added;
      for (int i = 0; i < 900; ++i) {
        const geo::Bbox b = box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                                   rng.range(0.0002, 0.002));
        qt.insert(next, b);
        bf.insert(next, b);
        added.push_back(next);
        ++next;
      }
      peak = std::max(peak, qt.stats().node_count);
      for (ZoneId id : added) { qt.remove(id); bf.remove(id); }
    }
    const size_t nodes_after_cycles = qt.stats().node_count;
    std::printf("       quadtree nodes after 4 x (insert 900, delete 900): %zu (peak %zu)\n",
                nodes_after_cycles, peak);
    t::ok(nodes_after_cycles <= nodes_at_100 * 2,
          "repeated churn does not ratchet the node count upward");
    t::ok(qt.size() == 100, "and the live count returns to 100");
    t::ok(disagreements(qt, bf, probes) == 0, "and it still matches brute force");
  }

  // ── quadtree: root expansion keeps the subdivision coherent ────────────────
  {
    // Build fitted to a small area, then insert far outside it in each direction.
    std::vector<std::pair<ZoneId, geo::Bbox>> seed;
    for (ZoneId i = 0; i < 200; ++i)
      seed.emplace_back(i, box_at(rng.range(25.55, 25.57), rng.range(91.87, 91.89), 0.0005));

    struct Dir { const char* name; double lat, lon; };
    const Dir dirs[] = {
        {"north", 26.40, 91.88}, {"south", 24.70, 91.88},
        {"east", 25.56, 92.70},  {"west", 25.56, 91.05},
    };

    for (const auto& d : dirs) {
      Quadtree qt;
      BruteForceIndex bf;
      qt.build(seed);
      bf.build(seed);
      const geo::Bbox before = qt.root_region();

      const geo::Bbox far = box_at(d.lat, d.lon, 0.001);
      qt.insert(9999, far);
      bf.insert(9999, far);

      const geo::Bbox after = qt.root_region();
      t::ok(after.min_lat <= far.min_lat && after.max_lat >= far.max_lat &&
                after.min_lon <= far.min_lon && after.max_lon >= far.max_lon,
            std::string("root covers the far insert to the ") + d.name);

      // Doubling, not tight-fitting: each expansion multiplies an extent by two,
      // so the new root is at least twice as tall or twice as wide as the old one.
      const double old_h = before.max_lat - before.min_lat;
      const double old_w = before.max_lon - before.min_lon;
      const double new_h = after.max_lat - after.min_lat;
      const double new_w = after.max_lon - after.min_lon;
      t::ok(new_h >= old_h * 1.99 || new_w >= old_w * 1.99,
            std::string("expansion to the ") + d.name + " doubled an extent");

      // The old root must still be a whole quadrant of the new one: one of its
      // corners is a corner of the new root.
      const bool corner_kept =
          (std::fabs(before.min_lat - after.min_lat) < 1e-12 ||
           std::fabs(before.max_lat - after.max_lat) < 1e-12) &&
          (std::fabs(before.min_lon - after.min_lon) < 1e-12 ||
           std::fabs(before.max_lon - after.max_lon) < 1e-12);
      t::ok(corner_kept,
            std::string("the old root stayed a quadrant of the new root (") + d.name + ")");

      std::vector<geo::Bbox> local_probes = probes;
      local_probes.push_back(geo::Bbox::around({d.lat, d.lon}, 500));
      t::ok(disagreements(qt, bf, local_probes) == 0,
            std::string("queries still exact after expanding ") + d.name);
    }

    // Many successive outward inserts, each further than the last.
    {
      Quadtree qt;
      BruteForceIndex bf;
      qt.build(seed);
      bf.build(seed);
      std::vector<geo::Bbox> local_probes = probes;
      for (int i = 1; i <= 12; ++i) {
        const geo::Bbox b = box_at(25.56 + 0.05 * i, 91.88 + 0.05 * i, 0.001);
        qt.insert(ZoneId(5000 + i), b);
        bf.insert(ZoneId(5000 + i), b);
        local_probes.push_back(geo::Bbox::around(b.center(), 400));
      }
      t::ok(disagreements(qt, bf, local_probes) == 0,
            "12 successive expanding inserts stay exact");
      t::ok(qt.stats().max_depth >= 1, "the tree still has real structure after expansion");
    }
  }

  // ── r-tree: condense on delete ─────────────────────────────────────────────
  {
    RTree rt;
    BruteForceIndex bf;
    std::vector<std::pair<ZoneId, geo::Bbox>> items;
    for (ZoneId i = 0; i < 1000; ++i)
      items.emplace_back(i, box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                                   rng.range(0.0002, 0.002)));
    rt.build(items);
    bf.build(items);
    const size_t nodes_at_1000 = rt.stats().node_count;
    t::ok(disagreements(rt, bf, probes) == 0, "r-tree matches brute force at 1000 items");

    for (ZoneId i = 0; i < 900; ++i) { rt.remove(i); bf.remove(i); }
    t::ok(rt.size() == 100, "r-tree size is 100 after removing 900");
    const size_t nodes_at_100 = rt.stats().node_count;
    std::printf("       r-tree nodes:   %zu at 1000 items -> %zu after deleting 900\n",
                nodes_at_1000, nodes_at_100);
    t::ok(nodes_at_100 < nodes_at_1000 / 2,
          "condensing shrinks the tree (" + std::to_string(nodes_at_1000) + " -> " +
              std::to_string(nodes_at_100) + ")");
    t::ok(disagreements(rt, bf, probes) == 0, "still matches brute force after deletion");

    // Delete everything: the root must survive and answer empty queries.
    for (ZoneId i = 900; i < 1000; ++i) { rt.remove(i); bf.remove(i); }
    t::ok(rt.size() == 0, "r-tree empties cleanly");
    std::vector<ZoneId> out;
    rt.query(geo::Bbox::around({25.55, 91.88}, 5000), out);
    t::ok(out.empty(), "an emptied r-tree returns nothing rather than crashing");
    t::ok(rt.stats().node_count <= 2, "and collapses to (near) a single node");

    // Refill it.
    for (ZoneId i = 0; i < 300; ++i) {
      const geo::Bbox b = box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96), 0.001);
      rt.insert(i, b);
      bf.insert(i, b);
    }
    t::ok(rt.size() == 300, "and refills");
    t::ok(disagreements(rt, bf, probes) == 0, "and is exact again after refilling");
  }

  // ── geohash: query padding must shrink when the big item leaves ────────────
  {
    Geohash gh;
    // Many small boxes, plus one district-sized one.
    for (ZoneId i = 0; i < 500; ++i)
      gh.insert(i, box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96), 0.0005));
    gh.insert(9999, box_at(25.56, 91.88, 0.08));           // the outlier

    const double pad_with = gh.query_pad_lat();
    gh.remove(9999);
    const double pad_without = gh.query_pad_lat();
    std::printf("       geohash lat padding: %.5f deg with the outlier -> %.5f without\n",
                pad_with, pad_without);
    t::ok(pad_without < pad_with * 0.2,
          "removing the one large item shrinks the query padding");

    // And the pruning actually improves: fewer keys scanned per query.
    gh.reset_counters();
    std::vector<ZoneId> out;
    for (const auto& q : probes) { out.clear(); gh.query(q, out); }
    const double after = gh.stats().avg_candidates();

    Geohash leaky;                                  // same data, padding never reset
    for (ZoneId i = 0; i < 500; ++i)
      leaky.insert(i, box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96), 0.0005));
    leaky.insert(9999, box_at(25.56, 91.88, 0.08));
    leaky.reset_counters();
    for (const auto& q : probes) { out.clear(); leaky.query(q, out); }
    const double with_outlier = leaky.stats().avg_candidates();
    std::printf("       geohash keys scanned per query: %.1f (outlier present) -> %.1f "
                "(outlier removed)\n", with_outlier, after);
    t::ok(after < with_outlier, "and the number of keys scanned per query drops");
  }

  // ── the whole set, cross-checked under randomised churn ────────────────────
  {
    BruteForceIndex bf;
    Quadtree qt;
    RTree rt;
    Geohash gh;
    std::vector<ZoneId> live;
    ZoneId next = 0;
    size_t bad = 0, rounds = 0;

    for (int step = 0; step < 400; ++step) {
      const bool removing = !live.empty() && rng.uniform() < 0.45;
      if (removing) {
        const size_t k = rng.below(uint32_t(live.size()));
        const ZoneId id = live[k];
        live.erase(live.begin() + long(k));
        bf.remove(id); qt.remove(id); rt.remove(id); gh.remove(id);
      } else {
        const geo::Bbox b = box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                                   rng.range(0.0002, 0.004));
        bf.insert(next, b); qt.insert(next, b); rt.insert(next, b); gh.insert(next, b);
        live.push_back(next);
        ++next;
      }
      if (step % 20 == 0) {
        ++rounds;
        bad += disagreements(qt, bf, probes);
        bad += disagreements(rt, bf, probes);
        bad += disagreements(gh, bf, probes);
      }
    }
    t::ok(bad == 0, "quadtree, r-tree and geohash all agree with brute force across " +
                        std::to_string(rounds) + " churn checkpoints");
    t::ok(qt.size() == live.size() && rt.size() == live.size() && gh.size() == live.size(),
          "and all three report the same live count");
  }

  return t::report("index/churn");
}
