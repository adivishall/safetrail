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
//
// ── What exactly is persistent ───────────────────────────────────────────────
//
// Both halves of a zone's identity, and it is worth being precise because an
// earlier version of this file got it wrong in a way that produced silently
// incorrect history:
//
//   GEOMETRY   the quadtree, by path copying. O(log n) new nodes per mutation.
//   VALIDITY   a per-zone APPEND-ONLY log of (version, Validity) records, one
//              entry per change. Lookup at version v is a binary search for the
//              last record with record.version <= v -- O(log h) in that zone's
//              own change count, which is a handful even over a long day.
//
// The bug this replaced: validity used to live in a mutable current-state array,
// so query_at(t) fetched the HISTORICAL geometry and then filtered it with
// TODAY'S rules. Ask "what was in force at 14:32 yesterday" after an operator
// extended a closure this morning and you got yesterday's zones gated by this
// morning's interval -- an answer that never existed. That is precisely the
// question the persistent index is built to answer, so getting it wrong made the
// whole structure decorative.
//
// The obvious alternative -- snapshot the whole validity array per version --
// would restore correctness while destroying the point: O(Z) copied state per
// mutation, which is the O(n) full copy that path copying exists to avoid. The
// append-only log keeps mutation cost O(1) amortised in validity and O(log n) in
// geometry, so the structural-sharing argument survives intact.
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

  // ── Mutation: every effective call creates exactly one new version ────────
  //
  // Semantics, pinned by tests/index/versioned_index_test.cpp and the replay
  // oracle in tests/index/persistence_differential_test.cpp:
  //
  //   add_zone      on an id that is not present: inserts it. On an id that IS
  //                 present: REPLACES its geometry and validity in one version
  //                 (a moved or redrawn zone). It used to insert a second copy,
  //                 so every later query returned the id twice.
  //   remove_zone   on a present id: removes it, path-copying only the one
  //                 root-to-node path that holds it (O(depth) new nodes). On an
  //                 absent id: a no-op that creates no version and returns the
  //                 latest one. It used to copy every node it VISITED while
  //                 searching -- 96 allocations to remove one zone from a
  //                 2,000-zone index -- and to mint a version for an id that
  //                 never existed.
  //   update_validity  changes only the validity; shares the whole tree.
  //
  // `at` is TRANSACTION time and is monotone: a change stamped earlier than the
  // latest version is recorded at the latest version's time. The log is
  // append-only, so history cannot be inserted into the past -- and version_at()
  // binary-searches the commit times, which is only valid on a sorted array. An
  // out-of-order commit used to make query_at() silently return the wrong
  // version.
  VersionId add_zone(ZoneId id, const geo::Bbox& box, Validity v, Timestamp at);
  VersionId remove_zone(ZoneId id, Timestamp at);
  VersionId update_validity(ZoneId id, Validity v, Timestamp at);

  // Is the zone present in the latest version?
  bool contains_zone(ZoneId id) const;

  // Structural audit of EVERY retained version: each item's box lies inside the
  // region of the node that stores it (the invariant that makes pruning by
  // region sound), children lie inside their parent with depth = parent + 1,
  // no id appears twice in one version, and each version's item count equals
  // the number of zones the history says were present at that version.
  bool check_invariants() const;

  // ── Query ─────────────────────────────────────────────────────────────────
  //
  // Two independent time axes, and conflating them is the classic bug in
  // temporal databases, so they are named here explicitly:
  //
  //   TRANSACTION time  when an operator changed the rules  -> picks a version
  //   VALID time        when a zone is in force             -> Validity{from,to}
  //
  // query_at(t) uses t for both: "evaluate the world as the rules stood at t".
  // That is the incident-investigation question -- what would the system have
  // said at 14:32 -- and it is the only one that is forensically meaningful.
  //
  // Zones intersecting `box` that were in force at time `t`. Two filters compose:
  //
  //   1. spatial  -- the quadtree version covering t          O(log n + k)
  //   2. temporal -- per-candidate validity AS OF that version   O(log h) each
  //
  // Spatial first, deliberately. A tourist is near a handful of zones but MANY
  // zones are temporally active at any instant, so the spatial filter prunes far
  // harder. The interval tree is used for the temporal-only query below, where it
  // is the right structure.
  void query_at(Timestamp t, const geo::Bbox& box, std::vector<ZoneId>& out) const;

  // Spatial-only against the newest version: NO validity filter is applied.
  // Named `_now` rather than `_at` for exactly that reason -- it answers "what
  // geometry is in the current index", which is what the benchmark and the
  // diagnostics overlay want. For the gated question use query_at(now, ...).
  void query_now(const geo::Bbox& box, std::vector<ZoneId>& out) const;

  // Every zone in force at t, ignoring position -- and in force according to the
  // rules as they stood at t, not today's. THIS is where the interval tree earns
  // its place: O(log n + k) via the subtree-max-high pruning. Every historical
  // validity interval is retained in the tree; the version filter selects the one
  // record per zone that was actually in effect, so a stab returns each zone at
  // most once.
  void active_at(Timestamp t, std::vector<ZoneId>& out) const;

  // The validity a zone had according to the rules in force at time `t`.
  // Returns false if the zone did not exist then. This is the accessor the
  // history/audit view uses, and what the tests assert against.
  bool validity_at(ZoneId id, Timestamp t, Validity* out) const;

  // ── History ───────────────────────────────────────────────────────────────
  struct Change {
    VersionId version;
    Timestamp at;
    ZoneId    zone;
    enum class Kind { Added, Removed, ValidityChanged, Replaced } kind;
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

  // One entry in a zone's validity history. Append-only: a change pushes a new
  // record rather than overwriting, so every past version keeps seeing the rule
  // that was actually in force under it.
  struct ValidityRecord {
    VersionId version;    // the version at which this became the zone's validity
    Validity  validity;
    bool      present;    // false = the zone was removed at this version
  };

  // How many (version, Validity) records exist in total. Reported so the
  // "history is cheap" claim is measured rather than asserted: this grows with
  // the number of CHANGES, not with versions x zones.
  size_t validity_records() const;

  // ── Structural-sharing visualization ────────────────────────────────────────
  //
  // For the dashboard's persistent-index panel: a few consecutive versions'
  // quadtrees, flattened, each node tagged `shared` (this exact Node object is
  // reused from the previous version by refcount) or new (freshly allocated on
  // this version's copied root-to-leaf path). Drawing the shared nodes dimmed and
  // the new ones highlighted makes path copying literally visible -- a mutation
  // lights up one path and shares everything else. A validity-only change lights
  // up nothing: it shares the whole tree and appends one record.
  struct VizNode {
    geo::Bbox region;
    uint8_t   depth = 0;
    bool      leaf = true;
    bool      shared = false;   // reused from the previous version (pointer identity)
    int       items = 0;        // zones stored directly at this node
    int       kids[4] = {-1, -1, -1, -1};   // indices into VizVersion::nodes, or -1
  };
  struct VizVersion {
    VersionId version = 0;
    Timestamp at = 0;
    std::vector<VizNode> nodes;   // nodes[0] is the root; empty if the version is empty
    size_t new_nodes = 0;         // freshly allocated this version (the copied path)
    size_t shared_nodes = 0;      // reused from the previous version by refcount
  };
  // Up to `max_versions` CONSECUTIVE versions, chosen from mid-history where the
  // tree has real structure. `shared` on the first returned version is always
  // false (there is no previous version in the window to share from).
  std::vector<VizVersion> viz_versions(size_t max_versions) const;

 private:
  std::vector<std::shared_ptr<const Node>> roots_;
  std::vector<Timestamp> version_times_;

  // history_[zone] is that zone's records, strictly increasing in `version`.
  std::vector<std::vector<ValidityRecord>> history_;

  // Temporal index over EVERY historical validity interval, not just the current
  // ones. The payload packs (zone, slot-in-that-zone's-history) into 64 bits so
  // a stab can resolve straight back to the record and check whether it is the
  // one in force at the queried version.
  ds::IntervalTree<uint64_t> validity_tree_;

  std::vector<Change>   changelog_;
  mutable size_t nodes_allocated_ = 0;

  // The box each PRESENT zone was inserted with, indexed by id. Removal uses it
  // to follow the single root-to-node path the insert rule put the zone on,
  // instead of searching (and copying) the tree.
  std::vector<geo::Bbox> live_box_;

  const ValidityRecord* record_as_of(ZoneId id, VersionId v) const;
  void push_record(ZoneId id, Validity v, bool present, VersionId version);

  VersionId commit(std::shared_ptr<const Node> root, Timestamp at);
  Timestamp monotone(Timestamp at) const;
};

}  // namespace safetrail::index
