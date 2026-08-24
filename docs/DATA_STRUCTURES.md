# Data Structures Inventory

Every structure in the project, why it is here, and its complexity target. This is
the document to hand an examiner who asks "what did you actually implement?"

**Ground rule:** the *data structures* are hand-written. No `std::unordered_map`,
`std::set`, `std::map`, `std::priority_queue`, no Boost.Geometry, no PostGIS —
those are the things the course is about, so they are ours. Permitted: `std::vector`
and `std::string` as raw storage, `std::shared_ptr` for the persistent index's node
sharing (memory management, not a container), and `std::sort` / `std::min` (an
algorithm and a comparison, not a structure). The line is "did we build the
structure being graded?", and for every structure below the answer is yes.

**Status is marked honestly.** `✅ built` = implemented, exercised, and (where a
structure) unit-tested. `◻ designed` = the header and interface exist with the
approach documented, but the body is a stub — these are the roadmap, not claims of
completed work. Do not read a `◻` row as delivered.

---

## Core — the baseline problem

| Status | Structure | Header | Complexity | Role |
|---|---|---|---|---|
| ✅ built | **Quadtree** | `index/quadtree.hpp` | build O(n log n) · query O(log n + k) avg | Primary spatial index; drawn live in the diagnostics overlay |
| ✅ built | **R-tree** | `index/rtree.hpp` | query O(log n + k) avg · insert O(log n) | Second index, quadratic node split — the head-to-head comparison |
| ✅ built | **Brute force** | `index/brute_force.hpp` | O(n) | **Never deleted.** Correctness oracle + speedup denominator |
| ✅ built | **Interval tree** | `ds/interval_tree.hpp` | query O(log n + k) | AVL-balanced; zone validity spans (Gap 3). Real O(log n) guarantee |
| ✅ built | **Circular buffer** | `ds/circular_buffer.hpp` | push/access O(1) | Fixed-window GPS ping history, bounded memory per tourist |
| ✅ built | **Geohash** | `index/geohash.hpp` | encode O(1) · query O(range + k) | Third index (Morton/Z-order) + offline serialisation format; == brute force |
| ✅ built | **k-d tree** | `index/kd_tree.hpp` | build O(n log n) · NN O(log n) avg | Nearest-responder / nearest-hazard queries; NN & k-NN vs linear-scan oracle |
| ✅ built | **Binary heap** | `ds/priority_queue.hpp` | push/pop O(log n) | Min-heap; the Dijkstra/A* frontier, and the alert-triage frontier |
| ✅ built | **Hash table** | `ds/hash_table.hpp` | O(1) expected | Open-addressed, linear probing, tombstone delete; vs linear-scan oracle |
| ✅ built | **Timer wheel** | `ds/timer_wheel.hpp` | O(1) amortised | Escalation deadlines; single-level hashed wheel vs brute-force due-set oracle |
| ✅ built | **Adjacency list** | `graph/road_graph.hpp` | space O(V+E) · neighbours O(deg) | Weighted road graph; loads real OSM roads (`tools/osm_to_roads.py`) or a synthetic grid |
| ✅ built | **Merkle tree** (RFC 6962) | `evidence/merkle_log.hpp` | append O(1) am. · proof O(log n) | Tamper-evident log (Gap 9), SHA-256 from scratch |

## Algorithms on those structures

| Status | Algorithm | Header | Complexity | Purpose |
|---|---|---|---|---|
| ✅ built | Ray casting | `geo/containment.hpp` | O(V) | Point-in-polygon, the fundamental test |
| ✅ built | Winding number | `geo/containment.hpp` | O(V) | Independent 2nd implementation; cross-validates ray casting |
| ✅ built | Self-intersection check | `geo/polygon.hpp` | O(V²) | Zone validation (Gap 10). Simple pairwise; the sweep-line below is the faster path |
| ✅ built | Douglas–Peucker | `tools/osm_to_zones.py` | O(n log n) avg | Boundary simplification (in the data-prep tool) |
| ✅ built | **Sweep-line (Shamos–Hoey)** | `geo/sweep_line.hpp` | O(n²) status-vector today; O(n log n) with a BST status | Faster self-intersection detection; verdict == `validate()`, verified |
| ✅ built | **Dijkstra / A*** | `graph/dijkstra.hpp`, `astar.hpp` | O((V+E) log V) | Responder routing. Dijkstra checked vs Floyd–Warshall; A* uses an admissible haversine heuristic, verified to expand ≤ Dijkstra |
| ✅ built | **Kuhn's / Hungarian** | `graph/bipartite_match.hpp` | O(VE) / O(n³) | Responder→incident assignment. Both checked against exhaustive search |

