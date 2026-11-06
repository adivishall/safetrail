# 11 — Viva question bank (115 questions)

> Each question: **Short** (say this), **Deeper** (if pressed), **Source** (where
> to point). Tricky questions are marked ★. Numbers are verified against
> `bench/results/` and the test run of 2026-11-02.

## 1. Basic project questions

**Q1. What is SafeTrail in one sentence?**
Short: A C++ geofencing engine that decides inside/outside/uncertain for every hazard zone near each tourist's GPS fix, using hand-written spatial and temporal indexes, with an operations console on top.
Deeper: The platform layer adds consent, alerts, incidents, dispatch and evidence.
Source: `README.md`, `src/fence/evaluator.cpp`.

**Q2. What problem does it solve?**
Short: Checking every zone for every fix is O(n) per fix; a spatial index prunes zones that cannot overlap.
Deeper: At 100,000 zones the brute-force scan measured 244.6 µs per query; the R-tree 1.09 µs.
Source: `bench/results/index_scaling.csv`.

**Q3. Is the data real?**
Short: The geography is real (38 OSM hazard zones, 144,090 road junctions); the people are simulated and marked SIM.
Deeper: The 100,000-zone benchmarks use synthetic zones.
Source: `data/`, `docs/RESULTS.md`.

**Q4. Who are the users?**
Short: Admin, operator (staff), responder, tourist — each sees different pages.
Source: `ROUTES` in `web/app/js/app.js`.

**Q5. What is the output of the engine?**
Short: Events — only on changes: ZoneEnter, ZoneExit, ZoneUncertain, ZoneApproaching, DwellExceeded.
Source: `fence::EventKind`, `Evaluator::emit_transition()`.

**Q6. Why C++ and no libraries?**
Short: The course is about data structures, so none is borrowed — no `std::map`, `std::set`, `std::unordered_map`, `std::priority_queue`, no spatial library.
Deeper: Also predictable performance for benchmarking.
Source: `README.md`.

**Q7. Where is it deployed?**
Short: https://safetrail-v0bh.onrender.com on Render's free plan; the static replay on GitHub Pages and Vercel.
Source: `docs/final/DEPLOYMENT_GUIDE.md`.

**Q8. What makes it different from a typical geofencing app?**
Short: Three-valued containment under GPS uncertainty, hysteresis, historical ("what were the rules at 14:32") queries, verified evidence, and every fast structure checked against a brute-force oracle.
Source: `docs/ARCHITECTURE.md`.

## 2. Data-structures fundamentals

**Q9. ★ Why not use a simple array of zones?**
Short: A scan is Θ(n) per fix; that is exactly the brute force we measure against.
Deeper: At 100k zones: 244.6 µs vs 1.09 µs for the R-tree in one district; worse as n grows.
Source: `index::BruteForceIndex`.

**Q10. ★ Why not use a hash table for zones?**
Short: A hash table answers "is key K present?", not "which boxes overlap this box?"; nearby positions don't hash to nearby buckets.
Deeper: The geohash index uses a Morton *sort* (order-preserving), not hashing, for that reason. Hash tables are used where lookup by key is the question: sessions, usernames, de-duplication.
Source: `include/safetrail/ds/hash_table.hpp`, `src/index/geohash.cpp`.

**Q11. What is an abstract interface here, and why?**
Short: `index::SpatialIndex` (build, insert, remove, query, stats): the engine depends on the interface, so any index plugs in.
Deeper: Tests require byte-identical events under all four.
Source: `include/safetrail/index/spatial_index.hpp`, `tests/fence/index_independence_test.cpp`.

**Q12. Difference between expected and worst-case bounds?**
Short: Expected assumes reasonable data; worst case holds for any input. Quadtree/R-tree queries are O(log n + k) expected, O(n) worst; AVL operations are O(log n) guaranteed.
Source: `docs/DATA_STRUCTURES.md`.

**Q13. What is an augmented tree?**
Short: A tree where each node stores an aggregate of its subtree — here, the maximum interval end (`max_high`) — maintained through rotations.
Source: `ds::IntervalTree::refit()`.

**Q14. Why open addressing over chaining?**
Short: One flat array, no per-node allocation, cache-friendly; tombstones keep probe chains intact.
Source: `ds::HashMap`.

**Q15. What is a tombstone and why rehash at the same size?**
Short: A marker for a deleted slot so probes continue past it. If the load is mostly tombstones, rebuilding at the same size cleans them without growing forever.
Source: comments in `hash_table.hpp`.

