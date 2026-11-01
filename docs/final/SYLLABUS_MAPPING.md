# Syllabus mapping — which Data Structures concepts SafeTrail demonstrates

Every row names the file that implements the concept and where SafeTrail
uses it. A concept is listed only if the code really does it; "could be
related" is not enough. The **Lab** column says which Data Structures Lab
experiment in the web app (`make app` → Data Structures Lab) shows it running
on the real C++ code.

Four groups:

- **A. Direct syllabus applications** — core course topics, implemented by hand here.
- **B. Supporting concepts** — course topics used along the way, often through the C++ standard library.
- **C. Beyond the syllabus** — advanced material; [ADVANCED_CONCEPTS.md](ADVANCED_CONCEPTS.md) explains each.
- **D. Not implemented** — listed so nobody claims them.

Each concept is explained twice: once in plain words, once technically, with a SafeTrail example.

---

## A. Direct syllabus applications

| Syllabus concept | Implementation | File / function | Where SafeTrail uses it | Complexity | Lab |
|---|---|---|---|---|---|
| Arrays / dynamic arrays | `std::vector` as the backing store of every structure; flat `ZoneStore` indexed by id | `fence::ZoneStore` (`src/fence/zone.cpp`) | zone storage, alert table, trails | O(1) index, amortised O(1) append | — |
| Circular (ring) buffer | fixed array + wrapping head | `ds::CircularBuffer` (`include/safetrail/ds/circular_buffer.hpp`) | last 64 GPS fixes per person | O(1) push and index | Circular buffer |
| Linked (pointer) structures | tree nodes owned by `unique_ptr` / `shared_ptr` | `index::RTree::Node`, `index::Quadtree::Node`, persistent `VersionedIndex::Node` | the spatial indexes | — | Spatial; Persistent |
| Stack | explicit stack in an iterative DFS | `ds::components_bruteforce` (`src/ds/dynamic_connectivity.cpp`) | test oracle for union-find | O(V + E) | — |
| Queue (priority) | binary min/max heap | `ds::BinaryHeap` (`include/safetrail/ds/priority_queue.hpp`) | Dijkstra and A* frontier; alert triage | push/pop O(log n), top O(1) | Binary heap; Graph |
| Trees (general), recursion | recursive build, query, delete | quadtree and R-tree `query_node`, `insert_into`, `remove_node` | every geofence query | O(depth) recursion | Spatial |
| Binary search tree | BST ordered on (low, high, insertion seq) | `ds::IntervalTree` (`include/safetrail/ds/interval_tree.hpp`) | zone validity history | O(log n) search | AVL |
| AVL tree, rotations | height-balanced BST, single and double rotations, rebalance on delete | `IntervalTree::rot_left/rot_right/rebalance` | as above | height ≤ 1.44 log₂(n+2); insert/delete O(log n) | AVL (shows each rotation) |
| AVL tree (second instance) | index-backed AVL tree as the sweep-line status structure | `src/geo/sweep_line.cpp` | rejecting self-intersecting zone polygons (`Polygon::validate`) | O(V log V) per polygon | — |
| Augmented tree | each node stores max end time of its subtree | `IntervalTree::refit`, prune in `descend` | "which rules were in force at t" | stab O(min(n, (k+1) log n)) | AVL (shows nodes visited) |
| Tree traversal | pre-order (describe), in-order (layout), DFS queries | `Quadtree::describe`, `IntervalTree::collect_view` | Lab, history queries | O(n) | AVL, Spatial |
| Quadtree | region quadtree over boxes, fitted root, collapse on delete | `index::Quadtree` (`src/index/quadtree.cpp`) | spatial index option | query O(log n + k) expected | Spatial |
| R-tree | Guttman quadratic split; STR bulk loading; condensing delete | `index::RTree` (`src/index/rtree.cpp`) | the platform's live spatial index | query O(log n + k) expected | Spatial (STR vs insert) |
| k-d tree | 2-d tree, median split, nearest neighbour with pruning | `index::KdTree` (`include/safetrail/index/kd_tree.hpp`) | snapping to road junctions | build O(n log n), NN O(log n) expected | k-d tree |
| Hashing, hash table | open addressing, linear probing, tombstones, load-factor rehash | `ds::HashMap` (`include/safetrail/ds/hash_table.hpp`) | usernames, sessions, escalation records, sync de-duplication | O(1) expected | Hash table |
| Collision handling | linear probing; deletion by tombstone | `HashMap::put/find/erase/maybe_rehash` | as above | probe length grows with load | Hash table (shows probe) |
| Graphs, adjacency list | `std::vector<std::vector<Edge>>` | `graph::RoadGraph` (`include/safetrail/graph/road_graph.hpp`) | 144,090-junction OSM road network | O(V + E) space | Graph |
| Shortest paths: Dijkstra | binary-heap Dijkstra with lazy deletion | `graph::dijkstra` (`src/graph/dijkstra.cpp`) | nearest responder by road distance | O((V + E) log V) | Graph |
| A* search | Dijkstra + admissible straight-line heuristic | `graph::astar` (`src/graph/astar.cpp`) | responder route | ≤ Dijkstra's work | Graph |
| Disjoint-set union (union-find) | union by rank, rollback stack | `ds::RollbackDSU` (`src/ds/dynamic_connectivity.cpp`) | group cohesion; alert → incident correlation | O(log n) per op | Union-find |
| Searching: binary search | `upper_bound` on a time-sorted trail; sorted geohash keys | `Platform::position_at` (`src/app/platform.cpp`), `index::Geohash` | "where was X at time t" | O(log n) | Investigation page |
| Algorithmic complexity, worst vs expected | brute force kept as the O(n) baseline; bounds stated per structure | `index::BruteForceIndex`; [ALGORITHM_COMPLEXITY.md](ALGORITHM_COMPLEXITY.md) | every comparison | — | Spatial (timings) |
| Empirical performance measurement | interleaved, paired, repeated benchmarks | `apps/safetrail_bench.cpp` (19 sections) | `make bench`, Analytics page | — | — |

