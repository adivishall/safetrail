# Project review — questions and answers

Answers from the code as it is. Where a number comes from a benchmark it is
quoted as the range measured on this laptop, and the section of
[RESULTS.md](RESULTS.md) that holds the table is named so you can show it.
`make review` reproduces the counts; `make bench` the timings.

The ten questions marked ★ are the hardest; rehearse those out loud.

---

## Fundamentals

**What problem are you solving?**
Repeated containment under uncertainty. Up to 100,000 hazard-zone polygons,
each with rules and a validity window; a stream of GPS fixes, one per person per
second, each with an accuracy radius. For every fix, decide inside / outside /
uncertain for every zone near it, and emit an event only when that changes:
ENTER, EXIT, UNCERTAIN. The engine is `fence::Evaluator`; the data structures
exist to make that decision fast without changing it.

**Why is brute force slow?**
It compares the query box with every zone box — O(n) per fix, then exact
geometry on the few that overlap. In the review scenario that is 141,130 queries
× 5,038 zones = 711 million box tests in one simulated hour; at 100,000 zones one
query costs about 240 µs on this laptop (RESULTS.md §1). Multiply by people and
seconds and it does not scale. It is kept anyway, as the oracle (§Correctness).

**What is a quadtree?**
A tree that recursively splits *space* into four quadrants. Here an item descends
into a child only if that child's region fully contains its box, otherwise it
stays at the current node, so every item lives in exactly one node and a query
cannot return duplicates. A query descends only into quadrants that overlap the
query box. Ours fits the root to the data (not the planet), doubles the root
outward when an insert lands outside it, and collapses a subtree back into one
node on delete. `include/safetrail/index/quadtree.hpp`.

**What is an R-tree?**
A balanced tree that groups *items* into tight bounding envelopes, leaves all at
one depth. Envelopes may overlap, so a query may descend several branches.
Insertion uses Guttman's quadratic split; bulk loading uses STR — sort by
longitude, cut into vertical slices, sort each by latitude, pack runs of M —
which gives a smaller tree with less overlap. Deletion condenses underfull nodes
and reinserts their entries. `include/safetrail/index/rtree.hpp`.

**Why did you implement both? What is the difference?** ★
They make opposite trade-offs, and only building both makes that measurable.
The quadtree partitions space: disjoint cells, simple, and it path-copies
cleanly — it is what the persistent index is built on. Its weakness is a box
that straddles a split line: it stays high in the tree, and every query that
enters that node tests it. The R-tree partitions items: nothing is forced
upward, but envelopes overlap. On identical data they return identical
candidates (they must), and the STR-packed R-tree reaches them through fewer
nodes: at 100,000 dense zones it is 100–235× faster than brute force depending
on the session, the quadtree 27–34× (RESULTS.md §1). STR alone is worth ~6×
over building the same R-tree by insertion (§7).

## Complexity

**What is the complexity?**

| Operation | Bound | Guaranteed? |
|---|---|---|
| brute-force query | O(n) | yes |
| quadtree / R-tree query | O(log n + k) expected | no — O(n) worst case |
| index removal (all three) | O(n) | yes (no id → node map) |
| interval tree: find one overlap / insert / remove | O(log n) | yes (AVL) |
| interval tree: report all k overlaps | O(min(n, (k + 1) log n)) | yes — not O(log n + k) |
| persistent mutation | O(depth) new nodes | yes, by construction |
| exact point-in-polygon | O(V) | yes |
| polygon validation (Shamos–Hoey) | O(V log V) | yes |

**Is O(log n + k) guaranteed?** ★
No. For the spatial trees it is the expected cost on spread-out data. The
quadtree cannot separate identical boxes, and straddling boxes pile up at
internal nodes, so a query can test O(n) boxes while k is small. The R-tree is
height-balanced, but overlapping sibling envelopes can send a query down every
branch. The structure with a worst-case guarantee is the AVL interval tree — and
there the honest bound for reporting k results is O(min(n, (k + 1) log n)),
because the max-high prune admits a subtree as soon as *any* interval in it
reaches past the query, so each result can cost a root-to-leaf path. An earlier
version of the docs said O(log n + k), which is a centred interval tree's bound,
not this traversal's; that was corrected.