**Q16. Array-based heap: where are the children?**
Short: Children of i at 2i+1 and 2i+2; parent at (i−1)/2.
Source: `ds::BinaryHeap::sift_up/sift_down`.

## 3. Quadtree

**Q17. Define a quadtree.**
Short: A tree whose internal nodes split a rectangle into four quadrants; a node splits when it holds more than its capacity (8).
Source: `src/index/quadtree.cpp`.

**Q18. Where does a box that crosses a split line go?**
Short: It stays in the internal node's own item list (it fits no single child).
Deeper: Such straddlers are tested by every query reaching that node.
Source: `insert_into()`.

**Q19. ★ What is the worst case for a quadtree?**
Short: O(n) per query — e.g. every box straddles the root's split lines (all stored at the root), or heavy clustering at maximum depth.
Deeper: That is why the bound I quote is *expected* O(log n + k), and brute force remains the oracle.
Source: `include/safetrail/index/quadtree.hpp`.

**Q20. How does the query prune?**
Short: If a node's region doesn't intersect the query box, its whole subtree is skipped.
Source: `query_node()`.

**Q21. What happens on deletion?**
Short: Remove the item; if the subtree now holds ≤ capacity items, collapse its children into it (inverse of split).
Deeper: Without collapse, a tree that grew and shrank kept its old shape.
Source: `try_collapse()`, `count_upto()`.

**Q22. Inserting outside the root?**
Short: The root is doubled so the old root becomes one quadrant; the tiling invariant holds.
Source: `Quadtree::expand_root_to_cover()`.

**Q23. Why maximum depth 12?**
Short: To stop infinite subdivision when many boxes are identical or tiny.
Source: `Quadtree(size_t node_capacity = 8, size_t max_depth = 12)`.

**Q24. Measured quadtree speedup?**
Short: 32.71× at 100k zones in one district; 555.74× at constant density.
Source: `index_scaling.csv`, `index_density.csv`.

## 4. R-tree

**Q25. Define an R-tree.**
Short: A balanced tree where each node stores the minimum bounding rectangle of its children; leaves hold the items' boxes. Nodes may overlap.
Source: `src/index/rtree.cpp`.

**Q26. ★ Why use both a quadtree and an R-tree?**
Short: To compare two partitioning philosophies on the same data: space (quadtree) vs items (R-tree). The R-tree is the live index; the quadtree shows why; brute force proves both correct.
Source: `index_scaling.csv`, Lab spatial experiment.

**Q27. ★ Why is the R-tree better for this workload?**
Short: Its boxes hug clustered data, and no box ever straddles a node; balanced depth. 223.67× vs 32.71× at 100k in one district.
Deeper: Quadtree cells are fixed regardless of data; large or central zones stay high in the tree.
Source: `index_scaling.csv`.

**Q28. How does insertion choose a subtree?**
Short: The child needing the least enlargement of its box; ties by smaller area.
Source: `do_insert()` (ChooseSubtree).

**Q29. What is quadratic split?**
Short: On overflow, pick as seeds the two entries wasting the most area together, then assign others to the group whose box grows less, ensuring the minimum fill (M/2 = 4).
Source: `quadratic_split()`.

**Q30. What is STR bulk loading?**
Short: Sort-Tile-Recursive: sort by longitude into √P slices, sort each slice by latitude, cut into leaves of M, repeat upward.
Deeper: Builds a near-square tiling with little overlap; 6.26× faster queries, 33 % fewer nodes than insertion at 100k.
Source: `RTree::build()`, `index_build.csv`.

**Q31. Why does overlap matter?**
Short: Overlapping node boxes force a query to descend several branches.
Source: comment above STR in `rtree.cpp`.

**Q32. How does deletion work?**
Short: Remove the entry; detach any under-filled node on the path and reinsert its entries (simplified CondenseTree).
Source: `remove_node()` in `rtree.cpp`.

## 5. AVL interval tree

**Q33. What does it answer?**
Short: Which validity intervals contain time t (stabbing query) — "which zones were in force at t".
Source: `VersionedIndex::active_at()`.

**Q34. ★ Why not an ordinary AVL tree?**
Short: Keyed on start time alone it cannot skip subtrees whose intervals have all ended; the `max_high` augmentation enables pruning.
Source: `include/safetrail/ds/interval_tree.hpp`.

