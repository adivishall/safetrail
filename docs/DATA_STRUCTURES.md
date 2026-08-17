# Data Structures Inventory

Every structure in the project, why it is here, and its complexity target. This is
the document to hand an examiner who asks "what did you actually implement?"

**Ground rule:** all of these are hand-written. `std::vector` and `std::string`
are permitted as raw storage. `std::unordered_map`, `std::set`, `std::map`,
`std::priority_queue` and any external geometry or spatial library are not.

---

## Core — the baseline problem

| Structure | Header | Complexity | Why it's here |
|---|---|---|---|
| **Quadtree** | `index/quadtree.hpp` | build O(n log n) · query O(log n + k) | Primary spatial index. Recursive subdivision, and it draws beautifully over a map for the diagnostics overlay. |
| **R-tree** | `index/rtree.hpp` | query O(log n + k) · insert O(log n) | Second index. Quadratic node split; STR bulk-packing for comparison. Better than quadtree on irregular polygon extents. |
| **Geohash** | `index/geohash.hpp` | encode O(1) · query O(p + k) | Third index. Z-order/Morton bit interleaving where prefix match equals spatial proximity. Also the serialisation format for offline mode. |
| **k-d tree** | `index/kd_tree.hpp` | build O(n log n) · NN O(log n) avg | Nearest-responder queries for dispatch. |
| **Brute force** | `index/brute_force.hpp` | O(n) | **Never deleted.** Correctness oracle for the other three and the denominator in every speedup figure. |
| **Binary heap** | `ds/priority_queue.hpp` | push/pop O(log n) | Alert triage, A* frontier. |
| **Interval tree** | `ds/interval_tree.hpp` | query O(log n + k) | Escalation deadlines, and zone validity spans (Gap 3). Augmented BST caching subtree-max. |
| **Circular buffer** | `ds/circular_buffer.hpp` | push/access O(1) | Fixed-window GPS ping history. Bounded memory per tourist. |
| **Hash table** | `ds/hash_table.hpp` | O(1) expected | Entity lookup. Open addressing with Robin Hood probing, explicit load-factor policy. |
| **Timer wheel** | `ds/timer_wheel.hpp` | O(1) amortised | Alternative to the interval tree for escalation. Benchmarked against it. |
| **Adjacency list** | `graph/road_graph.hpp` | — | Road network from OSM. |
| **Merkle tree** | `evidence/merkle_log.hpp` | append O(1) am. · proof O(log n) | Tamper-evident log (Gap 9). |

## Algorithms on those structures

| Algorithm | Header | Complexity | Purpose |
|---|---|---|---|
| Ray casting | `geo/containment.hpp` | O(V) | Point-in-polygon, the fundamental test |
| Winding number | `geo/containment.hpp` | O(V) | Independent second implementation, cross-validates ray casting |
| Douglas–Peucker | `geo/douglas_peucker.hpp` | O(n log n) avg | Trajectory simplification |
| Bentley–Ottmann | `geo/sweep_line.hpp` | O((n+k) log n) | Self-intersection detection (Gap 10) |
| Dijkstra | `graph/dijkstra.hpp` | O(E log V) | Responder routing baseline |
| A* | `graph/astar.hpp` | O(E log V) | Same bound, 3–10× fewer expansions with the haversine heuristic |
| Kuhn's / Hungarian | `graph/bipartite_match.hpp` | O(VE) / O(n³) | Responder-to-incident assignment |

---

## Added by the gap analysis

These have no counterpart in any existing implementation of `SIH25002`. Each
traces to a documented gap — see [GAP_ANALYSIS.md](GAP_ANALYSIS.md).

| Structure / Algorithm | Header | Complexity | Gap |
|---|---|---|---|
| **Persistent quadtree** (path copying) | `index/versioned_index.hpp` | mutate O(log n) extra nodes · query-at-time O(log n + k) | **3** — time-travelling zone queries. The most advanced structure here. |
| **Union-Find with rollback** | `ds/dynamic_connectivity.hpp` | find O(log n) · undo O(1) per union | **4** — groups split as well as merge; path compression is unrollbackable |
| **Circle–polygon intersection** | `geo/containment.hpp` | O(V) | **1** — three-valued containment under GPS uncertainty |
| **Signed distance to boundary** | `geo/containment.hpp` | O(V) | **1, 2, 7** — feeds uncertainty, prediction, and adaptive sampling alike |
| **Segment–polygon intersection** | `geo/predict.hpp` | O(V) | **2** — time-to-boundary for predictive alerts |
| **Spatio-temporal DSU clustering** | `alert/correlator.hpp` | O(n α(n)) after O(n) bucketing | **5** — forty alerts collapse into one incident |
| **Lamport clocks** | `sync/lamport.hpp` | O(1) per event · merge O(n log n) | **6** — offline event ordering without trusting device clocks |
| **Index serialisation** | `index/spatial_index.hpp` | O(n) | **6** — ship the index to the device |
| **Hysteresis state machine** | `fence/hysteresis.hpp` | O(1) per observation | **8** — drift suppression, with a measured false-alert reduction |
| **Adaptive rate controller** | `power/adaptive_sampler.hpp` | O(1) per tick | **7** — sampling as a function of risk distance |
| **Merkle consistency proof** | `evidence/merkle_log.hpp` | O(log n) | **9** — proves append, not just inclusion |
| **Polygon nesting hierarchy** | `jurisdiction/hierarchy.hpp` | build O(n² V) · resolve O(log n) | **11** — which authority owns this alert |