**What happens in the worst case?**
Every zone with the same bounding box: the quadtree keeps them all in one node
and a query tests all n; the R-tree's envelopes all overlap and a query visits
every leaf. The differential tests include exactly this profile ("clustered":
many identical and nested boxes) to make sure the answer is still right when the
speed is gone.

**Why does performance depend on k?** ★
k is the number of zones that genuinely overlap the query, and every index must
return all of them — no structure can return fewer answers than exist. In one
crowded district k grows with n: about 98 zones overlap each 450 m query at
100,000 zones, so the quadtree's ratio stops growing after a few thousand zones
(RESULTS.md §1). Hold the density constant so k ≈ 1 and the same code is
hundreds of times faster at 100,000 zones (§2). The index removes the O(n) scan;
it cannot remove the O(k) answer.

## Correctness

**How did you prove the indexes are correct?** ★
"Proved" is too strong; the evidence is differential testing against an oracle.
`tests/index/differential_test.cpp` compares five configurations (quadtree,
quadtree at capacity 1 / depth 30, R-tree, R-tree with fan-out 4, geohash)
with brute force over seven workload profiles chosen for where spatial indexes
break — identical boxes, zero-area boxes, edges and corners exactly on query
boundaries, both hemispheres, the poles, past longitude 180, inserts hundreds of
km from the build area — through builds, inserts, removals of present *and*
absent ids, rebuilds and drain-to-empty, checking results, size and the return
value of `remove()` after every operation. About 55,000 queries per
configuration; `make stress` runs ten times as many. On top of that, every
operation is followed by `check_invariants()`, and the whole engine must emit
bit-identical events under all four indexes (`tests/fence/index_independence_test.cpp`).

**Why is brute force your oracle?**
Because it is a linear scan too simple to get wrong, and it implements exactly
the same contract as every index: return each id whose box intersects the query
box, touching included. Its one known weakness is that it shares
`Bbox::intersects` with the indexes, so a bug in that predicate would change both
sides identically; that predicate has no direct test of its own, and it is the
gap I would close first.

**What is mutation testing?** ★
A check on the tests. `tools/mutation_check.py` applies one realistic bug at a
time to a copy of the tree — the quadtree query skips a child, the R-tree insert
does not grow the node box, the interval tree's `max_high` ignores the right
subtree, the persistent index filters history with today's rules, the evaluator
never reconciles zones outside the candidate window, hysteresis lets an
Uncertain fix complete an exit — rebuilds, and requires one of the named tests
to fail. 22 mutants, 22 killed (`make mutation`). It found two gaps in the tests
before it reached 22/22: the quadtree audit could not tell a quadtree from an
arbitrary tree of boxes, and a duplicate-vertex screen in the sweep line was
redundant. What it does not prove: the mutants are hand-picked, so 22/22 says
those 22 faults are caught and nothing about faults unlike them.

**What bugs did you actually find?** ★
Ten, in code that was passing its tests ([DEFECT_LOG.md](DEFECT_LOG.md), pass 6).
The three to tell:

1. **The state machine lost zones that left the candidate window.** The
   evaluator only advanced a (person, zone) state for zones the index returned
   this fix. A GPS gap or a car ride that moved someone out of the zone's query
   window in one step meant no EXIT, state stuck at Inside, and a later real
   re-entry was silent. A walk across 60 zones produced 60 entries and zero
   exits. Fix: every open state is observed every fix; a zone not returned by a
   conservative filter is a certain Outside, still fed through hysteresis.
2. **Hysteresis let Uncertain through.** An Uncertain fix during a pending exit
   was reported as Uncertain, which the evaluator turned into an EXIT, and the
   next inside fix re-entered. Plus a fresh UNCERTAIN event on every 4 m / 35 m
   accuracy alternation — 150 in five minutes for one person standing still.
   About 60% of the demo's enter/exit events were these flaps: entries fell from
   2,440 to 987 after the fix.
