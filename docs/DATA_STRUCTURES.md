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
| ✅ built | **Quadtree** | `index/quadtree.hpp` | build O(n log n) · query O(log n + k) avg · **doubling root expansion, subtree collapse on delete** | Primary spatial index; drawn live in the diagnostics overlay |
| ✅ built | **R-tree** | `index/rtree.hpp` | query O(log n + k) avg · insert O(log n) · **STR bulk build O(n log n)** · condensing delete | Second index, quadratic node split + Sort-Tile-Recursive packing — the head-to-head comparison |
| ✅ built | **Brute force** | `index/brute_force.hpp` | O(n) | **Never deleted.** Correctness oracle + speedup denominator |
| ✅ built | **Interval tree** | `ds/interval_tree.hpp` | query O(log n + k) · **delete O(log n) unconditionally** | AVL-balanced, rotation-repairing deletion (not tombstones); zone validity spans (Gap 3). Ordered on the total key **(low, high, value, seq)** so deletion is one descent even when thousands of intervals share a low endpoint — which is the normal case here |
| ✅ built | **Circular buffer** | `ds/circular_buffer.hpp` | push/access O(1) | Fixed-window GPS ping history, bounded memory per tourist |
| ✅ built | **Geohash** | `index/geohash.hpp` | encode O(1) · query O(range + k) | Third index (Morton/Z-order) + offline serialisation format; == brute force |
| ✅ built | **k-d tree** | `index/kd_tree.hpp` | build O(n log n) · NN O(log n) avg | Nearest-responder / nearest-hazard / road-junction snapping; NN & k-NN vs a linear scan **in the tree's own metric** (the local tangent plane). Backs `RoadGraph::nearest_node`, 46× the scan at 10k junctions |
| ✅ built | **Binary heap** | `ds/priority_queue.hpp` | push/pop O(log n) | Min-heap; the Dijkstra/A* frontier, and the alert-triage frontier |
| ✅ built | **Hash table** | `ds/hash_table.hpp` | O(1) expected · rebuild O(n) amortised O(1) | Open-addressed, linear probing, tombstone delete with a **same-size rebuild policy** so churn does not grow the table; vs linear-scan oracle |
| ✅ built | **Timer wheel** | `ds/timer_wheel.hpp` | schedule O(1) · cancel O(b) · **advance O(Δticks + fired)** | Escalation deadlines; single-level hashed wheel vs brute-force due-set oracle. Not flatly "O(1)": `advance` steps tick by tick, so a large jump walks every slot in between — see the worst-case table |
| ✅ built | **Adjacency list** | `graph/road_graph.hpp` | space O(V+E) · neighbours O(deg) | Weighted road graph; loads real OSM roads (`tools/osm_to_roads.py`) or a synthetic grid |
| ✅ built | **Merkle tree** (RFC 6962) | `evidence/merkle_log.hpp` | append O(1) am. · proof O(log n) | Tamper-evident log (Gap 9), SHA-256 from scratch |

## Algorithms on those structures

