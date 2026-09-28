# Data structures

The five core structures, the geometry they feed, and the state machine at the
end of the pipeline; then the extension inventory. For each: the design
decision that matters, the invariants its audit checks, and the complexity —
**theoretical bounds stated as bounds, measurements cited from
[RESULTS.md](RESULTS.md)** (which is generated from `bench/results/*.csv`).
Nothing here is a library: no Boost.Geometry, no PostGIS, no `std::map`,
`std::set`, `std::unordered_map` or `std::priority_queue`. `std::vector`,
`std::sort` and `std::shared_ptr` are used as storage, algorithm and ownership.

| # | Structure | File | Answers | Audit |
|---|---|---|---|---|
| 1 | Brute force | `include/safetrail/index/brute_force.hpp` | which zone boxes intersect this box? (the oracle) | — |
| 2 | Quadtree | `include/safetrail/index/quadtree.hpp` | same, by partitioning **space** | `Quadtree::check_invariants` |
| 3 | R-tree (STR) | `include/safetrail/index/rtree.hpp` | same, by partitioning **items** | `RTree::check_invariants` |
| 4 | AVL interval tree | `include/safetrail/ds/interval_tree.hpp` | which validity windows contain instant t? | `IntervalTree::check_invariants` |
| 5 | Persistent quadtree | `include/safetrail/index/versioned_index.hpp` | which zones were in force here at time t, *by the rules of time t*? | `VersionedIndex::check_invariants` |

All four spatial indexes (the geohash extension included) sit behind one
interface, `index::SpatialIndex`, so the evaluator, the tests and the benchmark
are written once and every index is interchangeable — which is what lets
`tests/fence/index_independence_test.cpp` require bit-identical event streams
across them.

---

## 1. Brute force — the oracle

A vector of `(id, box)`, scanned. O(n) per query, O(1) insert, O(n) remove.
It is **never deleted**: it is the correctness oracle every other index is
compared with, and the denominator of every speedup. A measurement bug in the
oracle is worse than one in the thing being measured — an earlier audit found
its candidate counter adding the whole accumulating output buffer instead of
what the call appended, inflating every pruning figure the project reported
([DEFECT_LOG.md](DEFECT_LOG.md), pass 4).

## 2. Quadtree — partition space

**Rule:** an item descends only into a child whose region *fully contains* its
box; otherwise it stays at the current node. Every item therefore lives in
exactly one node, so `remove` is unambiguous and `query` cannot return
duplicates — at the cost that a large box, or one straddling a split, settles
near the root.

- **Root fitted to the data** (plus 2% padding), not the planet. A world-rooted
  tree spends ~11 of its 12 levels before cells are district-sized.
- **Doubling root expansion** for an insert outside the root: the old root
  becomes one quadrant of a root twice its size, so the tree stays a quadtree
  and the old subtree is reused untouched. (Widening the rectangle in place —
  the earlier fix — kept queries correct but left regions covered by no child.)
- **Collapse on delete**: a node whose whole subtree now fits in one node pulls
  its items up; the check stops counting at `cap + 1`, so it is O(cap).

**Invariants:** every item inside its node's region (the reason pruning by
region is sound); every child exactly its parent's quadrant; four children or
none; item count = `size()`.

| Operation | Bound | Notes |
|---|---|---|
| query | O(log n + k) expected, **O(n) worst** | worst case: every zone the same box — no split separates them |
| insert | O(depth) + O(log R) root doublings | R = how far outside the root the box lies |
| remove | **O(n)** | no id → node map: the item is found by search, then O(cap) collapse per level |
| build | O(n log n) expected | n inserts after fitting the root |

## 3. R-tree — partition items

Guttman's R-tree with quadratic split. Node boxes are the **tight** envelope of
their contents, so nothing is forced upward the way a straddling box is in a
quadtree — but sibling boxes may overlap, so a query can descend several
branches.

- **STR bulk loading** (Sort-Tile-Recursive): sort by centre longitude, cut into
  √P vertical slices, sort each by latitude, pack runs of M. Same O(n log n) as
  n insertions, but a smaller tree with less overlap. `build_incremental` keeps
  the insertion-built version so the two can be compared (RESULTS.md §7).
- **Deletion condenses**: nodes that fall below minimum fill are detached and
  their entries reinserted, then a single-child root collapses. Entries are
  reinserted at the leaf level rather than as whole subtrees (Guttman), which is
  simpler and measurably bounded (RESULTS.md §14).

**Invariants:** all leaves at one depth; each node's box *exactly* the union of
its contents; no empty non-root node; no node over capacity; entry count =
`size()`. Minimum fill is not an invariant — STR legitimately leaves the last
node of a slice underfull.

| Operation | Bound |
|---|---|
| query | O(log n + k) expected, **O(n) worst** (overlapping boxes) |
| insert | O(log n) amortised over splits |
| build (STR) | O(n log n), dominated by the sorts |
| remove | **O(n)** to find + reinsertion of orphaned entries |

