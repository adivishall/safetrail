# 06 — The Data Structures Lab, in depth

> Frontend: `web/app/js/pages/lab.js`. Backend: `src/app/lab.cpp` (`run_lab()`
> dispatches to `lab_spatial`, `lab_kdtree`, … `lab_geometry`). Endpoint:
> `POST /api/lab/<experiment>` (any signed-in role). Fifteen experiments.

## 1. Real operations, not illustrations — be exact about this

Every experiment **constructs the real C++ class from this repository on the
server**, runs the requested operations, and returns the structure's **own
internal state and trace** as JSON. The JavaScript only draws; it computes
nothing about the structures (`lab.js` header comment). Concretely:

| What you see | Where it comes from |
|---|---|
| Quadtree / R-tree node rectangles and the visit order | `Quadtree::describe()`, `trace_query()`; `RTree::describe()`, `trace_query()` |
| Query result ids and "Mismatch" | `idx.query()` compared set-for-set with `BruteForceIndex` |
| AVL nodes, heights, `max_high`, rotation counts | `ds::IntervalTree` node view and `rotations_left/right()` after each operation |
| Persistent quadtree versions and shared nodes | `VersionedIndex` visualisation nodes tagged `shared` |
| Dijkstra/A* settle order and heap contents | `graph::SearchTrace` filled by `dijkstra()` / `astar()` |
| Heap array, hash slots, wheel slots, ring buffer cells | the class's actual storage after each operation |
| Merkle root | `MerkleLog::root()`; the drawn tree is recomputed and must equal it ("drawn tree root = MerkleLog::root()") |

Two honest qualifications:
1. **Replay of recorded steps.** For the operation-list experiments (AVL, heap,
   DSU, hash, wheel, ring, Lamport), the server runs the *whole* list at once and
   returns a snapshot after each step; the **stepper** then replays those
   snapshots. You are scrubbing through recorded real states, not watching the
   structure run in real time.
2. **Synthetic inputs.** The Lab does **not** use the live OSM zones. It
   generates its own data on the server: random boxes in a 1° × 1° square
   (`lab_boxes()`, "clustered" around five centres or "uniform"), random points,
   a jittered grid graph (`RoadGraph::grid()`), a fixed concave polygon, etc.
   This keeps the experiments small enough to draw and repeatable from a seed.

**Inputs are bounded on the server** (`inum()`, `ops_of()`): e.g. ≤ 3,000 boxes,
≤ 600 k-d points, grids ≤ 30 × 30, ≤ 80–120 operations, ≤ 32 Merkle leaves. An
out-of-range value returns **400** with a message (shown as a red toast), and the
view keeps its last good result. In public mode a Lab run costs 5 rate-limit
tokens.

## 2. Shared controls

| Control | Behaviour (`stepper()` in `lab.js`) |
|---|---|
| Experiment list (left) | Grouped: Spatial indexes, Trees, Graphs, Hashing and queues, Integrity and time. The URL becomes `#/lab/<id>`. |
| "Facts" cards | Static text: Problem it solves, How it works, Where SafeTrail uses it, Time, Space, Compared with. |
| **◀ / ▶** and the slider | step backward/forward through recorded steps; label "step k / n". |
| **Play / Stop** | animates the steps (interval ≈ 2.4 s / n, at least 30 ms); stops automatically if the panel is replaced. |

There is **no scenario, speed or pause** in the Lab: those controls belong to
the operations console (top bar). The Lab is request–response: each button sends
one request.

## 3. The fifteen experiments

### 3.1 Brute force · Quadtree · R-tree · Geohash (`spatial`)
- **Controls:** Zones (10–3,000), Distribution (clustered/uniform), Node capacity
  (2–64), Seed, Show structure (Quadtree / R-tree STR / R-tree by insertion),
  **Build and query**; **drag on the canvas** to draw a new query box.
- **Server:** builds `BruteForceIndex`, `Quadtree(cap, 12)`, `RTree(cap).build()`
  (STR), `RTree(cap).build_incremental()`, `Geohash` on the **same** boxes; runs
  the same query on each.
- **Drawing:** grey = items; coloured outlines = the chosen structure's nodes by
  depth; **teal** = nodes the query descended into; **red dashed** = nodes pruned;
  **amber fill** = results; the query box.
- **Metrics table:** Results, **Mismatch vs brute** (must be 0), Query µs
  (**median of 101 runs** of `query()` on the server), Visited, Nodes, Depth,
  Build µs (median of 3), Bytes. Also "Traced queries are checked against
  query(): all identical."
- **Conclusions:** identical answers (correctness); pruning visible as red
  rectangles; R-tree nodes hug the data while quadtree cells are fixed; STR builds
  fewer nodes than insertion. A single small query can favour either R-tree; the
  committed benchmark averages many.

### 3.2 k-d tree nearest neighbour (`kdtree`)
- **Controls:** Points (5–600), **Rebuild**; **click** to move the query point.
- **Server:** `index::KdTree<uint32_t>` built on random points; `nearest_traced()`
  and a linear scan with the same metric.