## B. Supporting concepts

| Concept | Where | Notes |
|---|---|---|
| Sorting | `std::sort` / `std::stable_sort` in STR packing (`src/index/rtree.cpp`), Lamport merge (`src/sync/lamport.cpp`), incident ranking (`alert::rank_by_seriousness`); `std::nth_element` (median selection) in the k-d tree build | Library sorts are used; none is hand-written. The **stability** and **total order** requirements are worked out in the code: incident ranking needed an id tie-break because `std::sort` is unstable. |
| Selection (median) | `std::nth_element` in `KdTree::build_rec` | expected O(n) per level → O(n log n) build |
| Recursion vs iteration | recursive tree code; iterative DFS in the union-find oracle; iterative Dijkstra | |
| Amortised analysis | `std::vector` growth; hash table rehash; Merkle log append | the hash table rehashes by doubling **only** if live entries need it, else rebuilds at the same size to drop tombstones |
| Time–space trade-offs | persistent index (shares nodes instead of copying); per-zone validity checked in O(1) on the hot path but stored in an interval tree for history | [ALGORITHM_COMPLEXITY.md](ALGORITHM_COMPLEXITY.md) §6 |
| Bipartite matching | Kuhn's augmenting paths (`graph::kuhn_matching`) and the Hungarian algorithm (`graph::hungarian`) | `src/graph/bipartite_match.cpp`; dispatch |
| State machines | hysteresis filter (`fence::HysteresisState`), alert and incident life cycles | |
| Computational geometry | ray casting, winding number, segment intersection, Shamos–Hoey sweep line (with a balanced status tree) | `src/geo/` |
| Differential testing | every index against brute force, with invariant audits after every operation | `tests/index/differential_test.cpp` |
| Mutation testing | 23 injected bugs, each must fail a test | `tools/mutation_check.py` |

