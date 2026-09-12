# Course Mapping — which Data Structures concept each part demonstrates

This maps the implementation onto the concepts a Data Structures course covers, and
draws a hard line between **CORE** (the five graded structures + the geometry that
makes them useful) and **EXTENSIONS** (substantial supporting work that is not
required to understand or defend the core).

Read this to answer *"which topics from the syllabus does this project actually
exercise?"* — the answer is "most of them, and here is exactly where."

---

## The four levels (the whole project in one view)

| Level | What | Files |
|---|---|---|
| **1 — Core problem** | Efficient repeated geofencing | `fence/evaluator.*` |
| **2 — Core structures** | Brute force · Quadtree · R-tree · Interval tree · Persistent quadtree | `index/`, `ds/interval_tree.hpp` |
| **3 — Core algorithms** | Ray casting · Winding number · Segment intersection · State transitions | `geo/`, `fence/` |
| **4 — Extensions** | Groups · Prediction · Offline · Merkle · Routing · Dispatch · Jurisdiction · … | everything else |

## The five core structures → concepts (at a glance)

| Core structure | Concepts it demonstrates |
|---|---|
| **Brute force** | baseline · complexity analysis (O(n)) · correctness oracle |
| **Quadtree** | trees · recursion · spatial indexing · average vs worst case |
| **R-tree** | balanced-by-construction · bulk loading (STR) · indexing |
| **Interval tree** | balanced (AVL) trees · augmented BST · guaranteed bounds |
| **Persistent quadtree** | persistence · structural sharing · immutability |

This same five-row map is shown live in the dashboard's "course concepts" panel.

---

## Concept → implementation

Legend: **[CORE]** is graded spine; **[EXT]** is extension.

### Trees (general)

| Concept | Where | Notes |
|---|---|---|
| Recursive spatial subdivision | **[CORE]** Quadtree `index/quadtree.hpp` | 4-way split; doubling root expansion; subtree collapse on delete |
| Bounding-volume hierarchy | **[CORE]** R-tree `index/rtree.hpp` | items in rectangles; Guttman quadratic split |
| Tree traversal / recursion | **[CORE]** all indexes; ray casting | descent + backtracking |

### Balanced (self-balancing) trees

| Concept | Where | Notes |
|---|---|---|
| AVL balancing + rotations | **[CORE]** Interval tree `ds/interval_tree.hpp` | height ≤ 1.44·log₂(n+2) asserted every run; rotation-repairing delete |
| Augmented BST (interval augmentation) | **[CORE]** Interval tree | each node stores subtree `max_high` for pruning |

### Spatial trees

| Concept | Where | Notes |
|---|---|---|
| Quadtree | **[CORE]** `index/quadtree.hpp` | primary spatial index |
| R-tree + STR bulk loading | **[CORE]** `index/rtree.hpp` | the head-to-head comparison |
| k-d tree (k-NN) | **[EXT]** `index/kd_tree.hpp` | nearest road junction / responder |
| Z-order / Morton (geohash) | **[EXT]** `index/geohash.hpp` | third index + serialisation format |

### Persistence

| Concept | Where | Notes |
|---|---|---|
| Persistent (fully-versioned) structure | **[CORE]** Persistent quadtree `index/versioned_index.hpp` | path copying + structural sharing, 13× vs full copies |
| Append-only versioned log | **[CORE]** validity history `index/versioned_index.hpp` | `O(1)` per change, `O(log h)` lookup |

### Hashing

| Concept | Where | Notes |
|---|---|---|
| Open addressing (linear probing) | **[EXT]** `ds/hash_table.hpp` | tombstone delete + same-size rebuild to bound churn |
| Cryptographic hashing | **[EXT]** `evidence/sha256.hpp` | SHA-256 from scratch, checked vs NIST vectors |

### Priority queues / heaps

| Concept | Where | Notes |
|---|---|---|
| Binary min-heap | **[EXT]** `ds/priority_queue.hpp` | Dijkstra/A* frontier; alert triage |
| Hashed timing wheel | **[EXT]** `ds/timer_wheel.hpp` | escalation deadlines; not flatly O(1) — see worst-case table |

### Graphs

| Concept | Where | Notes |
|---|---|---|
| Adjacency-list graph | **[EXT]** `graph/road_graph.hpp` | real OSM roads or synthetic grid |
| Dijkstra shortest path | **[EXT]** `graph/dijkstra.hpp` | checked vs Floyd–Warshall |
| A* search | **[EXT]** `graph/astar.hpp` | admissible haversine heuristic; 77.7% fewer expansions |
| Bipartite matching / assignment | **[EXT]** `graph/bipartite_match.hpp` | Kuhn's + Hungarian, vs exhaustive |

### Union-Find (disjoint sets)

| Concept | Where | Notes |
|---|---|---|
| Union by rank | **[EXT]** `ds/dynamic_connectivity.hpp` | no path compression (must stay undoable) |
| Rollback / undo | **[EXT]** `ds/dynamic_connectivity.hpp` | `O(1)` undo per union — the interesting trade-off |
| DSU clustering | **[EXT]** `alert/correlator.hpp` | spatio-temporal alert → incident collapse |

### Searching

| Concept | Where | Notes |
|---|---|---|
| Range search | **[CORE]** all spatial indexes | the core operation |
| Binary search | **[CORE]** validity-history lookup; **[EXT]** many |
| Nearest-neighbour search | **[EXT]** k-d tree |

### Sorting

| Concept | Where | Notes |
|---|---|---|
| Comparison sort (as a tool) | **[CORE]** STR bulk load sorts by centre lon/lat; **[EXT]** many | `std::sort` is permitted (an algorithm, not a graded structure) |
| Sweep-line ordering | **[EXT]** `geo/sweep_line.hpp` | Shamos–Hoey self-intersection, AVL status structure |

### Computational geometry (Level-3 core algorithms)

| Concept | Where | Notes |
|---|---|---|
| Point-in-polygon (ray casting) | **[CORE]** `geo/containment.hpp` | the fundamental test |
| Point-in-polygon (winding number) | **[CORE]** `geo/containment.hpp` | independent cross-check |
| Segment intersection / orientation | **[CORE]** `geo/segment.hpp` | shared predicate across validation, sweep, containment |
| Three-valued containment | **[CORE]** `geo/containment.hpp` | Inside / Outside / Uncertain under GPS error |

### Complexity analysis

| Concept | Where | Notes |
|---|---|---|
| Average vs worst case | **[CORE]** [DATA_STRUCTURES.md](DATA_STRUCTURES.md) worst-case table | explicit for every structure |
| Output-sensitive bounds (`+k`) | **[CORE]** [RESULTS.md](RESULTS.md) §1 | the candidate column proves the `k` ceiling empirically |
| Amortised analysis | **[EXT]** hash-table rebuild, Merkle append | stated where it applies |
| Empirical validation | **[CORE]** `make bench` | every asymptotic claim measured against the code |

---

## What the CORE alone demonstrates

If an examiner only had time for the five core structures, they would still see:
recursion, tree construction and traversal, self-balancing (AVL) trees, augmented
BSTs, two contrasting spatial indexes with a measured comparison, persistence via
structural sharing, range and binary search, sorting as a bulk-load tool,
output-sensitive complexity analysis, and computational geometry with an
independent cross-check — **plus** the methodology that ties it together: a
brute-force oracle, randomized equivalence testing, and deterministic,
reproducible measurement.

That is a complete Data Structures project on its own. The extensions add breadth
(graphs, hashing, priority queues, union-find, more algorithms) but the depth is in
the core.
