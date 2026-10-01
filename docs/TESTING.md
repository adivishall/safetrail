# How correctness is established

SafeTrail's claim is *faster repeated geofencing without changing the answer*.
The second half of that sentence is the harder one, so it gets more machinery
than the first:

1. **Every fast structure has an independent oracle**, and randomised
   differential tests compare them over deliberately hostile workloads.
2. **Every core structure audits its own invariants**, and the differential
   tests call that audit after every single operation.
3. **The tests are themselves tested**: a mutation check injects realistic bugs
   into the core and fails unless some test catches each one.
4. **Every real defect found is pinned**: by a regression test that fails on
   the old code, or, where the test now uses API the old code lacks, by a mutant
   that reverts the fix (§5, [DEFECT_LOG.md](DEFECT_LOG.md)).
5. The whole suite runs under **AddressSanitizer + UBSan** (Linux CI) and
   **UBSan** (macOS), with **warnings as errors** on gcc and clang and the
   **Clang Static Analyzer** over the library.

```bash
make test        # the suite (~20 s to run; ~1.5 min from a clean build)
make stress      # the randomised tests at 10x their default size
make mutation    # 22 injected bugs; every one must be caught
make sanitize    # ASan+UBSan on Linux, UBSan on macOS
make validate    # all of the above that gates a merge, in one command
```

---

## 1. What "correct" means, layer by layer

| Layer | Correct means | Oracle | Where |
|---|---|---|---|
| Spatial index | `query(box)` returns exactly the ids whose boxes intersect `box` (closed intervals: touching counts), each once | `BruteForceIndex`, a linear scan | `tests/index/differential_test.cpp`, `tests/index/equivalence_test.cpp`, `tests/index/churn_test.cpp` |
| Query box | `Bbox::around(c, r)` contains every point within haversine distance `r` of `c` (the filter must be a superset of what the exact test can accept) | sampled points from `geo::offset` | `tests/geo/bbox_around_test.cpp` |
| Point in polygon | Boundary counts as inside; holes flip parity; winding direction is irrelevant | an independent winding-number implementation | `tests/geo/ray_casting_test.cpp`, `tests/geo/polygon_holes_test.cpp` |
| Three-valued containment | Inside / Outside only when the whole accuracy disc is on one side; Uncertain otherwise | the documented rule, re-derived from `signed_distance_m` | `tests/geo/containment_uncertainty_test.cpp`, `tests/fence/evaluator_agreement_test.cpp` |
| Polygon validation | Self-intersection = any two non-adjacent edges touch or cross | the O(V²) pairwise scan | `tests/geo/sweep_line_test.cpp` |
| Temporal filter | `stabbing(t)` returns exactly the windows with `low <= t < high` | a linear scan | `tests/ds/interval_tree_test.cpp` |
| Persistent index | A query "at time t" answers from the zone set *and the rules* as they stood at t | a replay model holding a full table per version | `tests/index/persistence_differential_test.cpp` |
| State machine | An event is emitted exactly when confirmed containment changes; hysteresis filters every band (Inside, Outside and Uncertain); every zone with open state is observed every tick | hand-built scenarios with known answers | `tests/fence/hysteresis_test.cpp`, `tests/fence/state_reconciliation_test.cpp` |
| Whole engine | The event stream is a function of zones and fixes only — identical, bit for bit, whichever index is plugged in | the same run under brute force | `tests/fence/index_independence_test.cpp` |
| Determinism | Same seed → byte-identical output | a second run | `tests/golden/*`, `make determinism` |

## 2. Structural invariants

Queries on a subtly broken tree often still come out right — a rotation that
fixes heights but not the `max_high` augmentation passes small tests for a long
time. So each core structure exposes a full audit, and the differential tests
call it after every operation rather than inferring health from query results.

| Structure | `check_invariants()` verifies |
|---|---|
| `Quadtree` | every item lies inside the region of the node that stores it (the property that makes subtree pruning sound); every child is exactly its parent's quadrant; a node has four children or none; stored item count = `size()` |
| `RTree` | all leaves at one depth; every node's box is *exactly* the union of its contents (tight — a loose box costs pruning, a short one loses results); no empty non-root node; no node over capacity; leaves hold only entries, internal nodes only children; entry count = `size()` |
| `IntervalTree` | strict BST order on the total key `(low, high, value, seq)`; AVL balance and stored height at every node; `max_high` = the true subtree maximum; live node count = `size()` |
| `VersionedIndex` | for **every retained version**: item-in-region containment, child-in-parent and depth, no id twice, and the item count equals the zones the history says were present at that version; commit times sorted |

Minimum fill is deliberately *not* an R-tree invariant: STR packing
legitimately leaves the last node of each slice underfull.

## 3. Differential testing

**Spatial indexes** (`tests/index/differential_test.cpp`). Five configurations —
quadtree, quadtree with capacity 1 and depth 30 (to force deep trees), R-tree,
R-tree with fan-out 4 (tall trees), geohash — against brute force, over seven
workload profiles chosen for where spatial indexes break:

| Profile | What it stresses |
|---|---|
| uniform | the control |
| clustered | many zones with *identical* boxes, plus nested ones — no split separates them |
| degenerate | zero-area points, zero-width lines, lattice-aligned edges, point queries exactly on box edges and corners |
| mixed-scale | district-sized boxes among 10 m ones |
| hemispheres | negative coordinates, boxes straddling (0, 0) |
| extremes | near both poles, at the antimeridian, and past lon 180 |
| far-inserts | build fitted to one area, then insert hundreds of km away (root expansion) |

Each runs a mixed sequence — bulk build, inserts, removal of present **and
absent** ids, rebuilds, drain-to-empty — and after every operation checks
results (sorted, no duplicates), `size()`, `remove()`'s return value, and the
structural audit. About 55,000 differential queries per configuration by
default; `make stress` runs ten times as many.

**Persistent quadtree** (`tests/index/persistence_differential_test.cpp`). 25
seeds × 160 operations — adds, replacements, removals of present and absent
ids, validity edits, and out-of-order commit times — with zero-area,
on-split-line, negative, past-180 and world-sized boxes. The oracle keeps a full
copy of the zone table per version (exactly the O(versions × zones) cost path
copying exists to avoid, which is what makes it an independent check). Every
historical `query_at`, `active_at`, `validity_at`, `zone_count_at` and
`query_now` must match, the audit must pass after every operation, and no
mutation may allocate more than two root-to-leaf paths plus a split.

**Whole engine** (`tests/fence/index_independence_test.cpp`). The full
simulation — OSM zones plus 3,000 synthetic ones, 40 tourists, 15 minutes —
through alerts, incident correlation and dispatch, under all four indexes; then a
hostile evaluator workload with concave zones, holes, validity windows that open
and close mid-run, tourists that teleport and go silent. Every event is compared
field by field with `memcmp` on the doubles.

**Geometry.** Ray casting against winding number on randomised concave polygons
with holes in every orientation; the Shamos–Hoey sweep against the pairwise
scan on 12,000 lattice rings full of touches, shared vertices and collinear
overlaps (0 disagreements; the same generator at 800,000 rings is how the sweep
defect was found); `Bbox::around` against 160,000 sampled points.

Extension structures have their own oracles: the k-d tree and hash table against
linear scans, Dijkstra against Floyd–Warshall, the Hungarian assignment against
exhaustive search, the timer wheel against a brute-force due set, jurisdiction
resolution against smallest-containing search.

## 4. Mutation testing

A green suite proves nothing on its own; a test that cannot fail when the code
is wrong is decoration. `tools/mutation_check.py` applies one realistic bug at a
time to a copy of the tree, rebuilds, and runs the tests named for it. A mutant
is **killed** if any of them fails; any survivor fails the run. Each edit is
written in a later wall-clock second than the last build, because macOS's make
compares timestamps to the second: without that wait, a mutant could be judged
by the previous build's binary ([DEFECT_LOG.md](DEFECT_LOG.md)).

| Mutant | Killed by |
|---|---|
| quadtree query skips one child | `index/differential_test` |
| quadtree collapse loses grandchildren | `index/differential_test` |
| quadtree root expansion puts the old root in the wrong quadrant | `index/differential_test` (tiling invariant) |
| R-tree insert does not grow the node box | `index/differential_test` |
| R-tree remove drops orphaned entries instead of reinserting | `index/differential_test` |
| interval tree `max_high` ignores the right subtree | `ds/interval_tree_test` |
| interval tree skips the left-right double rotation | `ds/interval_tree_test` |
| interval tree prune off by one | `ds/interval_tree_test` |
| persistent index filters history with *today's* validity | `index/persistence_differential_test` |
| persistent index accepts out-of-order commit times | `index/persistence_differential_test` |
| persistent index duplicates on re-add | `index/persistence_differential_test` |
| evaluator never reconciles zones outside the candidate window | `fence/state_reconciliation_test` |
| evaluator processes candidates in index order | `fence/index_independence_test` |
| evaluator ignores validity windows | `fence/state_reconciliation_test` |
| hysteresis lets an Uncertain fix complete a pending exit | `fence/hysteresis_test` |
| hysteresis passes the Uncertain band straight through | `fence/hysteresis_test` |
| ray casting ignores holes | `geo/polygon_holes_test` |
| containment ignores the GPS accuracy radius | `geo/containment_uncertainty_test` |
| boundary tolerance back to an absolute cross product | `geo/ray_casting_test` |
| query box without the 1/cos(lat) longitude stretch | `geo/bbox_around_test` |
| sweep line tests only the immediate neighbour | `geo/sweep_line_test` |
| sweep line tests only the first non-exempt neighbour on insert | `geo/sweep_line_test` |

**Result: 22 of 22 killed.** Two earlier survivors were real findings: the
quadtree audit could not distinguish a quadtree from an arbitrary tree of boxes
(fixed by adding the tiling invariant), and a duplicate-vertex screen in the
sweep turned out to be redundant (removed — the fuzz shows zero disagreements
without it, and no test could tell it was there).

## 5. Regression tests for real defects

