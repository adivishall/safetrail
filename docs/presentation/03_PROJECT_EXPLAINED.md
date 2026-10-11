# SafeTrail explained, for the presenter

Every concept twice: **Simple** is what you say to the teacher in one breath;
**Detailed** is what you need to understand so the follow-up does not stall.
Each section names the files and functions it describes, as they are at commit
`76b63af`. Nothing here is invented; where the project documents say more, they
are linked.

Sources behind this file: [ARCHITECTURE.md](../ARCHITECTURE.md),
[DATA_STRUCTURES.md](../DATA_STRUCTURES.md), [GEOMETRY_EDGE_CASES.md](../GEOMETRY_EDGE_CASES.md),
[TESTING.md](../TESTING.md), [RESULTS.md](../RESULTS.md), [DEFECT_LOG.md](../DEFECT_LOG.md),
and the headers under `include/safetrail/`.

---

## 1. Problem and architecture

### What geofencing means

**Simple.** A geofence is a region on the map with a rule attached. Geofencing
is noticing when a person crosses into or out of such a region. The hard part
is doing it for many regions and many people, continuously, from GPS positions
that are not exact.

**Detailed.** The operation underneath is point-in-polygon, repeated once per
person per zone per fix. With n zones and one fix a second per person, the
naive cost is O(n) box tests plus O(V) exact geometry per fix. SafeTrail's
framing (README, first screen) is "repeated containment under uncertainty": the
index removes the O(n), the geometry layer handles the uncertainty, and the
state machine turns per-fix verdicts into events.

### Person, zone, GPS reading, event

**Simple.** A *zone* is a polygon with a name, a kind (restricted, caution,
advisory), a severity and a validity window. A *person* is a tracked tourist
with a latest fix and a small list of zone states. A *GPS reading* is a position
plus an accuracy radius: a disc, not a point. An *event* is a confirmed change:
ENTER, EXIT or UNCERTAIN (plus APPROACHING and DWELL EXCEEDED).

**Detailed.** The types are in `include/safetrail/fence/zone.hpp` (`Zone`:
`id`, `kind`, `severity`, `shape` as a `geo::Polygon`, `synthetic`, `validity`
as an `index::Validity{from, to}`, per-zone hysteresis margin overrides,
`max_dwell_ms`), `include/safetrail/track/tourist.hpp` (`Tourist`: `last_fix`,
a 64-entry ring of pings for speed and heading, `zone_states`, one `ZoneState`
per zone the person is near or has open state with) and
`include/safetrail/geo/point.hpp` (`UncertainPoint`: `pos`, `accuracy_m`, `t_ms`;
`usable()` is false above 150 m). Events are `fence::Event` in
`include/safetrail/fence/evaluator.hpp` with `kind`, `tourist`, `zone`, `t_ms`,
the signed `depth_m`, the fix's `accuracy_m` and the three-valued `containment`.
There is deliberately no "is currently inside" event: state is not an event.

### How one reading flows through the engine

**Simple.** Reject hopeless fixes; ask the spatial index for the few zones near
the fix; keep only those in force now; run exact geometry on each to get
inside / outside / uncertain; pass that through the hysteresis filter; emit an
event if the confirmed state changed; finally look again at every zone this
person still has open state with, even if the index did not return it.

**Detailed.** `fence::Evaluator::evaluate()` in `src/fence/evaluator.cpp`, in
order (the header lists the same steps):

1. Usability gate: `t.last_fix.usable()`; a fix over 150 m accuracy is counted and dropped.
2. Query radius: `query_radius_m()` = accuracy + speed × prediction horizon (300 s), clamped to [100 m, 10 km].
3. Spatial filter: `index_.query(geo::Bbox::around(pos, radius), candidates)`, then `std::sort` so candidates are processed in zone-id order whatever the index emitted.
4. Temporal filter: `z->validity.active_at(now_ms)`, O(1) per candidate.
5. Exact geometry: `geo::evaluate(z->shape, fix, &sd)` returns the verdict and the signed distance.
6. Hysteresis: `st.hyst.update(raw, sd, now_ms, cfg)` returns the *confirmed* containment.
7. Transition diff: `emit_transition()` pushes an event only if confirmed != previous.
8. Prediction: if outside and moving towards the zone, extrapolate and emit APPROACHING with an ETA.
9. Reconciliation: `reconcile()` visits every `ZoneState` not seen this tick; out of force or deleted → close it (EXIT if it was Inside); still in force but not returned → a certain Outside observation through hysteresis; settled Outside states outside the window are dropped.

### What is implemented and what is simulated

**Simple.** The engine, the data structures, the geometry, the tests and the
benchmark are real code. The geography is real OpenStreetMap data. The *people*
and their GPS errors are simulated.