3. **The index query box was 0.11% too narrow east–west.** It used a WGS84
   metres-per-degree constant while the geometry measures on a sphere, so a zone
   just inside the accuracy radius due east could be dropped by the filter. That
   is a correctness bug in filter-then-refine, and it mattered twice because the
   reconciliation step treats "not returned" as "outside". Fix: the exact
   bounding box of a spherical cap; 1,728 of 160,000 sampled points fell outside
   the old box.

Also: a boundary tolerance that made edges 11 cm thick on a 1 m edge; the sweep
line calling 94 of 800,000 degenerate rings simple; interval-tree deletion
breaking its own ordering on duplicate keys; the persistent index copying 96
nodes to remove one zone and trusting commit times to be ordered; undefined
double→integer conversions in the GeoJSON loader; and, in the measurement, a
benchmark headline from 2 ms timing windows that could not be reproduced.

## Geometry

**How does point-in-polygon work?**
Ray casting: cast a ray from the point and count edge crossings; odd means
inside. The half-open crossing rule means a ray through a vertex is counted
once, and holes are rings whose crossings flip the same parity. On the boundary
counts as inside. A second, independent implementation — the winding number —
is kept and cross-checked against it (100,000 points, 0 disagreements, `make
bench` §6); the one time they disagreed it was a real bug in hole orientation.

**What happens on boundaries?** ★
Two different things. Geometrically, a point within ~0.1 mm of an edge is on
the boundary and counts as inside. Operationally, a position is a disc, not a
point: `classify()` returns Inside or Outside only when the whole accuracy disc
is on one side of the boundary (signed distance beyond the accuracy radius) and
Uncertain otherwise. Uncertain is reported but never acted on: it cannot end an
Inside state, it cancels a pending exit, and from Outside it moves to a phase
that clears only the way Inside does. The review's step 5 shows one person
sitting in exactly that band.

**Why do you use tolerances?**
Because computed points are not exactly on lines: an edge midpoint or an
interpolated vertex has a cross product of about 1e-16 relative, not zero.
Without a tolerance "on the boundary" is a coin flip, and ray casting and
winding number disagree on it. The on-edge tolerance is a perpendicular
*distance* (1e-9°, about 0.1 mm) so it means the same on every edge — it used to
be a cross-product threshold, which made short edges 11 cm thick. Every caller
shares `src/geo/segment.cpp`, so validation, containment and the sweep cannot
disagree about where an edge is. Exact predicates would remove the tolerances;
they are not implemented.

**Why can floating-point arithmetic affect reproducibility?**
Two reasons the build guards against. The compiler may contract `a*b + c` into a
fused multiply-add, which rounds once instead of twice, and whether it does
depends on the compiler and the optimisation level — so `-ffp-contract=off` is
in every build, and `make validate` requires the Make build (`-O2`) and the
CMake Release build (`-O3`) to produce byte-identical output. And `libm`
differs between platforms in the last ulp of `sin`/`cos`/`asin`: the one
macOS-vs-Linux comparison matched in the evaluation core and differed in the
last digits of the dispatch travel totals. Determinism is gated per platform,
not across them.

## Persistent data structures