## C. Beyond the syllabus (detail in [ADVANCED_CONCEPTS.md](ADVANCED_CONCEPTS.md))

Persistent data structures and structural sharing · STR bulk loading · Z-order
(Morton) curves · hashed timing wheel · Merkle trees with RFC 6962 inclusion
proofs · SHA-256, HMAC, PBKDF2 · Lamport logical clocks and offline
reconciliation · bitemporal history (when it happened vs when the system knew)
· hysteresis and three-valued containment under GPS uncertainty · predictive
crossing · Hungarian assignment · battery-aware adaptive sampling · single-writer
event loop with `poll(2)` · deterministic replay across compilers.

## D. Not implemented (do not claim)

| Topic | Status |
|---|---|
| Breadth-first search | no BFS in the product; components come from union-find (the test oracle uses a DFS with an explicit stack) |
| Linked lists as a data structure | trees use pointers, but there is no linked list |
| Hand-written sorting algorithms (quick/merge/heap sort) | library sorts only; the heap exists as a priority queue, not as heapsort |
| B-trees / B+ trees, tries, skip lists, red-black trees | absent |
| Minimum spanning trees, topological sort, strongly connected components | absent |
| Dynamic programming | absent |
| String algorithms | absent |

---

## Every concept, explained twice

### Dynamic array (`std::vector`)
**Simply:** a row of boxes that grows when it fills, by moving into a bigger row.
**Technically:** contiguous storage with capacity ≥ size; appends are amortised O(1) because capacity doubles, so n appends copy O(n) elements in total.
**SafeTrail:** the zone store is a vector indexed by zone id, so `get(id)` is one array access.

### Circular buffer
**Simply:** a clock face with 64 slots; each new GPS fix goes in the next slot and overwrites the oldest.
**Technically:** `head = (head + 1) mod N`, `count = min(count + 1, N)`; element i (newest first) is at `(head − 1 − i) mod N`. O(1) everything, no allocation.
**SafeTrail:** speed, heading and the "stationary" and "signal lost" checks read the last 64 fixes.

### Stack
**Simply:** a pile of plates — take from the top.
**Technically:** LIFO; here a vector used with `push_back`/`pop_back` to run a depth-first search without recursion.
**SafeTrail:** the union-find test oracle labels connected components by DFS.

### Binary heap / priority queue
**Simply:** a tournament bracket in which the winner is always at the top.
**Technically:** a complete binary tree in an array; children of i at 2i+1 and 2i+2; parent ≤ children. Push sifts up, pop moves the last leaf to the root and sifts down: O(log n).
**SafeTrail:** Dijkstra pops the nearest unsettled junction; the alert board pops the most urgent alert, where urgency = severity + 0.5 per minute open + 0.1 per km to the nearest free responder.

### Tree, recursion and traversal
**Simply:** a family tree; to search it you ask each child to search its own branch.
**Technically:** recursive descent with pruning — a quadtree query returns at once from any node whose box misses the query box, so whole subtrees are skipped.
**SafeTrail:** every GPS fix runs one such query.

### Binary search tree and AVL tree
**Simply:** a sorted tree that keeps itself evenly shaped by "rotating" lopsided parts.
**Technically:** BST order on (start, end, insertion number); AVL keeps |height(left) − height(right)| ≤ 1 at every node by single or double rotations after inserts and deletes, so height ≤ 1.44 log₂(n + 2). Deletion can need a rotation at every level on the way up, so rebalancing is general rather than insert-specific.
**SafeTrail:** holds every zone's validity windows over all history.

### Augmented tree (interval tree)
**Simply:** each branch remembers the latest time anything under it ends, so a question about 14:32 can skip branches that ended at 14:00.
**Technically:** `max_high = max(own end, left.max_high, right.max_high)`, maintained by rotations; a stabbing query prunes any subtree with `max_high ≤ t`. Bound O(min(n, (k + 1) log n)).
**SafeTrail:** "which zones were in force at t?" in the persistent index.

