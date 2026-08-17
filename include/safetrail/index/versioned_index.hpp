#pragma once
//
// Time-travelling spatial index.  [GAP 3]
//
// Every existing implementation stores one current polygon set. That makes two
// things impossible:
//
//   - Zones whose risk varies with time. A river crossing is safe in dry season
//     and lethal after rain. A mountain road closes at dusk. A border buffer has
//     different rules on different days. All of these are the same zone with
//     different validity, not different zones.
//
//   - Answering the only question an incident investigation actually asks:
//     "what were the rules at 14:32 on the day this happened?" If you overwrite
//     a polygon when the rules change, that answer is gone.
//
// So the index is persistent: mutations produce a new version without destroying
// the old one, and any past version stays queryable.
//
// Implementation: path-copying quadtree. An insert or delete copies only the
// nodes along the root-to-leaf path and shares every untouched subtree with the
// previous version. Cost per mutation is O(log n) extra nodes rather than a full
// O(n) copy, which is what makes keeping full history affordable.
//
// This is the most advanced structure in the project. It is here because the
// problem needs it, not to be impressive — but it is also the thing no other
// team will have.
//
#include <cstdint>
#include <memory>
#include <vector>

#include "safetrail/ds/interval_tree.hpp"
#include "safetrail/geo/bbox.hpp"
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

// Timestamp comes from safetrail/types.hpp
using VersionId = uint32_t;

// ─── A zone's validity in time ──────────────────────────────────────────────
//
// Half-open [from, to). `to == INF` means "still in force". Recurring windows
// (every night 18:00-06:00) expand into concrete intervals at load time — an
// interval tree over concrete spans is far simpler to reason about and to test
// than a recurrence-rule evaluator on the hot path.
struct Validity {
  Timestamp from = 0;
  Timestamp to   = kForever;
  static constexpr Timestamp kForever = INT64_MAX;

  bool active_at(Timestamp t) const { return t >= from && t < to; }
};

// ─── The versioned index ────────────────────────────────────────────────────
class VersionedIndex {
 public:
  VersionedIndex();
  ~VersionedIndex();

  // ── Mutation: each call creates a new version ─────────────────────────────
  // Returns the id of the version created. Version 0 is the empty index.
  VersionId add_zone(ZoneId id, const geo::Bbox& box, Validity v, Timestamp at);
  VersionId remove_zone(ZoneId id, Timestamp at);
  VersionId update_validity(ZoneId id, Validity v, Timestamp at);

  // ── Query ─────────────────────────────────────────────────────────────────
  //
  // Zones intersecting `box` that were ACTIVE at time `t`. Two filters compose
  // here, and the ordering matters for performance:
  //
  //   1. spatial:  path-copied quadtree at the version covering t   O(log n + k)
  //   2. temporal: interval tree over validity spans                O(log n + k)
  //
  // Spatial first — it prunes harder in practice, because a tourist is near a
  // handful of zones but many zones are temporally active at any instant.
  void query_at(Timestamp t, const geo::Bbox& box, std::vector<ZoneId>& out) const;

  // Current state. Convenience wrapper over query_at(now()).
  void query_now(const geo::Bbox& box, std::vector<ZoneId>& out) const;

  // ── History ───────────────────────────────────────────────────────────────
  // What the operator sees in the incident-investigation view.
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

  // Structural sharing metric — the number that justifies the whole design.
  // Report total nodes allocated against nodes that would be needed if every
  // version were a full copy. Expect the ratio to be dramatic.
  struct ShareStats {
    size_t total_nodes_allocated = 0;
    size_t nodes_if_full_copies  = 0;
    double sharing_ratio() const {
      return total_nodes_allocated
                 ? double(nodes_if_full_copies) / double(total_nodes_allocated)
                 : 0.0;
    }
  };
  ShareStats share_stats() const;

 private:
  struct Node;                                  // path-copied quadtree node
  std::vector<std::shared_ptr<const Node>> roots_;   // roots_[v] = version v
  ds::IntervalTree<ZoneId> validity_;                // validity spans
  std::vector<Change> changelog_;
};

}  // namespace safetrail::index
