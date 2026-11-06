# 03 — Data structures deep dive

> Every data structure the code implements, where it is actually used, how it is
> built, and what its bounds really are. Expected bounds are labelled
> **expected**; guarantees are labelled **guaranteed**. Read
> [01_PROJECT_OVERVIEW.md](01_PROJECT_OVERVIEW.md) §8 for the glossary.

## 0. Where each structure is used (not all are equally central)

| Structure | File | Used at runtime by | Centrality |
|---|---|---|---|
| Brute-force scan | `src/index/brute_force.cpp` | tests and benchmarks as the **oracle**; Lab | correctness backbone |
| **R-tree (STR-packed)** | `src/index/rtree.cpp` | **the live per-fix index** (`Platform` constructs `index::RTree`) | **core, hot path** |
| Quadtree | `src/index/quadtree.cpp` | benchmarks, Lab, engine tests, static dashboard overlay | core comparison |
| Geohash / Z-order | `src/index/geohash.cpp` | the **offline phone**'s index (serialised to bytes) | offline path |
| k-d tree | `include/safetrail/index/kd_tree.hpp` | `RoadGraph::nearest_node()`: snapping responders and incidents to junctions | dispatch |
| AVL interval tree | `include/safetrail/ds/interval_tree.hpp` | `VersionedIndex::active_at(t)`: "zones in force at t" | history |
| Persistent quadtree | `src/index/versioned_index.cpp` | every zone change; Investigation page | history |
| Rollback union-find | `src/ds/dynamic_connectivity.cpp` | `CohesionMonitor` (groups), `Correlator` (incidents) — **built fresh each pass; rollback unused at runtime** | grouping |
| Binary heap | `include/safetrail/ds/priority_queue.hpp` | Dijkstra/A* frontier; `alert::TriageQueue` | routing, triage |
| Open-addressing hash map | `include/safetrail/ds/hash_table.hpp` | sessions, username index, escalation records, reconciler de-duplication, throttles, rate-limit buckets | everywhere |
| Hashed timing wheel | `include/safetrail/ds/timer_wheel.hpp` | `alert::EscalationTracker` | escalation |
| Circular buffer | `include/safetrail/ds/circular_buffer.hpp` | last 64 pings per tourist (`track::Tourist::pings`) | speed, anomalies |
| Road graph (adjacency list) | `src/graph/road_graph.cpp` | Dijkstra, A*, the map's road layer | dispatch |
| Merkle log | `src/evidence/merkle_log.cpp` | every log entry; access audit; browser proofs | evidence |
| Lamport clock + offline queue + reconciler | `include/safetrail/sync/lamport.hpp` | offline phone scenario | sync |
| AVL status tree (sweep line) | `src/geo/sweep_line.cpp` | polygon validation for rings ≥ 80 vertices | validation |

---

## 1. Brute-force spatial scan — the oracle

**Definition.** A vector of `(ZoneId, Bbox)` pairs; a query tests every box.

**Why it exists.** It is obviously correct, so every fast index is checked
against it, permanently (`index::BruteForceIndex`). It is also the baseline every
speedup is divided by.

| Operation | Implementation | Cost |
|---|---|---|
| build | copy the items | Θ(n) |
| insert | `push_back` | O(1) amortised |
| remove | find and erase | O(n) |
| query(box) | test all n boxes with `Bbox::intersects` | **Θ(n), guaranteed** |
| space | | Θ(n) |

**Worked example.** 5 zones, query box Q. Test Z1…Z5 against Q: 5 tests, return
the ones that overlap. At 100,000 zones the committed benchmark measured 244.6 µs
per query (`index_scaling.csv`).

**Why keep it?** "Fast but unverified" is worthless for a safety system.

---

## 2. Quadtree — partitions SPACE

**Definition.** A tree in which every internal node covers a rectangle and has
exactly four children covering its four quadrants (NW, NE, SW, SE).

**Problem in SafeTrail.** Range query: which zone boxes overlap the query box
around a fix?

**Node structure** (`struct Quadtree::Node` in `src/index/quadtree.cpp`):
```cpp
geo::Bbox region;                                   // the square this node covers
std::vector<std::pair<ZoneId, geo::Bbox>> items;     // boxes stored here
std::unique_ptr<Node> kids[4];                       // all null for a leaf
```
Constructor: `Quadtree(size_t node_capacity = 8, size_t max_depth = 12)`.