| Status | Algorithm | Header | Complexity | Purpose |
|---|---|---|---|---|
| ✅ built | Ray casting | `geo/containment.hpp` | O(V) | Point-in-polygon, the fundamental test |
| ✅ built | Winding number | `geo/containment.hpp` | O(V) | Independent 2nd implementation; cross-validates ray casting. Hole winding is normalised, so the two agree whichever way a hole is authored |
| ✅ built | **Local tangent-plane projection** | `geo/projection.hpp` | O(1) | Planar arithmetic in metres instead of degrees; error budget measured, not assumed (`tests/geo/projection_test.cpp` prints it every run) |
| ✅ built | **Shared segment predicates** | `geo/segment.hpp` | O(1) | One orientation / intersection / proper-crossing definition for polygon validation, the sweep, ray casting and jurisdiction nesting |
| ✅ built | **STR bulk loading** | `index/rtree.hpp` | O(n log n) | Sort-Tile-Recursive packing; measured against incremental insertion (section 9 of `make bench`) |
| ✅ built | Self-intersection check | `geo/polygon.hpp` | **O(V log V) at ≥56 vertices**, O(V²) below | Zone validation (Gap 10): outer ring, every hole, holes inside the outer ring, holes not crossing it, holes pairwise disjoint. `validate()` dispatches to the sweep-line below at `kSweepThresholdVertices`; the pairwise version stays as the oracle. Threshold measured (`make bench` §12) |
| ✅ built | Douglas–Peucker | `tools/osm_to_zones.py` | O(n log n) avg | Boundary simplification (in the data-prep tool) |
| ✅ built | **Sweep-line (Shamos–Hoey)** | `geo/sweep_line.hpp` | O(V log V) — AVL status structure | **What `Polygon::validate()` actually calls** for rings of ≥56 vertices, outer and holes alike. Existence, not enumeration — which is all a validity gate needs and is why it is Shamos–Hoey rather than Bentley–Ottmann. 1.6× the pairwise scan at 128 vertices, ~9× at 2048 (8.9–9.1 across runs); verdicts identical on every ring tested |
| ✅ built | **Dijkstra / A*** | `graph/dijkstra.hpp`, `astar.hpp` | O((V+E) log V) | Responder routing. Dijkstra checked vs Floyd–Warshall; A* uses an admissible haversine heuristic, verified to expand ≤ Dijkstra |
| ✅ built | **Kuhn's / Hungarian** | `graph/bipartite_match.hpp` | O(VE) / O(n³) | Responder→incident assignment. Both checked against exhaustive search |

---

## Added by the gap analysis

These have no counterpart in any existing implementation of `SIH25002`. Each
traces to a documented gap — see [GAP_ANALYSIS.md](GAP_ANALYSIS.md).

| Status | Structure / Algorithm | Header | Complexity | Gap |
|---|---|---|---|---|
| ✅ built | **Persistent quadtree** (path copying) | `index/versioned_index.hpp` | mutate O(log n) extra nodes · query-at-time O(log n + k + log h) | **3** — time-travel zone queries. The most advanced structure here |
| ✅ built | **Versioned validity history** | `index/versioned_index.hpp` | append O(1) per change · lookup O(log h) | **3** — geometry AND rules are persistent. A historical query is gated by the rule in force *then*, not by today's |
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
| ✅ built | **QR digital ID** | `evidence/digital_id.hpp` | encode O(log n) · verify O(log n) | **9** — fully offline verification against a cached root; forged/corrupted/wrong-record cases rejected |
| ✅ built | **Polygon nesting hierarchy** | `jurisdiction/hierarchy.hpp` | build O(n²V) · resolve O(depth·V) | **11** — jurisdiction ownership; resolve == smallest-containing oracle |

### Honest count

**Built and tested: 12 core structures** (quadtree, R-tree, brute-force, AVL
interval tree, circular buffer, Merkle tree, binary heap, adjacency-list graph,
geohash/Morton index, k-d tree, open-addressed hash table, hashed timing wheel)
**+ 3 advanced structures** (persistent path-copying quadtree, per-zone versioned
validity history, rollback union-find) **+ 20 algorithms/mechanisms** (ray
casting, winding number, self-intersection and hole validation, Shamos–Hoey
sweep-line, shared segment predicates, local tangent-plane projection,
three-valued containment, signed distance, predictive crossing, STR bulk loading,
spatio-temporal clustering, hysteresis, adaptive sampling, Dijkstra, A*, Kuhn's
matching, Hungarian assignment, Lamport reconciliation, polygon-nesting
resolution, QR digital ID verification) **+ SHA-256** implemented from scratch and
checked against NIST vectors. Every one is exercised by the test suite (**769 assertions across 39 files**), and every fast structure is checked against a
brute-force oracle.

**Nothing in this inventory is `◻ designed`.** Every row above is implemented and
tested.

**What is deliberately NOT built**, so the absence is a decision rather than an
omission:

| Not built | Why |
|---|---|
| `server/http_api.hpp`, `server/ws_stream.hpp` | The project is serverless by design — the engine emits one self-contained HTML replay. `apps/safetrail_server.cpp` exists only to fail loudly if something still references the target. |
| `geo/haversine.hpp`, `geo/predict.hpp` | Header placeholders whose logic lives elsewhere: haversine in `geo/point.cpp`, predictive crossing in `fence/evaluator.cpp`. They carry no code and no `.cpp`. |
| `geo/douglas_peucker.hpp` | Boundary simplification runs in the data-prep tool (`tools/osm_to_zones.py`), not in the engine. |
| `sim/recorder.hpp`, `sync/delta_sync.hpp` | Superseded — trace recording lives in `viz/html_export.cpp`, and offline sync is `sync/lamport.hpp`. |
| Rollback DSU **in the hot path** | Implemented and independently tested, but the runtime rebuilds the DSU each tick because n = 200 makes rebuilding free. See the note in `ds/dynamic_connectivity.hpp` — the structure is real, the speedup claim would not be. |
| Guttman same-level reinsertion on R-tree delete | The simplified condense (flatten orphans, reinsert from the root) is correct and bounded; the measured cost is in section 10 of `make bench`. |
| Grid bucketing for group cohesion | The proximity graph is O(n²). At n = 200 that is ~20k distance calls per tick and is not the bottleneck. |

## Worst case and guarantees (read this before the viva)

The average-case numbers below are the headline, but the honest analytical picture
has to include the worst case — and for the spatial indexes it is not O(log n).

