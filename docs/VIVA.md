# Viva Preparation — 20 questions, answered from the implementation

Every answer here is grounded in code that exists in this repository, and every
number is authoritative in [RESULTS.md](RESULTS.md). If you internalise the five
core structures ([DATA_STRUCTURES.md](DATA_STRUCTURES.md)) and these twenty
answers, you can defend the project.

**Golden rule for the room:** never oversell. Where something is an approximation,
a worst case, or unbuilt, say so first — the project's credibility is built on that
honesty, and an examiner rewards it.

A deeper, prose version of the hardest questions is in
[DESIGN_DEFENSE.md](DESIGN_DEFENSE.md); this file is the quick-recall sheet.

---

### 1. Why not brute force?

Brute force checks every zone against every tourist every tick — `O(n)` per query,
linear in the zone count. At 100,000 zones that is ~245 µs per query and it grows
without bound as zones are added. It is correct, so we keep it forever as the
**oracle**; it is just too slow to run in the loop. The whole project exists to
beat it while returning *exactly* its answers.

### 2. Why quadtree?

It subdivides *space* into four quadrants recursively, so a small query descends
only the quadrants that can possibly overlap it and skips the rest. On data spread
across the region that is `O(log n + k)` — measured ~35× faster than brute force at
100,000 zones. It is also the index we can *draw* (fixed spatial subdivision), and
the one that path-copies cleanly for the persistent version.

### 3. Why R-tree?

To have a second spatial index that makes the *opposite* trade-off, so the
comparison teaches something. The quadtree partitions space; the R-tree partitions
*items* into tight bounding rectangles, so nothing is forced high in the tree by a
split it happens to straddle. The price is that envelopes can overlap, so a query
may descend several branches. With STR bulk loading it reaches ~240–260× at 100k —
faster than the quadtree — which is the payoff for building both.

### 4. Why can a quadtree degrade to O(n)?

Because it partitions **space on a fixed grid, not data**. If every zone falls in
one small region — realistic, since hazards cluster in a valley — they all pile
into one branch and a query walks a linear chain. There is no balance invariant to
appeal to. We mitigate by rooting the tree to the data extent and capping depth,
but there is **no asymptotic guarantee**. (This is exactly why we also built the
R-tree, and why the one guaranteed structure on the query path is the interval
tree.)

### 5. What is k?

`k` is the **output size** — the number of zones a query actually overlaps. Every
`O(log n + k)` bound includes it because *an index cannot return fewer results than
exist*. `make bench` §1 reports `k` in its own column, and it is identical across
all three indexes (0.97 at 1k zones, 98.78 at 100k). `k` is what caps the speedup:
at 100k dense zones ~99 zones genuinely overlap each query, so no structure can do
better than returning those ~99 quickly.

### 6. Why does the R-tree sometimes outperform the quadtree?

Not because it returns fewer results (it returns the same `k`) but because it
**visits fewer nodes** to find them. STR (Sort-Tile-Recursive) bulk loading sees
the whole dataset up front and tiles it into near-square, minimally-overlapping
rectangles; a query then enters fewer branches. Incremental insertion, by
contrast, makes every placement decision blind to future items, so envelopes
overlap more. Same data, same query code, ~6× difference from tree shape alone
(`make bench` §9).

### 7. How is a spatial index different from a database?

A database (e.g. PostGIS) is a general storage engine that *contains* a spatial
index (a GiST tree) and adds transactions, SQL, persistence, and a network
protocol. Here we build **only the index and the geometry** — the algorithmic
core — by hand, in-process, no query planner, no disk pages, no server. The point
is to implement and analyse the structure the database would hide, which is what
the course grades.

### 8. What does the interval tree add?

The temporal dimension. Zones turn on and off (a curfew, a temporary closure), so
"which zones are *near* here?" (spatial) is not the same as "which zones are *in
force at time t*?" (temporal). The AVL interval tree stabs the validity windows
overlapping `t` in **guaranteed** `O(log n + k)` — the only worst-case guarantee on
the query path, because it balances on data. In the hot loop the spatial prune runs
first (it cuts harder), then an `O(1)` validity check per surviving candidate; the
interval tree earns its place on the temporal-*only* query where no spatial prune
exists.

### 9. What is persistence?

A persistent data structure preserves its **previous versions** when modified, so
you can query any past state. Ours is a persistent quadtree: after 5,000 edits you
can still ask "what did the map look like at version 37?" and get the exact answer.
It is what turns incident investigation from guesswork into a lookup.

### 10. Why path copying?

Because it makes a new version cost `O(depth)` instead of `O(n)`. Nodes are
immutable; a mutation copies only the nodes on the single root-to-leaf path it
touches and **shares every other subtree** with the previous version by
`shared_ptr` refcount. Querying a past version is just a descent from an older root
pointer — no replay, no reconstruction.

### 11. Why not copy the entire tree?

That would be `O(n)` in nodes *per version*: at 5,001 versions, 930,257 nodes
instead of the 71,314 path copying actually allocates — **13× more memory** for the
same information (`make bench` §5). Full copies throw away the fact that almost
nothing changes between consecutive versions. A validity-only change is the extreme
case: path copying allocates **zero** new nodes (it shares the whole tree and
appends one validity record), where a full copy would still duplicate everything.

### 12. What is the complexity? (of each core structure)

