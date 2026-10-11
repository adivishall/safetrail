// The persistent quadtree against a replay oracle.
//
// VersionedIndex keeps every version of the zone set. The oracle here is the
// dumbest possible model of the same thing: a full copy of the zone table per
// version (O(versions x zones) memory -- exactly the cost path copying exists to
// avoid, which is what makes it an independent check rather than a second copy of
// the same idea). Seeded random operation sequences drive both; then every
// historical question the index answers is asked of both:
//
//   query_at(t, box)    zones intersecting box, in force at t, per the rules at t
//   query_now(box)      spatial-only, newest version
//   active_at(t)        the interval-tree path, no geometry
//   validity_at(id, t)  one zone's rule as of t
//   zone_count_at(t)
//
// and after EVERY operation the structure is audited (check_invariants) and the
// allocation count is checked against the O(depth) bound path copying promises.
// The workload deliberately includes zone replacement, removal of absent ids,
// validity edits of absent ids, out-of-order commit times, zero-area boxes,
// boxes on quadrant split lines, negative coordinates, and boxes past lon 180.
#include "../test_harness.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "safetrail/index/versioned_index.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;
using namespace safetrail::index;

namespace {

struct ZoneRow { bool present = false; geo::Bbox box{}; Validity validity{}; };
struct Snapshot { Timestamp at = 0; std::vector<ZoneRow> zones; };

// The model: one full table per version, plus the transaction-time rule.
struct Oracle {
  std::vector<Snapshot> versions{Snapshot{0, {}}};

  Snapshot& fork(Timestamp at, size_t nzones) {
    Snapshot s = versions.back();
    s.at = at < s.at ? s.at : at;                       // monotone transaction time
    if (s.zones.size() < nzones) s.zones.resize(nzones);
    versions.push_back(std::move(s));
    return versions.back();
  }
  const Snapshot& as_of(Timestamp t) const {           // latest version created at or before t
    size_t v = 0;
    for (size_t i = 0; i < versions.size(); ++i) if (versions[i].at <= t) v = i;
    return versions[v];
  }
};

geo::Bbox random_box(sim::Rng& rng, int profile) {
  switch (profile) {
    case 0: {                                           // ordinary small zone
      const double la = rng.range(25.50, 25.62), lo = rng.range(91.80, 91.96);
      const double r = rng.range(0.0002, 0.003);
      return {la - r, lo - r, la + r, lo + r};
    }
    case 1: {                                           // zero-area (a point)
      const double la = rng.range(25.50, 25.62), lo = rng.range(91.80, 91.96);
      return {la, lo, la, lo};
    }
    case 2:                                             // exactly on the world's split lines
      return {0.0, rng.range(-1.0, 1.0), rng.range(0.0, 0.5), 0.0};
    case 3: {                                           // southern / western hemisphere
      const double la = rng.range(-40.0, -30.0), lo = rng.range(-75.0, -60.0);
      return {la, lo, la + 0.01, lo + 0.01};
    }
    case 4:                                             // straddles lon 180 (stored > 180)
      return {rng.range(-5.0, 5.0), 179.9, 5.5, 180.4};
    default: {                                          // huge
      return {-60.0, -150.0, 60.0, 150.0};
    }
  }
}

Validity random_validity(sim::Rng& rng) {
  if (rng.uniform() < 0.35) return Validity{0, kForever};
  const Timestamp from = Timestamp(rng.below(20000));
  return Validity{from, from + 1 + Timestamp(rng.below(15000))};
}

std::vector<ZoneId> sorted(std::vector<ZoneId> v) { std::sort(v.begin(), v.end()); return v; }

}  // namespace

