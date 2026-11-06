# 07 — Benchmarks and performance

> How the numbers are produced, what they mean, and how not to over-claim them.
> Source: `apps/safetrail_bench.cpp` (`make bench`), results in `bench/results/*.csv`,
> machine in `bench/results/environment.txt`, discussion in `docs/RESULTS.md`.

## 1. How benchmarks are generated

`make bench` builds and runs `safetrail_bench`, which writes one CSV per section
and `environment.txt` (date, commit `9d33bb6`, Apple clang 17, `-O2
-ffp-contract=off`, Apple M4, 10 cores, **battery power**, load 2.91).
`tools/render_results.py` regenerates the README and `docs/RESULTS.md` tables
from those CSVs, so no number is typed by hand.

**Measurement protocol** (comment at the top of `safetrail_bench.cpp`):
1. **Work is precomputed** — query boxes are built before timing, so a query time
   is index traversal, not trigonometry.
2. **Samples last ≥ 20 ms** — each sample runs the whole probe set enough times,
   calibrated per contender after an untimed warm-up.
3. **11 rounds, order rotated** — in every round each contender takes one sample,
   in a rotated order, so thermal or frequency drift falls on all of them.
4. **Median and IQR** reported; **paired speedups**: the median over rounds of
   (baseline / contender) measured back to back in the same round.
5. **Checksums as a correctness gate** — every pass returns an order-independent
   checksum of its results; contenders doing the same job must agree. That also
   stops the optimiser deleting the work. Section 5 compares result sets exactly.
6. On macOS the process requests performance cores (QoS user-interactive).

## 2. Sections, workloads and metrics

| § | CSV | Workload | Metrics |
|---|---|---|---|
| 1 | `index_scaling.csv` | **synthetic** zones packed into one district: 100 … 100,000 zones | brute/quad/R-tree µs per query, speedups, **candidates (k)**, IQR %, build ms, bytes, nodes |
| 2 | `index_density.csv` | **synthetic**, area grows with n so k ≈ 1: 1,000 … 100,000 | same |
| 3 | `end_to_end.csv` | 5,000 polygons, 8 … 512 vertices: naive (ray-cast every polygon), bbox scan + exact, quadtree + exact, R-tree + exact | µs per point query and ratios |
| 4 | `index_costs.csv` | build, memory, updates | ms, bytes |
| 5 | (gate) | equivalence | identical result sets |
| 7 | `index_build.csv` | R-tree insertion-built vs STR | build ms, nodes, depth, query µs, gain |
| 8 | `interval_tree.csv` | selective vs dense validity windows, 1k … 100k | scan vs tree µs, hits per stab, height vs AVL bound |
| 9 | `versioned_index.csv`, `versioned_mutations.csv` | persistent quadtree, up to 5,001 versions | nodes allocated vs full copies, past vs present query µs |
| 10 | `hysteresis_ab.csv` | noise model white vs drift | transitions: raw truth, target, naive, filtered |
| — | `routing.csv` | road-like grids 64 … 1,024 nodes, 400 queries | Dijkstra vs A* nodes settled, µs |
| — | `dispatch.csv` | 5 … 40 responders × incidents | greedy vs Hungarian average metres |
| — | `power.csv` | 8-hour simulated trek | fixes taken, battery saved (model), near-zone recall |
| — | `index_churn.csv` | insert/delete churn | nodes, query time after churn |
| 11 | `serialization.csv` | geohash bytes round trip | size, time |
| 12 | `self_intersection.csv` | sweep vs pairwise | crossover |
| 14 | `node_snap.csv` | k-d tree vs linear scan over road junctions | agreement, time |
| 18 | `evaluator_cost.csv` | the whole `evaluate()` per fix, real OSM zones + synthetic | µs per fix, candidates per fix |
| 19 | `multi_user.csv` | 1 … 5,000 users through `Platform::ingest_fix` | µs per fix, fixes/s, bytes per user |

(The section numbering in the source repeats some numbers for different
sections; the CSV names are the stable reference.)

## 3. Why brute force is the baseline, and how speedup is computed

Brute force (`index::BruteForceIndex`) is the only answer that is obviously
correct and the honest "what if we had no data structure" cost. Speedup =
**median over 11 rounds of (brute-force time / index time)**, each pair measured
back to back. It is a ratio *against this project's own brute force*, not
against another library.

## 4. The headline numbers, verified

| Claim (README) | CSV row | Verified value |
|---|---|---|
| Brute force 245 µs at 100k, one district | `index_scaling.csv` 100000 | 244.6117 µs |
| Quadtree 32.7× | same | 32.71 |
| R-tree 224× | same | 223.67 |
| k ≈ 98 | same, `candidates` | 98.45 |
| Constant density: quadtree 556×, R-tree 1027× | `index_density.csv` 100000 | 555.74, 1026.86; k = 0.99 |
| Point-in-zone 128 vertices: quadtree 1498×, R-tree 2072× | `end_to_end.csv` 128 | quad_vs_naive 1498.4; naive 254.6365 / rtree 0.1229 = 2071.9 |
| STR ≈ 6× faster queries | `index_build.csv` 100000 | 6.264 |
| R-tree range 211–224× over 4 runs | `bench/results/variation/` + `docs/RESULTS.md` | as stated |