**Detailed.** Zones: `data/zones/shillong_osm.geojson`, 38 polygons (1
restricted, 19 caution, 18 advisory) fetched from the Overpass API and converted
by `tools/osm_to_zones.py`; the demo pads them with 5,000 synthetic zones that
never raise alerts (`Zone::synthetic`). People: `src/sim/simulator.cpp` with
mobility models (random waypoint, route following, guided groups) and the GPS
error model in `include/safetrail/sim/mobility.hpp`: 4 m accuracy in open sky,
35 m under multipath with probability 0.25, 2% dropouts, and AR(1) temporal
correlation 0.9 so error drifts rather than teleporting. Simulation is chosen
because it supplies ground truth: the hysteresis experiment replays the same
trajectories with and without noise. Say "simulated tourists over real
geography" every time.

---

## 2. Data structures

All four spatial indexes (brute force, quadtree, R-tree and the geohash
extension) implement one interface, `index::SpatialIndex` in
`include/safetrail/index/spatial_index.hpp`: `build`, `insert`, `remove`,
`query(box, out)`, `size`, `stats`. That is what lets the engine, the tests and
the benchmark treat them interchangeably.

### Brute-force baseline

**Simple.** A list of (id, box) scanned from start to end. Slow, obviously
right, and kept forever as the answer key.

**Detailed.** `include/safetrail/index/brute_force.hpp`, `src/index/brute_force.cpp`.
O(n) query, O(1) insert, O(n) remove. It is the oracle for every differential
test and the denominator of every speedup. Its one shared assumption with the
indexes is `Bbox::intersects` (closed intervals: touching counts); a bug there
would move both sides together, which REVIEW_QA names as the first gap to close.

### Quadtree

**Simple.** Split the map into four, and each quarter into four again, until a
cell holds few enough zones. A zone sits in the smallest cell that fully
contains its box. A query only descends into cells that overlap the query box.

**Detailed.** `include/safetrail/index/quadtree.hpp`, `src/index/quadtree.cpp`;
default node capacity 8, maximum depth 12. The placement rule (descend only into
a child that *fully contains* the box, otherwise stay at this node) means each
item lives in exactly one node, so `remove` is unambiguous and `query` cannot
return duplicates; the cost is that a box straddling a split line settles high
in the tree and is tested by every query that enters that node. The root is
fitted to the data plus 2% padding, not to the planet; `expand_root_to_cover`
doubles the root outward when an insert lands outside it, keeping the old root
as one quadrant; `try_collapse` pulls a subtree back into one node on delete.
`check_invariants()` verifies item-in-region, children exactly tiling the parent,
four children or none, and the item count. Bounds: query O(log n + k) expected,
**O(n) worst** (give every zone the same box and nothing separates them);
insert O(depth); remove **O(n)** because there is no id → node map; build
O(n log n) expected.

### R-tree

**Simple.** Instead of cutting space, group the zones themselves into small
bundles, each with a tight bounding envelope; bundle the bundles; and so on up
to a root. Envelopes may overlap, so a query may have to look in more than one.

**Detailed.** `include/safetrail/index/rtree.hpp`, `src/index/rtree.cpp`;
Guttman's R-tree with quadratic split, maximum 8 entries per node, all leaves at
one depth. `build()` is **STR bulk loading** (Sort-Tile-Recursive): sort items by
centre longitude, cut into ⌈√P⌉ vertical slices, sort each slice by latitude,
pack runs of M into leaves, repeat upward. `build_incremental()` keeps the
insertion-built version so the two can be compared: STR gives 14,383 nodes
against 21,368 at 100,000 zones and a 6.46× faster query (RESULTS.md §7,
`bench/results/index_build.csv`). Deletion condenses underfull nodes and
reinserts their entries at leaf level. `check_invariants()` verifies equal leaf
depth, every node box *exactly* the union of its contents, no empty non-root
node, capacity, and the count; minimum fill is deliberately not checked because
STR leaves the last node of a slice underfull. Bounds: query O(log n + k)
expected, **O(n) worst** (every envelope overlapping the query); insert
O(log n) amortised; build O(n log n); remove **O(n)** to find plus reinsertion.

### AVL interval tree

**Simple.** A balanced binary search tree over time windows, where every node
remembers the latest end time in its subtree. Asking "which windows contain
time t" can skip any subtree whose latest end is before t.

