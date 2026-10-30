# Data structures, explained

One section per structure, each with the same parts: the problem, why this
structure, how it works, how this repository implements it, where SafeTrail
uses it, cost, and what it is compared against. Each can be watched running on
the real code in the web app's **Data Structures Lab**.

The deeper invariants and proofs are in [../DATA_STRUCTURES.md](../DATA_STRUCTURES.md);
measured costs in [../RESULTS.md](../RESULTS.md).

---

## 1. Brute force — the baseline and the oracle

- **Problem:** which hazard zones' boxes overlap the box around a GPS fix?
- **How:** test every box. Θ(n) per query.
- **Implementation:** `index::BruteForceIndex` (`src/index/brute_force.cpp`), a vector scan.
- **Why keep it:** it is obviously correct, so every fast index is compared against it — in the tests (`tests/index/differential_test.cpp`), in the benchmark, and live in the Lab, which prints the mismatch count for every query.

## 2. Quadtree — partition space

- **Problem:** the same query, without looking at every zone.
- **How:** a square region split into four children once it holds more than `capacity` boxes (default 8, max depth 12). A box that straddles a split line stays in the internal node. A query descends only into children whose region meets the query box.
- **Implementation:** `index::Quadtree` (`src/index/quadtree.cpp`). The root is fitted to the data, not to the whole planet (a world root wastes ~11 levels on Meghalaya); inserting outside the root doubles it outward; deleting collapses a subtree that has fallen below capacity.
- **Cost:** O(log n + k) expected, O(n) worst case (all boxes straddling the centre).
- **Lab:** shows every node and, step by step, which nodes the query entered (teal) and pruned (red).

## 3. R-tree — partition items

- **Problem:** the same, with nodes that adapt to where the zones actually are.
- **How:** every node is the minimum bounding rectangle (MBR) of its children; siblings may overlap. All leaves are at the same depth. On overflow, Guttman's **quadratic split** picks the two entries that would waste most area together as seeds and distributes the rest.
- **STR bulk loading:** sort all boxes by centre longitude, cut into ⌈√P⌉ vertical slices (P = leaf count), sort each slice by latitude, pack runs of `M`. Fewer, fuller, less-overlapping nodes than n inserts.
- **Implementation:** `index::RTree` (`src/index/rtree.cpp`); `build()` is STR, `build_incremental()` keeps the insert-built tree for comparison; removal condenses under-full nodes and reinserts orphans.
- **Where:** the platform's live index (`app::Platform`).
- **Cost:** O(log n + k) expected, O(n) worst; STR build O(n log n).

## 4. Geohash / Z-order — a flat, shippable index

- **Problem:** an index a phone can download as bytes and use with no network.
- **How:** interleave the bits of latitude and longitude (Morton code) of each box centre into a 48-bit key; sort; answer range queries by padded key ranges and binary search.
- **Implementation:** `index::Geohash` (`src/index/geohash.cpp`) with `serialize()/deserialize()`.
- **Where:** an offline phone in the platform evaluates zones from a geohash index shipped to it as bytes (`Platform::go_offline`).

## 5. k-d tree — nearest point

- **Problem:** the nearest of 144,090 road junctions to a responder or an incident.
- **How:** median split (`std::nth_element`) on alternating axes; nearest-neighbour search visits the far side of a split only when the split plane is no farther than the best point so far.
- **Implementation:** `index::KdTree<Id>` (`include/safetrail/index/kd_tree.hpp`), used by `graph::RoadGraph::nearest_node()`. A linear-scan oracle (`nearest_node_linear`) uses the same metric so they agree node for node.
- **Cost:** build O(n log n), query O(log n) expected.

## 6. AVL interval tree — the time dimension

- **Problem:** zones have validity windows; history asks "which were in force at t?"
- **How:** a BST keyed on (start, end, insertion sequence), AVL-balanced, **augmented** with the maximum end time in each subtree. A stabbing query skips every subtree whose maximum end is ≤ t.
- **Implementation:** `ds::IntervalTree<T>` (`include/safetrail/ds/interval_tree.hpp`), index-based nodes with a free list, single-path deletion, a general rebalance used by both insert and delete, and `check_invariants()` (BST order, balance, augmentation, size) run after every operation in its tests.
- **Cost:** insert/delete O(log n); stab O(min(n, (k + 1) log n)).
- **Lab:** each operation's tree, balance factors, heights and rotations; the nodes a stab entered.

## 7. Persistent quadtree — every past version