**Q35. What is the ordering key?**
Short: (low, high, value, seq) — a total order, so duplicate start times still give one deletion path.
Source: header comment.

**Q36. Complexity of reporting k results?**
Short: O(min(n, (k + 1) log n)) guaranteed — not O(log n + k).
Deeper: Each result can light a different root-to-leaf path; a centred interval tree would give O(log n + k).
Source: `docs/DATA_STRUCTURES.md`, DEFECT_LOG pass 7 (the bound was overstated once).

**Q37. When does it lose to a scan?**
Short: When queries are not selective: dense windows, 1,000 intervals → 0.61×.
Source: `interval_tree.csv`.

**Q38. How are rotations handled with augmentation?**
Short: After each rotation, recompute height and `max_high` bottom-up for the rotated nodes.
Source: `refit()`, rotation helpers.

**Q39. Is it on the per-fix path?**
Short: No. The evaluator checks validity in O(1) per candidate; the tree serves history.
Source: `Evaluator::evaluate()` step 4.

## 6. Persistent data structures

**Q40. What is persistence?**
Short: Updates create a new version; every old version stays queryable.
Source: `index::VersionedIndex`.

**Q41. How does path copying work?**
Short: Copy only the nodes from the root to the changed leaf; share every other subtree with the previous version via `shared_ptr`.
Source: `src/index/versioned_index.cpp`.

**Q42. Cost per update?**
Short: O(depth) new nodes, instead of O(n) for a full copy. Measured 13.04× sharing at 5,001 versions.
Source: `versioned_index.csv`.

**Q43. ★ Difference between persistence and rollback?**
Short: Persistence keeps all versions alive and queryable at once; rollback undoes changes so only one (earlier) state is live.
Deeper: The persistent quadtree vs the rollback DSU in this repo.
Source: 03 §7 and §8.

**Q44. How is validity versioned?**
Short: A per-zone append-only log of (version, validity) records; lookup by binary search for the last record ≤ version.
Deeper: An earlier version filtered history with today's validity — a real defect, now pinned by a mutant.
Source: `versioned_index.hpp` header.

**Q45. Why is a past query as fast as a present one?**
Short: Every version is a complete tree; querying an old root walks the same kind of structure.
Source: `versioned_index.csv` (past/now ≈ 0.95–1.02, 0.59 at 5,001).

## 7. Union-find

**Q46. Where is union-find used?**
Short: Group cohesion (components of the proximity graph) and incident correlation (alerts within 200 m and 10 minutes).
Source: `src/group/cohesion.cpp`, `src/alert/correlator.cpp`.

**Q47. ★ Why no path compression?**
Short: It makes unbounded writes per find, which can't be recorded on a bounded undo stack — rollback would be impossible. Union by rank alone gives O(log n) find.
Source: `src/ds/dynamic_connectivity.cpp`.

**Q48. How does rollback work?**
Short: Each union pushes (child, old parent, root, old rank); rollback pops and restores to a saved mark, O(1) per undone union.
Source: `RollbackDSU::rollback_to()`.

**Q49. Is rollback used at runtime?**
Short: No — both callers build a fresh DSU per pass. Rollback is implemented and tested, shown in the Lab, not on the hot path.
Source: header comment of `dynamic_connectivity.hpp`.

**Q50. Why not BFS for components?**
Short: BFS works for one snapshot; DSU is the incremental structure, and correlation adds pairs one at a time.
Source: `Correlator::ingest()`.

**Q51. How is the DSU tested?**
Short: Against a flood-fill oracle (`components_bruteforce`) on random merge/split sequences.
Source: `tests/ds/dynamic_connectivity_test.cpp`.

## 8. Graph algorithms

**Q52. How is the road network stored?**
Short: Adjacency list: node positions plus a vector of (neighbour, weight in metres) per node; every road both ways.
Source: `graph::RoadGraph`.

**Q53. ★ How does Dijkstra differ from A*?**
Short: Dijkstra expands by distance from the source in all directions; A* orders by f = g + h with h a lower bound to the target, so it expands toward the target and settles fewer nodes.
Deeper: In the demo incident: 772 vs 5,557 settled junctions; 71–78 % fewer on benchmark grids.
Source: `src/graph/astar.cpp`, `routing.csv`.

**Q54. Why is A* still optimal?**
Short: The straight-line heuristic never over-estimates road distance (edge weights are great-circle lengths), i.e. admissible.
Source: `graph::heuristic_is_admissible()`.