### Quadtree
**Simply:** cut the map into four squares, and keep cutting any square that holds too many zones.
**Technically:** region quadtree over bounding boxes; root fitted to the data and doubled outward when a zone falls outside; a box straddling a split stays at the internal node; subtrees collapse after deletes.
**SafeTrail:** an index option; the Lab shows its cells and which ones a query entered.

### R-tree and STR packing
**Simply:** instead of cutting the map, put nearby zones into labelled envelopes, and envelopes into bigger envelopes.
**Technically:** height-balanced tree of minimum bounding rectangles (MBRs), node capacity 8; quadratic split on overflow (Guttman); **STR bulk load** sorts by longitude into √(n/M) vertical slices, sorts each slice by latitude, and packs runs of M — fuller nodes, less overlap.
**SafeTrail:** the platform's live index; its measured speedup over brute force at 100,000 zones is in [RESULTS.md](../RESULTS.md) §1 (regenerated by `make bench`).

### k-d tree
**Simply:** split the points alternately left/right and up/down at the middle one.
**Technically:** median split by `nth_element` on alternating axes; nearest-neighbour search descends towards the query, then crosses a split only if the splitting plane is closer than the best point found.
**SafeTrail:** snaps a responder or incident to the nearest of 144,090 road junctions.

### Hash table with linear probing
**Simply:** a key's number tells you which drawer to try; if it is taken, try the next one.
**Technically:** Fibonacci hashing `k × 2654435769`, masked to a power-of-two table; linear probing; deletion leaves a tombstone so probe chains do not break; rehash when (live + tombstones) reach 70 %, growing only if the live entries need it.
**SafeTrail:** the username index, sessions (keyed by SHA-256 of the token), alert escalation records, offline-sync de-duplication.

### Graph, adjacency list
**Simply:** each junction keeps a list of the roads leaving it.
**Technically:** `vector<vector<Edge{to, weight}>>`, O(V + E) space, neighbours in O(degree).
**SafeTrail:** the OpenStreetMap road network.

### Dijkstra and A*
**Simply:** Dijkstra grows a circle of "known shortest" junctions outward; A* grows it towards the target first, guided by straight-line distance.
**Technically:** Dijkstra settles nodes in increasing distance from a binary heap (lazy deletion: stale entries are skipped on pop). A* orders by f = g + h with h = great-circle distance, which never exceeds road distance (admissible), so the first time the target is popped its cost is optimal.
**SafeTrail:** one Dijkstra from an incident gives the road distance to every responder (valid because the road graph is undirected); A* then routes the chosen one. Assigning an incident in the walkthrough, A* settled 772 junctions where Dijkstra settles 5,557 for the same pair.

### Union-find (disjoint-set union) with rollback
**Simply:** everyone points to a group leader; to merge two groups, one leader points to the other.
**Technically:** union by rank keeps trees O(log n) deep; **no path compression**, because rollback must be able to restore every pointer exactly; each union is recorded on an undo stack.
**SafeTrail:** whether a tour group is still one group (members within 150 m are linked); which alerts are one incident (within 200 m and 10 minutes).

### Binary search
**Simply:** to find a time in a sorted list, look in the middle and throw away half.
**Technically:** `std::upper_bound` over the time-sorted trail, O(log n).
**SafeTrail:** "where was this person at 10:05?" in the Investigation page.

### Complexity, worst versus expected, measured
**Simply:** big-O says how the work grows; benchmarks say what it actually costs on this laptop.
**Technically:** spatial trees are O(log n + k) *expected* and O(n) *worst case*; the AVL tree's height bound is *guaranteed*; constants and cache effects decide real times, which is why every claim here has a measurement next to it.
**SafeTrail:** the speedup stops growing once k (zones that really overlap the query) dominates — measured in section 1 and explained in [TESTING_AND_BENCHMARKS.md](TESTING_AND_BENCHMARKS.md).