- **Drawing:** split lines (vertical on longitude, horizontal on latitude), visited
  nodes lit in search order, a line to the nearest point.
- **Metrics:** Nearest (tree), Nearest (linear scan), **Agree**, Nodes visited of n.
- **Conclusion:** the far side of a split is visited only when the split line is
  closer than the best so far.

### 3.3 Z-order (Morton) curve (`zorder`)
- **Controls:** Grid 2×2 … 32×32, **Draw**.
- **Server:** `index::Geohash::morton()` for each cell centre, then sort.
- **Drawing:** the cells numbered in Morton order and the Z-shaped path.
- **Conclusion:** nearby cells mostly get nearby keys; the curve jumps at quadrant
  boundaries — why the geohash index needs a refine step.

### 3.4 AVL interval tree (`avl`)
- **Controls:** Operations preset — *Ascending inserts (forces left rotations)*,
  *Zig-zag (forces double rotations)*, *Insert then delete*; **Stabbing query at
  t =**; **Run**; or a custom list like `5-30, 12-18, 40-70, -12` ("a-b" inserts
  [a, b); "-a" removes the interval starting at a).
- **Server:** `ds::IntervalTree<uint32_t>`; after each operation it records nodes,
  heights, `max_high`, rotation counts and the invariant checks; finally a traced
  stabbing query and a linear-scan count.
- **Drawing:** the tree with `[low,high)`, `max`, `h` (height), `bf` (balance
  factor); on the last step, stab-visited nodes in teal.
- **Chips:** the operation; rotations left/right; height for n nodes; **"AVL +
  augmentation invariants hold"**; stab results vs linear scan, nodes entered.
- **Conclusion:** height stays ≈ log n; `max_high` lets the stab skip subtrees.

### 3.5 Persistent quadtree (`persistent`)
- **Controls:** Zones (2–200), Edit (move one zone / add / remove / change
  validity only), Zone id, **Apply**.
- **Server:** `index::VersionedIndex` over random boxes; applies the edit; returns
  the before and after versions with each node tagged shared or new.
- **Drawing:** two trees side by side; **amber** = newly allocated nodes (the copied
  path); grey = shared with the previous version.
- **Chips:** versions, nodes allocated, "full copies would need …", **sharing ×**,
  validity records; a table "zones in force then" over time.
- **Conclusion:** a geometry change copies one path; a validity-only change copies
  **no** nodes (one appended record).

### 3.6 Adjacency list · Dijkstra · A* · priority queue (`graph`)
- **Controls:** Rows, Cols (3–30), Seed, Algorithm (Dijkstra / A*), **Run**;
  **click** a junction = source, **shift-click** = target.
- **Server:** `graph::RoadGraph::grid()` (jittered grid, segment lengths in metres);
  `graph::dijkstra(g, s, t, &trace)` and `graph::astar(g, s, t, &trace)`;
  `heuristic_is_admissible(g)`.
- **Drawing:** settled nodes in order; frontier; the final path; a panel showing
  the **binary-heap array** before each pop (`[i] key→node`, children at 2i+1, 2i+2).
- **Table:** Settled nodes, Heap pushes, **Stale pops (lazy deletion)**, Path cost
  for both; "Same cost: yes"; "Heuristic admissible: yes".
- **Conclusion:** same cost, far fewer settled nodes with A*.

### 3.7 Hungarian assignment vs greedy (`assignment`)
- **Controls:** Responders (1–10), Incidents (1–10, ≥ responders), Seed, **Solve**.
- **Server:** random cost matrix; `graph::hungarian(cost)`; greedy cheapest pair first.
- **Display:** the matrix with **teal** = Hungarian's choice, **amber outline** =
  greedy's; totals and "greedy pays X more (Y %)".
- **Conclusion:** greedy is sometimes optimal, never better.

### 3.8 Union-find with rollback (`dsu`)
- **Controls:** a, b, **Unite**, **Snapshot**, mark, **Roll back to mark**, **Clear**.
  Default list: unite 0 1, unite 2 3, snapshot, unite 1 3, unite 4 5, unite 5 3,
  rollback to mark 2, unite 6 7.
- **Server:** `ds::RollbackDSU` (n = 10); snapshot after each op.
- **Drawing:** forest with arrows to parents, roots in teal, ranks.
- **Chips:** the op (merged / already joined), components, **undo stack depth**.
- **Conclusion:** rollback restores the exact earlier forest because there is no
  path compression.

### 3.9 Binary heap (`heap`)
- **Controls:** value, **Push**, **Pop min**, **Clear**. Default: push 41, 17, 88,
  5, 23, 61, 9, 72, 30, then pop twice.
- **Server:** `ds::BinaryHeap<int64_t>` (min-heap).
- **Drawing:** the tree and the **same values as the array** with indices.
- **Conclusion:** the tree is only a view of the array; root = minimum.