**Q55. Why Dijkstra for the recommendation?**
Short: One run from the incident gives the distance to every responder at once (valid because the graph is undirected).
Source: `Platform::recommend()`.

**Q56. Dijkstra complexity?**
Short: O((V + E) log V) with a binary heap and lazy deletion.
Source: `src/graph/dijkstra.cpp`.

**Q57. What is lazy deletion?**
Short: Instead of decrease-key, push a new entry; when an outdated entry is popped, skip it ("stale pops" in the Lab).
Source: Lab graph experiment.

**Q58. What is the Hungarian algorithm used for?**
Short: Batch dispatch: assign several responders to several incidents minimising total road distance, O(n³).
Deeper: Greedy wastes 6.5–15.7 % in the benchmark; optimal never worse.
Source: `graph::hungarian()`, `dispatch.csv`.

**Q59. How do you find the nearest junction?**
Short: A k-d tree, O(log n) expected, instead of scanning 144,090 junctions.
Source: `RoadGraph::nearest_node()`.

**Q60. Why no travel time?**
Short: The road file has lengths, not speeds; times would need speeds and a heuristic divided by max speed.
Source: `include/safetrail/graph/astar.hpp`.

## 9. Geometry

**Q61. How does point-in-polygon work?**
Short: Ray casting — count crossings of a horizontal ray; odd = inside.
Source: `geo::contains()`.

**Q62. How are vertices and horizontal edges handled?**
Short: The half-open rule `(y1 > py) != (y2 > py)` counts each vertex for exactly one edge and never counts horizontal edges.
Source: `ring_crossings()` in `containment.cpp`.

**Q63. Holes?**
Short: Holes add to the same parity count (counting separately and subtracting breaks nested holes).
Source: `contains()`.

**Q64. Why also winding number?**
Short: An independent second implementation to cross-check; it found a hole-orientation bug.
Source: `geo::contains_winding()`, `tests/geo/polygon_holes_test.cpp`.

**Q65. ★ How does GPS uncertainty affect containment?**
Short: A fix is a disc. Inside only if signed distance < −accuracy; outside if > +accuracy; otherwise **uncertain**.
Source: `classify()` in `containment.cpp`.

**Q66. What is the signed distance?**
Short: Distance to the nearest boundary edge, negative inside.
Source: `geo::signed_distance_m()`.

**Q67. How is the query box made conservative?**
Short: Radius = accuracy + speed × 300 s, clamped to 100 m–10 km, with the longitude stretched by 1/cos(latitude).
Source: `Evaluator::query_radius_m()`, `Bbox::around()`.

**Q68. How do you reject invalid polygons?**
Short: Self-intersection check: pairwise below 80 vertices, Shamos–Hoey sweep line above; finite coordinates; holes inside the outer ring.
Source: `Polygon::validate()`, `src/geo/sweep_line.cpp`.

## 10. Complexity analysis

**Q69. Per-fix cost of the engine?**
Short: Index O(log n + k) expected + O(k·V) exact geometry + O(s) reconciliation of open states.
Deeper: Measured 2.24 µs at 1,038 zones; ≈ 5–5.7 µs at constant density up to 50,038.
Source: `evaluator_cost.csv`.

**Q70. ★ What happens if 100,000 zones overlap the same region?**
Short: Then k = 100,000 for a query there: every index must return all of them, so the query is effectively O(n) and the exact geometry dominates. No index can avoid returning genuine matches.
Deeper: The benchmark shows the trend: with ~98 overlapping per query the quadtree's ratio plateaus at ~33×. The evaluator's optional capped policy would rank and cut, but it is off because a dropped candidate is a missed breach.
Source: `index_scaling.csv`, `CandidatePolicy` in `evaluator.hpp`.

**Q71. Why does the speedup flatten in one district?**
Short: k grows with n there; O(log n + k) is dominated by k.
Source: `docs/RESULTS.md` §1.

**Q72. Triage complexity?**
Short: Building the triage order is O(n log n) for n open alerts (push all, pop all).
Source: `Platform::triage_order()`.

**Q73. Timer wheel complexity?**
Short: schedule O(1); cancel O(b) for b entries in that slot; advance O(D + F + S), amortised O(1) per timer.
Source: `timer_wheel.hpp`.

**Q74. Merkle proof size?**
Short: O(log n) sibling hashes.
Source: `MerkleLog::prove()`.

