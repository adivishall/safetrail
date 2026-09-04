# Design Defense — Answers to the Hard Questions

Prep for the review. These are the questions a data-structures examiner is most
likely to ask, and the honest, precise answer to each. If you can explain the
five "load-bearing" ones from memory, the sophistication of the project becomes an
asset instead of a liability.

The rule for the room: **never oversell.** Where something is unbuilt, say so.
The project's credibility is built on honesty (see the "what is deliberately NOT
built" table in [DATA_STRUCTURES.md](DATA_STRUCTURES.md)) — protect it.

---

## The five load-bearing explanations

If you internalise nothing else, internalise these. They are the "smart" parts,
and they are what will be probed.

### 1. Why the speedup ceiling is ~33×, not thousands

> The index turns an O(n) scan into O(log n + **k**), where k is the number of
> zones the query actually overlaps. At 100,000 dense zones, ~99 of them genuinely
> intersect each query box — those are correct answers, and no index can return
> fewer results than exist. So the speedup is bounded by output size k, not by the
> tree. That's why all three indexes report the *same* candidate count: it's a
> property of the data, not the structure. The naive early estimate assumed k≈3,
> which needs sparse zones.

The point to land: *stating this caveat is more rigorous than quoting a big
number.*

### 2. Why the quadtree's worst case is O(n)

> The quadtree partitions **space** on a fixed grid, not **data**. If every zone
> falls in one region — which is realistic; hazards cluster in a valley — they all
> pile into one branch and a query walks a linear chain. Average case is
> O(log n + k); worst case is O(n). We root the tree to the data extent and cap
> its depth to mitigate, but there's no asymptotic guarantee. The one structure on
> the query path that *does* guarantee O(log n) is the AVL interval tree.

Do not claim the quadtree is O(log n) worst case. It isn't, and admitting it is
the stronger answer.

### 3. Why the rollback union-find can't use path compression

> Path compression rewrites an unbounded number of parent pointers on every
> `find`. To support rollback we keep an undo stack of every write a union made —
> but we can't undo an unbounded, unrecorded set of compression writes. So we drop
> compression and keep union-by-rank alone, which still gives O(log n) finds and
> O(1) rollback per union. That trade — giving up near-constant find to gain
> undo — is the actual design decision, not an oversight. Groups split as well as
> merge, and plain compressed DSU cannot un-merge.

### 4. How the persistent (time-travel) index works — and what exactly is persistent

> It's a **path-copying** quadtree. Nodes are immutable and children are
> `shared_ptr`. A mutation copies only the O(depth) nodes on the root-to-leaf path
> and *shares* every untouched subtree with the previous version by reference
> count. So we keep the whole history for O(log n) extra nodes per change instead
> of an O(n) full copy — measured at 13× sharing across 5,000 versions. Querying
> the past is just a descent from an older root pointer; it costs the same as the
> present, no replay.

### 5. Why "transitions, not states" is the core design decision

> The evaluator emits an event only when a tourist's confirmed containment
> *changes* — Enter, Exit — never "still inside" every tick. Reporting state every
> tick is what floods existing dashboards into an unreadable wall. Diffing against
> the previous tick is also where the hard bugs live: GPS jitter makes a tourist
> flicker across a boundary, which is exactly what the hysteresis filter absorbs
> before the diff runs.

---

## The structure-by-structure drill

An examiner picks one and goes to bedrock. These are the answers.

### Quadtree

**Why average O(log n + k)?** Each level splits the region into four; on data
spread across the extent, the number of nodes whose region intersects a small
query box shrinks geometrically with depth, so the descent is O(log n) and the
rest is the k results you must return anyway.

**Why O(n) worst case?** It partitions *space*, not *data*. Cluster every zone in
one small valley — which is realistic, hazards do cluster — and they all pile into
one branch; a query walks a linear chain. Give every zone the *same* bounding box
and the tree degenerates to a single node, because an item descends only into a
child that fully contains it, so nothing separates them. There is no balance
invariant to appeal to. That is exactly why we also built the R-tree, and why the
one structure on the query path with a real guarantee is the AVL interval tree.

**What happens on a late insert outside the root?** The root **doubles**, keeping
the old root as one quadrant of the new one. The naive alternative — widen the
root rectangle in place — keeps queries *correct* (the root still contains
everything) but stops the tree being a quadtree: the four children no longer tile
their parent, so a large region is covered by no child and everything landing
there piles up in the root's own item list. Pruning degrades silently toward a
linear scan. Doubling costs O(log R) expansions and needs no rebuild, because
depth is a property of the path and is not stored on the node.

**What happens on delete?** The item is removed and, on the way back up, any node
whose whole subtree now fits in one node collapses — the exact inverse of the
split rule. Without it, a tree that had held 1,000 items keeps that shape after
900 are deleted. Measured: 309 nodes → 33.