**Quadtree vs R-tree, measured on identical data (RESULTS.md §1–3):** both
return the same candidates (they must); the STR-packed R-tree visits fewer
nodes to find them and is the faster of the two at every size past a few
thousand zones. The quadtree is simpler, its cells are disjoint, and it is the
one that path-copies cleanly for persistence.

## 4. AVL interval tree — the time dimension

A BST over intervals, AVL-balanced, each node caching the maximum `high` in its
subtree. That augmentation is the point: a search skips a subtree the moment
its `max_high <= t`, which is what `std::multimap` has no hook for.

- **Total order `(low, high, value, seq)`**, not `low` alone. With `low` as the
  key, the many windows that share a start time (every closure that starts at
  midnight) form a block of equal keys that rotations scatter, and deletion
  degenerates to a traversal. With a strict total order, `remove(low, high,
  value)` is one descent.
- **Real AVL deletion** with rotation and `max_high` repair on the way up.
  Two-child deletion unlinks the in-order successor *by position*
  (`erase_min`); searching for its triple instead could remove a different node
  with the same triple and leave two nodes with one key (pass 6 defect).
- Freed slots are recycled, so the node array is bounded by the peak live size.

**Invariants:** strict order on the total key; AVL balance and correct stored
heights; `max_high` equal to the true subtree maximum; live count = `size()`.

| Operation | Bound |
|---|---|
| stab / overlap query | **O(log n + k), guaranteed** — the only structure here with a worst-case bound on the query |
| insert, remove | **O(log n), guaranteed** |

**Where it is used, precisely.** In `VersionedIndex::active_at(t)` — "every
zone in force at t", a temporal-only question with no spatial filter to lean on.
It is **not** the per-tick temporal filter: the evaluator checks
`zone.validity.active_at(now)` in O(1) on the handful of candidates the spatial
index returned, which is cheaper than any temporal index could be, because the
spatial filter prunes far harder. RESULTS.md §8 measures when the tree beats a
linear scan (selective windows: an order of magnitude) and when it cannot
(hundreds of windows containing every instant: the same O(k) ceiling as the
spatial indexes).

## 5. Persistent quadtree — history

"What were the rules at 14:32 on the day of the incident?" A mutation produces a
new version without destroying the old; every version stays queryable.