**Q75. Space per tracked user?**
Short: About 6.9 KB (measured).
Source: `multi_user.csv`.

## 11. Benchmarking

**Q76. Why is brute force the baseline?**
Short: It is obviously correct and represents "no data structure".
Source: `apps/safetrail_bench.cpp`.

**Q77. How is speedup computed?**
Short: The median over 11 rounds of (brute-force time / index time), each pair measured back to back.
Source: protocol comment in `safetrail_bench.cpp`.

**Q78. ★ Why is the comparison fair?**
Short: Same data and queries; checksums must match; rotated order per round; ≥ 20 ms samples after warm-up; build cost reported separately; both regimes and every size reported; and a second comparison against a bbox scan where the margin is smaller, also reported.
Source: `07_BENCHMARKS_AND_PERFORMANCE.md` §8.

**Q79. Why do results vary between sessions?**
Short: Absolute times depend on machine state; the R-tree query is ~1 µs, the most sensitive. Its ratio moved ~100×–236× between sessions; within a session 211–224×.
Source: `docs/RESULTS.md`.

**Q80. Is 224× a guarantee?**
Short: No — one session's measurement on one laptop with synthetic zones; quote ~100× as the floor.

**Q81. What does STR gain?**
Short: 6.26× faster queries, 14,383 vs 21,368 nodes, faster build at 100k.
Source: `index_build.csv`.

**Q82. How is correctness checked in the benchmark?**
Short: Order-independent checksums of each contender's results must be equal; section 5 compares sets exactly; CI runs 8 gates.
Source: `safetrail_bench.cpp`.

## 12. Testing and correctness

**Q83. ★ How do you prove the optimised result is correct?**
Short: Differential tests against brute force on ~55,000 hostile random queries per index, invariant audits after every operation, byte-identical engine output under all indexes, and mutation testing.
Source: `tests/index/differential_test.cpp`, `08_TESTING_AND_CORRECTNESS.md`.

**Q84. What is mutation testing?**
Short: Inject a realistic bug; the suite must fail. 28 mutants, all caught.
Source: `tools/mutation_check.py`.

**Q85. Give an example mutant.**
Short: "quadtree query skips the NE child" — killed by `index/differential_test`.

**Q86. What hostile workloads?**
Short: Identical boxes, zero-area boxes, touching edges, both hemispheres, poles, past longitude 180, far inserts.
Source: `README.md`.

**Q87. What is an invariant audit?**
Short: `check_invariants()` verifies structural properties (tiling, MBRs, AVL balance, max_high) after every operation in the tests.

**Q88. Most important bug found?**
Short: Hysteresis let the uncertain band through — about 60 % of the demo's enter/exit events were flaps; and the state machine missed exits for zones that left the candidate window.
Source: `docs/DEFECT_LOG.md` pass 6.

**Q89. How many tests?**
Short: 51 test files, 12,166 checks; 38 end-to-end HTTP checks; 28/28 mutants.

**Q90. What do sanitizers add?**
Short: ASan/UBSan catch memory errors and undefined behaviour at run time (including float-to-int overflow).
Source: CI `sanitize`, `make validate`.

## 13. Backend and frontend architecture

**Q91. How do frontend and backend communicate?**
Short: JSON over HTTPS on the same origin; the console polls `/api/live` once a second.
Source: `web/app/js/app.js`, `api.js`.

**Q92. Why one thread?**
Short: One writer, no locks or races; at ~3 µs per fix it is far from saturated.
Source: `src/net/http.cpp`.

**Q93. Where is state stored?**
Short: In memory in one `Platform` object — no database.
Source: `include/safetrail/app/platform.hpp`.

**Q94. ★ What happens if two events arrive out of order?**
Short: A fix older than the last accepted one is recorded in the trail (binary-search insert) but not evaluated; offline events are ordered by Lamport (counter, device), and duplicates are rejected by that stamp.
Source: `Platform::ingest_fix()`, `sync::Reconciler::merge()`.

**Q95. ★ How do you prevent duplicate alerts?**
Short: Events fire only on state *changes*; hysteresis suppresses flapping; duplicate fixes are dropped; approach and dwell are reported once per stay; related alerts collapse into one incident; replayed offline events are rejected by (counter, device).
Source: `Evaluator::emit_transition()`, `HysteresisState`, `Correlator`, `Reconciler`.