| Structure | Build | Query | Worst case | Guaranteed? |
|---|---|---|---|---|
| Brute force | `O(n)` | `O(n)` | `O(n)` | ✓ (trivially) |
| Quadtree | `O(n log n)` | `O(log n + k)` avg | `O(n + k)` | ✗ (partitions space) |
| R-tree (STR) | `O(n log n)` | `O(log n + k)` avg | `O(n + k)` | ✗ (envelopes overlap) |
| Interval tree | `O(n log n)` | `O(log n + k)` | `O(log n + k)` | ✓ (AVL-balanced) |
| Persistent quadtree | — | `O(log n + k)` per version | `O(n + k)` | ✗ query · ✓ sharing bound |

Mutating the persistent quadtree allocates `O(depth)` new nodes — that bound *is*
guaranteed and measured.

### 13. What happens if many zones overlap?

Then `k` is large, and *that* dominates — correctly. Every index must return all
`k` overlapping zones, so the query time rises with `k` no matter how good the
tree is; this is the output-size ceiling from Q5. For the R-tree specifically,
heavy overlap of the *data* boxes also means envelopes overlap more, so a query
descends more branches — the quadtree is less sensitive there. Neither returns a
wrong answer; both slow toward `O(n + k)`. `make bench` §1's candidate column is
exactly this effect made visible.

### 14. Why do you keep the brute-force implementation?

Two permanent jobs. (1) **Correctness oracle:** `tests/index/equivalence_test.cpp`
and `make bench` §2 assert every fast index returns *exactly* brute force's results
— 18,000 randomized queries, 0 mismatches. (2) **Speedup denominator:** every "N×
faster" is a ratio to brute force, not to an external library, so it measures our
structure honestly. Without the oracle, a benchmark could compare a correct slow
thing against a fast *wrong* thing and never notice.

### 15. How did you test correctness?

Layered. Every fast structure has a unit test comparing it to a brute-force oracle
on randomized input; the two point-in-polygon algorithms (ray casting and winding
number) cross-check each other over 100,000 points with 0 disagreements; the
benchmark re-runs the equivalence gate (18,000 queries) every time; and the whole
suite (39 files, ≈770 assertion sites, 11,616 checks executed) runs under
AddressSanitizer + UBSan in CI. Plus a determinism gate: same seed → byte-identical
output.

### 16. What was your most important bug?

The one only measurement could catch: **the brute-force oracle over-counted its own
candidates**, adding the whole accumulating output buffer instead of what each call
appended. Because brute force is the *denominator* of every speedup and the source
of the candidate column, a counting bug in the oracle silently inflated the numbers
the project reports about itself — and no correctness test could catch it, because
the *answers* were right, only the counting was wrong. (Runner-up: the quadtree was
rooted at the whole planet, wasting 11 levels of depth; the dashboard's cell
overlay is what made it visible, and fixing it more than doubled performance.)

### 17. What limitations does the system have?

- Quadtree and R-tree have **no worst-case guarantee** (`O(n)` on clustered data).
- The tourists are **simulated** (real geography, simulated people — by design, for
  ground truth).
- Prediction uses **straight-line extrapolation**, fiction beyond a few minutes in
  hill terrain — capped at a 5-minute horizon.
- Nearest-neighbour is answered in the **local tangent plane**, not the great
  circle; the two disagree on ~1 junction in 4,000, by ~2 mm (far inside GPS noise).
- Dispatch travel totals differ ~3% **across operating systems** (libm `asin`/`sin`
  last-ulp differences); the evaluation core stays byte-identical.

### 18. Why is the data simulated?

Because simulation gives **ground truth**. No real tourist-tracking dataset exists
for this problem, and even if it did, a GPS recording cannot tell you where the
person *truly* was — only where the receiver *thought* they were. The simulator
knows the true position, so we can measure whether the engine got the right answer.
The *geography* is real (OpenStreetMap: Wards Lake, Sonapani Waterfall Cliff, real
reservoirs); only the people are simulated.

### 19. Which structure would you choose in a production system?

For static or bulk-loaded spatial data, the **R-tree with STR packing** — it had
the best measured query time and is the industry standard (PostGIS, SQLite R*Tree)
for exactly this reason. The **quadtree** if the workload is update-heavy and you
want a structure that is trivial to visualise and reason about. In real software I
would use a mature library (Boost.Geometry, PostGIS) rather than these
hand-written versions — the hand-writing is the *course* constraint, not a
production recommendation.

### 20. What would you remove if you had to simplify the project?

The **Level-4 extensions** — routing, dispatch, offline sync, Merkle log,
jurisdiction, groups. They are real, tested engineering, but the project stands
entirely on the five core structures plus the geometry, and the extensions are what
made it "too broad to explain." I would keep brute force, quadtree, R-tree, interval
tree, the persistent quadtree, and the point-in-polygon geometry — that is the
gradeable spine. (This reorganization already demotes them to a clearly-labelled
extensions section for exactly that reason.)

---

## The five load-bearing explanations (memorise these)

1. **The speedup ceiling is `k`, not the tree** — Q5.
2. **Quadtree worst case is `O(n)` because it partitions space, not data** — Q4.
3. **Persistent index = path copying + structural sharing** — Q10/Q11.
4. **Brute force is permanent: oracle + denominator** — Q14.
5. **Spatial index finds candidates; geometry determines the answer** — the whole
   hot path.

If asked "which structure did you understand most deeply?", pick the **persistent
quadtree**: it has a proven sharing bound, a measured 13× result, an equivalence
test against brute force across historical versions, and no counterpart in existing
implementations.