### 3.10 Open-addressing hash table (`hash`)
- **Controls:** key, **put / erase / get**, **Insert 10 colliding keys**, **Clear**.
- **Server:** `ds::HashMap<uint32_t, uint32_t>` (16 initial slots, Fibonacci hash).
- **Display:** slots: teal ring = home slot, amber = probed, dashed `†` =
  tombstone; chips: result, probe length, "table rebuilt", live/slots and load,
  tombstones, growths, same-size rehashes.
- **Conclusion:** collisions lengthen probes; tombstones keep chains intact;
  rehash at 70 % load.

### 3.11 Hashed timing wheel (`wheel`)
- **Controls:** Alert id, Deadline ms, **Schedule**, **Cancel (acknowledge)**,
  Advance to ms, **Advance**, **Clear**. 8 slots of 1 s.
- **Server:** `ds::TimerWheel<uint32_t>(1000, slots, 0)`.
- **Display:** slots with their entries and expiry ticks, "◀ now"; fired list.
- **Conclusion:** a deadline more than one lap away waits in its slot and fires
  on a later lap.

### 3.12 Circular buffer (`ring`)
- **Controls:** none (fixed: push 10, 20, … 110 into capacity 8); stepper only.
- **Server:** `ds::CircularBuffer<int64_t, 8>`.
- **Display:** physical cells, head (next write), newest-first order.
- **Conclusion:** the 9th push overwrites the oldest; nothing is shifted or allocated.

### 3.13 Merkle tree and inclusion proofs (`merkle`)
- **Controls:** Leaves (comma separated, ≤ 32), Prove leaf, Tamper leaf (−1 none), **Build**.
- **Server:** `evidence::MerkleLog`; draws the tree from the leaf hashes and checks
  it equals `root()`; `prove(i)` and verification; if tampering, rebuilds with the
  edited leaf.
- **Display:** teal = the leaf and its ancestors; amber = the sibling hashes
  forming the proof; chips: root, proof verifies; tamper note ("no longer verifies").
- **Conclusion:** O(log n) hashes prove membership; any edit changes the root.

### 3.14 Lamport logical clocks (`lamport`)
- **Controls:** none (fixed: 3 devices, local/send/receive events).
- **Server:** `sync::LamportClock` per device: `tick()` for local/send,
  `observe()` for receive.
- **Display:** device lanes, stamps above events, send→receive arrows; the total
  order by (counter, device).
- **Conclusion:** every receive is stamped after its send, whatever the wall clocks.

### 3.15 Point in polygon under uncertainty (`geometry`)
- **Controls:** GPS accuracy slider (1–120 m); **click** to move the fix.
- **Server:** a fixed concave polygon; `geo::contains()` (ray casting),
  `geo::contains_winding()`, `geo::evaluate()` with the signed distance; polygon
  validity.
- **Display:** the polygon, the accuracy disc, the ray; Ray casting,
  Winding number, Signed distance, **INSIDE / OUTSIDE / UNCERTAIN**.
- **Conclusion:** uncertain exactly when |distance| < accuracy.

## 4. How performance is measured in the Lab

- Only the spatial experiment reports timings: query µs = **median of 101
  repetitions** of `query()`; build µs = median of 3 (`time_us()` in `lab.cpp`).
- These are measurements on **whatever machine serves the page** — on the free
  Render instance (0.1 CPU), slower and noisier than the committed laptop
  benchmarks. Use Lab timings for *relative* comparison within one request; quote
  `bench/results/` for numbers.

## 5. Connecting the Lab to the engine

| Lab experiment | Same class used by the platform |
|---|---|
| spatial | `index::RTree` is the live index (`Platform`), `Quadtree` and `BruteForceIndex` in tests and benchmarks |
| kdtree | `RoadGraph::nearest_node()` |
| zorder | `Geohash` shipped to the offline phone |
| avl | `VersionedIndex::active_at()` |
| persistent | `Platform::index_zone()` |
| graph | `Platform::recommend()` (Dijkstra) and `route_between()` (A*) |
| assignment | `Platform::plan_dispatch()` |
| dsu | `CohesionMonitor`, `Correlator` |
| heap | Dijkstra/A* frontier, `TriageQueue` |
| hash | sessions, usernames, reconciler, escalation |
| wheel | `EscalationTracker` |
| ring | `track::Tourist::pings` |
| merkle | `Platform::append_log()`, the access audit |
| lamport | offline phone + `Reconciler` |
| geometry | `Evaluator::evaluate()` step 5 |

## 6. Two viva questions about the Lab

- *Is the animation JavaScript re-implementing the structure?* No. The server
  returns the structure's own state after each operation; the browser draws it.
  For the spatial experiment the traced query is even checked against the plain
  `query()` and against brute force.
- *Why doesn't the Lab use the real zones?* So the pictures are small enough to
  read and reproducible from a seed. The real zones go through the same classes
  in the live engine and the benchmarks.