**Detailed.** `include/safetrail/ds/interval_tree.hpp`, header-only template.
Nodes are ordered by the total key (low, high, value, seq) so no two live nodes
compare equal, which is what makes `remove` a single O(log n) descent even when
many windows share a start time (every closure that starts at midnight). Real
AVL deletion with rotations, height and `max_high` repair; the two-child case
unlinks the in-order successor *by position* (`erase_min`), because searching for
its triple could remove a different duplicate and leave the order broken (pass-6
defect). `stabbing(t)` and `overlapping(lo, hi)` call `descend()`, which prunes
on `max_high <= low`. **The bound, checked against `descend()`:** finding one
overlap is O(log n); reporting all k is O(min(n, (k + 1) log n)), because the
prune admits a subtree as soon as *any* interval in it reaches past t, so each
reported interval can cost a root-to-leaf path. That is a worst-case guarantee
from the AVL height and never worse than the scan, but it is **not**
O(log n + k); a centred interval tree would give that and was not built. Used
only by `VersionedIndex::active_at(t)`, never on the per-fix path.

### Persistent quadtree (the history index)

**Simple.** A quadtree where changing something does not overwrite the old tree:
the one path from the root to the changed cell is copied, everything else is
shared, and every old version can still be queried. So you can ask what the
zones and rules were at 00:45.

**Detailed.** `include/safetrail/index/versioned_index.hpp`,
`src/index/versioned_index.cpp`. Nodes are immutable; children are
`shared_ptr<const Node>`. `add_zone` copies the root-to-leaf path (~15 nodes on
a 5,000-zone index) and shares the other three children at every level;
`remove_zone` follows the single path the insert rule put the zone on, guided by
the stored box (`live_box_`), so it is O(depth) too; `update_validity` allocates
**zero** nodes because validity is a per-zone append-only log of
`(version, Validity, present)` records, looked up by binary search. Two time
axes are kept apart: *transaction time* (when the operator changed the rules)
selects the version via `version_at(t)`, a binary search over commit times kept
monotone; *valid time* is the zone's own window. `query_at(t, box)` filters
spatially on the version current at t, then checks each candidate's validity *as
of that version*. An earlier version filtered historical geometry with today's
rules; the replay oracle caught it. `check_invariants()` audits every retained
version. Measured: 5,001 versions allocate 71,314 nodes against 930,257 for full
copies, 13.0× sharing (RESULTS.md §9, `bench/results/versioned_index.csv`); a
past query costs about what a present one does. Honest caveat: the bound is on
nodes; each copied node also copies its item list, so bytes grow with the
straddling boxes on that path.

### Why several indexes, and the trade-offs

**Simple.** Brute force is the answer key; the quadtree and R-tree are two
different ways to be fast, and building both is what makes the difference
measurable; the interval tree and the persistent quadtree answer questions about
time that a spatial index cannot.

**Detailed.**

| Structure | Partitions | Strength | Weakness | On the per-fix path? |
|---|---|---|---|---|
| Brute force | nothing | cannot be wrong; oracle and baseline | O(n) | yes, as one of the drop-in indexes |
| Quadtree | space (disjoint cells) | simple; no duplicates; path-copies cleanly | straddling boxes pile up near the root; O(n) worst | yes (the default) |
| R-tree (STR) | items (overlapping envelopes) | tight envelopes, fewer nodes visited; fastest past a few thousand zones | overlap can send a query down several branches; O(n) worst; slower insert | yes |
| AVL interval tree | time | guaranteed bounds; selective queries 15–64× over a scan | loses to a scan when hundreds of windows contain every instant (0.73× at 1,000 dense windows) | **no**: validity is checked in O(1) per candidate |
| Persistent quadtree | space + version | history at O(depth) nodes per change | bytes per change depend on straddlers; removal needs the stored box | no: built at load, read by the dashboard's history panel |

Measured head to head at 100,000 zones in one district: quadtree 33.7×, R-tree
224× over brute force, identical candidates (RESULTS.md §1).

---

## 3. Geometry

### Bounding boxes and candidate pruning

**Simple.** Every zone has a rectangle around it; the index only stores
rectangles. A query asks for every rectangle that touches the query rectangle,
and the expensive polygon test runs only on those.

**Detailed.** `include/safetrail/geo/bbox.hpp`: `Bbox{min_lat, min_lon, max_lat,
max_lon}`, `intersects` with closed intervals. The query box is
`Bbox::around(centre, radius)` in `src/geo/bbox.cpp`: the exact bounding box of
a spherical cap on the same 6,371,008.8 m sphere that `distance_m` uses, half-width
r/R in latitude, `asin(sin(r/R) / cos(lat))` in longitude (a great circle bulges
poleward), the full longitude range when the cap reaches a pole, plus a 1e-9
relative pad. This replaced WGS84 metres-per-degree constants that made the box
0.11% too narrow east-west, which could drop a zone the exact test would have
called Uncertain: a correctness bug in filter-then-refine. Pinned by
`tests/geo/bbox_around_test.cpp` on 160,000 sampled points.

### Point-in-polygon