---

## Added by the gap analysis

These have no counterpart in any existing implementation of `SIH25002`. Each
traces to a documented gap — see [GAP_ANALYSIS.md](GAP_ANALYSIS.md).

| Status | Structure / Algorithm | Header | Complexity | Gap |
|---|---|---|---|---|
| ✅ built | **Persistent quadtree** (path copying) | `index/versioned_index.hpp` | mutate O(log n) extra nodes · query-at-time O(log n + k) | **3** — time-travel zone queries. The most advanced structure here |
| ✅ built | **Union-Find with rollback** | `ds/dynamic_connectivity.hpp` | find O(log n) · undo O(1) per union | **4** — groups split as well as merge; path compression is unrollbackable |
| ✅ built | **Three-valued containment** (disc vs polygon) | `geo/containment.hpp` | O(V) | **1** — Inside / Outside / Uncertain under GPS uncertainty |
| ✅ built | **Signed distance to boundary** | `geo/containment.hpp` | O(V) | **1,2,7** — feeds uncertainty, prediction, adaptive sampling |
| ✅ built | **Predictive crossing** (project + retest) | `fence/evaluator.cpp` | O(V) | **2** — time-to-boundary alerts |
| ✅ built | **Spatio-temporal DSU clustering** | `alert/correlator.hpp` | O(n α(n)) | **5** — forty alerts collapse into one incident |
| ✅ built | **Hysteresis state machine** | `fence/hysteresis.hpp` | O(1) per observation | **8** — drift suppression, measured under two noise models |
| ✅ built | **Adaptive rate controller** | `power/adaptive_sampler.hpp` | O(1) per tick | **7** — sampling as a function of risk distance |
| ✅ built | **Lamport clocks + reconciler** | `sync/lamport.hpp` | O(1)/event · merge O(n log n) | **6** — offline event ordering; idempotent, deterministic, clock-skew-proof |
| ✅ built | **Index serialisation** | `index/geohash.hpp` | O(n) | **6** — ship the geohash index to the device; round-trips identically |
| ✅ built | **Merkle inclusion + consistency proofs** | `evidence/merkle_log.hpp` | O(log n) | **9** — proves append, not just inclusion. Verified on all prefix pairs |
| ✅ built | **Polygon nesting hierarchy** | `jurisdiction/hierarchy.hpp` | build O(n²V) · resolve O(depth·V) | **11** — jurisdiction ownership; resolve == smallest-containing oracle |

### Honest count

**Built and tested: 12 core structures** (quadtree, R-tree, brute-force, AVL
interval tree, circular buffer, Merkle tree, binary heap, adjacency-list graph,
geohash/Morton index, k-d tree, open-addressed hash table, hashed timing wheel)
**+ 2 advanced structures** (persistent path-copying quadtree, rollback union-find)
**+ 16 algorithms/mechanisms** (ray casting, winding number, self-intersection
check, Shamos–Hoey sweep-line, three-valued containment, signed distance,
predictive crossing, spatio-temporal clustering, hysteresis, adaptive sampling,
Dijkstra, A*, Kuhn's matching, Hungarian assignment, Lamport reconciliation,
polygon-nesting resolution) **+ SHA-256** implemented from scratch and checked
against NIST vectors. Every one is exercised by the test suite (10,924 checks
across 26 files).

**Designed but not yet built (◻): none.** Every structure and algorithm in this
inventory is implemented, exercised, and checked against a brute-force oracle. The
only remaining `TODO(impl)` stubs in the tree are deliberately-unbuilt app scaffolding
(`server/` — the project is serverless by design; see the README) and helpers
whose logic lives in another file (e.g. haversine in `geo/point.cpp`, predictive
crossing in `fence/evaluator.cpp`, Douglas–Peucker in the data-prep tool).

---

## Worst case and guarantees (read this before the viva)

The average-case numbers below are the headline, but the honest analytical picture
has to include the worst case — and for the spatial indexes it is not O(log n).