**Count: 12 core structures, 7 classical algorithms, 12 gap-driven additions.**

---

## The headline result — measured, not projected

The design doc originally claimed a ~29,000x reduction. **Measurement disciplined
that number down, and understanding why is the most interesting result in the
project.**

### What was measured

`make bench`, 2000 probe queries per row, 450 m query box, Apple clang -O2:

| zones | brute force | quadtree | speedup | candidates returned |
|---|---|---|---|---|
| 10 | 0.04 us | 0.07 us | 0.5x | 0.01 |
| 100 | 0.14 us | 0.10 us | 1.4x | 0.10 |
| 1,000 | 1.85 us | 0.25 us | 7.4x | 0.97 |
| 5,000 | 11.47 us | 1.07 us | 10.8x | 4.90 |
| 20,000 | 48.24 us | 3.77 us | 12.8x | 19.86 |
| 50,000 | 119.02 us | 8.87 us | 13.4x | 49.37 |
| 100,000 | 242.71 us | 17.24 us | **14.1x** | 98.78 |

### Why 14x and not 29,000x

Look at the last column. At 100,000 zones packed into roughly 40 km2, **98.78
zones genuinely intersect each 450 m query box.** Those are true positives. No
index can return fewer results than the query actually has, so the achievable
speedup is bounded by output size, not by the tree.

That is precisely what `O(log n + k)` predicts. Brute force grows as `O(n)` —
visible in the linear brute-force column, 242 us at 100k. The quadtree grows as
`O(log n + k)`, and with `k ~ 99` the `k` term dominates the `log n` term
completely. The index is behaving exactly as designed; the original estimate
simply assumed `k ~ 3`, which requires sparse zones.

**The zone-density caveat matters more than the speedup figure**, and stating it
is worth more marks than quoting a big number would be.

### The representative operational number

In the actual simulation — 5,008 zones, query radius derived from position
uncertainty plus reachable distance rather than a fixed guess:

```
pruning: 5008 zones -> 24.27 candidates per query   (206x reduction)
quadtree: 73 nodes, max depth 12, 202 KB
```

**206x** is the honest headline, because it reflects a real query radius against a
realistic zone density.

### A bug this measurement caught

The first run reported `1x reduction`. The evaluator was querying a fixed 20 km
box for zones about 2 km across, so every query returned every zone and the index
was doing nothing. The fix was to derive the radius from what the query actually
needs — position uncertainty for containment, plus `speed x horizon` for the
predictive path. Without the candidates-returned column in the benchmark output,
that bug would have shipped invisibly, since the results were still *correct*.

### Other verified results

| Result | Measured |
|---|---|
| Hysteresis A/B (GAP 8) | **87.6% of false transitions removed** — 733 to 91, same seed, same injected noise |
| Quadtree vs brute force equivalence | 27,000 queries across 3 densities, **0 mismatches** |
| Ray casting vs winding number | 100,000 points, 200 polygons, **0 disagreements** |
| Alert correlation (GAP 5) | 1.30 alerts per incident; 6,587 operator cards suppressed |
| Unit tests | 90 checks across geometry, index equivalence, rollback DSU — all pass |

## Measurements to produce

Each of these is a plot or table in the report. They are the difference between
"we built it" and "here is what it does".

1. **Index scaling** — query time vs zone count, 10 → 100,000, all four
   implementations on one chart. The brute-force line going vertical is the
   picture.
2. **Candidates returned per query** — how hard each index actually prunes.
   More honest than wall-clock, since it is machine-independent.
3. **Ray casting vs winding number** — throughput, plus where they disagree.
4. **Hysteresis A/B** — false alert rate with and without, same replay, same
   injected GPS noise. Gap 8.
5. **Adaptive sampling** — projected battery life and fix count vs fixed-interval
   polling, **at matched alert recall**. Showing the recall trade-off curve is
   the interesting part. Gap 7.
6. **Alert compression ratio** — alerts ingested per incident opened during the
   landslide scenario. Gap 5.
7. **Structural sharing** — nodes allocated vs nodes a full copy per version
   would need. Justifies the persistent index. Gap 3.
8. **A* vs Dijkstra** — node expansions on the same routes.
9. **Serialised index size** — bytes per zone, per format. Determines whether
   offline mode is actually deployable. Gap 6.
10. **Clock skew observed** — max device-vs-server skew during reconciliation.
    Motivates Lamport ordering with real numbers. Gap 6.