**Insertion** (`insert_into`):
1. If the node has children, descend into the **one** child whose region fully
   contains the box. If no single child contains it (the box **straddles** a
   split line), keep it in this internal node's own `items`.
2. At a leaf, append. If the leaf now holds more than `capacity` items and depth
   < `max_depth`, **subdivide** into four and push down every item that fits
   entirely in one child.
3. If an insert lies outside the root, `expand_root_to_cover()` **doubles** the
   root so the old root becomes exactly one quadrant (the tiling stays exact).

**Search** (`query_node`):
```
query(node, Q):
  if node.region does not intersect Q: return          // prune the whole subtree
  for each (id, box) in node.items: if box intersects Q: output id
  for each child: query(child, Q)
```

**Deletion** (`remove_node` + `try_collapse`): erase the item; if a subtree now
holds ≤ capacity items, pull them up and drop the four children (the exact
inverse of the split), so a tree that grew and shrank looks like a fresh one.
`count_upto()` stops counting at capacity + 1, so the check is cheap.

**Diagram — subdivision and a query.**
```
Root region (whole district), capacity 2 for illustration
+-----------------------+-----------------------+
|          NW           |          NE           |
|   z1 []               |  +---------+---------+|
|                       |  |  NE.NW  |  NE.NE  ||
|            [z3 straddles the vertical split]  |
|                       |  |  z4 []  |   z5 [] ||
+-----------------------+--+---------+---------++
|          SW           |          SE           |
|                       |                       |
|      z2 []            |                       |
+-----------------------+-----------------------+
z3 overlaps both NW and NE, so it is stored in the ROOT's own item list.

Query box Q in the NE.NE corner:
  root: region meets Q -> test root items (z3: no)
  NW: region misses Q   -> PRUNED (whole subtree skipped)
  SW, SE: miss          -> PRUNED
  NE: meets Q -> NE.NW misses (pruned), NE.NE meets -> test z5: yes
Result {z5}. Boxes tested: 2 instead of 5.
```

**Invariants** (`Quadtree::check_invariants()`): children tile the parent exactly;
every item lies inside its node's region; an item in an internal node does not
fit any single child; counts match.