| Structure | Average | **Worst case** | Guaranteed? |
|---|---|---|---|
| Quadtree | O(log n + k) | **O(n)** | ✗ — not balanced |
| R-tree | O(log n + k) | **O(n)** | ✗ — no height bound |
| AVL interval tree | O(log n + k) | **O(log n + k)** | ✓ — AVL-balanced, checked in tests |
| Persistent quadtree | O(log n + k) | O(n) query · **O(depth) nodes/mutation** | sharing bound is proven |
| Rollback union-find | O(log n) find | **O(log n)** | ✓ — union by rank, no compression |
| Binary heap | O(log n) push/pop | **O(log n)** | ✓ — complete tree, height ⌊log₂ n⌋ |
| Dijkstra (heap) | O((V+E) log V) | **O((V+E) log V)** | ✓ — non-negative weights; lazy-deletion frontier |
| Hungarian assignment | O(n³) | **O(n³)** | ✓ — fixed n phases, each O(n²) |
| k-d tree | O(log n) NN | **O(n)** | ✗ — not balanced; pathological on clustered data, like the quadtree |
| Hash table | O(1) get/put | **O(n)** | ✗ — probe chains degrade under a bad hash; amortised O(1) at ≤0.7 load |
| Timing wheel | O(1) schedule/expire | **O(n) per tick** | ✗ — amortised O(1); a slot can hold many entries |
| Geohash query | O(range + k) | **O(n)** | ✗ — Z-order gaps can over-scan; exact after refine |

**Why the quadtree can degrade to O(n).** It subdivides *space* on a fixed grid,
not *data*. If every zone falls in one small region (which is realistic — hazards
cluster in a valley), they all pile into one branch and a query walks a
linear chain. We mitigate this two ways, and both are worth stating: the tree is
**rooted to the data extent** (not the whole planet), which recovered ~11 levels
of usable depth (see the bug below); and the `max_depth` cap bounds the chain. But
there is no asymptotic guarantee, and an examiner is right to press on it. The
honest answer: *"the quadtree is average-case O(log n + k); its worst case is
O(n) because it partitions space, not data — that is exactly why we also built the
R-tree, and why the one structure on the query path with a real guarantee is the
AVL interval tree."*

**The structures that DO have guarantees** are the ones where we spent the
analytical effort: the interval tree is AVL-balanced (the test asserts
`height ≤ 1.44·log₂(n+2)` on every random trial), and the rollback union-find uses
union-by-rank giving O(log n) finds — path compression was deliberately given up
because it cannot be rolled back (that trade-off is the interesting analysis, not
an oversight).

---

## The headline result — measured, not projected

The design doc originally claimed ~29,000x. Measurement disciplined that number,
and **the two rounds of correction are the most interesting content in the
project.**

### Three indexes, measured

`make bench` — 2000 probe queries per row, 450 m query box, Apple clang -O2:

| zones | brute force | quadtree | R-tree | QT gain | RT gain | candidates |
|---|---|---|---|---|---|---|
| 10 | 0.03 us | 0.04 us | 0.04 us | 0.8x | 0.9x | 0.01 |
| 100 | 0.12 us | 0.07 us | 0.07 us | 1.7x | 1.6x | 0.10 |
| 1,000 | 1.84 us | 0.16 us | 0.25 us | 11.4x | 7.3x | 0.97 |
| 5,000 | 11.23 us | 0.45 us | 0.89 us | 24.9x | 12.6x | 4.90 |
| 20,000 | 47.74 us | 1.51 us | 2.66 us | 31.5x | 17.9x | 19.86 |
| 50,000 | 118.83 us | 3.62 us | 4.58 us | 32.9x | 25.9x | 49.37 |
| 100,000 | 242.23 us | 7.41 us | 7.45 us | **32.7x** | **32.5x** | 98.78 |

Brute force is visibly linear. Both trees converge on ~33x and then stop
improving, for the reason in the last column.

### Why the ceiling is ~33x, not 29,000x

At 100,000 zones over roughly 40 km2, **98.78 zones genuinely intersect each
450 m query box.** Those are true positives — no index can return fewer results
than the query actually has. The achievable speedup is bounded by output size, not
by the tree.

That is exactly what `O(log n + k)` predicts: with `k ~ 99` the `k` term dominates
`log n` completely. The candidates column is *identical* across all three
implementations, which is simultaneously the proof of correctness and the
explanation of the ceiling. The original estimate assumed `k ~ 3`, which requires
sparse zones.

**Stating the zone-density caveat is worth more than quoting a big number.**

### Quadtree vs R-tree

The R-tree led at every size until the quadtree was fixed (see below): 12.6x vs
24.9x at 5,000 zones reversed once the root was fitted. They now tie at 100k.

The structural reason is worth stating: the **quadtree partitions space** on fixed
subdivisions, so an item straddling a split settles high in the tree no matter how
small it is. The **R-tree partitions items** into tight envelopes, so nothing is
forced upward — but envelopes overlap, so a query may descend several branches. At
uniform zone sizes those effects roughly cancel; at irregular extents the R-tree
should pull ahead, which is the next experiment.

### The operational number

In the running simulation on the real dataset — 438 zones (38 real OSM + 400
synthetic filler), query radius derived from position uncertainty plus reachable
distance rather than a fixed guess:

```
pruning: 438 zones -> 2.42 candidates per query   (181x reduction)
```