| Defect ([DEFECT_LOG.md](DEFECT_LOG.md), pass 6) | Test that fails on the old code |
|---|---|
| State machine lost zones that left the candidate window — missed exits and silent re-entries | `tests/fence/state_reconciliation_test.cpp` (an API-compatible first version: 9 of 18 checks; the current test reads counters the old code lacks and does not compile against it, so the live evidence is the "never reconciles" and "ignores validity windows" mutants) |
| Hysteresis let Uncertain through: exit/enter flap pairs and an Uncertain event per accuracy alternation | `tests/fence/hysteresis_test.cpp` (first version: 7 of 13; 150 events where 1 is right; the current test uses the new `Ambiguous` phase and is pinned by the two hysteresis mutants) |
| Index query box 0.11% too narrow east-west | `tests/geo/bbox_around_test.cpp` (7 of 10) |
| Boundary 11 cm thick on a 1 m edge | `tests/geo/ray_casting_test.cpp` (1 of 35) |
| Sweep line called degenerate self-intersecting rings simple | `tests/geo/sweep_line_test.cpp` (9 of 56: all 8 pinned rings and the fuzz) |
| Interval-tree two-child delete broke strict order on duplicates; `at + 1` overflow | `tests/ds/interval_tree_test.cpp` (7 of 185) |
| Persistent index: O(n) removal allocations, versions for absent ids, duplicate re-adds, unsorted commit times, lost out-of-domain boxes | `tests/index/persistence_differential_test.cpp` (uses `check_invariants` and `contains_zone`, which the old code lacks; each fix confirmed by a mutant) |
| Loader: undefined double→integer conversions on hostile files; out-of-domain coordinates | `tests/fence/zone_roundtrip_test.cpp` (7 of 37: all 7 hostile files loaded; UBSan reported 3 of the conversions) |
| Hysteresis experiment scored filter-off vs filter-on, with no ground truth | `tests/golden/hysteresis_ab_test.cpp` (2 of 11: the filtered noisy run reported 241% and 219% of the noise-free target; the earlier version of this test, which checked only off vs on, passed 6 of 6 on the same old code — pass 7) |
| Benchmark headline from 2 ms windows, not reproducible | no unit test: the protocol in `apps/safetrail_bench.cpp` (≥ 20 ms samples, interleaved rounds, paired ratios) and its checksum gates, run by `make bench`; `make bench-variation` measures the spread |

## 6. Sanitizers, warnings, static analysis

| Check | Where it runs | Result |
|---|---|---|
| AddressSanitizer + UBSan, whole suite, `-fno-sanitize-recover` | Linux CI (g++) | gates deployment |
| UBSan, whole suite | macOS locally and in CI (Apple clang) | clean locally (Apple clang 17) |
| Warnings as errors: `tools/build/warnings.flags` (gcc, clang) + the `-Wconversion` family (clang) | every CI build job; `make validate` | clean with Apple clang 17; g++ 13 and Linux clang are checked by CI |
| Clang Static Analyzer, library + apps | `make analyze`; Linux CI (clang) | 0 findings with Apple clang 17 |

AddressSanitizer cannot run on macOS 26 with Apple clang 17 — an empty program
linked with `-fsanitize=address` hangs before `main` — so ASan is authoritative
in Linux CI only. Apple's UBSan is also weaker than gcc's (it missed a null
`memcpy` source that gcc's caught, in an earlier audit), so a green local run is
not the last word.

## 7. Determinism

Same seed → byte-identical output, on one platform, gated by `make determinism`
and `tests/golden/`; `make validate` and CI also require the Make build (library
`-O2`, tests `-O1`) and the CMake Release build (`-O3`, `NDEBUG` — there are no
`assert`s to lose) to agree byte for byte. What makes it hold: a fixed-seed PRNG,
explicit tie-breaks in every ordered structure (heaps, sorts, k-d tree medians,
Hungarian ties, the sweep's event order), candidates evaluated in zone-id order,
and `-ffp-contract=off` so the compiler cannot fuse `a*b + c` differently at
different optimisation levels. Across operating systems nothing is gated: the
one comparison of a macOS and a Linux build (WORKLOG, 2026-09-05, before the
pass-6 fixes) found the evaluation core byte-identical and dispatch travel totals
different in the last digits, because Apple's libm and glibc differ in the last
ulp of `asin`/`sin`/`cos`. That boundary is documented rather than closed —
closing it would mean shipping transcendental functions.

## 8. What the suite does not cover

- **Real GPS traces.** Fixes come from a simulator with a GPS error model (open
  sky, multipath, dropouts, correlated drift). Simulation is what provides
  ground truth; it is not field data.
- **Exact arithmetic.** Geometry uses doubles with deliberate tolerances
  (`geo/segment.cpp`), not exact predicates. Validation and containment share
  one set of predicates so they cannot disagree with each other, but a
  configuration within ~1e-14 degrees² of collinear is decided by tolerance.
- **Concurrency.** The engine is single-threaded; there is nothing to race.
- **Antimeridian-spanning zones** are rejected at load, not supported.