**Complexity.**
| Operation | Bound |
|---|---|
| query | **expected** O(log n + k) on well-spread data; **worst case O(n)** (all boxes straddle the root's split lines, or adversarial clustering at max depth) |
| insert | expected O(log n); worst O(depth + capacity) per push-down chain |
| remove | O(n) — finding the item: there is no id → node map (README limitation) |
| build | expected O(n log n) |
| space | O(n) |

**Limitations.** Fixed split lines: a large box near the centre straddles and
sits high in the tree, so it is tested by almost every query. Clustered data
deepens the tree. Removal is O(n).

**Why chosen.** The simplest space-partitioning tree, easy to reason about and to
draw; it is the comparison that shows *why* the R-tree's item partitioning wins on
this data.

---

## 3. R-tree — partitions ITEMS (the live index)

**Definition.** A balanced tree where every node stores the **minimum bounding
rectangle (MBR)** of everything beneath it. Leaves hold `(id, box)` entries;
internal nodes hold children. Node boxes may **overlap** (unlike quadtree regions).

**Why it is the live index.** Its boxes hug the data, so queries prune better on
real clustered zones; the platform constructs `index::RTree` (`src/app/platform.cpp`).

**Node structure** (`struct RTree::Node` in `src/index/rtree.cpp`):
```cpp
bool leaf = true;
geo::Bbox box;                                          // MBR of the subtree
std::vector<std::pair<ZoneId, geo::Bbox>> entries;      // leaf payload
std::vector<std::unique_ptr<Node>> kids;                // internal children
```
`RTree(size_t max_entries = 8)`: M = 8 entries per node, m = M/2 = 4 minimum.

**Insertion** (`do_insert`, Guttman 1984):
1. **ChooseSubtree**: descend into the child needing the **least enlargement** of
   its box to include the new box (ties: smaller area).
2. Insert at the leaf. If it overflows (> M entries), **quadratic split**: pick
   the two entries that would waste the most area together as seeds
   (`quadratic_split`), then assign each remaining entry to the group whose box
   grows less, forcing assignments so each group reaches m.
3. Propagate the split upward; refit boxes; a root split grows the tree by one level.

**Search** (`query_node`): like the quadtree, but tests node **boxes**:
```
query(node, Q):
  if node.box does not intersect Q: return
  if leaf: output every entry whose box intersects Q
  else: for each child: query(child, Q)        // may descend several overlapping children
```

**Deletion with condensing** (`remove_node`): erase the entry, then on the way
back up detach any non-root node that fell below m entries and **reinsert** its
entries from the top. (Simplified CondenseTree: orphans are flattened to leaf
entries, stated as a trade-off in the source.)

**STR bulk loading** (`RTree::build`, Sort-Tile-Recursive, Leutenegger et al. 1997):
```
P = ceil(n / M) leaves needed; S = ceil(sqrt(P)) vertical slices
1. sort all boxes by centre longitude, cut into S slices
2. sort each slice by centre latitude, cut into runs of M -> leaves
3. repeat over the leaves' boxes to build the next level, until one node
Ties broken on id so the packing is deterministic.
```
```
STR on 16 boxes, M = 4  (S = 2 slices)

  slice 1 (west)    slice 2 (east)
  +------+          +------+
  | L1   |          | L3   |     each Li = 4 boxes sorted by latitude
  +------+          +------+     -> near-square tiles, little overlap
  | L2   |          | L4   |
  +------+          +------+
Root = [ MBR(L1), MBR(L2), MBR(L3), MBR(L4) ]
```
Insertion order decides an incrementally built tree's shape; STR sees all items
at once and tiles them, so node boxes overlap less and queries descend fewer
branches. Measured (`index_build.csv`, 100,000 zones): STR builds in 16.2 ms vs
26.7 ms, uses 14,383 nodes vs 21,368, and queries **6.26× faster**.

**Invariants** (`RTree::check_invariants()`): every node box is exactly the MBR of
its children; all leaves at the same depth; fill ≤ M (STR may leave the last node
of a slice under-full, which is allowed).

**Complexity.**
| Operation | Bound |
|---|---|
| query | **expected** O(log n + k); **worst case O(n)** when node boxes overlap heavily |
| insert | O(log n) descent + O(M²) quadratic split per level |
| remove | O(n) to find the entry (no id map) + reinsertion of orphans |
| STR build | O(n log n) (dominated by the sorts) |
| space | O(n) |

**Quadtree vs R-tree (a favourite viva question).**
| | Quadtree | R-tree |
|---|---|---|
| Partitions | space (fixed quadrants) | items (data-driven boxes) |
| Node regions | disjoint | may overlap |
| A box crossing a split | stays high in the tree | always fits inside some MBR |
| Balance | depth depends on data | all leaves at one depth |
| Measured at 100k, one district | 32.71× | 223.67× |

---

## 4. Geohash index and the Z-order (Morton) curve

**Definition.** Quantise latitude and longitude, **interleave their bits** into one
48-bit key (`index::Geohash::morton()`), sort the items by key in a flat array, and
answer a box query by binary-searching the key range of the (padded) box, then
filtering by bounding box.

```
2-bit example: cell (row r, col c), key = interleave(r bits, c bits)
  r=1 (01), c=2 (10)  ->  bits r1 c1 r0 c0 = 0 1 1 0 = 6
Ordering of a 4x4 grid follows a recursive "Z":
   0  1 | 4  5
   2  3 | 6  7
  ------+------
   8  9 | 12 13
  10 11 | 14 15
```

**Why in SafeTrail.** A sorted array with no pointers serialises to bytes
(`Geohash::serialize()`), so it ships to an **offline phone** that evaluates zones
locally (`Platform::go_offline()` builds and serialises one). It returns exactly
what brute force returns (`tests/index/equivalence_test.cpp`).

**Complexity.** Key computation O(bits); lookup O(log n) binary search per key
range plus the refine step; insert/remove keep the array sorted (O(n)). Space O(n).

**Limitation.** One query box can map to a long key range that includes cells
outside the box ("Z-jumps"), so the refine step does real work.

---

## 5. k-d tree — nearest neighbour over points

**Definition.** A binary tree over points that splits alternately on latitude and
longitude at the median (`index::KdTree<Id>`).

**Use.** `graph::RoadGraph::nearest_node()` snaps a responder or an incident
centroid to the nearest of **144,090** road junctions. Built lazily on first use.

**Node structure:** `struct Node { int32_t item; int32_t left = -1, right = -1; uint8_t axis = 0; };`
in a flat `std::vector<Node>`.

**Nearest-neighbour search** (`nn_rec`): descend toward the query point; on the
way back, visit the far side of a split **only if** the splitting line is closer
than the best point found so far.

**Metric detail.** Longitude is scaled by cos(mean latitude) so distances are in a
local tangent plane; `distance_m()` exposes that exact metric so the linear-scan
oracle (`nearest_node_linear()`) answers the same question.

**Complexity.** Build O(n log n); nearest **expected** O(log n) for well-spread
points, **worst case O(n)**. Space O(n).

**Worked example (Lab).** 60 random points, click a query point: the tree visits a
handful of nodes; the linear scan visits all 60; "Agree: yes".

---

## 6. AVL interval tree — "which validity windows contain time t?"

**Definition.** A binary search tree over intervals `[low, high)`, kept balanced by
**AVL rotations**, and **augmented**: each node stores the maximum `high` in its
subtree (`max_high`).

**Use.** `VersionedIndex::active_at(t)` stabs the tree for all zone-validity
intervals containing t (`validity_tree_.stabbing(t, hits)` in
`src/index/versioned_index.cpp`). **Not** the per-fix filter: the evaluator checks
validity in O(1) per candidate (`Validity::active_at`).

**Ordering key.** The total key `(low, high, value, seq)` — `seq` is a per-insert
counter so no two nodes are equal. This makes deletion follow one path even when
many intervals share a start time (e.g. many closures starting at midnight).

**Insertion.** BST insert by key; on the way up update `height` and `max_high`,
and rebalance with single or double rotations when a balance factor reaches ±2.

**Deletion.** Real AVL deletion with rotations and `max_high` repair, O(log n);
freed slots go on a free list. (It used to be a tombstone — a defect fixed in the
audit, see the header comment.)

**Stabbing search** (point query at t):
```
stab(node, t):
  if node is null or node.max_high <= t: return        // nothing below ends after t
  stab(node.left, t)
  if node.low <= t < node.high: output node
  if node.low <= t: stab(node.right, t)                // right keys start later
```

**Rotation example (ascending inserts force a left rotation).**
```
insert [10,35) [20,45) [30,55)          after left rotation at 10
   10                                        20
     \                                      /  \
      20          balance(10) = -2   ->   10    30
        \
         30
max_high is recomputed bottom-up: node 20 now has max_high = 55.
```

**Invariants** (`check_invariants()`, `balanced()`): BST order on the full key;
|balance factor| ≤ 1; `max_high` = max of own high and children's; height ≤
1.44 log₂(n + 2).

**Complexity.**
| Operation | Bound |
|---|---|
| insert / delete | **guaranteed** O(log n) |
| find one overlapping interval | **guaranteed** O(log n) |
| report all k overlaps | **guaranteed** O(min(n, (k + 1) log n)) — *not* O(log n + k) |
| space | O(n) |

The honest bound matters: each reported interval can light a different
root-to-leaf path. A centred interval tree or priority search tree would give
O(log n + k); not needed here. Measured (`interval_tree.csv`): 14–57× faster than
a scan when queries are **selective**; when hundreds of intervals contain every
instant ("dense") it is 0.61× at 1,000 (loses) and 2.12× at 100,000.

**Why not an ordinary AVL tree?** An ordinary BST keyed on `low` can find
intervals that *start* before t, but cannot skip a subtree whose intervals have
all *ended* — the `max_high` augmentation is what allows pruning.

---

## 7. Persistent path-copying quadtree — every past version stays queryable

**Definition.** A quadtree where an update creates a **new root** and copies only
the nodes on the path it changes; all other subtrees are **shared** by reference
with the previous version (`index::VersionedIndex`, `std::shared_ptr<const Node>`).

**Use.** Every zone create/update/delete (`Platform::index_zone()` →
`VersionedIndex::add_zone/remove_zone/update_validity`); the Investigation page
asks "which zones were in force at t" and "what version was current then".

**Node:** `region`, `depth`, `items`, `std::shared_ptr<const Node> kids[4]`
(capacity `kCap = 8`). Versions are `std::vector<std::shared_ptr<const Node>> roots_`.

**What is persistent — two halves:**
| Half | How |
|---|---|
| Geometry (the boxes) | path copying, O(depth) new nodes per change |
| Validity (time windows) | a per-zone **append-only** log of `(version, Validity)` records; lookup at version v = binary search for the last record ≤ v |

**Path copying diagram.**
```
Version 1                          Version 2 (zone in the SE leaf moved)
   R1                                 R2  (new)
 / | | \                            / | | \
A  B  C  D                         A  B  C  D'  (new copy of the changed path)
         |                                  |
        ... leaf L                         L'  (new)
A, B, C and every unchanged node below them are SHARED by R1 and R2.
R1 is never modified, so querying version 1 still gives the old answer.
```

**Complexity.** Update: O(depth) new nodes (vs O(n) for a full copy). Query at any
version: same cost as the present. A validity-only change copies **no** nodes.
Measured (`versioned_index.csv`): after 5,001 versions, 71,314 nodes allocated vs
930,257 for full copies — **13.04× sharing**; past queries cost the same as
present ones.

**Persistence vs rollback (a common viva question).**
| | Persistence (this structure) | Rollback (DSU, §8) |
|---|---|---|
| Versions alive at once | all of them | one |
| Old states | queryable forever | restored by undoing |
| Mechanism | copy-on-write + sharing | undo stack |

---

## 8. Rollback union-find (disjoint-set union)

**Definition.** Disjoint sets with `find(x)` (root of x's set) and `unite(x, y)`;
**union by rank, no path compression**, plus an **undo stack** of every write so
`rollback_to(mark)` can restore an earlier state (`ds::RollbackDSU`).

**Use.**
- `group::CohesionMonitor`: tourists within the proximity distance are united;
  more than one component = the group split.
- `alert::Correlator::ingest()`: alerts within 200 m and the time window are
  united; each component becomes (or joins) one incident.

**Honest scope.** Both callers build a **fresh** DSU per pass and never call
`snapshot()`/`rollback_to()` (stated in `include/safetrail/ds/dynamic_connectivity.hpp`).
Rollback is implemented, tested against a flood-fill oracle, and demonstrated in
the Lab — but it is **not** what makes the tick fast.

**Why no path compression?** Compression rewrites an unbounded number of parent
pointers during a `find`; those writes could not be recorded on a bounded undo
stack, so rollback would be impossible. The price: `find` is O(log n) (union by
rank keeps trees shallow) instead of near-constant α(n).

**Operations** (`src/ds/dynamic_connectivity.cpp`):
```
find(x):  while parent[x] != x: x = parent[x]            // no compression
unite(x,y): rx, ry = find(x), find(y); if equal return false
            make the lower-rank root the child; push (child, old parent, root, old rank)
            on the undo stack; bump rank on a tie; components--
rollback_to(mark): pop undo records until the stack size == mark, restoring them
```

**Worked example (the Lab's default operations).**
```
unite 0 1, unite 2 3, snapshot (mark 2), unite 1 3, unite 4 5, unite 5 3
forest: {0,1,2,3,4,5} one tree; components = 5 (of 10 elements)
rollback to mark 2  -> undo the last three unions
forest: {0,1}, {2,3}, and 4..9 alone; components = 8
```

**Complexity (guaranteed).** find O(log n); unite O(log n); rollback O(1) per
undone union; space O(n) + undo stack.

---

## 9. Binary heap (priority queue)

**Definition.** A complete binary tree stored in an array, children of i at
2i + 1 and 2i + 2; the heap property holds between parent and child
(`ds::BinaryHeap<T, Less>`; min-heap with the default comparator).

**Use.** The Dijkstra and A* frontier (`src/graph/dijkstra.cpp`, `astar.cpp`) with
**lazy deletion** (stale entries are skipped when popped instead of decreasing
keys); `alert::TriageQueue`, a max-heap on alert priority.

**Operations.** `push` = append + `sift_up`; `pop` = move last to root +
`sift_down`; `top` = `heap_.front()`.

```
push 41,17,88,5,23 (min-heap)          array: [5, 17, 88, 41, 23]
          5                            pop -> 5; last (23) moves to the root,
        /   \                          sifts down past 17:
      17     88                        [17, 23, 88, 41]
     /  \
   41    23
```

**Complexity (guaranteed).** push/pop O(log n); top O(1); space O(n), contiguous.

---

## 10. Open-addressing hash map (linear probing)

**Definition.** One flat array of slots; a key hashes to a **home slot**; on
collision, probe the next slot; deletion leaves a **tombstone**
(`ds::HashMap<K, V, Hash>`).

**Hash function.** Integers: Fibonacci multiplication `k × 2654435769` (Knuth),
masked to the power-of-two table size; strings: a mixing hash.

**Rehash policy.** At 70 % load **counting tombstones**. If live entries need the
room, double; if the load is mostly tombstones, rebuild at the **same size**
(prevents unbounded growth under insert/erase churn — a defect fixed in the audit).

**Use.** Sessions (by token hash), username → user, escalation records,
reconciler de-duplication of `(counter, device)` stamps, login throttles,
rate-limit buckets.

**Complexity.** **Expected** O(1) at bounded load; **worst case O(n)** (all keys
collide). Space O(capacity).

**Why not chaining?** No per-node allocation, cache-friendly, right for small
POD keys and values.

---

## 11. Hashed timing wheel

**Definition.** A circular array of slots; a deadline goes into slot
`(expiry tick) mod slots` with its **absolute** expiry tick; advancing one tick
examines only the current slot and fires the entries whose tick has arrived
(Varghese & Lauck, single level) (`ds::TimerWheel<Id>`).

**Use.** `alert::EscalationTracker`: 1-second ticks, 512 slots, deadline = raised
+ 120 s. `Platform::escalate()` advances it every tick.

```
8 slots, 1 s ticks, now = tick 0
schedule #1 @3 s -> slot 3   schedule #3 @11 s -> slot 11 mod 8 = 3 (next lap)
advance to 5 s: visit slots 1..5; slot 3 fires #1 (tick 3 arrived), KEEPS #3 (tick 11)
advance to 12 s: slot 3 again -> fires #3
```

**Complexity** (as implemented, from the header): schedule O(1); cancel O(b), b =
entries in that slot; advance O(D + F + S) — ticks elapsed, timers fired, entries
kept for a later lap; amortised O(1) per timer.

**Why not a heap?** A heap is O(log n) per operation; the wheel answers "what
expired by now" by looking at one slot per tick.

---

## 12. Circular buffer

`ds::CircularBuffer<T, N>`: a fixed array with a `head` index that wraps; pushing
when full overwrites the oldest. **Use:** the last 64 pings per tourist
(`track::Tourist::pings`), read by speed estimation and anomaly detection.
push O(1), index O(1), space O(N) fixed, no allocation.

---

## 13. Road graph (adjacency list)

`graph::RoadGraph` (`src/graph/road_graph.cpp`): `nodes_` (positions) and
`adj_` — for each node a `std::vector<Edge>` of `(to, weight_m)`. Loaded from
`data/osm/roads.txt` (144,090 junctions); every OSM road is stored **both ways**,
so the graph is undirected — which is why one Dijkstra from an incident gives the
distance to every responder. Space O(V + E); neighbours of u in O(deg u).
`RoadGraph::grid()` builds a jittered grid for tests and the Lab.

---

## 14. Merkle log (RFC 6962 hashing)

**Definition.** An append-only list of entries whose SHA-256 leaf hashes are
combined pairwise into a binary tree; the root summarises the whole log
(`evidence::MerkleLog`, `src/evidence/merkle_log.cpp`, own SHA-256 in
`src/evidence/sha256.cpp`). Leaves are hashed as `SHA-256(0x00 || entry)`, nodes as
`SHA-256(0x01 || left || right)` (domain separation).

**Inclusion proof.** The O(log n) sibling hashes on the path from a leaf to the
root (`MerkleLog::prove()`); anyone with the root recomputes it
(`InclusionProof::verify()` in C++, `verifyInclusion()` in the browser).

```
            root
          /      \
       h01        h23
      /   \      /   \
    h0    h1   h2    h3        prove leaf 2: send [h3, h01]
    e0    e1   e2    e3        verifier: h2 = H(0x00||e2); h23 = H(0x01||h2||h3);
                                         root' = H(0x01||h01||h23); compare
```

**Use.** Every platform log entry (`Platform::append_log()`), the separate
location-access audit log, the Investigation "Verify" button.

**Complexity.** append O(1) amortised; prove/verify O(log n); space O(n).

---

## 15. Lamport clocks, offline queue and reconciler

- `sync::LamportClock`: `tick()` = counter + 1; `observe(remote)` = max(own,
  remote) + 1. Order events by `(counter, device)` — a total order consistent with
  causality whatever the wall clocks say.
- `sync::OfflineQueue`: the phone stamps and queues events while offline.
- `sync::Reconciler::merge()`: rejects duplicates with a `ds::HashMap` keyed by
  `(counter, device)` (a retried batch is admitted **once**), absorbs the remote
  counter, and `std::stable_sort`s the timeline by Lamport order. O(1) per event
  plus O(n log n) for the sort.

---

## 16. Sweep-line status tree (Shamos–Hoey)

`geo::ring_self_intersects_sweep()` (`src/geo/sweep_line.cpp`) keeps active
segments in a hand-written **AVL** tree ordered by y at the sweep x; a segment is
tested only against its neighbours. O((n + k) log n) instead of O(n²). Used for
rings of 80+ vertices; the pairwise scan remains the oracle and the small-ring path.

---

## 17. Master complexity table

| Structure | Query / main op | Insert | Delete | Space | Bound type |
|---|---|---|---|---|---|
| Brute force | Θ(n) | O(1) | O(n) | Θ(n) | guaranteed |
| Quadtree | O(log n + k) / O(n) worst | O(log n) exp. | O(n) | O(n) | expected / worst |
| R-tree | O(log n + k) / O(n) worst | O(log n) + split | O(n) | O(n) | expected / worst |
| Geohash | O(log n) per key range + refine | O(n) | O(n) | O(n) | — |
| k-d tree | NN O(log n) / O(n) worst | rebuild | rebuild | O(n) | expected / worst |
| AVL interval tree | O(min(n,(k+1) log n)) | O(log n) | O(log n) | O(n) | guaranteed |
| Persistent quadtree | as quadtree, any version | O(depth) new nodes | O(depth) | O(n + changes·depth) | expected |
| Rollback DSU | find O(log n) | unite O(log n) | rollback O(1)/union | O(n) | guaranteed |
| Binary heap | top O(1) | O(log n) | pop O(log n) | O(n) | guaranteed |
| Hash map | O(1) exp. / O(n) worst | O(1) exp. | O(1) exp. | O(cap) | expected |
| Timing wheel | advance O(D+F+S) | O(1) | O(b) | O(slots + timers) | as implemented |
| Circular buffer | O(1) | O(1) | — | O(N) | guaranteed |
| Merkle log | prove O(log n) | O(1) amort. | — | O(n) | guaranteed |
| Dijkstra (binary heap) | O((V + E) log V) | — | — | O(V + E) | guaranteed |

## 18. Why these and not the obvious alternatives

| Alternative | Why not |
|---|---|
| A plain array of zones | Θ(n) per fix; that is the brute force the project measures against. |
| A hash table for zones | Hashing answers "is key K present?", not "which boxes overlap this box?"; nearby positions do not hash nearby. (The geohash index uses a Morton *sort*, not a hash table, for exactly this reason.) |
| `std::map` / `std::unordered_map` / `std::priority_queue` | Excluded on purpose: the course is about building them. |
| Only an R-tree | Then there is nothing to compare: the quadtree shows *why* item partitioning wins, and brute force shows correctness. |
| Path compression in DSU | Breaks rollback (unbounded writes per find). |
| A heap for escalation | O(log n) per operation; the wheel checks one slot per tick. |
| A full copy per zone version | O(n) per change; path copying is O(depth). |
