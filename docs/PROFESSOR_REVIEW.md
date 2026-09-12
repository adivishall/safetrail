# Professor Review

An examiner's-eye assessment of this project as a **Data Structures course
project** — written to be read before the viva, and to be honest about where the
project is strong and where it is thin. Numbers are authoritative in
[RESULTS.md](RESULTS.md); question drills are in [VIVA.md](VIVA.md).

---

## The project in one screen

| Question | Answer |
|---|---|
| **What is it?** | An efficient tourist geofencing engine: given many hazard-zone polygons and a stream of tourist locations, decide repeatedly whether each tourist is inside / outside / uncertain, using hand-built spatial data structures and computational geometry. |
| **Core problem?** | Repeated geofencing gets expensive as zones grow. The naive check is O(n) per query (every zone, every tourist, every tick). Make it fast **without changing the answer.** |
| **Five primary data structures?** | Brute force (oracle) · Quadtree · R-tree · Interval tree · Persistent quadtree. |
| **Primary algorithmic contribution?** | Filter-then-refine: a spatial index prunes 100,000 zones to a handful, then exact point-in-polygon (ray casting, cross-checked by the winding number) decides the answer. "Spatial index finds candidates; geometry determines the answer." |
| **Experiment that proves the DS matter?** | Brute force vs quadtree vs R-tree on identical data and queries: ~35× / ~240–260× faster at 100k zones, with **0 mismatches across 18,000 queries** — correctness first, then speed. |
| **Most advanced concept?** | The persistent (path-copying) quadtree: retains every historical version by sharing untouched subtrees, 13× cheaper than full copies, answering "what was the map at 14:32?". |
| **What is merely an extension?** | Everything else — groups, prediction, alert correlation, routing (Dijkstra/A*), dispatch (Hungarian), offline sync, Merkle log, jurisdiction, adaptive sampling. All real and tested; none needed to understand the core. |

---

## What an examiner is most likely to ask

Ranked by how probable and how load-bearing each is.

### 1. "How much faster is the index, and how do you know it's still correct?"
**Strongest answer:** ~35× (quadtree) to ~240–260× (R-tree) at 100,000 zones, a
ratio to our own brute force — and correctness is proven *first*: 18,000 randomized
queries across three densities, **0 mismatches** against the brute-force oracle.
**Evidence:** `make bench` §1 and §2; [RESULTS.md](RESULTS.md) §1–2; the dashboard's
"main experiment" panel re-measures the mismatch count live (3,000 queries, 0
mismatches over these zones).

### 2. "Why does the speedup plateau at ~35× and not thousands?"
**Strongest answer:** the index turns O(n) into O(log n + **k**), where k is the
output size — the zones the query genuinely overlaps. At 100k dense zones ~99 zones
overlap each 450 m query, and no index can return fewer results than exist. All
three indexes report the *same* candidate count, which proves it is a property of
the data, not the tree. **Evidence:** the identical `cand` column in `make bench` §1
and the dashboard experiment panel; [VIVA.md](VIVA.md) Q5.

### 3. "Why build two spatial indexes?"
**Strongest answer:** they make opposite trade-offs — the quadtree partitions
*space* (disjoint grid), the R-tree partitions *items* (tight, overlapping
envelopes). Measuring both shows the R-tree's STR bulk packing winning ~6× over
insertion from tree shape alone. **Evidence:** `make bench` §9; the dashboard's
index switch draws both indexes' real cells over the same data.

### 4. "Prove the persistent index isn't just a fancy tree drawing."
**Strongest answer:** it answers the *same* query at different historical versions
and gets different answers, each from the version in force *then* — e.g. "is Wards
Lake in force here?" is no at 00:20 and yes at 00:30. Storage is path copying:
each mutation copies O(depth) nodes and shares the rest by refcount, 13× cheaper
than full copies. **Evidence:** the dashboard's persistent panel ("same query, two
versions" + per-version "+14 new / 115 shared"); `make bench` §5;
`tests/index/versioned_index_test.cpp`.

### 5. "What are the complexities — and which are guaranteed?"
**Strongest answer:** distinguish four things clearly:
- **Average:** quadtree/R-tree query O(log n + k).
- **Worst case:** quadtree/R-tree O(n + k) — they partition space/items with no
  balance invariant, so clustered data degrades them.