- **Problem:** "what were the rules at 14:32?" for every past moment, cheaply.
- **How:** **path copying** — an update copies only the root-to-leaf path it changes; every other node is shared by reference (`shared_ptr<const Node>`) with the previous version. Validity changes copy nothing: they append one record to the zone's history, and an interval tree over all historical windows answers time queries.
- **Implementation:** `index::VersionedIndex` (`src/index/versioned_index.cpp`).
- **Where:** zone history in the platform; the Investigation page's "zones in force at t".
- **Cost:** O(depth) new nodes per geometry change; a query against the past costs the same as one against the present.
- **Lab:** two consecutive versions with the copied path in amber and shared nodes in grey.

## 8. Graph (adjacency list), Dijkstra, A*

- **Problem:** route a responder along roads.
- **Representation:** `graph::RoadGraph` — `vector<vector<Edge{to, weight_m}>>`, weights validated non-negative and finite at insertion (Dijkstra's correctness depends on it).
- **Dijkstra:** binary-heap frontier with lazy deletion; deterministic tie-breaking on node id.
- **A\*:** f = g + h, h = great-circle distance to the target, which never exceeds a road distance (`heuristic_is_admissible()` checks it), so A* returns a shortest path while settling fewer nodes.
- **Where:** `Platform::recommend` (one Dijkstra from the incident, valid because the graph is undirected), `Platform::route_between` (A*).
- **Lab:** settle order step by step, the heap's contents before each pop, the two searches' work side by side.

## 9. Binary heap

- **Implementation:** `ds::BinaryHeap<T, Less>` (`include/safetrail/ds/priority_queue.hpp`), array-backed, sift up/down.
- **Where:** Dijkstra and A* frontiers; `alert::TriageQueue`, which orders open alerts by severity, age and isolation.

## 10. Hungarian assignment

- **Problem:** several incidents, several free responders — minimise total travel.
- **Implementation:** `graph::hungarian` (`src/graph/bipartite_match.cpp`), O(n³), validated input (ragged rows, NaN, more rows than columns are rejected with a status). Greedy cheapest-pair-first (`dispatch::assign_greedy`) is kept beside it for comparison.
- **Where:** Incidents → "Batch dispatch: greedy vs optimal".

## 11. Union-find with rollback

- **Implementation:** `ds::RollbackDSU` — union by rank, no path compression (so every change can be undone), an undo stack, `snapshot()` / `rollback_to()`.
- **Where:** `group::CohesionMonitor` (members within the proximity distance are united; more than one component = the group has split) and `alert::Correlator` (alerts close in space and time are united into one incident).

## 12. Open-addressing hash table

- **Implementation:** `ds::HashMap<K, V, H>` — power-of-two table, Fibonacci hashing, linear probing, tombstones, rehash at 70 % occupancy counting tombstones, grow only when live entries need it.
- **Where:** usernames → ids; API sessions (keyed by SHA-256 of the token); alert escalation records; offline-sync de-duplication by (Lamport counter, device); per-actor audit throttling.

## 13. Hashed timing wheel

- **Implementation:** `ds::TimerWheel<Id>` — slots of one tick; a deadline goes to slot (expiry tick mod slots) and fires on the first tick at or after it; cancellation removes it from its slot.
- **Where:** `alert::EscalationTracker` (wheel + hash map): an operator alert unacknowledged for 2 minutes escalates one severity level. The platform advances it once per simulated second, so each advance is O(1) plus what fires.

## 14. Circular buffer

- **Implementation:** `ds::CircularBuffer<T, N>`, N = 64 for GPS pings.
- **Where:** speed and heading estimation, the stationary and signal-lost anomaly detectors.

## 15. Merkle tree (evidence log)

- **Implementation:** `evidence::MerkleLog` — RFC 6962 hashing (leaf = SHA-256(0x00 ‖ entry), node = SHA-256(0x01 ‖ left ‖ right)), inclusion and consistency proofs, validated binary save/load; SHA-256 itself in `src/evidence/sha256.cpp`.
- **Where:** every platform log entry is a leaf; the access audit log is a second Merkle log. The Investigation page re-verifies proofs in the browser with WebCrypto; `tools/api_e2e.py` re-verifies with Python's hashlib.

## 16. Lamport clocks and the offline queue

- **Implementation:** `sync::LamportClock`, `sync::OfflineQueue` (append-only, survives restart via validated file format), `sync::Reconciler` (idempotent merge, ordered by (counter, device), de-duplicated in a hash set).
- **Where:** offline phones in the platform; the offline scenario.