### R-tree

**Why an R-tree as well?** It partitions *items* into tight envelopes, so nothing
is forced upward by a split it happens to straddle. The cost is that envelopes may
overlap, so a query can descend several branches — the opposite trade to the
quadtree's.

**How are nodes split?** Guttman's quadratic split: pick as seeds the two entries
whose combined bounding box wastes the most area (the pair most worth separating),
then assign the rest to whichever group they enlarge least, honouring the minimum
fill so you do not end up with 1-entry nodes.

**What happens on deletion?** Condense. Remove the entry, then unwind: any node
now below minimum fill is detached from its parent and its entries are collected
and reinserted from the root; the root collapses if it is left with one child.
**Our simplification, stated:** Guttman reinserts an orphaned *node* at its
original level, preserving both the height balance and the grouping work already
done. We flatten orphans to leaf entries and reinsert them individually — simpler,
still correct, and it costs O(m log n) instead of O(1) relinks per underflow. The
consequence is visible in the churn benchmark: the node count comes back down but
not all the way to a fresh build's (1.57×), because a churned tree is validly
shaped but differently grouped.

**How does bulk loading differ?** Incremental insertion makes every ChooseSubtree
decision blind to the items still to come, so early splits are guesses and
envelopes overlap more than necessary. **STR** (Sort-Tile-Recursive) knows the
whole set: sort by centre longitude, cut into ⌈√P⌉ vertical slices, sort each
slice by centre latitude, cut into leaves of M — a near-square tiling. Then repeat
over those leaves to build the level above. Both are O(n log n); STR produces a
33% smaller tree and **6.5× faster queries** on the same data with the same query
code. That is the cleanest "the structure, not the machine" result in the project.

### k-d tree

**Why not just scan?** Dispatch asks "which responders are closest to this
incident" and the graph asks "snap this GPS fix to a junction" — both are
nearest-neighbour over points, and a scan is O(V) per query. Building the cost
matrix snaps every responder and every incident, so it is not a one-off.

**What is the worst case?** O(n). It is not balanced; the median split keeps it
balanced *for the build set*, but a query in a pathological configuration can be
forced to visit everything, and there is no rebalancing on insert (we rebuild
instead).

**Why is the distance metric valid?** Degrees are not isotropic — a degree of
longitude is cos(lat) times shorter. We scale longitude by cos(mean latitude)
captured at build time, which makes the space locally metric, so comparisons and
pruning are valid and the result matches a great-circle nearest to far below GPS
noise across a district.

**Why does the pruning test use `<=` and not `<`?** Because the tie-break promise
("equal distances resolve to the lowest id") is otherwise unkeepable. With strict
`<`, the far subtree is pruned exactly when the splitting plane is as far as the
current best — which is precisely when it holds an equally near point. The answer
would still be *a* correct nearest neighbour, just a traversal-order-dependent
one. The extra descent only happens when the query lies exactly on a splitting
plane.

### Persistent index

**What exactly is persistent?** Both halves of a zone's identity — and this is
worth being precise about, because an earlier version of the code got it wrong.
Geometry is a path-copied quadtree: O(depth) new nodes per mutation, everything
else shared by refcount. Validity is a **per-zone append-only log** of
`(version, Validity)` records; lookup at a version is a binary search.

**What gets copied, what gets shared?** An insert copies only the nodes on the
root-to-leaf path; the three children it did not descend into are `shared_ptr`
copies — a refcount bump, not a deep copy. A validity-only change copies **nothing**:
the new version shares the entire tree and appends one record. Measured 13×
sharing at 5,001 versions.

**How do historical validity rules work?** `query_at(t)` picks the version covering
`t`, walks that root, and gates each candidate by the validity record in force *at
that version*. Before this fix, validity lived in a mutable current-state array, so
a historical query fetched the right geometry and filtered it with today's rules —
the one question the structure exists to answer, returning something that had never
been true.

**Why not just snapshot the validity array per version?** Because that is O(Z)
copied state per mutation: exactly the O(n) full copy that path copying exists to
avoid. It would trade the whole structural-sharing argument for correctness you
can get for O(1).

**Name the two time axes.** *Transaction time* — when an operator changed the
rules — selects a version. *Valid time* — when a zone is in force — is the
`Validity{from, to}` window. Conflating them is the classic temporal-database bug.
An edit made at 20:00 does not apply to a query about 19:26, even if 19:26 falls
inside the window the edit created.

### Rollback DSU

**Why rollback?** Groups split as readily as they merge, and plain DSU cannot
un-merge.

**Why no path compression?** Compression writes an unbounded number of parent
pointers per `find`, and an undo stack can only record a bounded number of writes
per operation. Giving up near-constant `find` for O(log n) buys O(1) undo — that
trade *is* the interesting analysis.

