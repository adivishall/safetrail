// GAP 3. The properties that matter for a persistent structure:
//   - a past version is UNCHANGED by later mutations
//   - validity intervals gate results by time
//   - structural sharing actually happens (else it is just N full copies)
//   - every historical version agrees with a brute-force index of what was live then
#include "../test_harness.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/sim/mobility.hpp"
#include <algorithm>

using namespace safetrail;
using index::Validity;

static geo::Bbox box_at(double lat, double lon, double r = 0.001) {
  return {lat - r, lon - r, lat + r, lon + r};
}

int main() {
  const geo::Bbox world = geo::Bbox::around({25.55, 91.88}, 60000);

  // ── time travel ────────────────────────────────────────────────────────────
  {
    index::VersionedIndex ix;
    ix.add_zone(0, box_at(25.55, 91.88), Validity{0, kForever}, 1000);
    ix.add_zone(1, box_at(25.56, 91.89), Validity{0, kForever}, 2000);
    ix.add_zone(2, box_at(25.57, 91.90), Validity{0, kForever}, 3000);

    std::vector<ZoneId> o;
    o.clear(); ix.query_at(1500, world, o);
    t::ok(o.size() == 1, "at t=1500 only the first zone existed");
    o.clear(); ix.query_at(2500, world, o);
    t::ok(o.size() == 2, "at t=2500 two zones existed");
    o.clear(); ix.query_at(9999, world, o);
    t::ok(o.size() == 3, "at t=9999 all three exist");
    o.clear(); ix.query_at(0, world, o);
    t::ok(o.empty(), "at t=0 the index was empty");

    // Removal must not rewrite history.
    ix.remove_zone(1, 5000);
    o.clear(); ix.query_at(9999, world, o);
    t::ok(o.size() == 2, "after removal the present has two zones");
    o.clear(); ix.query_at(4000, world, o);
    t::ok(o.size() == 3, "the PAST still shows three zones after removal");
    t::ok(std::find(o.begin(), o.end(), ZoneId(1)) != o.end(),
          "the removed zone is still visible in the past");
  }

  // ── validity intervals ─────────────────────────────────────────────────────
  {
    index::VersionedIndex ix;
    // A road closed only at night: in force 18:00-06:00.
    ix.add_zone(0, box_at(25.55, 91.88), Validity{64800000, 108000000}, 100);
    ix.add_zone(1, box_at(25.55, 91.881), Validity{0, kForever}, 100);

    std::vector<ZoneId> o;
    o.clear(); ix.query_at(43200000, world, o);           // noon
    t::ok(o.size() == 1 && o[0] == 1, "at noon only the always-on zone is in force");
    o.clear(); ix.query_at(72000000, world, o);           // 20:00
    t::ok(o.size() == 2, "at 20:00 the night closure is also in force");

    o.clear(); ix.active_at(43200000, o);
    t::ok(o.size() == 1, "interval tree: one zone active at noon");
    o.clear(); ix.active_at(72000000, o);
    t::ok(o.size() == 2, "interval tree: two zones active at 20:00");
  }

  // ── structural sharing ─────────────────────────────────────────────────────
  {
    index::VersionedIndex ix;
    sim::Rng rng(808);
    for (size_t i = 0; i < 600; ++i)
      ix.add_zone(ZoneId(i), box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96)),
                  Validity{0, kForever}, Timestamp(1000 + i));

    const auto st = ix.share_stats();
    t::ok(ix.version_count() == 601, "601 versions retained (600 inserts + empty)");
    t::ok(st.sharing_ratio() > 5.0,
          "sharing ratio > 5x vs full copies (got " +
              std::to_string(st.sharing_ratio()) + "x)");
    printf("       sharing: %zu nodes allocated vs %zu for full copies = %.1fx\n",
           st.total_nodes_allocated, st.nodes_if_full_copies, st.sharing_ratio());

    // A validity change touches no geometry, so it must allocate ZERO new nodes.
    const size_t before = ix.share_stats().total_nodes_allocated;
    ix.update_validity(5, Validity{0, 999999}, 99999);
    t::ok(ix.share_stats().total_nodes_allocated == before,
          "validity-only change allocates no new nodes");
  }

  // ── the real gate: agree with brute force at EVERY historical version ──────
  {
    sim::Rng rng(1234);
    index::VersionedIndex ix;
    std::vector<std::pair<ZoneId, geo::Bbox>> live;
    std::vector<Timestamp> stamps;
    std::vector<std::vector<std::pair<ZoneId, geo::Bbox>>> snapshots;

    for (size_t step = 0; step < 120; ++step) {
      const Timestamp at = Timestamp(1000 * (step + 1));
      const bool removing = step > 20 && rng.uniform() < 0.3 && !live.empty();
      if (removing) {
        const size_t k = rng.below(uint32_t(live.size()));
        ix.remove_zone(live[k].first, at);
        live.erase(live.begin() + long(k));
      } else {
        const ZoneId id = ZoneId(step + 1000);
        const geo::Bbox b = box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                                   rng.range(0.0004, 0.004));
        ix.add_zone(id, b, Validity{0, kForever}, at);
        live.emplace_back(id, b);
      }
      stamps.push_back(at);
      snapshots.push_back(live);
    }

    size_t bad = 0, checked = 0;
    for (size_t s = 0; s < stamps.size(); ++s) {
      index::BruteForceIndex bf;
      bf.build(snapshots[s]);
      for (int q = 0; q < 12; ++q) {
        const geo::Bbox box = geo::Bbox::around(
            {rng.range(25.49, 25.63), rng.range(91.79, 91.97)}, rng.range(200, 4000));
        std::vector<ZoneId> a, b2;
        bf.query(box, a);
        ix.query_at(stamps[s], box, b2);
        std::sort(a.begin(), a.end()); std::sort(b2.begin(), b2.end());
        ++checked;
        if (a != b2) ++bad;
      }
    }
    t::ok(bad == 0, "versioned index == brute force at all 120 versions (" +
                        std::to_string(checked) + " queries)");
  }

  return t::report("index/versioned_index");
}
