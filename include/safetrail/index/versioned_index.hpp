#pragma once
// Time-travelling spatial index.  [GAP 3]
//
// Every existing implementation of this problem stores one current polygon set.
// That makes two things impossible:
//
//   - Zones whose risk varies with time. A river crossing is safe in dry season
//     and lethal after rain; a mountain road closes at dusk. Those are the same
//     zone with different validity, not different zones.
//
//   - Answering the one question an incident investigation actually asks:
//     "what were the rules at 14:32 on the day this happened?" Overwrite the
//     polygon when the rules change and that answer is gone forever.
//
// So the index is PERSISTENT: a mutation produces a new version without
// destroying the old one, and every past version stays queryable.
//
// Implementation: path-copying quadtree. An insert copies only the nodes on the
// root-to-leaf path and shares every untouched subtree with the previous version
// via shared_ptr. Cost is O(log n) new nodes per mutation instead of O(n) for a
// full copy -- which is exactly what makes keeping the entire history affordable.
// share_stats() reports the ratio so the claim is measured, not asserted.
#include <cstdint>
#include <memory>
#include <vector>

#include "safetrail/ds/interval_tree.hpp"
#include "safetrail/geo/bbox.hpp"
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

using VersionId = uint32_t;

// Half-open [from, to). Recurring windows (every night 18:00-06:00) are expanded
// into concrete spans at load time -- an interval tree over concrete intervals is
// far easier to test than a recurrence evaluator on the hot path.
struct Validity {
  Timestamp from = 0;
  Timestamp to   = kForever;
  bool active_at(Timestamp t) const { return t >= from && t < to; }
};

class VersionedIndex {
 public:
  VersionedIndex();
  ~VersionedIndex();

  // ── Mutation: every call creates a new version ────────────────────────────
  VersionId add_zone(ZoneId id, const geo::Bbox& box, Validity v, Timestamp at);
  VersionId remove_zone(ZoneId id, Timestamp at);
  VersionId update_validity(ZoneId id, Validity v, Timestamp at);

  // ── Query ─────────────────────────────────────────────────────────────────
  // Zones intersecting `box` that were in force at time `t`. Two filters compose:
  //
  //   1. spatial  -- the quadtree version covering t          O(log n + k)
  //   2. temporal -- per-candidate validity check             O(1) each
  //
  // Spatial first, deliberately. A tourist is near a handful of zones but MANY
  // zones are temporally active at any instant, so the spatial filter prunes far
  // harder. The interval tree is used for the temporal-only query below, where it
  // is the right structure.
  void query_at(Timestamp t, const geo::Bbox& box, std::vector<ZoneId>& out) const;
  void query_now(const geo::Bbox& box, std::vector<ZoneId>& out) const;

  // Every zone in force at t, ignoring position. THIS is where the interval tree
  // earns its place: O(log n + k) via the subtree-max-high pruning.
  void active_at(Timestamp t, std::vector<ZoneId>& out) const;

  // ── History ───────────────────────────────────────────────────────────────
  struct Change {
    VersionId version;
    Timestamp at;
    ZoneId    zone;
    enum class Kind { Added, Removed, ValidityChanged } kind;
  };
  std::vector<Change> history_for(ZoneId id) const;
  std::vector<Change> changes_between(Timestamp from, Timestamp to) const;

  VersionId latest_version() const;
  size_t    version_count() const;
  VersionId version_at(Timestamp t) const;      // latest version created at or before t
  size_t    zone_count_at(Timestamp t) const;

  // The metric that justifies the whole design.
  struct ShareStats {
    size_t total_nodes_allocated = 0;
    size_t nodes_if_full_copies  = 0;
    double sharing_ratio() const {
      return total_nodes_allocated
                 ? double(nodes_if_full_copies) / double(total_nodes_allocated) : 0.0;
    }
  };
  ShareStats share_stats() const;

  struct Node;

 private:
  std::vector<std::shared_ptr<const Node>> roots_;
  std::vector<Timestamp> version_times_;
  ds::IntervalTree<ZoneId> validity_tree_;
  std::vector<Validity> validity_;              // indexed by ZoneId, O(1) check
  std::vector<uint8_t>  live_;
  std::vector<Change>   changelog_;
  mutable size_t nodes_allocated_ = 0;

  VersionId commit(std::shared_ptr<const Node> root, Timestamp at);
};

}  // namespace safetrail::index