**Simple.** Shoot a ray from the point due east and count how many edges it
crosses. Odd means inside. Holes count too.

**Detailed.** `contains()` in `src/geo/containment.cpp`: ray casting with the
half-open rule on the y-comparison (`(y1 > py) != (y2 > py)`) so a ray through a
vertex is counted once and horizontal edges never count; all rings, outer and
holes, feed one parity; on-boundary counts as inside. `contains_winding()` is an
independent winding-number implementation, orientation-normalised for holes,
kept permanently as a cross-check (100,000 points, 0 disagreements, `make bench`
§6); the one time they disagreed it was a real hole-orientation bug. O(V) each.

### Boundary tolerance

**Simple.** Computed points are never exactly on a line, so "on the edge" needs
a tolerance, and it must be a distance (about 0.1 mm), not something that
depends on how long the edge is.

**Detailed.** `src/geo/segment.cpp` holds the one orientation / intersection /
on-edge definition every caller shares (containment, validation, the sweep,
jurisdiction nesting). The on-edge test is `|cross| <= 1e-9° × |edge|`, i.e. a
perpendicular distance of ~0.1 mm; the earlier absolute cross-product threshold
made the boundary ~11 cm thick on a 1 m edge (pass-6 defect, pinned in
`tests/geo/ray_casting_test.cpp`). These are tolerance-based predicates, not
exact (adaptive-precision) ones; a configuration within ~1e-14 deg² of collinear
is decided by the tolerance.

### Self-intersection validation

**Simple.** A figure-eight polygon has no well-defined inside, so such zones are
refused when loaded, using a sweep-line check.

**Detailed.** `Polygon::validate()` in `src/geo/polygon.cpp` with Shamos–Hoey in
`src/geo/sweep_line.cpp`, O(V log V) over a hand-written AVL status tree, used at
≥ 80 vertices; the O(V²) pairwise scan is kept as the oracle and used below that
(crossover measured in `make bench` §16). Shamos–Hoey answers "does any crossing
exist" and stops at the first, which is all a validity gate needs. Ring
validation is all degeneracies (adjacent edges share a vertex and are exempt),
and the sweep used to test only the first non-exempt neighbour on each side:
fuzzing 800,000 lattice rings found 94 it called simple that the oracle did not.
It now tests the nearest three eligible segments per side and orders same-x
events totally. `tests/geo/sweep_line_test.cpp` pins the failing rings and
fuzzes 12,000 more per run. Holes are validated too: outside the shell, crossing
the shell, or touching anything is refused.

### Uncertain GPS readings

**Simple.** A fix is a disc. If the whole disc is inside, the person is Inside;
if the whole disc is outside, Outside; if the disc straddles the boundary, the
honest answer is Uncertain.