- **Guaranteed:** the AVL interval tree is O(log n + k) *always* (it balances on
  data); the persistent quadtree's O(depth)-new-nodes-per-mutation sharing bound is
  proven and measured.
- **Output-sensitive k:** every "+ k" is the result-set size, the speedup ceiling.

**Evidence:** the worst-case table in [DATA_STRUCTURES.md](DATA_STRUCTURES.md); the
test asserts the interval tree's height ≤ 1.44·log₂(n+2) every run.

### 6. "You use `std::sort` / `std::shared_ptr` — is anything actually hand-written?"
**Strongest answer:** the graded structures are ours — quadtree, R-tree, interval
tree, brute force, persistent quadtree, plus the heap, hash table, union-find. The
banned list is `std::map/set/unordered_map/priority_queue`, Boost.Geometry, PostGIS.
`std::sort` is an algorithm; `std::shared_ptr` is the memory management path copying
needs. **Evidence:** [DATA_STRUCTURES.md](DATA_STRUCTURES.md) ground rule.

### 7. "The data is simulated — doesn't that invalidate the results?"
**Strongest answer:** the *geography* is real (OpenStreetMap); only the *tourists*
are simulated, deliberately, because simulation gives **ground truth** — we know
the true position, so we can measure whether the engine is right. The performance
results don't depend on the people at all; they're a function of zone count and
query geometry. **Evidence:** [DATA_PROVENANCE.md](DATA_PROVENANCE.md).

---

## The hardest question (and the honest answer)

> **"Your two headline spatial indexes have no worst-case guarantee. On clustered
> hazard data — which you admit is the realistic case — both degrade to O(n). So in
> the very scenario this project targets, your fast indexes can be no better than
> the brute force you're comparing against. Why should I be impressed?"**

**Honest answer:** Three points, in order.
1. *It is true and we state it first* — the quadtree and R-tree are O(n) worst case
   because they partition space/items with no balance invariant. We do not hide it;
   the worst-case table and the viva prep lead with it.
2. *The average case is what the workload actually hits*, and we mitigate the worst
   case concretely: the quadtree is rooted to the data extent (which recovered ~11
   levels of depth and 2.3× throughput) and depth-capped; the measured candidate
   count (~99 at 100k) shows real queries are nowhere near the pathological case.
3. *This is exactly why the project also builds the interval tree* — the one
   structure on the query path with a **guaranteed** O(log n + k), because it
   balances on data (AVL), not space. Knowing *which* structure to trust for a
   guarantee, and why, is the analysis the course is testing.

That answer turns the weakness into evidence of understanding, which is the whole
posture of the project.

---

## The weakest current area

Stated plainly, so it isn't discovered instead of disclosed:

- **Breadth still outweighs the core in sheer file count.** The reorganization
  demotes the extensions to a labelled Level 4, but the repository still contains a
  large amount of extension code (routing, dispatch, evidence, sync, jurisdiction).
  An examiner who wanders into it can lose the thread. *Mitigation:* the four-level
  hierarchy, [COURSE_MAPPING.md](COURSE_MAPPING.md), and a demo script that never
  leaves the five core structures. *If forced to cut, the extensions are what go.*
- **The R-tree 100k speedup is the noisiest number in the project** (~240–260×
  band, because it's ~1 µs and scheduler-sensitive). It is always quoted as a band,
  never a constant — but it is the one figure that moves visibly run to run.
- **`std::shared_ptr` refcounting is the persistence mechanism**, not a hand-rolled
  arena. Defensible (it's memory management, not a graded container), but an
  examiner could ask for a custom allocator; the honest answer is that it was out of
  scope and the sharing bound is what's graded, which is measured.

None of these is a correctness problem; all are disclosed.

---

## Bottom line

The project stands on a genuinely strong spine: five hand-built structures, an
oracle-based correctness methodology, a clean output-sensitive complexity story,
and reproducible measurement — now presented so the spine is visible in one
screen. Defend the five core structures and the `k` ceiling with confidence and the
sophistication reads as mastery rather than sprawl.