int main() {
  constexpr ZoneId kZones = 40;
  size_t query_mismatch = 0, active_mismatch = 0, validity_mismatch = 0,
         count_mismatch = 0, now_mismatch = 0, audit_fail = 0, alloc_over = 0,
         version_mismatch = 0;
  size_t ops_total = 0, replaced = 0, absent_removes = 0, out_of_order = 0;

  for (uint64_t seed = 1; seed <= uint64_t(25 * t::stress()); ++seed) {
    sim::Rng rng(0xFE11 * seed);
    VersionedIndex ix;
    Oracle orc;
    Timestamp clock = 0;

    for (int op = 0; op < 160; ++op, ++ops_total) {
      const ZoneId id = ZoneId(rng.below(kZones));
      // Mostly forward in time, sometimes a late arrival stamped in the past.
      Timestamp at = clock + Timestamp(rng.below(400));
      if (rng.uniform() < 0.1) { at = clock - Timestamp(1 + rng.below(3000)); ++out_of_order; }
      else clock = at;

      const bool was_present = ix.contains_zone(id);
      const VersionId before_v = ix.latest_version();
      const size_t before_alloc = ix.share_stats().total_nodes_allocated;
      const double u = rng.uniform();

      if (u < 0.50) {                                   // add, or replace if present
        const geo::Bbox b = random_box(rng, rng.uniform() < 0.7 ? 0 : int(1 + rng.below(5)));
        const Validity val = random_validity(rng);
        ix.add_zone(id, b, val, at);
        Snapshot& s = orc.fork(at, kZones);
        s.zones[id] = ZoneRow{true, b, val};
        if (was_present) ++replaced;
      } else if (u < 0.80) {                            // remove (present or absent)
        ix.remove_zone(id, at);
        if (was_present) {
          Snapshot& s = orc.fork(at, kZones);
          s.zones[id].present = false;
        } else {
          ++absent_removes;
          if (ix.latest_version() != before_v) ++version_mismatch;   // must be a no-op
        }
      } else {                                          // validity edit (present or absent)
        const Validity val = random_validity(rng);
        ix.update_validity(id, val, at);
        if (was_present) {
          Snapshot& s = orc.fork(at, kZones);
          s.zones[id].validity = val;
        } else if (ix.latest_version() != before_v) {
          ++version_mismatch;
        }
      }

      if (ix.version_count() != orc.versions.size()) ++version_mismatch;
      if (!ix.check_invariants()) ++audit_fail;

      // Path copying: an insert copies one root-to-leaf path plus, at most, the
      // four children of a split; a replace is a removal path plus an insert.
      // 14 is the persistent tree's depth cap, so depth + 1 <= 15 nodes per path.
      const size_t spent = ix.share_stats().total_nodes_allocated - before_alloc;
      if (spent > 15 + 15 + 4) ++alloc_over;
    }

    // ── Every historical question, against the model ──────────────────────────
    for (int q = 0; q < 300; ++q) {
      const Timestamp t = Timestamp(rng.below(uint32_t(clock + 2000)));
      const Snapshot& snap = orc.as_of(t);

      const geo::Bbox box = random_box(rng, rng.uniform() < 0.6 ? 0 : int(1 + rng.below(5)));
      std::vector<ZoneId> got, want;
      ix.query_at(t, box, got);
      for (ZoneId z = 0; z < snap.zones.size(); ++z) {
        const ZoneRow& r = snap.zones[z];
        if (r.present && r.box.intersects(box) && r.validity.active_at(t)) want.push_back(z);
      }
      if (sorted(got) != sorted(want)) ++query_mismatch;

      got.clear(); want.clear();
      ix.active_at(t, got);
      for (ZoneId z = 0; z < snap.zones.size(); ++z)
        if (snap.zones[z].present && snap.zones[z].validity.active_at(t)) want.push_back(z);
      if (sorted(got) != sorted(want)) ++active_mismatch;

      size_t present = 0;
      for (const auto& r : snap.zones) present += r.present;
      if (ix.zone_count_at(t) != present) ++count_mismatch;

      const ZoneId z = ZoneId(rng.below(kZones));
      Validity v{};
      const bool has = ix.validity_at(z, t, &v);
      const bool want_has = z < snap.zones.size() && snap.zones[z].present;
      if (has != want_has ||
          (has && (v.from != snap.zones[z].validity.from || v.to != snap.zones[z].validity.to)))
        ++validity_mismatch;
    }
    for (int q = 0; q < 100; ++q) {
      const geo::Bbox box = random_box(rng, int(rng.below(6)));
      std::vector<ZoneId> got, want;
      ix.query_now(box, got);
      const Snapshot& snap = orc.versions.back();
      for (ZoneId z = 0; z < snap.zones.size(); ++z)
        if (snap.zones[z].present && snap.zones[z].box.intersects(box)) want.push_back(z);
      if (sorted(got) != sorted(want)) ++now_mismatch;
    }
  }

  const std::string ctx = " across " + std::to_string(25 * t::stress()) + " seeds x 160 ops (" + std::to_string(replaced) +
                          " replacements, " + std::to_string(absent_removes) +
                          " absent removals, " + std::to_string(out_of_order) +
                          " out-of-order commits)";
  t::ok(query_mismatch == 0, "query_at == replay oracle" + ctx);
  t::ok(active_mismatch == 0, "active_at == replay oracle" + ctx);
  t::ok(validity_mismatch == 0, "validity_at == replay oracle" + ctx);
  t::ok(count_mismatch == 0, "zone_count_at == replay oracle" + ctx);
  t::ok(now_mismatch == 0, "query_now == newest oracle version" + ctx);
  t::ok(version_mismatch == 0,
        "exactly one version per effective change; no-ops create none" + ctx);
  t::ok(audit_fail == 0, "check_invariants() holds for every retained version after every op");
  t::ok(alloc_over == 0, "no mutation allocates more than two root-to-leaf paths + a split");

  // ── Mutation check: the oracle comparison has teeth ────────────────────────
  // The same harness must FAIL when handed a deliberately wrong answer: here, the
  // rules as they stand TODAY applied to a historical version -- the exact bug
  // the append-only validity log was introduced to fix.
  {
    VersionedIndex ix;
    ix.add_zone(0, {25.5, 91.8, 25.6, 91.9}, Validity{0, 1000}, 10);
    ix.update_validity(0, Validity{5000, 6000}, 2000);
    std::vector<ZoneId> historical;
    ix.query_at(500, {25.4, 91.7, 25.7, 92.0}, historical);
    Validity today{};
    ix.validity_at(0, 99999, &today);
    const bool todays_rule_says_active = today.active_at(500);
    t::ok(historical.size() == 1 && !todays_rule_says_active,
          "a 'today's rules' implementation would disagree with query_at here, so the "
          "oracle comparison above would catch it");
  }

  return t::report("index/persistence_differential");
}