**Detailed.** `geo::evaluate(poly, uncertain_point, &sd)` in
`src/geo/containment.cpp` computes `signed_distance_m()` (negative inside) via
the nearest edge in a local metre plane (`include/safetrail/geo/projection.hpp`,
because degree-space projection is 11% skewed at Shillong's latitude), then
`classify(sd, accuracy)`: Inside/Outside only when |sd| > accuracy, else
Uncertain. That static `classify()` is the single definition of three-valued
containment; the evaluator used to re-derive it with its own thresholds and was
stopped from doing so (`tests/geo/containment_uncertainty_test.cpp` asserts the
layers agree).

### Longitude and geographic edge cases

**Simple.** Distances and bearings handle the ±180° seam automatically; zones
that cross it are refused at load; the poles make the query box cover every
longitude.

**Detailed.** `distance_m` and `bearing_deg` take longitude deltas only through
periodic functions, so wrap needs no special case; `offset()` and `LocalPlane`
normalise the longitude they *produce* to (−180, 180]. The loader rejects
non-finite or out-of-domain coordinates, so a ring stored as lon 179.5 → 180.5
is refused with a reason; query boxes do not wrap, so a fix at 179.99° cannot
see a zone at −179.99° (stated limitation). The indexes themselves are correct
on out-of-domain boxes (root widening, geohash clamping), checked by the
"extremes" profile of `tests/index/differential_test.cpp`. All in
[GEOMETRY_EDGE_CASES.md](../GEOMETRY_EDGE_CASES.md).

---

## 4. Event processing

### ENTER and EXIT transitions

**Simple.** The engine never says "this person is inside"; it says "this person
just became inside" or "just left". Events are changes of a *confirmed* state.

**Detailed.** `emit_transition()` in `src/fence/evaluator.cpp`: if the confirmed
containment equals the stored one, nothing; Inside → `ZoneEnter` (records
`entered_ms`); from Inside to anything else → `ZoneExit`; to Uncertain →
`ZoneUncertain`. `DwellExceeded` fires once if a zone has `max_dwell_ms` and the
stay exceeds it; `ZoneApproaching` is step 8's predicted crossing with an ETA,
emitted once per approach when the extrapolated position is inside.

### GPS gaps (and the bug they exposed)

**Simple.** If a person's phone goes quiet and the next fix is far away, the
index no longer returns the zone they were in. The old code never looked at that
zone again, so the exit was missed. Now every zone a person has state with is
checked every fix.

**Detailed.** Step 9, `reconcile()`. For each `ZoneState` whose `seen_epoch` is
not this tick's: if the zone is deleted or out of force, close the state with a
`ZoneExit` if it was Inside (a rule change is not noise); otherwise feed a
certain Outside through hysteresis with the box distance as the signed distance
(it is at least the query radius, which exceeds the 25 m exit margin, so it reads
as a clear exit but still needs three agreeing fixes). States settled to Outside
outside the window are dropped, which bounds `zone_states` by the neighbourhood
rather than by every zone ever visited (RESULTS.md §18: 0.08–1.42 zones
reconciled per fix). Probe before the fix: a 60-zone walk gave 60 entries and 0
exits. Pinned by `tests/fence/state_reconciliation_test.cpp` and two mutants.

### Hysteresis and noise filtering

**Simple.** GPS drift near a boundary would otherwise produce enter/exit pairs
all day. Three cheap rules stop it: you must be clearly inside (15 m) to enter
and clearly outside (25 m) to leave; three fixes in a row must agree; and a
brief clip through a corner (under 5 s) is ignored.

**Detailed.** `HysteresisState::update()` in `src/fence/hysteresis.cpp`, config in
`include/safetrail/fence/hysteresis.hpp` (`enter_margin_m` 15, `exit_margin_m`
25, `confirm_samples` 3, `min_dwell_ms` 5000; per-zone overrides allowed). Phases:
Outside, Ambiguous, EnteringPending, Inside, ExitingPending. The asymmetric
margins are a Schmitt trigger: the dead band between −15 m and +25 m is where
drift lives harmlessly. An Uncertain verdict is reported but never acted on: it
cannot end Inside, it *cancels* a pending exit, it restarts a pending entry's
agreement count but keeps its dwell clock, and from Outside it moves to
Ambiguous, which clears only the way Inside does (three clearly-outside fixes).
Before pass 6, Uncertain passed straight through: an exit/enter flap pair could
leak during a pending exit, and a 4 m / 35 m accuracy alternation emitted a
fresh UNCERTAIN every time (150 in five minutes for a stationary person). That
leak was about 60% of the demo's enter/exit events (2,440 → 987 entries). Pinned
by `tests/fence/hysteresis_test.cpp` and two mutants.

### Why filtering improves event quality

**Simple.** Without it the operator sees thirty times as many transitions, almost
all false. With it, under realistic drift, 96% of the real transitions survive.

**Detailed.** The review's step 6: 602 entries with the filter against 17,906
without, on identical trajectories and noise. `make bench` §10
(`bench/results/hysteresis_ab.csv`) scores against ground truth: four runs on
identical trajectories (the GPS model draws the same random numbers at any
sigma): noise-free filter off (1,934 raw crossings), noise-free filter on (the
*target*, 521), noisy filter off (20,848 white / 16,168 drift), noisy filter on
(343 white / 502 drift). Filtered-vs-target is the check that matters: 96.4%
under realistic drift, 65.8% under white noise, which is the real cost and is
reported. The earlier experiment compared only off vs on and could not tell a
removed flap from a missed crossing (pass-6 measurement defect); the regression
test `tests/golden/hysteresis_ab_test.cpp` now gates on the target.

### When a zone is removed or its validity changes

**Simple.** A zone that is switched off or deleted is closed at once for
everyone inside it, with an EXIT; when it comes back it starts clean. The
history index remembers both the old and the new rule.

**Detailed.** In `evaluate()` a candidate failing `validity.active_at(now)` is
skipped; `reconcile()` then finds its open state (not seen this tick) and closes
it, counting `states_closed`. In the `VersionedIndex`, `update_validity`
appends one record and shares the whole tree; `remove_zone` path-copies one
root-to-node path and appends a `present = false` record; `history_for` and
`changes_between` expose the audit trail. The dashboard's "zone change log" and
"rules in force as of" panels read this (Wards Lake: `active_from_s` 1800 to
`active_to_s` 5400, a spillway discharge window).

---

## 5. Testing

### Differential testing against brute force

**Simple.** Run the same queries through the fast index and the slow scan and
demand identical answers, on inputs chosen to break spatial indexes.

**Detailed.** `tests/index/differential_test.cpp`: five configurations (quadtree;
quadtree capacity 1 / depth 30 to force deep trees; R-tree; R-tree fan-out 4;
geohash) against `BruteForceIndex` over seven profiles: uniform, clustered
(identical and nested boxes), degenerate (zero-area, lattice-aligned, point
queries on box edges), mixed-scale, hemispheres, extremes (poles, antimeridian,
past 180), far-inserts (root expansion). Each runs bulk build, inserts, removal
of present *and absent* ids, rebuilds, drain-to-empty, checking sorted
duplicate-free results, `size()`, `remove()`'s return value and the structural
audit after every operation. 55,440 queries per configuration (the test prints
it); `make stress` runs 10×. `tests/index/equivalence_test.cpp` and
`tests/index/churn_test.cpp` cover the same contract from other angles.

### Invariant checks

**Simple.** Each tree can audit itself: is every item inside its node, are the
children where they should be, is the balance right. The tests run that audit
after every single operation, because a slightly broken tree often still
answers small queries correctly.

**Detailed.** `check_invariants()` on `Quadtree`, `RTree`, `IntervalTree` and
`VersionedIndex` (every retained version). The quadtree audit gained the tiling
invariant after the mutation check showed a root expansion that put the old root
in the wrong quadrant survived every test. Table in [TESTING.md](../TESTING.md) §2.

### Randomized and hostile workloads

**Simple.** Random inputs find bugs you did not think of; hostile inputs find
the ones you suspected.

**Detailed.** Seven profiles above; `tests/index/persistence_differential_test.cpp`
(25 seeds × 160 operations against a replay oracle holding a full table per
version); `tests/fence/index_independence_test.cpp` (the whole simulation plus a
hostile evaluator workload with concave zones, holes, mid-run validity changes,
teleports and silences under all four indexes, events compared with `memcmp`);
geometry fuzz (ray casting vs winding number; 12,000 lattice rings for the
sweep; 160,000 points for the query box). Seeds are fixed, so failures reproduce.

### Regression tests

**Simple.** Every real bug that was found has a test that fails on the old code,
so it cannot come back unnoticed.

**Detailed.** [TESTING.md](../TESTING.md) §5 lists each pass-6 defect and the
test: `tests/fence/state_reconciliation_test.cpp` (9 of 18 checks fail on the
old code), `tests/fence/hysteresis_test.cpp` (7 of 13), `tests/geo/bbox_around_test.cpp`
(7 of 10), `tests/geo/ray_casting_test.cpp` (1 of 35), `tests/geo/sweep_line_test.cpp`
(9 of 56), `tests/ds/interval_tree_test.cpp` (7 of 185), `tests/fence/zone_roundtrip_test.cpp`
(7 of 37), `tests/golden/hysteresis_ab_test.cpp` (2 of 11). Where a test now uses
API the old code lacks, a mutant that reverts the fix stands in for it.

### Mutation testing

**Simple.** Deliberately break the code in 22 realistic ways, one at a time, and
check that some test fails each time. If a bug survives, the tests were not
testing.

**Detailed.** `tools/mutation_check.py`, `make mutation`, about 90 s locally and a
CI job. Mutants include: quadtree query skips one child; R-tree insert does not
grow the node box; interval tree `max_high` ignores the right subtree; persistent
index filters history with today's validity; evaluator never reconciles;
hysteresis lets an Uncertain fix complete an exit; ray casting ignores holes;
query box without the 1/cos(lat) stretch. Result 22/22. Two earlier survivors
were real findings (the quadtree audit gap; a redundant duplicate-vertex screen).
Each write happens in a later wall-clock second than the last build because
macOS's make 3.81 compares mtimes to the second. What it does not prove: the
mutants are hand-picked, so nothing is said about faults unlike them.

### Determinism tests

**Simple.** Same input, same output, byte for byte, every time, on this
platform. That is what makes A/B comparisons and bug reproduction possible.

**Detailed.** `make determinism` runs the same seed twice and `cmp`s;
`tests/golden/determinism_test.cpp` and the other golden tests; `make validate`
and CI require the Make build (−O2) and the CMake Release build (−O3) to agree
byte for byte, which `-ffp-contract=off` guarantees (fused multiply-adds would
otherwise differ by a last bit between optimisation levels, and a last bit flips
threshold comparisons). Also: fixed-seed PRNG, explicit tie-breaks in every
ordered structure, candidates in zone-id order. Not gated across operating
systems: libm's `asin`/`sin`/`cos` differ in the last ulp between macOS and Linux
(the one comparison matched in the evaluation core, differed in dispatch travel
totals).

### Sanitizers and static analysis

**Simple.** Extra compilers' checks for memory errors and undefined behaviour,
and a static analyser over the code; all clean.

**Detailed.** `make ubsan` (whole suite under UBSan on macOS; ASan hangs on
macOS 26 with Apple clang 17, a platform issue, so `make asan` is authoritative
in Linux CI with `-fno-sanitize-recover`); `make analyze` (Clang Static
Analyzer, 0 findings); `-Werror` with the shared flag list in `tools/build/`
on clang and on g++ 13 in CI. UBSan found three of the loader's double→integer
conversions. These do not prove absence of bugs; they remove whole classes of
them from consideration.

---

## 6. Benchmarking

### How the benchmark is measured

**Simple.** Each index runs the same 2,000 queries many times; the median of
eleven rounds is reported; the indexes take turns within each round so a slow
moment on the laptop hits all of them equally; and the speedup is measured in
pairs, back to back.

**Detailed.** `apps/safetrail_bench.cpp`, `make bench`, writes
`bench/results/*.csv` and `bench/results/environment.txt` (date, commit,
compiler, flags, OS, CPU, cores, power source, load). Query boxes are built
before timing. A sample runs the probe set until it lasts ≥ 20 ms (calibrated
after an untimed warm-up); 11 rounds with contender order rotated per round;
median and IQR reported; a speedup is the median over rounds of baseline ÷
contender measured back to back. Every contender returns a checksum of its
results (ids mixed before summing, so {1, 4} ≠ {2, 3}) and the run fails on a
mismatch. `make bench-variation` repeats sections 1–3 three times into
`bench/results/variation/run1/`, `run2/`, `run3/` for the ranges.
`tools/render_results.py` renders every table in the docs and the slides from the
CSVs, and `--check` fails CI if a table disagrees with them.

### What is in the timed operation

**Simple.** Only the index lookup: given a query box, return the ids of zones
whose boxes overlap it. Not the polygon test, not the event logic.

**Detailed.** Sections 1, 2 and 7 time `SpatialIndex::query` alone. Section 3
("end to end") times the question the engine actually asks, "which zones contain
this point", four ways: naive (ray-cast every polygon), bounding-box scan plus
exact test, quadtree plus exact test, R-tree plus exact test. Section 18 times
the whole `Evaluator::evaluate()` per fix including reconciliation (4.88 µs at
5,038 zones in one district; 5.52 µs at 50,038 at constant density).

### What each benchmark compares

**Simple.** Fast index against my own brute force, same data, same queries,
same answers. Never against an external library.

**Detailed.** §1 scaling in one district (k grows with n); §2 constant density
(k ≈ 1); §3 end to end against naive and against a bbox scan; §4 build time,
memory, insert, remove; §5 equivalence (18,000 queries, 0 mismatches); §6 ray
casting vs winding number; §7 STR vs insertion build; §8 interval tree vs linear
scan, selective and dense; §9 persistence; §10 hysteresis A/B against ground
truth; §11–17 extensions; §18 the evaluator per fix.

### Why result distributions and spatial density matter

**Simple.** How much an index helps depends on how many zones genuinely
overlap each query. In a crowded district that number grows with the zone
count; spread the zones out and it stays near one.

**Detailed.** In §1 the district is fixed (39 × 35 km) so k ≈ n/1000: 0.10 at
100 zones, 98.45 at 100,000. In §2 the area grows with n so k ≈ 1 throughout.
Same code, two very different curves: quadtree 33.7× vs 543×, R-tree 224× vs
1042× at 100,000 zones. Both are O(log n + k) behaving as predicted.

### Why dense candidate sets limit speedups

**Simple.** No index can return fewer answers than there are. If 98 zones
overlap the query, all 98 must be found and returned, however clever the tree.

**Detailed.** The k term is output size and is reported in its own column of
every scaling table. It is why the quadtree's ratio stops rising after about
5,000 zones in §1 (39.6× at 5,000, 33.7× at 100,000) while brute force keeps
getting slower linearly. The R-tree keeps gaining because STR packing reaches
the same k results through fewer nodes. The same ceiling appears in §8 for the
interval tree (hundreds of windows containing every instant) and in §18 for the
evaluator (µs per fix tracks candidates per fix).

### Why measurements from one laptop are not universal

**Simple.** Absolute times belong to one machine on one day. Ratios travel
better, but even they moved by more than two times between sessions on this
laptop, so I report ranges and a floor.

**Detailed.** RESULTS.md "Read this first": brute force over 100,000 zones
measured 239–244 µs in three sessions and 460–560 µs in another, on battery and
mains alike, for reasons not established. The R-tree's 100,000-zone ratio has
been 102–114× (28 Sep), 97–229×, 206×/236×/200× (clean-clone runs), 223–235×
(1 Oct) and 211–224× (9 Oct); the quadtree's 26–35×. Within a session the
spread is small (worst IQR 4% on the 100,000 row). Hence: ranges, the session's
environment printed next to the table, and ~100× as the floor. Candidate
counts, node counts and event counts are deterministic and identical between
the Make and CMake builds.

---

## 7. Limitations and future work

**Simple.** It is a single-process, single-threaded engine on simulated people
over real geography, with expected-case fast paths that have an O(n) worst
case, tolerance-based geometry, and no load test. It is not production-ready and
I do not call it that.

**Detailed.**

- **Worst-case complexity.** Quadtree and R-tree queries are O(n) on adversarial
  data (identical or heavily overlapping boxes); removal is O(n) in every index
  (no id → node map; RESULTS.md §4 measures it: 31–61 µs at 100,000 zones). The
  interval tree's reporting bound is O(min(n, (k + 1) log n)), not O(log n + k).
- **No production-scale load test.** Every number is a single-threaded per-query
  or per-fix cost on one laptop. Throughput for thousands of concurrent users is
  reasoned about ([REVIEW_QA.md](../REVIEW_QA.md), "How would you scale this"),
  not demonstrated.
- **Single-threaded.** Tourists are independent within a tick and the index is
  read-only during one, so `evaluate_all` is the natural place to parallelise;
  it has not been done, and the `mutable` query counters would need moving off
  the hot path and events merged in id order to keep determinism.
- **Simulated people.** Real OpenStreetMap zones; synthetic movement and a GPS
  error model; no field data, no real device, no transport.
- **Geometry.** Tolerances, not exact predicates; antimeridian-spanning zones
  refused; a GPS jump inflates the speed estimate and so the query radius (up to
  the 10 km clamp) for about a minute, collapsing pruning for that person
  (correct, slow).
- **Before production** ([ARCHITECTURE.md](../ARCHITECTURE.md), "What would
  change"): id → node maps and incremental updates; a robust speed estimator;
  parallel evaluation sharded by region; a spatial partition of the zone set so
  no single index holds everything; exact predicates for authored geometry; a
  durable event log; real ingest with per-person sequencing for late and
  out-of-order fixes; and, honestly, a mature spatial library as the starting
  point, with this project's oracle and differential tests kept around it.

---

## Glossary

| Term | Meaning here |
|---|---|
| **Fix** | one GPS reading: position, accuracy radius (metres), timestamp |
| **Accuracy disc** | the circle of radius *accuracy* around the fix; the true position is assumed inside it |
| **Zone** | a hazard polygon with kind, severity, validity window and rules |
| **Validity (window)** | the half-open time interval [from, to) in which a zone is in force |
| **Candidate** | a zone whose bounding box overlaps the query box; what the index returns |
| **Filter then refine** | cheap conservative test first (boxes), exact test only on survivors (polygon) |
| **Conservative filter** | a filter that may return extra zones but never drops one the exact test could accept |
| **k** | the number of zones genuinely overlapping a query; the output size; the ceiling on any speedup |
| **Three-valued containment** | Inside / Outside / Uncertain, decided by comparing the signed boundary distance with the accuracy radius |
| **Signed distance** | distance from the fix to the nearest zone edge, negative inside |
| **Hysteresis** | the Schmitt-trigger state machine: asymmetric margins, N agreeing fixes, minimum dwell |
| **Flap** | a spurious enter/exit pair caused by GPS drift at a boundary |
| **Reconciliation** | step 9: observing every zone a person has open state with, whether or not the index returned it |
| **Oracle** | an independent, simple implementation whose answer is taken as correct (brute force for the indexes) |
| **Differential test** | running two implementations on the same input and demanding identical output |
| **Invariant audit** | a structure's self-check of its own structural rules (`check_invariants()`) |
| **Mutation testing** | injecting a known bug and requiring a test to fail; a test of the tests |
| **STR** | Sort-Tile-Recursive bulk loading of an R-tree: sort, slice, sort, pack |
| **Path copying** | making a new version of a tree by copying only the root-to-leaf path that changed and sharing the rest |
| **Structural sharing** | the unchanged subtrees shared between versions; measured as full-copy nodes ÷ nodes actually allocated |
| **Transaction time / valid time** | when a rule was changed by an operator / when a zone is actually in force |
| **Stabbing query** | "which intervals contain instant t" |
| **Paired ratio** | baseline time ÷ contender time measured back to back in the same round, then the median over rounds |
| **IQR** | interquartile range of the samples, reported as a percentage of the median |
| **Determinism** | same seed → byte-identical output |
| **UBSan / ASan** | UndefinedBehaviorSanitizer / AddressSanitizer, compiler instrumentation that aborts on UB or memory errors |
| **Synthetic zone** | a generated polygon used only to grow the index for scaling; never raises alerts |
| **Multipath** | GPS error from reflected signals in steep or dense terrain; modelled as 35 m accuracy on 25% of fixes |