- **Geometry by path copying.** Nodes are immutable and children are
  `shared_ptr<const Node>`. An insert copies the root-to-leaf path it touches
  and shares the other three children at every level — O(depth) new nodes, not
  O(n). Removal follows the single path the insert rule put the zone on
  (guided by the zone's box), so it is O(depth) too.
- **Validity by an append-only log per zone** of `(version, Validity)` records;
  lookup at a version is a binary search. A validity-only change shares the
  entire tree and allocates **zero** nodes. Snapshotting the validity table per
  version would have been correct and O(zones) per change — the full copy path
  copying exists to avoid.
- **Two time axes, named explicitly.** *Transaction time* (when an operator
  changed the rules) selects a version; *valid time* (`Validity{from, to}`) says
  when a zone is in force. `query_at(t)` uses t for both. Transaction time is
  monotone: a change stamped earlier than the latest version is recorded at the
  latest version's time — history is append-only.
- **Mutation semantics:** re-adding a present id *replaces* it in one version;
  removing an absent id is a no-op that creates no version.

**Invariants (every retained version):** item-in-region; child-in-parent and
depth; no id twice; item count equals the zones the history says were present.

| Operation | Bound |
|---|---|
| add / remove / replace | O(depth) new nodes (≤ 19 / 15 / 34 at depth cap 14, counting a split's four children) |
| validity change | 0 nodes, one record |
| query at version | O(log n + k) expected + O(log h) per candidate (h = that zone's changes) |
| version_at(t) | O(log V), binary search over commit times |

---

## Geometry

| Algorithm | File | Notes |
|---|---|---|
| Ray casting | `src/geo/containment.cpp` | half-open crossing rule (a vertex belongs to exactly one of its edges); holes counted in the same parity; on-boundary = inside |
| Winding number | `src/geo/containment.cpp` | independent second implementation, orientation-normalised for holes; kept as a cross-check, and the disagreement it found was a real bug |
| Three-valued containment | `src/geo/containment.cpp` | Inside/Outside only when the whole accuracy disc is on one side; one `classify()` defines it for the whole engine |
| Signed distance to boundary | `src/geo/containment.cpp` | computed in local metres (`include/safetrail/geo/projection.hpp`), not degrees |
| Query box | `src/geo/bbox.cpp` | the exact bounding box of a spherical cap on the sphere `distance_m` uses, so the index filter is provably conservative |
| Segment predicates | `src/geo/segment.cpp` | one orientation / intersection / on-edge definition for every caller; the on-edge tolerance is a perpendicular distance (~0.1 mm) |
| Self-intersection | `src/geo/polygon.cpp`, `src/geo/sweep_line.cpp` | Shamos–Hoey sweep over a hand-written AVL status tree at ≥ 80 vertices, O(V²) pairwise below (and as the oracle); threshold from RESULTS.md §16 |

Details and every edge case: [GEOMETRY_EDGE_CASES.md](GEOMETRY_EDGE_CASES.md).

## The state machine

Per (tourist, zone): a hysteresis machine (asymmetric enter/exit margins, N
consecutive confirming fixes, minimum dwell) over the three-valued verdict, and
an event only when the confirmed state changes. Every zone with open state is
observed on every fix — including zones the index did not return this tick
(a certain Outside, still filtered by hysteresis) and zones that went out of
force (closed immediately). See [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Guaranteed, expected, measured

| Structure / operation | Bound | Guaranteed? | Measured |
|---|---|---|---|
| Brute-force query | O(n) | ✓ | linear (RESULTS.md §1, §2) |
| Quadtree query | O(log n + k) expected, O(n) worst | ✗ — partitions space, not data | RESULTS.md §1–3 |
| R-tree query | O(log n + k) expected, O(n) worst | ✗ — boxes may overlap | RESULTS.md §1–3, §7 |
| Index remove (all three) | O(n) | ✓ (it is the bound) | RESULTS.md §4 |
| Interval tree stab | O(log n + k) | ✓ AVL | RESULTS.md §8 |
| Interval tree insert/remove | O(log n) | ✓ AVL | RESULTS.md §8 |
| Persistent mutation | O(depth) new nodes | ✓ by construction | RESULTS.md §9 |
| Ray casting / winding | O(V) | ✓ | RESULTS.md §3 |
| Shamos–Hoey validation | O(V log V) | ✓ AVL status tree | RESULTS.md §16 |

**k is never hidden.** `+ k` is the output size, and every scaling table reports
it in its own column, because it sets the ceiling on any speedup: an index cannot
return fewer results than the query has.

---

## Extensions

Built on the core and tested against their own oracles, but not part of the
headline claim.

| Structure / algorithm | File | Purpose |
|---|---|---|
| Geohash (Morton/Z-order) index | `include/safetrail/index/geohash.hpp` | fourth `SpatialIndex`; its sorted key array is the offline serialisation format |
| k-d tree | `include/safetrail/index/kd_tree.hpp` | nearest road junction for dispatch; linear scan as oracle |
| Binary heap | `include/safetrail/ds/priority_queue.hpp` | Dijkstra/A* frontier, alert triage |
| Open-addressed hash table | `include/safetrail/ds/hash_table.hpp` | tombstones with a same-size rebuild so churn does not grow it |
| Hashed timer wheel | `include/safetrail/ds/timer_wheel.hpp` | escalation deadlines |
| Rollback union-find | `include/safetrail/ds/dynamic_connectivity.hpp` | group cohesion; union by rank, no path compression (it cannot be undone) |
| Circular buffer | `include/safetrail/ds/circular_buffer.hpp` | bounded per-tourist ping history |
| Road graph, Dijkstra, A* | `include/safetrail/graph/road_graph.hpp`, `dijkstra.hpp`, `astar.hpp` | responder routing on real OSM roads |
| Hungarian assignment | `include/safetrail/graph/bipartite_match.hpp` | optimal responder → incident matching |
| Spatio-temporal DSU correlator | `include/safetrail/alert/correlator.hpp` | collapses an alert flood into incidents |
| Merkle log (RFC 6962), SHA-256 | `include/safetrail/evidence/merkle_log.hpp` | tamper-evident event log with inclusion and consistency proofs |
| Lamport-clock reconciliation | `include/safetrail/sync/lamport.hpp` | ordering events recorded offline |
| Adaptive sampler | `include/safetrail/power/adaptive_sampler.hpp` | GPS sampling rate as a function of distance to risk |
| Jurisdiction hierarchy | `include/safetrail/jurisdiction/hierarchy.hpp` | polygon nesting → which authority owns an alert |

Why each exists is in [GAP_ANALYSIS.md](GAP_ANALYSIS.md); their measurements are
RESULTS.md §10–17.

## Deliberately not built

| Not built | Why |
|---|---|
| An id → node map in the spatial indexes | The workload is read-heavy; removal is O(n) and measured (RESULTS.md §4). It is the first change for a churn-heavy deployment. |
| Guttman same-level reinsertion on R-tree delete | Leaf-level reinsertion is simpler and bounded (RESULTS.md §14). |
| Exact (adaptive-precision) geometric predicates | Tolerances are explicit and shared by every caller; see [GEOMETRY_EDGE_CASES.md](GEOMETRY_EDGE_CASES.md). |
| Antimeridian support | Rejected at load. A split-at-the-seam representation is the fix; no zone in scope needs it. |
| A server | The engine writes one self-contained HTML replay; there is no network surface. |
