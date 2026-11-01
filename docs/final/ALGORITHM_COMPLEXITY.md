# Algorithm complexity

n = number of items, k = number of results, V = vertices, E = edges,
P = polygon vertex count, U = tracked users, A = alerts. "Guaranteed" means a
proven worst-case bound; "expected" depends on the data's distribution;
"measured" means a number from `make bench` ([../RESULTS.md](../RESULTS.md)).

## 1. Spatial indexes (range query "which boxes meet this box?")

| Structure | Build | Query | Insert | Delete | Space | Kind of bound |
|---|---|---|---|---|---|---|
| Brute force | O(n) | Θ(n) | O(1) | O(n) | O(n) | guaranteed |
| Quadtree | O(n log n) expected | O(log n + k) expected, O(n) worst | O(log n) expected | O(n) find + O(depth) | O(n) | expected |
| R-tree (insertion, quadratic split) | O(n log n) expected | O(log n + k) expected, O(n) worst | O(log n) amortised | O(n) find + condense | O(n) | expected |
| R-tree (STR bulk load) | O(n log n) (the sorts) | same | same | same | O(n), fewer nodes | expected |
| Geohash (sorted Morton keys) | O(n log n) | O(log n + candidates) | O(n) (sorted array) | O(n) | O(n), no pointers | expected |
| k-d tree (nearest point) | O(n log n) | O(log n) expected, O(n) worst | rebuild | rebuild | O(n) | expected |

Why the worst cases are O(n): a quadtree query that overlaps boxes straddling
the root's split lines must look at all of them; an R-tree whose envelopes all
overlap the query descends every branch. Removal is O(n) in every index here
because none keeps an id → node map (an honest limitation, see
[LIMITATIONS_AND_FUTURE_SCOPE.md](LIMITATIONS_AND_FUTURE_SCOPE.md)).

## 2. Trees over time

| Operation | Bound | Kind |
|---|---|---|
| AVL interval tree insert / delete | O(log n), height ≤ 1.44 log₂(n + 2) | guaranteed |
| Stabbing query (intervals containing t) | O(min(n, (k + 1) log n)) | guaranteed |
| Persistent quadtree update (geometry) | O(depth) new nodes, rest shared | expected depth |
| Persistent quadtree validity change | O(log n) (interval tree) + 0 new nodes | guaranteed |
| Query at a past version | same as at the present | — |

## 3. Graphs and assignment

| Algorithm | Bound | Notes |
|---|---|---|
| Dijkstra (binary heap, lazy deletion) | O((V + E) log V) | each push O(log V); stale pops skipped |
| A* (admissible heuristic) | O((V + E) log V) worst; settles ≤ Dijkstra in practice | measured: section 11 |
| Hungarian assignment | O(n³) | n = max(responders, incidents) |
| Greedy cheapest-pair | O(r · i · min(r, i)) as written | no optimality guarantee |
| Union-find (rank, no compression) | O(log n) per find/union; rollback O(1) per undone union | compression would make rollback impossible |
| k-d tree snap | O(log V) expected | replaces an O(V) scan |

## 4. Hashing, queues, time

| Structure | Bound |
|---|---|
| Hash table put/get/erase | O(1) expected at load ≤ 0.7; O(n) worst; rehash O(n) amortised O(1) |
| Binary heap push/pop | O(log n); top O(1) |
| Timer wheel schedule | O(1); cancel O(slot size); advance O(Δticks + fired + held) |
| Circular buffer | O(1) |
| Merkle append / prove / verify | O(1) amortised append (root recomputed O(n) on demand); proof and verification O(log n) |
| Lamport merge | O(m) de-duplication (hash set) + O((n + m) log(n + m)) sort |

## 5. The geofencing pipeline per GPS fix (`fence::Evaluator::evaluate`)

| Step | Cost |
|---|---|
| usability gate | O(1) |
| spatial prune | O(log n + k) expected |
| validity check per candidate | O(1) each (window stored on the zone) |
| exact containment | O(k · P) |
| hysteresis + transition | O(1) per candidate |
| prediction | O(k · P) |
| reconciliation of open states | O(s), s = zones with open state for this user |

Platform ingestion adds O(1) validation and duplicate/order checks, an
amortised O(1) trail append (O(log t) insert only for late fixes), and alert
bookkeeping proportional to **that user's** alerts (`alerts_by_user_`). Before
the per-user index existed the bookkeeping scanned every alert ever raised,
O(A) per state change, which made a 5,000-user run quadratic; section 19 of
the benchmark found it.

Per platform tick: anomaly detection O(U); cohesion O(Σ g²) over each group's
tracked members; escalation O(1 + fired); retention O(U log t) once a minute.

## 6. Time–space trade-offs made here

| Choice | Pays | Gets |
|---|---|---|
| Persistent index instead of full copies | pointer overhead, shared_ptr refcounts | O(depth) per change instead of O(n) per version |
| Validity on the zone (O(1)) *and* in an interval tree | a second copy of the windows | an O(1) hot path and O(log n)-ish history queries |
| Hash table tombstones | occasional rebuilds | O(1) delete without breaking probe chains |
| Rollback DSU without path compression | O(log n) find instead of near-O(1) | the ability to undo |
| STR packing | needs all items up front | fewer, fuller nodes; faster queries |
| Geohash flat array | O(n) inserts | a pointer-free index that ships to a phone as bytes |

## 7. Why theory and the stopwatch disagree

- **The answer size bounds the speedup.** At 100,000 zones in one district
  about 100 zones overlap each query; every index must return them, so the
  speedup over brute force stops growing (section 1). Hold density constant
  and it keeps growing (section 2).
- **Constants and caches.** At 100 zones a brute-force scan of a contiguous
  vector is competitive with a tree walk; the tree pays off as n grows.
- **Expected, not guaranteed.** Clustered data can make a quadtree deep.
- **Measurement noise.** Absolute times on a laptop move between sessions
  (the R-tree's ratio has ranged roughly 100–235× across sessions), which is
  why ratios are paired and quoted as ranges ([../RESULTS.md](../RESULTS.md)).