**Important correction to remember:** the 100,000-zone scaling workloads use
**synthetic** zones (`docs/RESULTS.md`: "Zones in §1–9 are synthetic"). The real
OpenStreetMap zones (38) are used by the evaluator sections and the live system.
Say "100,000 synthetic zones in one district".

## 5. What the speedups mean

### 5.1 Why the one-district speedup flattens (the k argument)
In one crowded district, the answer size k grows with n: at 100,000 zones about
**98 zones genuinely overlap each query**. Every index must return all of them,
so the cost is O(log n + **k**) and k dominates. The quadtree's ratio stops
growing after a few thousand zones (30.75× at 1,000 → 32.71× at 100,000).

> An index can skip what does not match; it can never skip what does.

### 5.2 Constant density (k ≈ 1)
When the area grows with n, each query overlaps about one zone; only the log n
term remains, and the speedup keeps growing: R-tree 29× at 1,000, 189× at 10,000,
1,027× at 100,000.

### 5.3 Point-in-zone: filter vs refine
Against testing every polygon (naive), the tree wins by two to three orders of
magnitude, more as polygons get complex (R-tree 17.37 / 0.0628 ≈ 277× at 8
vertices, 951.34 / 0.3375 ≈ 2,819× at 512). Against a **bounding-box scan + the same exact test**, the margin is much
smaller and shrinks with vertex count (R-tree 152× at 8 → 30× at 512), because the
exact O(V) test on the few candidates is the same work for both.

### 5.4 STR bulk loading
At 100,000 zones (`index_build.csv`): STR builds in 16.2 ms vs 26.7 ms, 14,383
nodes vs 21,368, depth 5 vs 6, and queries 1.02 µs vs 6.36 µs — **6.26×**.
Reason: insertion-built trees decide splits without knowing future items, so node
boxes overlap more and queries descend more branches; STR tiles all items at once.

### 5.5 Other results
| Result | Value |
|---|---|
| Interval tree, selective windows | 14.25× (1k), 57.35× (10k), 52.79× (100k) faster than a scan |
| Interval tree, dense windows | 0.61× at 1k (**loses**), 1.02× at 10k, 2.12× at 100k |
| Persistent quadtree sharing | 3.64× (51 versions) → 13.04× (5,001 versions); past query ≈ present |
| A* vs Dijkstra | 71–78 % fewer nodes settled on grids of 64–1,024 nodes |
| Hungarian vs greedy | 6.5 % (5) → 15.7 % (40) shorter total; optimal never worse |
| Adaptive sampling (model) | 257 vs 28,800 fixes on an 8-hour trek (99.1 %), 100 % near-zone recall |
| Whole evaluate() per fix | 2.24 µs at 1,038 zones (one district) → 17.2 µs at 20,038; ≈ 5–5.7 µs at constant density up to 50,038 |
| Multi-user | ≈ 2.7–3.7 µs per fix from 1 to 5,000 users; ≈ 6.9 KB per user |
| Hysteresis (white noise) | naive 20,848 transitions vs target 521; filtered 343 |

## 6. How correctness is established inside the benchmark

- Checksums of every contender must match (`eq` column printed "ok").
- Section 5 compares sets exactly.
- `bench` in CI runs all 8 correctness gates (scaling, density, end-to-end,
  equivalence, containment, interval, evaluator, multi-user) — e.g. CI run
  41447026033 reported every gate "ok".

## 7. Why results vary between sessions

- Absolute times belong to one machine in one state (power, thermal, load,
  core scheduling). Brute force at 100k measured 244 µs on several days and
  460–560 µs on another, for reasons not established (`docs/RESULTS.md`).
- The R-tree query is about **1 µs** — the shortest timing, so the most sensitive.
  Its 100k ratio measured 102–114× in one session and between about 200× and
  236× in others.
- Within one session runs agree closely (211–224× over 4 runs), so the README
  quotes ranges and calls ~100× the floor.

**How to say it:** "Ratios measured back to back are stable within a session; the
R-tree ratio moved between sessions from about 100× to about 230×. I quote it as
~100× as the floor, 224× in the committed run."

## 8. Is the comparison fair?

| Concern | Answer |
|---|---|
| Same data? | All contenders run on the same boxes and query set. |
| Same work? | Checksums must match; exact equivalence in §5. |
| Warm caches favouring one? | Rotated order per round; ≥ 20 ms samples; warm-up pass. |
| Build cost hidden? | Build ms and bytes are reported separately. |
| Cherry-picked size? | Full scaling tables 100 → 100,000 and both regimes. |
| Against a strawman? | Brute force is the honest baseline; §3 also compares against a bbox scan + exact test, where the margin is smaller — reported, not hidden. |

## 9. Theory vs measurement

| Statement | Type |
|---|---|
| R-tree query O(log n + k) expected, O(n) worst | theory |
| 223.67× at 100k one district | measured, one session, one laptop |
| Interval tree O(min(n, (k + 1) log n)) | theory (guaranteed) |
| Interval tree loses at 1k dense | measured |
| A* settles fewer nodes than Dijkstra | measured on these graphs; guaranteed never more with a consistent heuristic |

Never present a ratio as a universal guarantee.
