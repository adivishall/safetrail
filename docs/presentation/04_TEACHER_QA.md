# Teacher Q&A — the question bank

For every question: **Say** (10–20 seconds, aloud), **If they follow up** (the
deeper answer), **Source** (where it is in the repository), **Do not overclaim**
(the qualification). Answers are from the code and the committed results at
`76b63af`; the project's own [REVIEW_QA.md](../REVIEW_QA.md) and
[INTERVIEW.md](../INTERVIEW.md) go deeper on several of them.

Three rules while answering:

1. If you do not know, say "I don't know; the answer would be in
   `docs/RESULTS.md` / `docs/TESTING.md` / the code, and I can show it."
2. Every number comes with "on this laptop" and, where it moves, its range.
3. Theory and measurement stay separate: "the bound is X; what I measured is Y."

---

## Basic

**Q1. What does SafeTrail do?**
**Say:** It is a geofencing engine. Hazard zones are polygons with rules; people report a GPS position every second; for every person and every zone near them it decides inside, outside or uncertain, and emits an event only when that changes: ENTER, EXIT, UNCERTAIN.
**If they follow up:** The engine is `fence::Evaluator`; around it are a simulator that produces the people and their GPS noise, a benchmark harness, a test suite and a one-file dashboard. The zones are real OpenStreetMap data from around Shillong; the people are simulated.
**Source:** `README.md` first screen; `include/safetrail/fence/evaluator.hpp`.
**Do not overclaim:** Not a deployed system; simulated people.

**Q2. Why did you choose this problem?**
**Say:** Because it is a data-structures problem disguised as an app. The whole cost of geofencing is repeated containment, and the question "which of 100,000 polygons are near this point" is exactly what spatial indexes exist for. It also has a correctness twist, GPS uncertainty, which made it more than a speed exercise.
**If they follow up:** It started as a Data Structures course project derived from Smart India Hackathon problem SIH25002 (tourist safety, Ministry of DoNER); the hackathon framing was dropped and the data-structures core was kept and hardened.
**Source:** `README.md` last paragraph; `docs/course/README.md`.
**Do not overclaim:** Do not present it as a hackathon entry or a product.

**Q3. What is geofencing?**
**Say:** Drawing a region on a map with a rule attached and detecting when someone crosses into or out of it. The primitive underneath is point-in-polygon, repeated for every person, every zone and every fix.
**If they follow up:** With uncertainty the question changes from "is this point inside" to "could the true position be inside", which needs distance to the boundary, not just parity.
**Source:** [03_PROJECT_EXPLAINED.md](03_PROJECT_EXPLAINED.md) §1; `include/safetrail/geo/containment.hpp`.

**Q4. What is the role of a spatial index?**
**Say:** To return, cheaply, the few zones whose bounding boxes overlap the query box, so the expensive polygon test runs on a handful instead of on all n. It is a filter; the exact geometry is the refinement.
**If they follow up:** The index stores only ids and boxes; the polygons live once in the `ZoneStore`. The contract is in `include/safetrail/index/spatial_index.hpp`: return every id whose box intersects the query, touching included.
**Source:** `include/safetrail/index/spatial_index.hpp`; the review's step 2 (13.91 candidates out of 5,038).
**Do not overclaim:** The index accelerates the filter, not the geometry.

**Q5. Why not just loop over every zone?**
**Say:** That is brute force: O(n) per fix. In the demo's one hour it is 711 million box tests to find about 14 relevant zones per query. At 100,000 zones one query costs about 240 µs on this laptop; multiply by people and seconds and it does not scale. I keep that loop anyway, as the oracle.
**If they follow up:** Brute force is linear in n in every measurement (RESULTS.md §1: 0.075 µs at 100 zones, 239 µs at 100,000). It is also the denominator of every speedup and the implementation every index is differentially tested against.
**Source:** `src/index/brute_force.cpp`; `bench/results/index_scaling.csv`.
**Do not overclaim:** 240 µs is one session on one laptop; another session measured 460–560 µs.