**What is path copying?**
Nodes are immutable; a change creates new copies of the nodes on the one
root-to-leaf path it touches and points them at the unchanged siblings, which
are shared by `shared_ptr`. The old root still sees the old tree; the new root
sees the new one. Removal follows the single path the insert rule put the zone
on (guided by the zone's stored box), so it is O(depth) too.

**Why does it save memory?** ★
Copying the whole tree per version is O(zones) per change. Path copying is
O(depth) new nodes — about 15 on a 5,000-zone index — and a rule change copies
nothing at all, because validity is an append-only log per zone rather than
part of the tree. Measured: 5,001 versions cost 71,314 nodes against 930,257 if
full-copied, 13× sharing; a query against a past version costs what one against
the present does (RESULTS.md §9). The honest caveat: the bound is on nodes;
each copied node also copies its item list, so bytes grow with the straddling
boxes on that path.

**What is shared between versions?**
Every subtree the change did not touch — three of the four children at every
level of the copied path — and the whole tree when only a rule changed. The
dashboard's persistent-index panel draws consecutive versions with the copied
cells highlighted and the shared ones dim. The thing to keep straight is the two
time axes: *transaction time* (when the operator changed the rules) selects the
version; *valid time* (the zone's validity window) says when a rule is in
force. `query_at(t)` uses t for both; an earlier version filtered historical
geometry with today's validity, which the replay oracle caught.

## Engineering

**Why C++?**
The data structures were the subject, and C++ gives direct control of memory
layout and ownership — `unique_ptr` children in the trees, `shared_ptr<const
Node>` where sharing *is* the structure, index-backed nodes with a free list in
the interval tree — plus the performance to make a 100,000-zone benchmark
meaningful on a laptop. C++17, no dependencies beyond the compiler, so the
project builds with `make` on macOS and Linux.

**Why no external spatial library?**
Because then the library would be the thing being measured, not my structures.
Building them is what made it possible to state their invariants, test them
against an oracle, and show their failure modes. In production I would start
from a mature library — and keep the oracle and the differential tests around it.

**How did you benchmark?** ★
Query boxes are built before timing, so a query time is index traversal only.
One sample runs the whole probe set until it lasts at least 20 ms; 11 rounds,
each contender taking one sample per round in a rotated order, so machine drift
lands on every contender rather than on whichever ran last; the median is
reported with its interquartile range. A speedup is *paired*: the median over
rounds of baseline ÷ contender measured back to back. Every contender returns a
checksum of its results and the run fails if they differ. The environment
(compiler, CPU, power, load) is recorded next to the CSVs, `make
bench-variation` repeats the main sections for the ranges, and the docs tables
are rendered from the CSVs — CI fails if a table disagrees with them. This
protocol replaced one that timed ~2 ms windows and produced a headline (~240×)
that could not be reproduced; even now the R-tree's ratio moves by more than 2×
between sessions on this laptop, which is why the docs quote ranges and the floor.

**Why does your benchmark use simulated tourists?**
Simulation provides ground truth: we know exactly where every person really was,
so we can measure whether the engine's answer is right — the hysteresis
experiment compares noisy runs with the same trajectories under perfect GPS.
Recorded traces would not allow that. The geography is real OpenStreetMap data;
the movement and the GPS error model (4 m open sky, 35 m multipath, dropouts,
correlated drift) are synthetic, and the project says so everywhere.

**What are the limitations?**
Simulated people over real geography, no field data. Worst-case O(n) queries
for both spatial trees and O(n) removal everywhere (no id → node map).
Tolerance-based geometry rather than exact predicates. No antimeridian support:
zones crossing ±180° are refused and query boxes do not wrap. A GPS jump
inflates the speed estimate and so the query radius for about a minute,
collapsing pruning for that person (correct, slow). Single-threaded, in-memory,
one process. Benchmarks from one laptop, quoted as ranges.

**How would you scale this to millions of users?** ★
It stops being one process. In order: an id → node map in each index so removal
and edits are O(log n); a robust speed estimate for the query radius; parallel
evaluation across people — each person owns their zone states and the index is
read-only during a tick, so the work shards by person, with the `mutable`
query counters moved off the hot path and per-thread event buffers merged in
id order to keep output deterministic; then shard by geography, each shard
owning its zones and the people in them, with hand-off at boundaries; ingest
fixes as a stream with per-person sequencing, because real fixes arrive late and
out of order; a durable event log instead of an in-memory vector. The numbers in
the repository are single-threaded per-query costs; I have not load-tested
anything, so capacity figures would be guesses.

---

## If the guide asks for more

- **The extensions** (alert correlation into incidents, Dijkstra/A* and Hungarian
  dispatch, group cohesion with rollback union-find, Merkle evidence log, adaptive
  sampling): built on the event stream, tested against their own oracles, not
  part of the headline claim. [GAP_ANALYSIS.md](GAP_ANALYSIS.md) says why each exists;
  RESULTS.md §10–17 measures them.
- **Where every number comes from**: [RESULTS.md](RESULTS.md) (generated from
  `bench/results/*.csv`), [TESTING.md](TESTING.md), [DEFECT_LOG.md](DEFECT_LOG.md).
- **The deeper version of these answers**: [INTERVIEW.md](INTERVIEW.md).