**Q96. ★ What is hysteresis and why is it needed?**
Short: A Schmitt-trigger-like state machine: confirm entry only 15 m inside after 3 agreeing fixes and 5 s; exit only 25 m outside after 3 agreeing fixes. Without it GPS noise produces storms of enter/exit events.
Deeper: White-noise test: naive 20,848 transitions vs target 521; filtered 343.
Source: `src/fence/hysteresis.cpp`, `hysteresis_ab.csv`.

**Q97. How does the map work without a map library?**
Short: Own Web-Mercator projection on a canvas; roads fetched as tiles from `/api/roads`.
Source: `web/app/js/map.js`.

**Q98. How are scenarios simulated?**
Short: `ScenarioRunner` scripts movements and sends fixes through the same `ingest_fix()`; nothing bypasses the engine.
Source: `src/app/scenario.cpp`.

## 14. Security and deployment

**Q99. How are passwords stored?**
Short: PBKDF2-HMAC-SHA256, random salt, 120,000 iterations (10,000 on the public host with 256-bit random passwords).
Source: `src/app/crypto.cpp`.

**Q100. How do you prevent CSRF?**
Short: Every mutation must carry `X-SafeTrail-Request: 1`, which a cross-site form can't set; SameSite=Strict cookies as a second layer.
Source: `Api::route()`.

**Q101. Who can see a tourist's location?**
Short: Staff; a responder only while assigned; the tourist themself. Every read by someone else is audited in a Merkle log.
Source: `Platform::may_view_location()`, `audit_access()`.

**Q102. How do you stop password guessing?**
Short: Three locks: per address (10 failures → 5 min), per account+address (5 → 60 s), per account (25 → 60 s); plus a server-wide sign-in rate in public mode.
Source: login handler in `api.cpp`.

**Q103. How is the visitor's IP known behind a proxy?**
Short: From `CF-Connecting-IP`, trusted only when the connection comes from a private (proxy) range; Cloudflare refuses client-set copies.
Source: `Api::client_of()`.

**Q104. Where are secrets?**
Short: Render generates the four passwords as environment variables; the server refuses to start without them and never prints them.
Source: `render.yaml`, `apps/safetrail_server.cpp`.

**Q105. What happens when the free host sleeps?**
Short: After 15 idle minutes it stops; the next request wakes a fresh instance (13.5 s measured); all state resets.
Source: `docs/final/DEPLOYMENT_GUIDE.md` §1.6.

## 15. Advanced questions and limitations

**Q106. What is a Merkle inclusion proof?**
Short: The sibling hashes from a leaf to the root; recomputing the root proves the entry is present and unedited.
Source: `src/evidence/merkle_log.cpp`, `verifyInclusion()` in `history.js`.

**Q107. What are Lamport clocks for?**
Short: Ordering events from offline phones with wrong clocks: receive sets max(own, received) + 1; order by (counter, device).
Source: `include/safetrail/sync/lamport.hpp`.

**Q108. How does the offline phone evaluate zones?**
Short: It receives a serialised geohash index (bytes) and runs the evaluator locally; events are queued and reconciled on reconnect.
Source: `Platform::go_offline()`, `reconnect()`.

**Q109. What is triage priority?**
Short: severity + 0.5 × minutes open + 0.1 × km to the nearest free responder, + 100 for SOS; ordered by a max-heap.
Source: `alert::triage_score()`.

**Q110. What is escalation?**
Short: An incident-worthy alert unacknowledged for 2 minutes rises one severity level; deadlines live in a timing wheel.
Source: `Platform::escalate()`, `EscalationTracker`.

**Q111. Biggest limitation?**
Short: Simulated people and in-memory state — no persistence; on the free host every restart is a fresh world.
Source: `10_LIMITATIONS_AND_FUTURE_SCOPE.md`.

**Q112. Why is removal O(n) in the indexes?**
Short: No id → node map; adding one would make removal O(log n). Edits are rare compared with queries.

**Q113. What about the antimeridian?**
Short: Zones crossing ±180° are refused; query boxes don't wrap — documented, tested.
Source: `tests/geo/wraparound_test.cpp`.

**Q114. What would you do with more time?**
Short: Persistence with signed Merkle roots, field evaluation on real traces, a phone app using the shipped geohash, O(log n) removal, travel-time routing.

**Q115. What is implemented but not used by the website?**
Short: The jurisdiction hierarchy, the QR digital-ID verification, Kuhn's matching, k-NN queries — tested library code.
Source: `src/jurisdiction/`, `src/evidence/digital_id.cpp`.