| Structure | Average / typical | **Worst case** | Amortised | Guaranteed? | Assumptions |
|---|---|---|---|---|---|
| Quadtree query | O(log n + k) | **O(n + k)** | — | ✗ — not balanced | spatially spread boxes; k = true hits |
| Quadtree insert | O(log n) | **O(max_depth)** | O(log n) + O(log R) root expansion | ✗ | R = how far outside the root the box lies |
| Quadtree remove | O(n) to find the id | **O(n)** | + O(cap) collapse check per level | ✗ | no id → node map; ids are searched for |
| R-tree query | O(log n + k) | **O(n + k)** | — | ✗ — boxes overlap, several branches may be entered | |
| R-tree build (STR) | O(n log n) | **O(n log n)** | — | ✓ — dominated by the sorts | |
| R-tree remove | O(n) to find | **O(n + m log n)** | — | ✗ | m = entries orphaned by condensing and reinserted |
| AVL interval tree | O(log n + k) | **O(log n + k)** | — | ✓ — AVL-balanced, audited in tests | k = overlapping entries |
| AVL interval tree delete | O(log n) | **O(log n)** | — | ✓ — real rotation-repairing delete | |
| Persistent quadtree query@t | O(log n + k + log h) | **O(n + k + log h)** | — | ✗ query · ✓ sharing bound | h = validity changes for that zone |
| Persistent quadtree mutate | **O(depth)** new nodes | O(max_depth) new nodes | — | ✓ — sharing bound is proven and measured | validity change costs O(1) records, 0 nodes |
| Rollback union-find | O(log n) find | **O(log n)** | O(1) per undone union | ✓ — union by rank, no compression | |
| Binary heap | O(log n) push/pop | **O(log n)** | — | ✓ — complete tree, height ⌊log₂ n⌋ | |
| Dijkstra (lazy-deletion heap) | O((V+E) log V) | **O((V+E) log V)** | — | ✓ | non-negative finite weights, enforced by `add_edge` |
| A* | ≤ Dijkstra | **O((V+E) log V)** | — | ✓ optimality **only if** h is admissible | checkable with `heuristic_is_admissible()` |
| Hungarian assignment | O(n³) | **O(n³)** | — | ✓ — fixed n phases, each O(n²) | rows ≤ cols, all costs finite |
| k-d tree NN | O(log n) | **O(n)** | — | ✗ — not balanced; pathological on clustered data | ties resolve to the lowest id, which costs an extra descent when the query sits exactly on a splitting plane. Minimises the tangent-plane metric, not the great circle — see below |
| Hash table get/put | O(1) | **O(n)** | O(1) | ✗ — probe chains degrade under a bad hash | load ≤ 0.7; tombstones swept by same-size rebuild |
| Timing wheel schedule | O(1) | **O(1)** | — | ✓ | one modulo, one push_back |
| Timing wheel cancel | O(b) | **O(n)** | — | ✗ | b = entries in that deadline's slot; O(n) only if every timer shares one slot |
| Timing wheel advance | O(Δ + F + S) | **O(Δ + n)** | O(1) per timer | ✗ — see below | Δ = ticks elapsed since the last call, F = fired, S = entries held for a later revolution |
| Geohash query | O(log n + s + k) | **O(n)** | — | ✗ — Z-order gaps over-scan; exact after refine | s = keys scanned, set by the largest item half-extent |
| Geohash insert/remove | O(n) (vector shift) | **O(n)** | — | ✗ | remove also recomputes extents, O(n) |
| Ray casting / winding | O(V) | **O(V)** | — | ✓ | V = vertices including holes |
| Polygon validate | O(V log V) outer at ≥56 vertices | **O(V² + H·V·Vₕ + H²·Vₕ²)** | — | ✓ | self-intersection is the sweep above the threshold; the hole-vs-outer and hole-vs-hole terms are still pairwise. H holes; run once per zone at authoring time |
| Sweep-line (Shamos–Hoey) | O(V log V) | **O(V log V)** | — | ✓ — AVL status structure | existence only, not enumeration. k does not appear: it stops at the first crossing |
| Merkle append / prove | O(1) am. / O(log n) | O(log n) | O(1) append | ✓ | `root()` recomputes in O(n) — see below |
| Jurisdiction build | O(n²·V) | O(n²·V·V') | — | ✓ | pairwise containment, V' = outer vertices |
| Jurisdiction resolve | O(depth·V) | **O(n·V)** | — | ✗ | degenerate when everything is a root |

Five rows deserve the fine print spelled out rather than left in a cell.

**The timing wheel is not unconditionally O(1), and saying so is the point.**
The literature's "O(1)" describes `schedule`, and that is exactly true here. What
`advance(now)` does is step one tick at a time — it has to, because the rounds
comparison that lets many expiry ticks share a slot is only correct if every tick
is visited. So advancing an hour on a one-second grid walks 3,600 slots whether or
not anything is in them. Amortised over the TIMERS it is still O(1) each; per
CALL it is not. The caller in this project advances once per simulation tick, so
Δ = 1 every time, and a workload that jumped far ahead sparsely would want the
hierarchical multi-level wheel — which is precisely the argument for that variant.
Writing "O(1) amortised" in the cell and leaving it there would have hidden the
one thing worth knowing about the structure.

**The k-d tree answers "nearest" in the tangent plane.** It minimises the local
east-north metric (`geo/projection.hpp`'s linearisation, anchored at the data
centroid), not the great-circle distance. Over a district those rank candidates
identically almost always; on a 4,096-junction grid they picked different
junctions on 1 probe in 4,000, and the plane's choice was **2 mm** further. That
is far inside GPS noise, so it cannot change an operational outcome — but it does
mean a brute-force oracle written against haversine is not an oracle for this
tree, which is a distinction that cost a real bug before it was noticed. Both
`RoadGraph::nearest_node` and `nearest_node_linear` now use the tree's metric, and
the modelling difference is measured on its own in
`tests/graph/road_graph_io_test.cpp` rather than folded into a correctness
assertion.

**`k` is never hidden.** Every query complexity above that says `+ k` means the
output size, and section 1 of `make bench` reports it in its own column precisely
because it sets the ceiling on any speedup (see below). An index cannot return
fewer results than the query actually has.

**`MerkleLog::root()` is O(n), not O(log n).** The tree is recomputed from the
leaf array on each call rather than cached. Appending is O(1) amortised and a
proof is O(log n), which are the operations the offline verification story depends
on; the root is computed once per publication, so the cost has never mattered.
Calling `root()` in a loop over appends would be quadratic, and nothing does.

**A\* is only optimal when its heuristic is admissible.** With great-circle edge
weights that holds by the triangle inequality. Nothing forces `RoadGraph` weights
to be distances, though — a travel-time or penalty weight can be smaller than the
segment's length, and A\* will then confidently return a suboptimal path.
`graph::heuristic_is_admissible()` checks the property in O(E) and the shortest-path
test runs it before comparing A\* to Dijkstra.

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

`make bench` — 2000 probe queries per row, 450 m query box, Apple clang -O2 on one
laptop. Each cell is the **median of 7 timed passes after a warmup pass**; the CSV
also carries the best run and the run-to-run spread. A representative run:

| zones | brute force | quadtree | R-tree | QT gain | RT gain | candidates | spread |
|---|---|---|---|---|---|---|---|
| 10 | 0.03 us | 0.02 us | 0.02 us | 1.5x | 1.7x | 0.01 | ±122% |
| 100 | 0.09 us | 0.04 us | 0.05 us | 2.3x | 1.9x | 0.10 | ±74% |
| 1,000 | 1.84 us | 0.09 us | 0.09 us | 21.3x | 20.3x | 0.97 | ±66% |
| 5,000 | 11.23 us | 0.32 us | 0.16 us | 35.0x | 72.0x | 4.90 | ±71% |
| 20,000 | 47.81 us | 1.44 us | 0.40 us | 33.3x | 119.5x | 19.86 | ±21% |
| 50,000 | 118.77 us | 3.66 us | 0.66 us | 32.4x | 179.1x | 49.37 | ±10% |
| 100,000 | 242.40 us | 7.28 us | 0.98 us | **33.3x** | **246.8x** | 98.78 | ±5% |

Brute force is visibly linear. **Read the small-n rows with the spread in mind** —
at 10–1,000 zones the times are sub-microsecond and dominated by noise
(±66–122%), so those speedups are not meaningful; the headline is the 100k row,
where the spread is ±5%. The speedup is a ratio to **our own brute force**, not an
external library — there is no third-party baseline here by design.

**The R-tree column changed by a factor of seven in this pass, and the reason is
the most interesting result in the file.** It used to be built by repeated
insertion and tied with the quadtree at ~33x. `build()` now uses STR bulk packing
(section 9), and the same tree answering the same queries went from 6.4 us to
0.98 us. Nothing about the query algorithm changed — only the shape of the tree it
walks.

### Bulk loading: STR vs repeated insertion

| zones | insert build | STR build | insert nodes | STR nodes | insert us/query | STR us/query | query gain |
|---|---|---|---|---|---|---|---|
| 1,000 | 0.2 ms | 0.1 ms | 214 | 155 | 0.17 us | 0.09 us | 1.98x |
| 10,000 | 2.2 ms | 1.4 ms | 2,133 | 1,456 | 1.28 us | 0.30 us | 4.29x |
| 50,000 | 13.0 ms | 7.7 ms | 10,684 | 7,259 | 4.34 us | 0.68 us | 6.40x |
| 100,000 | 28.2 ms | 16.5 ms | 21,368 | 14,383 | 6.42 us | 0.99 us | **6.47x** |

Both are O(n log n). What differs is tree *quality*. Inserting one item at a time
makes every ChooseSubtree decision blind to the items still to come, so early
splits are guesses and node envelopes overlap more than necessary — and overlap is
precisely what forces a query down several branches. STR sorts the whole set by
centre longitude, cuts it into ⌈√P⌉ vertical slices, sorts each slice by centre
latitude, and cuts those into leaves: a near-square tiling with far less overlap.
It also builds *faster* (fewer node splits) and produces a **33% smaller tree**.

This is the clearest "the structure, not the machine" result in the project: same
data, same queries, same hardware, 6.5x from how the tree was assembled.

### Quadtree vs R-tree

With both built by insertion they tied at ~33x. With STR the R-tree pulls decisively
ahead — 246.8x vs 33.3x at 100k zones.

The structural reason is worth stating, because it is the whole point of building
both. The **quadtree partitions space** on fixed subdivisions, so an item
straddling a split settles high in the tree no matter how small it is; nothing can
recover that, and a fixed grid cannot adapt to the data. The **R-tree partitions
items** into tight envelopes, so nothing is forced upward — but envelopes may
overlap, so a query can descend several branches, and how much they overlap
depends entirely on how the tree was built. Insertion order leaves that to chance;
STR does not.

The quadtree keeps two advantages that the timing column does not show: its
subdivision is a fixed function of space, so it can be *drawn* (which is what the
dashboard overlay does, and what caught the root-fitting bug below), and it is the
structure that path-copies cleanly for the persistent index.

### The operational number

In the running simulation on the real dataset — the exact `make dashboard`
configuration, so it is reproducible:

```
./build/safetrail_headless --zones data/zones/shillong_osm.geojson \
    --tourists 60 --hours 2 --synthetic 400

zones loaded: 438  (38 real OSM + 400 synthetic filler)
pruning: 438 zones -> 3.75 candidates per query   (117x reduction)
```

**117×** reflects a realistic query radius against realistic density. The query
radius here is *derived* — position uncertainty plus `speed × prediction horizon`
— not a fixed guess, which is why it is a meaningful number at all (see bug 1
below). The figure moves with zone count, zone size and how fast the population
is walking, so quote it with its configuration or not at all; on a sparser
908-zone run it was ~195×.

### Bugs the measurements and the oracles caught

**1. The index pruned nothing (1x).** The evaluator queried a fixed 20 km box for
zones ~2 km across, so every query returned every zone. The results were still
*correct*, just pointless — without the candidates-returned column this would have
shipped invisibly. Radius is now derived from uncertainty + `speed x horizon`.

**2. The quadtree wasted 11 levels of depth.** It was rooted at the whole planet
(-90..90, -180..180), so with `max_depth = 12` its cells were still kilometres
across where the zones actually were. **The dashboard's index overlay is what
revealed this** — the subdivision lines were visibly enormous. Fitting the root to
the data extent took 100k-zone performance from 14.1x to 32.7x, a 2.3x improvement
from a five-line change. This is the strongest argument in the project for building
the visualisation early.

**3. `validate()` misdiagnosed every bowtie.** It checked zero-area before
self-intersection, and a bowtie's two lobes cancel to exactly zero signed area, so
every self-intersecting polygon was reported as merely degenerate. Caught by
`tests/geo/ray_casting_test.cpp`.

**4. The brute-force oracle was over-counting its own candidates.** `query()` added
`out.size()` — the whole accumulating buffer — instead of what that call appended.
Since brute force is the denominator of every speedup figure and the source of the
candidates column, a measurement bug in the *oracle* silently inflated the numbers
the project reports about itself. This is the one class of bug that a correctness
test cannot catch, because the answers were right; only the counting was wrong.

**5. Ray casting and the winding number disagreed on holes.** The two
implementations exist to cross-validate each other, and on a counter-clockwise hole
inside a counter-clockwise outer ring they returned opposite answers for the hole's
interior: winding summed 1 + 1 = 2 ("non-zero, therefore inside") where parity
correctly said outside. The textbook winding rule requires holes wound opposite to
the shell; most GeoJSON producers do not do that, so this was the common case, not
an exotic one. Found by `tests/geo/polygon_holes_test.cpp` — which is exactly the
job the second implementation was kept for.

**6. The projection and the distance function used different Earth models.** A
first attempt at the local tangent plane used the WGS84 ellipsoidal series while
`geo::distance_m` uses a sphere of mean radius. Both are defensible models; mixing
them is not. They disagree by 0.37% at Shillong's latitude — 2.8 m over a 750 m
segment, comparable to the open-sky GPS accuracy the entire design is built around,
arising from nothing but a mismatch between two files. Caught by
`tests/geo/projection_test.cpp` within minutes of the test existing, which is the
argument for writing the measurement before trusting the model.

**7. The persistent index answered historical queries with today's rules.**
Validity lived in a mutable current-state array, so `query_at(t)` fetched the
correct historical geometry and then filtered it with whatever the rules had since
become. Ask "what was in force at 14:32 yesterday" after an operator edits a
closure this morning and the answer was one that never existed at any point in
time — in the one structure built specifically to answer that question. See below.

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

**What is persistent, precisely.** Both halves of a zone's identity, and the
distinction matters because getting it wrong produced silently invented history:

| | Structure | Cost per change | Lookup |
|---|---|---|---|
| Geometry | path-copied quadtree | O(depth) new nodes | O(log n + k) |
| Validity | per-zone append-only log of (version, Validity) | **one record** | O(log h) binary search |

Validity used to live in a mutable current-state array. The obvious fix —
snapshot the whole validity array per version — would restore correctness while
destroying the point: O(Z) copied state per mutation is exactly the O(n) full copy
that path copying exists to avoid. The append-only log keeps mutation O(1) in
validity and O(log n) in geometry, so the structural-sharing argument survives.

The interval tree over validity windows now holds **every historical** interval,
not just the current one, and `active_at(t)` filters a stab by which record was in
force at the version covering `t` — at most one per zone, so no de-duplication is
needed on top.

**Two time axes, named.** Conflating them is the classic temporal-database bug, so
the code says which is which: *transaction time* (when an operator changed the
rules) selects a version; *valid time* (when a zone is in force) is the
`Validity{from,to}` window. `query_at(t)` uses `t` for both, which is the
incident-investigation question — "what would the system have said at 14:32" — and
the only one that is forensically meaningful. A closure edited at 20:00 does not
apply to a query about 19:26, and `tests/index/versioned_index_test.cpp` asserts
exactly that.

### Churn: what a long-running index actually experiences

Every structure here was originally written as if it were built once and queried
forever. Section 10 of `make bench` holds the live set constant at 2,000 zones and
runs 20 rounds of "insert 2,000, delete 2,000":

| structure | nodes fresh → churned | query time fresh → churned |
|---|---|---|
| quadtree | 557 → 557 (**1.00x**) | 0.14 → 0.13 us (0.88x) |
| R-tree | 299 → 470 (1.57x) | 0.12 → 0.12 us (1.04x) |
| geohash | 2,000 → 2,000 (1.00x) | 0.14 → 0.15 us (1.03x) |
| hash table | 4,096 → 4,096 buckets (**1.00x**) | 461 same-size rebuilds, 8 growths |

Before compaction all four grew without bound: the quadtree kept the subdivision
of its high-water mark, the R-tree kept underfull nodes, the geohash kept a query
padding sized for a deleted outlier, and the hash table doubled to make room for
tombstones. **The R-tree does not return all the way to 1.00x**, and that is
expected rather than a residual bug — condensing reinserts orphaned entries from
the root, so a churned tree is validly shaped but differently grouped. It is
bounded, which is the property that matters.

The geohash's leak was the most visible: deleting the single district-sized zone
from a set of 50 m ones cut the query padding from 0.08° to 0.0005° and the keys
scanned per query from **489 to 110**, because the padding had been sized for an
item that no longer existed.

### Serialisation — GAP 6, measured

| zones | bytes | bytes/zone | write | read | read throughput |
|---|---|---|---|---|---|
| 1,000 | 44,024 | 44.0 | 0.02 ms | 0.00 ms | 11.9 GB/s |
| 10,000 | 440,024 | 44.0 | 0.18 ms | 0.04 ms | 11.4 GB/s |
| 100,000 | 4,400,024 | 44.0 | 1.96 ms | 0.38 ms | 11.1 GB/s |

44 bytes per zone: a 48-bit Morton key, a 32-bit id and four doubles. A whole
district fits in a few hundred kB — which is the actual offline argument, stated as
a number rather than an adjective. The layout is **explicitly little-endian**
(`util/bytes.hpp` assembles values with shifts, not `memcpy` of a native integer),
so the blob is portable across hosts rather than portable across hosts we happened
to test on.

### Other verified results

| Result | Measured |
|---|---|
| Hysteresis A/B (GAP 8) | **92.9% of false transitions removed under realistic correlated drift** (rho=0.9), 93.9% under white noise — measured under BOTH models, same seed |
| Index equivalence | 18,000 queries x 3 densities, quadtree and R-tree both **0 mismatches** vs brute force |
| Ray casting vs winding number | 100,000 points, 200 polygons, **0 disagreements** |
| Index churn | quadtree, R-tree and geohash all agree with brute force at 20 checkpoints across 400 randomised insert/delete operations |
| A* vs Dijkstra | **77.7% fewer nodes settled** at 1,024 nodes, same optimal path; heuristic admissibility asserted before the comparison |
| Dispatch (Hungarian vs greedy) | **15.7% less total travel** at 40 responders; Hungarian never worse than greedy in 200/200 layouts |
| Adaptive sampling (GAP 7) | 28,800 continuous fixes → 257 adaptive, **99.1% battery saved at 100% near-zone recall** |
| Alert correlation (GAP 5) | **Scenario-dependent** — a scripted cohort on one hazard collapses to a single incident of ~33 people (~450:1); a scattered run compresses ~9:1. Both reported; the ratio is a property of incident clustering, not a fixed law |
| Determinism | same seed, two runs: byte-identical event streams, parent trees, dispatch plans and k-d tree answers (`tests/golden/determinism_test.cpp`, `make determinism`) |
| Unit tests | **769 assertions across 39 files**, every fast structure vs a brute-force oracle, all pass; the whole suite is clean under UBSan locally and under ASan+UBSan in CI |

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
