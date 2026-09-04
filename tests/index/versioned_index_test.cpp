// GAP 3. The properties that matter for a persistent structure:
//   - a past version is UNCHANGED by later mutations
//   - validity intervals gate results by time
//   - structural sharing actually happens (else it is just N full copies)
//   - every historical version agrees with a brute-force index of what was live then
#include "../test_harness.hpp"
#include <string>
#include <vector>
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

  // ── validity history is itself versioned  ──────────────────────────────────
  //
  // The bug this pins: validity used to live in a mutable current-state array, so
  // a historical query fetched the OLD geometry and filtered it with the NEW
  // rules -- an answer that never existed at any point in time. These assertions
  // fail loudly against that implementation.
  {
    index::VersionedIndex ix;
    // A trail closed 10:00-12:00 on the day of the incident.
    ix.add_zone(0, box_at(25.55, 91.88), Validity{36000000, 43200000}, 1000);

    std::vector<ZoneId> o;
    o.clear(); ix.query_at(39600000, world, o);                 // 11:00
    t::ok(o.size() == 1, "closure is in force at 11:00 under the original rule");
    o.clear(); ix.query_at(46800000, world, o);                 // 13:00
    t::ok(o.empty(), "closure is NOT in force at 13:00 under the original rule");

    // At 20:00 the operator extends the closure retroactively-looking window to
    // 18:00-23:00 -- a different window that does not cover 11:00 at all.
    ix.update_validity(0, Validity{64800000, 82800000}, 72000000);

    o.clear(); ix.query_at(39600000, world, o);
    t::ok(o.size() == 1,
          "history intact: 11:00 still shows the closure that was in force THEN");
    o.clear(); ix.query_at(46800000, world, o);
    t::ok(o.empty(), "history intact: 13:00 still shows nothing in force");
    o.clear(); ix.query_at(75600000, world, o);                 // 21:00, after the edit
    t::ok(o.size() == 1, "the NEW window applies to queries after the edit");
    // Transaction time vs valid time, made explicit: 19:26 falls INSIDE the new
    // 18:00-23:00 window, but it is BEFORE the 20:00 edit that created it, so the
    // rules in force at 19:26 are still the old ones and the answer is empty.
    // Getting this wrong is exactly how a temporal query starts inventing history.
    o.clear(); ix.query_at(70000000, world, o);                 // 19:26
    t::ok(o.empty(), "an edit does not apply to timestamps before the edit itself");

    // Between the two intervals, under the new rules: not in force.
    o.clear(); ix.query_at(50400000, world, o);                 // 14:00
    t::ok(o.empty(), "query between the old and new windows returns nothing");

    // The accessor states the same thing directly.
    Validity got{};
    t::ok(ix.validity_at(0, 39600000, &got) && got.from == 36000000 && got.to == 43200000,
          "validity_at() reports the ORIGINAL window for a pre-edit timestamp");
    t::ok(ix.validity_at(0, 75600000, &got) && got.from == 64800000 && got.to == 82800000,
          "validity_at() reports the EDITED window for a post-edit timestamp");
  }

  // Several successive validity edits, each visible only from its own version on.
  {
    index::VersionedIndex ix;
    ix.add_zone(0, box_at(25.55, 91.88), Validity{0, 1000}, 100);
    ix.update_validity(0, Validity{0, 2000}, 200);
    ix.update_validity(0, Validity{0, 3000}, 300);
    ix.update_validity(0, Validity{0, 4000}, 400);

    Validity got{};
    t::ok(ix.validity_at(0, 150, &got) && got.to == 1000, "edit 1 of 4: window ends 1000");
    t::ok(ix.validity_at(0, 250, &got) && got.to == 2000, "edit 2 of 4: window ends 2000");
    t::ok(ix.validity_at(0, 350, &got) && got.to == 3000, "edit 3 of 4: window ends 3000");
    t::ok(ix.validity_at(0, 450, &got) && got.to == 4000, "edit 4 of 4: window ends 4000");

    // The history is O(changes), not O(versions x zones) -- the whole reason the
    // structural-sharing argument survives.
    t::ok(ix.validity_records() == 4, "one record per change, not per version");
  }

  // active_at() is version-aware too: the interval tree keeps every historical
  // window, and the version filter picks the one that was actually in effect.
  {
    index::VersionedIndex ix;
    ix.add_zone(0, box_at(25.55, 91.88), Validity{1000, 2000}, 10);
    ix.update_validity(0, Validity{5000, 6000}, 3000);

    std::vector<ZoneId> o;
    o.clear(); ix.active_at(1500, o);
    t::ok(o.size() == 1 && o[0] == 0, "active under the ORIGINAL window before the edit");
    o.clear(); ix.active_at(5500, o);
    t::ok(o.size() == 1 && o[0] == 0, "active under the EDITED window after the edit");
    o.clear(); ix.active_at(2500, o);
    t::ok(o.empty(), "not active between the windows");
    // A stab that matches BOTH historical intervals must still yield one zone.
    ix.update_validity(0, Validity{0, kForever}, 9000);
    o.clear(); ix.active_at(9500, o);
    t::ok(o.size() == 1, "overlapping historical intervals never duplicate a zone");
  }

  // Remove then re-add: the gap must read as "did not exist".
  {
    index::VersionedIndex ix;
    ix.add_zone(0, box_at(25.55, 91.88), Validity{0, kForever}, 100);
    ix.remove_zone(0, 200);
    ix.add_zone(0, box_at(25.55, 91.88), Validity{0, kForever}, 300);

    std::vector<ZoneId> o;
    o.clear(); ix.query_at(150, world, o); t::ok(o.size() == 1, "present before removal");
    o.clear(); ix.query_at(250, world, o); t::ok(o.empty(), "absent between removal and re-add");
    o.clear(); ix.query_at(350, world, o); t::ok(o.size() == 1, "present again after re-add");
    Validity got{};
    t::ok(!ix.validity_at(0, 250, &got), "validity_at() reports absence in the gap");
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