**181×** reflects a realistic query radius against realistic density. (The figure
scales with zone count and query radius; on a 908-zone run it was 195×.)

### Three bugs the measurements caught

**1. The index pruned nothing (1x).** The evaluator queried a fixed 20 km box for
zones ~2 km across, so every query returned every zone. The results were still
*correct*, just pointless — without the candidates-returned column this would have
shipped invisibly. Radius is now derived from uncertainty + `speed x horizon`.

**2. The quadtree wasted 11 levels of depth.** It was rooted at the whole planet
(-90..90, -180..180), so with `max_depth = 12` its cells were still kilometres
across where the zones actually were. **The dashboard's index overlay is what
revealed this** — the subdivision lines were visibly enormous. Fitting the root to
the data extent took 100k-zone performance from 14.1x to **32.7x**, a 2.3x
improvement from a five-line change. This is the strongest argument in the project
for building the visualisation early.

**3. `validate()` misdiagnosed every bowtie.** It checked zero-area before
self-intersection, and a bowtie's two lobes cancel to exactly zero signed area, so
every self-intersecting polygon was reported as merely degenerate. Caught by
`tests/geo/ray_casting_test.cpp`.

### Persistent index — GAP 3, measured

`make bench` section 5. Path copying against the naive alternative of copying the
whole tree per version:

| versions | nodes allocated | nodes if full copies | sharing | query@past | query@now |
|---|---|---|---|---|---|
| 51 | 523 | 1,903 | 3.6x | 0.10 us | 0.10 us |
| 201 | 2,582 | 14,633 | 5.7x | 0.15 us | 0.17 us |
| 1,001 | 14,025 | 158,257 | 11.3x | 0.34 us | 0.43 us |
| 5,001 | 71,314 | 930,257 | **13.0x** | 1.08 us | 1.56 us |

Two results worth stating separately.

**Sharing improves as history grows** — 3.6x at 51 versions, 13.0x at 5,001. That
is the point of path copying: each mutation allocates O(depth) new nodes and shares
every untouched subtree by refcount, so the marginal cost of keeping another
version falls as the tree gets wider. Retaining 5,000 versions costs 71k nodes
where full copies would cost 930k.

**Querying the past is as cheap as querying the present.** There is no replay, no
log reconstruction, no rebuild — a historical query is the same descent from a
different root pointer. (The past column is sometimes *faster* simply because a
mid-history version holds fewer zones.)

A validity-only change allocates **zero** new nodes, since the geometry is
untouched and the new version shares the entire tree. That case is asserted in
`tests/index/versioned_index_test.cpp`.

**The correctness gate:** the test builds a brute-force index of exactly what was
live at each of 120 historical versions and asserts the persistent index agrees —
1,440 queries, 0 mismatches. Removing a zone provably does not rewrite history.

### Other verified results

| Result | Measured |
|---|---|
| Hysteresis A/B (GAP 8) | **91.2% removed under realistic correlated drift** (rho=0.9), 92.3% under white noise — measured under BOTH models, same seed |
| Index equivalence | 18,000 queries x 3 densities, quadtree and R-tree both **0 mismatches** vs brute force |
| Ray casting vs winding number | 100,000 points, 200 polygons, **0 disagreements** |
| Alert correlation (GAP 5) | 833 operator cards suppressed |
| Unit tests | **10,924 checks across 26 real test files**, all pass |

## Measurements to produce

Each of these is a plot or table in the report. They are the difference between
"we built it" and "here is what it does".

1. **Index scaling** — query time vs zone count, 10 → 100,000, all four
   implementations on one chart. The brute-force line going vertical is the
   picture.
2. **Candidates returned per query** — how hard each index actually prunes.
   More honest than wall-clock, since it is machine-independent.
3. **Ray casting vs winding number** — throughput, plus where they disagree.
4. **Hysteresis A/B** — false alert rate with and without, same replay, same
   injected GPS noise. Gap 8.
5. **Adaptive sampling** — projected battery life and fix count vs fixed-interval
   polling, **at matched alert recall**. Showing the recall trade-off curve is
   the interesting part. Gap 7.
6. **Alert compression ratio** — alerts ingested per incident opened during the
   landslide scenario. Gap 5.
7. **Structural sharing** — nodes allocated vs nodes a full copy per version
   would need. Justifies the persistent index. Gap 3.
8. **A* vs Dijkstra** — node expansions on the same routes.
9. **Serialised index size** — bytes per zone, per format. Determines whether
   offline mode is actually deployable. Gap 6.
10. **Clock skew observed** — max device-vs-server skew during reconciliation.
    Motivates Lamport ordering with real numbers. Gap 6.