**Q6. What is a false-positive candidate?**
**Say:** A zone the index returns because its bounding box overlaps the query box, but whose polygon does not actually contain the point. It costs one exact test and is otherwise harmless. False negatives, zones the index fails to return, are the dangerous kind, and the filter is built so they cannot happen.
**If they follow up:** The query box is the exact bounding box of the accuracy disc on the sphere (`Bbox::around`), so every point the exact test could accept is inside it. The earlier box was 0.11% too narrow east-west and could produce false negatives; that was a real bug.
**Source:** `src/geo/bbox.cpp`; `tests/geo/bbox_around_test.cpp`; `docs/DEFECT_LOG.md` pass 6.

**Q7. What are ENTER and EXIT events?**
**Say:** ENTER is emitted when a person's confirmed state becomes Inside; EXIT when it leaves Inside. "Confirmed" means after the hysteresis filter: 15 m inside and three agreeing fixes and five seconds of dwell for ENTER; 25 m outside and three fixes for EXIT. The engine reports transitions, never states.
**If they follow up:** There is also UNCERTAIN (the accuracy disc straddles the boundary), APPROACHING (a predicted crossing with an ETA) and DWELL EXCEEDED (inside longer than a zone's limit).
**Source:** `src/fence/evaluator.cpp` (`emit_transition`); `include/safetrail/fence/hysteresis.hpp`.

---

## Data structures

**Q8. How does the quadtree subdivide space?**
**Say:** The root is a box fitted to the data. Each node splits into four quadrants. A zone's box goes down into a child only if that child fully contains it; otherwise it stays at the current node. So every zone lives in exactly one node, and a query descends only into quadrants that overlap the query box.
**If they follow up:** Node capacity 8, maximum depth 12. The root doubles outward when an insert lands outside it, keeping the old root as one quadrant; subtrees collapse on delete. The weakness is a box that straddles a split line: it settles high and is tested by every query entering that node.
**Source:** `include/safetrail/index/quadtree.hpp`, `src/index/quadtree.cpp`.
**Do not overclaim:** O(log n + k) is expected, not guaranteed.

**Q9. How does the R-tree group spatial objects?**
**Say:** It groups the zones themselves into nodes of up to eight, each with the tight bounding envelope of its contents, and groups those nodes upward to a root; all leaves are at one depth. Envelopes may overlap, so a query may descend several branches. I bulk-load it with STR: sort by longitude, cut into slices, sort each by latitude, pack.
**If they follow up:** Insertion uses Guttman's quadratic split; deletion condenses underfull nodes and reinserts their entries. STR gives a smaller, less overlapping tree: 14,383 nodes against 21,368 built by insertion at 100,000 zones, and a 6.46× faster query.
**Source:** `include/safetrail/index/rtree.hpp`, `src/index/rtree.cpp`; RESULTS.md §7; `bench/results/index_build.csv`.

**Q10. When would an R-tree outperform a quadtree?**
**Say:** When the data has irregular extents or many boxes that would straddle a quadtree's split lines, because the R-tree's envelopes adapt to the items. On my data it wins at every size past a few thousand zones: 224× against 33.7× over brute force at 100,000.
**If they follow up:** The quadtree wins on simplicity, disjoint cells and clean path copying, which is why the persistent index is a quadtree. At 1,000 zones they are equal (31.4× vs 30.6×).
**Source:** `bench/results/index_scaling.csv`; `docs/DATA_STRUCTURES.md` §3.
**Do not overclaim:** "On my data, on this laptop"; the ratio moved 102–235× across sessions.

**Q11. What is the AVL interval tree used for?**
**Say:** Answering "which validity windows contain time t" for the history index, the question "every zone in force at t". It is a balanced BST on windows where each node caches the latest end time in its subtree, so whole subtrees can be skipped. It is not on the per-fix path: there, validity is checked in O(1) on the few candidates the spatial index returned.
**If they follow up:** Keyed on the total order (low, high, value, seq) so deletion is one descent even with many equal start times; real AVL deletion with rotations and `max_high` repair; a free list so the node array is bounded by the peak live size.
**Source:** `include/safetrail/ds/interval_tree.hpp`; `VersionedIndex::active_at` in `src/index/versioned_index.cpp`; RESULTS.md §8.
**Do not overclaim:** Its reporting bound is O(min(n, (k + 1) log n)), not O(log n + k).

**Q12. What makes the persistent index different?**
**Say:** A change does not overwrite the tree. It copies only the root-to-leaf path it touches, about 15 nodes, and shares everything else with the previous version, so every past version stays queryable. A rule change copies nothing, because validity is an append-only log per zone.
**If they follow up:** Nodes are immutable `shared_ptr<const Node>`; removal follows the single path the insert rule used, guided by the stored box, so it is O(depth) too. 5,001 versions allocate 71,314 nodes against 930,257 for full copies: 13.0× sharing. Two time axes: transaction time selects the version; valid time is the zone's window; `query_at(t)` uses t for both.
**Source:** `include/safetrail/index/versioned_index.hpp`; `bench/results/versioned_index.csv`, `bench/results/versioned_mutations.csv`.
**Do not overclaim:** The bound is on nodes; bytes per change also grow with the item lists on the copied path.

**Q13. Why is brute force useful even after the optimized structures exist?**
**Say:** Three reasons: it is the oracle every fast index is compared with, it is the baseline every speedup is measured against, and it is too simple to be wrong. A benchmark without it would risk comparing a correct slow thing with a fast wrong thing.
**If they follow up:** Its one weakness: it shares `Bbox::intersects` with the indexes, so a bug in that predicate would change both sides identically. That predicate has no dedicated test; it is the gap I would close first.
**Source:** `include/safetrail/index/spatial_index.hpp` header comment; `docs/REVIEW_QA.md`.

**Q14. What are the space and time complexities?**
**Say:** Brute force O(n) query. Quadtree and R-tree O(log n + k) expected, O(n) worst case; removal O(n) in all three because there is no id-to-node map. Interval tree: O(log n) to find one, O(min(n, (k + 1) log n)) to report k, guaranteed. Persistent change: O(depth) new nodes. Space: about 40 bytes per zone for brute force, 68 for the quadtree, 54 for the R-tree at 100,000 zones, measured.
**If they follow up:** Build is O(n log n) for the trees (STR is dominated by its sorts). Point-in-polygon is O(V); Shamos–Hoey validation O(V log V).
**Source:** `docs/DATA_STRUCTURES.md` "Guaranteed, expected, measured"; `bench/results/index_costs.csv`.
**Do not overclaim:** Say "expected" and "worst" explicitly, every time.

**Q15. Why can tree queries still have an O(n) worst case?**
**Say:** Because these trees partition by geometry, not by a total order. Give every zone the same bounding box and the quadtree cannot separate them: they all sit in one node and a query tests all n. Give the R-tree heavily overlapping boxes and every envelope overlaps the query, so it visits every leaf.
**If they follow up:** The differential tests include exactly that profile ("clustered": identical and nested boxes) to make sure the answer is still right when the speed is gone. A balanced BST avoids this because its key is one-dimensional; boxes in two dimensions have no such order.
**Source:** `include/safetrail/index/quadtree.hpp` and `rtree.hpp` header comments; `tests/index/differential_test.cpp`.

**Q16. What does k mean in a query-complexity expression?**
**Say:** The number of results: the zones that genuinely overlap the query. No index can return fewer answers than exist, so k is the floor on the work and the ceiling on any speedup. In one crowded district k grows with n: about 98 at 100,000 zones.
**If they follow up:** That is why the quadtree's ratio plateaus in RESULTS.md §1 and why, at constant density with k ≈ 1 (§2), the same code is 543× and 1042× faster at 100,000 zones.
**Source:** the `k / query` column in `bench/results/index_scaling.csv` and `bench/results/index_density.csv`.

---

## Algorithms and correctness

**Q17. How is a GPS point tested against a zone?**
**Say:** First the cheap part: is the zone's bounding box near the fix at all (that is what the index answers). Then ray casting: shoot a ray east from the point and count edge crossings; odd means inside; holes count too. Then, because the fix is a disc, the signed distance to the nearest edge is compared with the accuracy radius: Inside or Outside only if the whole disc is on one side, otherwise Uncertain.
**If they follow up:** The half-open rule on the y-comparison counts a vertex crossing once and ignores horizontal edges; on-boundary counts as inside; a second implementation (winding number) cross-checks it. Distances are computed in a local metre plane, not in degrees, because a degree of longitude is 0.902 of a degree of latitude at Shillong.
**Source:** `src/geo/containment.cpp` (`contains`, `contains_winding`, `signed_distance_m`, `classify`); `docs/GEOMETRY_EDGE_CASES.md`.

**Q18. Why is bounding-box intersection not sufficient to prove a point is inside a polygon?**
**Say:** A box is a superset of the polygon. A point can be inside the box and outside the polygon, in a corner the polygon does not fill, or in a hole. The box is only the filter; the polygon test is the answer.
**If they follow up:** Conversely, the filter must be a superset of what the exact test can accept, or it drops true answers. That is why the query box is derived exactly on the same sphere as the distance function.
**Source:** `src/geo/bbox.cpp` header comment.

**Q19. How are uncertain readings handled?**
**Say:** As a third state. If the accuracy disc straddles the boundary the verdict is Uncertain. It is reported, so the operator sees it, but never acted on: it cannot end an Inside state, and it cancels a pending exit rather than completing it. A fix with accuracy worse than 150 m is rejected outright.
**If they follow up:** Hysteresis has an explicit Ambiguous phase: entered from Outside on an Uncertain verdict, left only by three clearly-outside fixes or by a confirmed entry. Letting Uncertain pass straight through was a real bug and caused about 60% of the demo's enter/exit events.
**Source:** `src/fence/hysteresis.cpp`; `tests/fence/hysteresis_test.cpp`; `tests/geo/containment_uncertainty_test.cpp`.

**Q20. Why did GPS gaps cause missed exits?**
**Say:** Because the state machine only looked at zones the index returned for the current fix. After a gap the person's next fix could be far away, the index no longer returned the zone they had been in, so nobody ever observed them leaving it. No EXIT, and a later real re-entry was silent because the state still said Inside.
**If they follow up:** The fix is the reconciliation step: every zone with open state is observed every fix; if the index did not return it, that is a certain Outside (the query box bounds a disc at least as large as the uncertainty), still fed through hysteresis so one wild fix cannot force an exit. A probe walk across 60 zones went from 60 entries and 0 exits to the right answer. Cost: 0.08–1.42 extra zones observed per fix, a box distance each.
**Source:** `Evaluator::reconcile` in `src/fence/evaluator.cpp`; `tests/fence/state_reconciliation_test.cpp`; RESULTS.md §18.

**Q21. What does hysteresis accomplish?**
**Say:** It stops GPS drift at a boundary from producing a flood of false enter/exit pairs. Three mechanisms: asymmetric margins (15 m in to enter, 25 m out to leave, a Schmitt trigger), three agreeing fixes, and a five-second dwell. In the demo hour: 602 entries with it, 17,906 without, on identical trajectories.
**If they follow up:** Measured against noise-free truth on the same paths it reports 96% of the real transitions under realistic correlated drift and 66% under white noise; the second number is the cost and I report it. The latency is up to three fixes on a genuine crossing.
**Source:** `src/fence/hysteresis.cpp`; `bench/results/hysteresis_ab.csv`; review step 6.

**Q22. How did the project discover the previous defects?**
**Say:** By asking, for each structure, "what input would make this disagree with its oracle?", and answering with randomised differential tests over hostile workloads, invariant audits after every operation, and mutation testing. Ten real defects came out of that, eight in the engine and two in how it was measured, all in code that was passing its tests.
**If they follow up:** Examples of how each was found: the reconciliation bug by a probe walk and the index-independence test; the hysteresis leak by counting UNCERTAIN events for a stationary person (150 in five minutes); the query box by sampling 160,000 points inside the disc; the sweep-line bug by fuzzing 800,000 lattice rings against the O(V²) oracle; the interval-tree deletion bug by auditing after every removal of duplicates; the benchmark headline by failing to reproduce it.
**Source:** `docs/DEFECT_LOG.md` pass 6; `docs/TESTING.md` §5.

**Q23. How do you know the indexes return the same answers?**
**Say:** Differential testing: five index configurations against brute force on about 55,000 queries each over seven hostile workload profiles, with the tree's invariants audited after every operation. Plus the whole engine under all four indexes must emit bit-identical event streams, and the benchmark checks a checksum of results on every timed row. The demo shows 27,935 events identical across three indexes.
**If they follow up:** The profiles: uniform, clustered (identical and nested boxes), degenerate (zero-area, lattice-aligned, on-edge point queries), mixed-scale, hemispheres, extremes (poles, past 180), far-inserts. Each sequence mixes builds, inserts, removals of present and absent ids, rebuilds and drain-to-empty.
**Source:** `tests/index/differential_test.cpp`; `tests/fence/index_independence_test.cpp`; `make bench` §5.
**Do not overclaim:** This is evidence, not proof.

**Q24. What does deterministic output mean here?**
**Say:** Same seed, same input, byte-identical output, every run, on this platform. It is a gated test, not a promise: `make determinism` runs the same seed twice and compares the files, and the Make and CMake builds must agree byte for byte.
**If they follow up:** What makes it hold: a fixed-seed PRNG, explicit tie-breaks in every ordered structure, candidates evaluated in zone-id order so the index is invisible in the output, and `-ffp-contract=off` so the compiler cannot fuse multiply-adds differently at different optimisation levels. Not gated across operating systems: libm differs in the last ulp of `asin`/`sin`/`cos`.
**Source:** `Makefile` (`determinism`, `validate`); `tests/golden/determinism_test.cpp`; `docs/TESTING.md` §7.

**Q25. What are the limitations of randomized testing?**
**Say:** It only finds bugs on inputs the generator can produce, and a random generator rarely produces the exact degenerate case. That is why the profiles are hostile by design, why every real bug gets a pinned regression case, and why mutation testing checks that the tests can fail at all.
**If they follow up:** The sweep-line bug is the example: the old test used jittered circles, which never produce degeneracies, and passed; lattice rings full of touches and collinear overlaps found 94 failures in 800,000.
**Source:** `docs/TESTING.md` §3–4; `tests/geo/sweep_line_test.cpp`.

---

## Testing

**Q26. What is a regression test?**
**Say:** A test written for a bug that was actually found, which fails on the old code and passes on the fix, so the bug cannot quietly come back. Every one of the ten defects has one, or a mutant that stands in for it where the test needs new API.
**If they follow up:** `docs/TESTING.md` §5 lists each: for example the reconciliation test fails 9 of 18 checks on the old code; the query-box test 7 of 10; the sweep-line test 9 of 56.
**Source:** `docs/TESTING.md` §5; `docs/DEFECT_LOG.md`.

**Q27. What is differential testing?**
**Say:** Running two implementations of the same contract on the same inputs and demanding identical outputs. Here the fast index against brute force; the interval tree against a linear scan; the persistent index against a full-table replay; ray casting against winding number; the sweep line against the pairwise scan.
**If they follow up:** The oracle must be independent and simpler: the replay oracle for the persistent index keeps a full copy of the zone table per version, exactly the cost path copying exists to avoid, which is what makes it an independent check.
**Source:** `docs/TESTING.md` §1 and §3.

**Q28. What is mutation testing?**
**Say:** A test of the tests. A script injects one realistic bug at a time into a copy of the code, rebuilds, and requires some test to fail. If a bug survives, the suite was not really testing that behaviour. Here 22 mutants, 22 killed.
**If they follow up:** Examples: the quadtree query skips one child; the R-tree insert does not grow the node box; the interval tree's `max_high` ignores the right subtree; the persistent index filters history with today's validity; the evaluator never reconciles; hysteresis lets Uncertain complete an exit. Two earlier survivors were real findings: the quadtree audit could not tell a quadtree from an arbitrary tree of boxes, and a duplicate-vertex screen in the sweep was redundant.
**Source:** `tools/mutation_check.py`; `make mutation`; `docs/TESTING.md` §4.

**Q29. What do the 22/22 mutation results actually prove?**
**Say:** That those 22 specific, hand-written faults are each caught by a test. Nothing more. They say nothing about faults unlike them. It is evidence that the tests have teeth, not a correctness proof.
**If they follow up:** The mutants were chosen to cover each structure's core invariant and each pass-6 fix; a different fault class, say an off-by-one in the GeoJSON parser, is not represented. A larger automated mutation tool would sample more broadly; this one is 22 curated edits.
**Source:** `tools/mutation_check.py`; `docs/RESUME.md` "Do not claim".
**Do not overclaim:** Never say "proven" or "100% mutation score" as if it were exhaustive.

**Q30. Why run sanitizers?**
**Say:** Because undefined behaviour and memory errors do not show up as wrong answers until they do, somewhere else. UBSan found three undefined double-to-integer conversions in the GeoJSON loader that no test had caught. The whole suite runs under UBSan here and under AddressSanitizer plus UBSan in Linux CI, set to abort rather than print and continue.
**If they follow up:** ASan cannot run on this Mac (macOS 26 with Apple clang 17 hangs before `main`), so Linux CI is authoritative for it; Apple's UBSan is also weaker than gcc's, so a green local run is not the last word.
**Source:** `Makefile` (`asan`, `ubsan`, `sanitize`); `.github/workflows/deploy.yml` sanitize job.

**Q31. What can static analysis miss?**
**Say:** Anything that depends on runtime values or on the semantics of the problem: a wrong threshold, a filter that is 0.11% too narrow, a state machine that forgets a zone. The Clang Static Analyzer reports zero findings on this code, and none of the ten defects would have been visible to it.
**If they follow up:** It is good at null dereferences, use after free, uninitialised reads along feasible paths within a translation unit; weak across calls it cannot see, and silent on logic.
**Source:** `make analyze`; `docs/TESTING.md` §6.

**Q32. Does 45 test files mean every possible case is correct?**
**Say:** No. It means 45 files and 11,844 checks pass, covering the cases I thought of, the hostile cases I generated, and the real bugs I found. The suite is backed by invariant audits and mutation testing, which is more than a pass count, but it is still evidence, not proof.
**If they follow up:** Known uncovered areas, stated in `docs/TESTING.md` §8: real GPS traces, exact arithmetic, concurrency (there is none), antimeridian-spanning zones (refused).
**Source:** `make test`; `docs/TESTING.md` §8.

---

## Benchmarks

**Q33. How were the measurements obtained?**
**Say:** With `make bench`: query boxes built before timing; each sample runs the probe set for at least 20 ms; eleven rounds with the indexes taking turns in rotated order so machine drift lands on all of them; the median is reported with its interquartile range; speedups are paired ratios measured back to back; every row is gated by a checksum of results; and the environment (compiler, CPU, power source, load, commit) is recorded next to the CSVs.
**If they follow up:** The docs tables are rendered from the CSVs by a script and CI fails if a table disagrees. `make bench-variation` repeats the main sections for the run-to-run ranges. This protocol replaced one that timed 2 ms windows and produced a headline (about 240×) that could not be reproduced.
**Source:** `apps/safetrail_bench.cpp` protocol comment; `docs/RESULTS.md` "Protocol"; `tools/render_results.py`.

**Q34. Why is the same tree much faster on one workload than another?**
**Say:** Because of k, the number of zones that genuinely overlap the query. In one crowded district about 98 do at 100,000 zones and the tree must return them all; at constant density about one does. Same code: quadtree 33.7× versus 543×, R-tree 224× versus 1042×.
**If they follow up:** The end-to-end section shows another axis: against ray-casting every polygon the tree wins by two to three orders of magnitude and more as polygons get complex; against a bounding-box scan followed by the same exact test the margin is far smaller and shrinks with vertex count, because the tree accelerates the filter, not the geometry.
**Source:** `bench/results/index_scaling.csv`, `bench/results/index_density.csv`, `bench/results/end_to_end.csv`.

**Q35. Why can a dense district reduce the speedup?**
**Say:** No index can return fewer results than exist. If 98 zones overlap each query, every index pays for 98 answers; brute force pays n box tests plus the same 98. As n grows in a fixed area, k grows with it, so the quadtree's ratio stops rising after a few thousand zones.
**If they follow up:** The R-tree keeps gaining because STR packing lets it reach the same k results through fewer nodes. The evaluator benchmark shows the same thing per fix: microseconds per fix track candidates per fix.
**Source:** RESULTS.md §1 and §18.

**Q36. Why is one observed speedup not a universal guarantee?**
**Say:** Because it is a measurement on one laptop, one session, one synthetic workload, against my own brute force. The bound is O(log n + k) expected with an O(n) worst case; the measurement is 224× in that session and has ranged from about 100× to 235× across sessions on the same machine.
**If they follow up:** Even brute force's absolute time moved from about 240 µs to 460–560 µs between sessions, on battery and mains alike, for reasons not established. Ratios measured back to back are more stable than absolute times, but the R-tree, at about a microsecond per query, is the shortest timing and moves most.
**Source:** `docs/RESULTS.md` "Read this first"; `docs/RESUME.md` "Do not claim".

**Q37. How do the recorded timing ranges improve the report?**
**Say:** They turn one number into a statement with error bars. Within a session the four runs agree closely (211–224× for the R-tree at 100,000 zones); between sessions they do not (102–235×). Reporting both, with the environment printed next to the table, lets a reader judge the number instead of trusting it.
**If they follow up:** The ranges are computed from `bench/results/variation/run1/`, `run2/`, `run3/` plus the main run, and the step-8 table in the demo prints them from those files.
**Source:** `bench/results/variation/run1/index_scaling.csv` and siblings; `tools/review.sh` step 8.

**Q38. Why should I trust the benchmark methodology?**
**Say:** Mostly because it distrusts itself: every timed comparison is also a correctness gate via result checksums; the environment is recorded; the tables are generated from the CSVs and checked in CI; the ranges are reported; and the protocol exists because the previous one produced a number I could not reproduce and I replaced it rather than keep the headline.
**If they follow up:** What it does not control: it is one machine; macOS may ignore the request for performance cores; timer resolution is handled by the 20 ms minimum sample. Timings in CI are run but deliberately not gated because shared runners are noisy.
**Source:** `apps/safetrail_bench.cpp`; `.github/workflows/deploy.yml` ("benchmarks INFORMATIONAL").

**Q39. Has the system been tested with millions of real users?**
**Say:** No. There are no real users at all: the people are simulated. And no production-scale load test has been run; every number is a single-threaded per-query or per-fix cost on one laptop. Throughput for thousands of concurrent users is reasoned about, not demonstrated.
**If they follow up:** The reasoning: tourists are independent within a tick and the index is read-only during one, so evaluation shards by person and then by geography; but the counters are `mutable` on the index, events would need merging in id order to stay deterministic, and none of that is built.
**Source:** `docs/REVIEW_QA.md` "What are the limitations" and "How would you scale this".
**Do not overclaim:** Do not give a capacity figure.

---

## Critical

**Q40. What are the biggest current limitations?**
**Say:** Simulated people over real geography. O(n) worst-case queries for both spatial trees and O(n) removal in every index. Tolerance-based geometry, not exact predicates. No antimeridian support. Single-threaded, in memory, one process. Benchmarks from one laptop, quoted as ranges. And no production-scale load test.
**If they follow up:** Also: a GPS jump inflates the speed estimate and therefore the query radius for about a minute, collapsing pruning for that person (correct but slow); cross-OS determinism has one known gap in the dispatch extension and is not gated.
**Source:** `README.md` "Limitations"; `docs/ARCHITECTURE.md` "Limitations".

**Q41. What would you change with another month?**
**Say:** In order of what bites first: an id-to-node map in each index so removal and edits are O(log n); a robust speed estimator for the query radius; parallel evaluation across people, then sharding by geography; a load test so I could state throughput instead of reasoning about it; and a direct test for `Bbox::intersects`, the one predicate the oracle shares with the indexes.
**If they follow up:** Longer term: exact predicates for authored geometry, a durable event log, real ingest with out-of-order fixes. And honestly, in production I would start from a mature spatial library and keep this project's oracle and differential tests around it.
**Source:** `docs/ARCHITECTURE.md` "What would change at production scale"; `docs/DATA_STRUCTURES.md` "Deliberately not built".

**Q42. What part was the most difficult?**
**Say:** Keeping the state machine truthful across fixes. The data structures were textbook work with care; the hard part was realising that the index only tells you about zones near the current fix, and that a person's state about a zone has to be maintained whether or not the index mentions it this time. Finding that bug, and the Uncertain leak next to it, changed the event counts by a factor of two or more.
**If they follow up:** A close second: making the benchmark trustworthy after the first headline could not be reproduced, which meant throwing away a number I liked.
**Source:** `docs/DEFECT_LOG.md` pass 6, first two and last entries.

**Q43. What was a bug you found that materially changed the result?**
**Say:** The hysteresis filter let Uncertain verdicts through. An Uncertain fix during a pending exit was emitted as an EXIT, and the next inside fix re-entered; and every 4 m / 35 m accuracy alternation near a boundary produced a fresh UNCERTAIN event. About 60% of the demo's enter/exit events were these flaps: entries fell from 2,440 to 987 after the fix, Uncertain events from 64,168 to 16,558.
**If they follow up:** The measurement it corrupted was the "94% of false transitions removed" figure, which compared filter-off with filter-on and had no ground truth. The replacement scores against noise-free runs: 96% of the target under realistic drift, 66% under white noise.
**Source:** `docs/DEFECT_LOG.md` pass 6 ("Hysteresis let the Uncertain band through"); `bench/results/hysteresis_ab.csv`.

**Q44. What did you learn beyond implementing data structures?**
**Say:** That a passing test suite proves very little on its own: ten real defects were sitting in code that passed its tests. What found them was asking what input would make each structure disagree with an independent oracle, auditing invariants after every operation, and testing the tests with mutations. And that a measurement without its environment and its range is a number, not evidence.
**If they follow up:** Smaller lessons: floating-point contraction can silently change event streams between optimisation levels; macOS's make compares timestamps to the second and that broke the mutation script once; a filter in filter-then-refine has a contract that must be proven, not assumed.
**Source:** `docs/DEFECT_LOG.md`; `docs/WORKLOG.md`.

**Q45. Why is this more than a simple DSA assignment?**
**Say:** Because the structures are measured against each other and against a baseline under a stated protocol, their expected and worst-case bounds are separated and demonstrated both ways, their correctness is established by differential and mutation testing rather than asserted, and the whole thing is reproducible: one command, seeded, byte-identical output, with the tables generated from the data.
**If they follow up:** Also the less common structures: a persistent path-copying tree with measured sharing, an augmented AVL interval tree with its bound stated honestly, STR bulk loading with the insertion build kept for comparison, a Shamos–Hoey sweep with its oracle beside it.
**Source:** `README.md`; `docs/DATA_STRUCTURES.md`.
**Do not overclaim:** Do not call it novel research; the algorithms are textbook. The engineering around them is the contribution.

**Q46. What would a production-ready implementation need?**
**Say:** It stops being one process. Id-to-node maps and incremental index updates; a robust motion estimator; parallel evaluation sharded by person and then by region, with deterministic merging; a partitioned zone set so no single index holds everything; exact geometric predicates for authored geometry; a durable event log; a real ingest path for late and out-of-order fixes from real devices; and a load test to replace reasoning with numbers.
**If they follow up:** The deployment story the project argues for is on-device evaluation (the engine is in-process and allocation-free in steady state; the geohash index serialises at 44 bytes per zone; offline reconciliation uses Lamport clocks), but the device packaging, GPS source and transport are not built.
**Source:** `docs/ARCHITECTURE.md` "What would change at production scale"; `docs/DEPLOYMENT.md` §4.
**Do not overclaim:** "Production-ready" is on the do-not-claim list in `docs/RESUME.md`.

---

## Complexity claims, checked against the implementation

Say these exactly.

| Claim | True? | Why |
|---|---|---|
| Quadtree query is O(log n + k) | **expected only** | partitions space; identical or straddling boxes pile up in one node → O(n) worst (`include/safetrail/index/quadtree.hpp`) |
| R-tree query is O(log n + k) | **expected only** | height-balanced, but overlapping envelopes can send a query down every branch → O(n) worst (`include/safetrail/index/rtree.hpp`) |
| Interval tree finds one overlap in O(log n) | yes, guaranteed | AVL height ≤ 1.44 log n |
| Interval tree reports k overlaps in O(log n + k) | **no** | `descend()` admits a subtree as soon as any interval in it reaches past t, so each result can cost a root-to-leaf path: O(min(n, (k + 1) log n)), guaranteed and never worse than the scan (`include/safetrail/ds/interval_tree.hpp`) |
| Interval tree insert / remove O(log n) | yes, guaranteed | total-order key makes remove a single descent |
| Persistent change allocates O(depth) nodes | yes, by construction | path copying; a validity change allocates 0 |
| Removal from a spatial index is O(log n) | **no** | O(n): no id → node map in any of the three |
| 224× faster | **a measurement**, not a bound | one session on this laptop, against own brute force; 102–235× across sessions |