**Where is rollback actually used?** **Nowhere in the runtime pipeline** — say this
plainly. The cohesion monitor rebuilds the DSU each tick and the correlator builds
a fresh one per batch; neither calls `snapshot()`/`rollback_to()`. At n = 200 the
O(n²) proximity scan dominates everything the DSU does, so rollback would optimise
the part that is already free. It is an implemented, independently tested
data structure — not a component of the hot path, and claiming otherwise would be
inventing a speedup.

### Hungarian

**Why is greedy not optimal?** Greedy commits the globally cheapest pair first,
which can strand a later incident with only a distant responder left. Classic
counterexample: costs [[1, 2], [1, 100]]. Greedy takes (0,0) at 1, forcing (1,1)
at 100, total 101. The optimum is (0,1) + (1,0) = 3.

**What does O(n³) mean here?** n augmenting phases, each doing O(n²) work
maintaining the dual potentials and finding the minimum slack column. Measured
against greedy: 16% less total travel at 40 responders, and never worse in 200/200
random layouts.

**What breaks on bad input?** A ragged matrix used to read past the end of a short
row, and a NaN entry poisons the potentials so every comparison is false and the
result is arbitrary but well-formed. Both are now refused with a typed status.

### Geometry

**What happens at the boundary?** On-boundary is **defined** as inside, and the
predicate is shared with polygon validation and the sweep so all three agree to
the last bit. Ray casting uses the half-open crossing rule `(y1 > py) != (y2 > py)`,
which handles a ray through a vertex (each vertex counts for exactly one of its
two edges) and horizontal collinear edges (they never qualify) in one comparison.

**What happens with holes?** Hole crossings feed the **same** parity count —
counting them separately and subtracting breaks on nested holes. Metrics are
region-aware: area subtracts holes, the centroid is the region's (so a ring-shaped
zone's label does not sit in the hole, i.e. outside itself), and the perimeter
includes hole boundaries because crossing one takes you out of the zone. Hole
*winding* is normalised away, because most GeoJSON producers get it wrong — and
that is not hypothetical: the winding-number implementation disagreed with ray
casting on exactly that case until it was fixed, which is what the second
implementation is kept for.

**What happens with concave polygons?** Ray casting handles them natively — that is
its advantage over any convexity-assuming test. Where concavity bites is
*containment of a region in a region*: "every inner vertex is inside" is not
sufficient, because a bar across the mouth of a C-shaped district has both ends in
the arms and its middle outside. Jurisdiction nesting therefore requires all
vertices inside **and** no edge properly crossing.

**What happens with GPS uncertainty?** Containment is three-valued. Resolve to
Inside or Outside only when the entire disc of radius `accuracy_m` falls on one
side of the boundary; otherwise the honest answer is Uncertain, and the operator UI
renders it as a distinct third state.

### Determinism

**What guarantees reproducibility?** Explicit tie-breaks everywhere a container
does not promise one. A binary heap is not stable, `std::nth_element` guarantees
nothing among equal elements, and `std::sort` is not stable — so Dijkstra's and
A\*'s frontiers order on `(distance, node)`, the k-d tree builds on
`(axis value, id)` and queries on `(distance, id)`, the Hungarian scan takes the
first minimum, and equal-cost parent selection prefers the lower node id (guarded
against zero-weight edges, which could otherwise make two nodes each other's
parent and send path reconstruction round a cycle forever). Plus a deterministic
PRNG (xorshift, ours) rather than `std::mt19937`, so runs are identical across
standard-library versions. `make determinism` runs the binary twice and `cmp`s the
output; `tests/golden/determinism_test.cpp` checks the parent *trees* match, not
merely the distances.

### Benchmarks

**What is the oracle?** `BruteForceIndex` — never deleted. It serves two permanent
jobs: every other index is asserted to return *exactly* its results on randomised
input, and it is the denominator of every speedup figure. Without it a benchmark
can compare a correct slow thing against a fast wrong thing.

**Why median, and why multiple runs?** A single pass is one sample of a noisy
process. We take a warmup pass (faults in caches, settles the branch predictor),
then the median of 7 timed passes — robust to a single scheduler hiccup — and
report the best run and the spread, so the reader can see how much to trust the
figure. At 100k the spread is ±5%; at 10 zones it is ±122% and the number means
nothing, which is why those rows are flagged rather than quoted.

**Why does the speedup plateau?** Because it is bounded by output size, not by the
tree. See explanation 1.

**One number in the benchmark was itself wrong, and it is worth admitting.** The
brute-force oracle added the whole accumulating output buffer to its candidate
counter instead of what each call appended, inflating the denominator of every
speedup figure. Correctness tests could never have caught it — the *answers* were
right, only the counting was wrong.

## Questions about scope and depth

**"Is this a data structures project or a systems project?"**
> Both surfaces exist, but the graded core is the data structures. The one I'll
> defend in depth is the **persistent path-copying quadtree**: it has a proven
> sharing bound, a measured 13× result, an equivalence test against brute force at
> 120 historical versions, and no equivalent in any existing implementation. The
> simulator, dashboard, and CI are the harness that makes those structures
> measurable and visible — they're how we *prove* the structures work, not the
> deliverable itself.

**"You have 49 modules but half are stubs."**
> Correct, and marked as such — 21 implemented, the rest are designed interfaces
> on the roadmap. We deliberately built the analytically interesting structures
> fully (the two trees, the persistent index, the rollback DSU, the interval tree)
> and left the routing/dispatch stack as designed stubs, because Dijkstra and
> Hungarian matching are well-understood and add breadth, not depth. The status
> marks in the docs never claim a stub as done.

**"Which structure did you understand most deeply?"**
> Pick one and go deep — recommend the persistent quadtree (explanation #4) or the
> rollback DSU (#3). Have the complexity analysis and the one key trade-off ready.

---

## Questions about the data and results

**"Where's the real human tracking data?"**
> There is none, by design. The *geography* is real (OpenStreetMap, fetched via
> Overpass — Wards Lake, Sonapani Waterfall Cliff, real reservoirs). The
> *tourists* are simulated with a random-waypoint mobility model, because no real
> tourist-tracking dataset exists and simulation gives us **ground truth**: we know
> where each tourist truly was, so we can measure whether the engine got the right
> answer. A recording of real GPS couldn't tell us that. See
> [DATA_PROVENANCE.md](DATA_PROVENANCE.md).

**"Your GPS noise is unrealistic — real error drifts, it doesn't jump."**
> We model both. The error is an **AR(1) process** (`correlation` = 0.9 by
> default), so consecutive fixes are temporally correlated — smooth drift, like a
> real receiver — not independent white noise. The hysteresis benchmark reports
> against *both* regimes: 92.3% of false transitions removed under white noise,
> **91.2% under realistic correlated drift**. The filter is robust to the honest,
> harder case, not just the flattering one.

**"The dashboard — is it live?"**
> No, it's a deterministic **replay** of a recorded run: 720 frames captured every
> 10 simulated seconds, serialised into one static HTML file. That's deliberate —
> it needs no server and works offline, which mirrors the whole project's thesis
> (no cloud dependency). The engine itself runs live; the dashboard is a recording
> of one run for inspection.

**"Prove the numbers aren't hardcoded."**
> Every dashboard field traces to an engine getter (no literals in the exporter);
> two different seeds give different results; the same seed gives a byte-identical
> file; and the accuracy values are exactly {4, 35, 999} — the three regimes the
> GPS model emits. Commands to verify all of this are in
> [DATA_PROVENANCE.md](DATA_PROVENANCE.md) §4.

---

## Questions about the "hand-written" rule

**"You use `std::sort` and `std::shared_ptr` — isn't that cheating?"**
> The rule is about the *data structures being graded* — no `std::map`, `std::set`,
> `std::unordered_map`, `std::priority_queue`, no Boost.Geometry, no PostGIS. Those
> are ours. `std::sort` is an algorithm, not a structure; `std::shared_ptr` is
> memory management that the persistent index's node-sharing needs. We hand-wrote
> the quadtree, R-tree, interval tree, union-find, circular buffer, and the
> persistent structure — the things the course is about.

**"Why hand-write a heap but not use one from the STL?"**
> The binary heap is currently a designed stub (◻) — the alert triage path that
> would use it isn't built yet. When it is, it'll be hand-written for the same
> reason as the rest. We didn't substitute `std::priority_queue` for it.

---

## Weaknesses to volunteer before they're found

Stating these first reads as rigor, not weakness:

- **Quadtree/R-tree have no worst-case guarantee** (O(n) on clustered data). The
  interval tree does (AVL).
- **~15 modules are designed stubs**, not built — routing, dispatch, offline sync,
  Merkle log. Marked ◻ throughout.
- **The predictive path uses straight-line extrapolation**, which is fiction
  beyond a few minutes in hill terrain — capped at a 5-minute horizon for that
  reason.
- **The tourists are simulated.** Real geography, simulated people.
- **The self-intersection check is O(V²)**, not the O((n+k) log n) Bentley–Ottmann
  sweep line (which is a ◻ upgrade). Fine at authoring time, once per zone.

---

## The one-sentence framing

> We built the geofencing engine that every competing implementation imports from
> PostGIS — the quadtree, the R-tree, the persistent time-travel index, the
> containment geometry — by hand, analysed it honestly including where it degrades,
> measured it against real geography and a realistic noise model, and marked
> exactly what is built versus planned.
