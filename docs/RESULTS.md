# Results

Every table on this page is **generated** from `bench/results/*.csv` by
`tools/render_results.py`; none is typed by hand. `make bench` writes the CSVs
and regenerates the tables, `make bench-variation` adds repeat runs for the
ranges, and CI fails if a committed table disagrees with the committed CSVs.

```bash
make bench             # every section below (~2–3 min), then re-render
make bench-variation   # sections 1-3 three more times, for the ranges
```

## Read this first

- **What is portable and what is not.** Absolute times belong to one machine in
  one state — the environment below records which, including power source and
  load. The run published here was on a laptop **on battery with a load average
  near 4**, which is not a quiet machine; absolute times are roughly twice what
  the same laptop measured on mains power in an earlier run. Ratios measured
  back to back, candidate counts, node counts, allocation counts and bytes are
  far more stable. The docs quote ratios, with their range.
- **Ranges are real.** Even paired, a speedup moves between runs: the R-tree's
  at 100,000 zones spanned roughly 97–229× across the runs below. Every
  headline ratio is shown with the min–max over
  <!-- results:runs -->4<!-- /results:runs --> independent runs.
- **Speedups are against this project's own brute force**, the correctness
  oracle — not against PostGIS, Boost.Geometry or any library.
- **Simulated people, real geography.** Benchmarks use synthetic zones in a
  real district's extent; the engine's end-to-end runs use real OpenStreetMap
  zones around Shillong with simulated tourists (simulation provides ground
  truth).
- **Every comparison is also a correctness gate.** Contenders that do the same
  job return a checksum of their results; the run fails if any differ.

<!-- results:env -->
```
date:     2026-09-28T09:37Z
commit:   d469066 (uncommitted changes)
compiler: Apple clang version 17.0.0 (clang-1700.4.4.1)
flags:    -std=c++17 -O2 -ffp-contract=off
os:       Darwin 25.5.0 arm64
cpu:      Apple M4
cores:    10
power:    Battery Power
load:     5.18 6.90 6.21
```
<!-- /results:env -->

### Protocol

Query boxes are built before timing, so a query time is index traversal only.
One *sample* runs the whole probe set enough times to last at least 20 ms
(calibrated per contender after an untimed warm-up); **11 rounds**, each
contender taking one sample per round in an order rotated round to round, so
drift lands on every contender rather than on whichever ran last. Reported:
the median, and the interquartile range as a percentage of it. A **speedup is
paired**: the median over rounds of *baseline ÷ contender* measured back to
back. On macOS the process requests the performance cores (QoS
user-interactive); that is a request, and the pairing is what keeps ratios
meaningful if it is not honoured. The previous protocol — the median of 7
passes of ~2 ms each, one index after another — is what produced headline
numbers this protocol could not reproduce ([DEFECT_LOG.md](DEFECT_LOG.md),
pass 6).

---

## 1. Scaling in one district — k grows with n

2,000 random 450 m query boxes against n zones (boxes 60–480 m across) in one
39 × 35 km district. Because the area is fixed, the number of zones genuinely
overlapping each query, **k**, grows in proportion to n.

<!-- results:scaling -->
| zones | brute µs | quadtree µs | R-tree µs | quadtree × | R-tree × | k / query | quadtree × range | R-tree × range | worst IQR |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 0.204 | 0.056 | 0.072 | **3.48×** | **2.83×** | 0.10 | 2.80–3.65× | 2.79–2.84× | 27% |
| 1,000 | 4.07 | 0.224 | 0.213 | **19.4×** | **19.2×** | 0.97 | 15.3–31.2× | 17.3–30.1× | 21% |
| 5,000 | 21.5 | 0.829 | 0.458 | **26.0×** | **46.9×** | 4.88 | 23.4–39.8× | 42.5–81.2× | 12% |
| 10,000 | 41.2 | 1.66 | 0.708 | **23.8×** | **58.5×** | 9.77 | 23.4–35.8× | 57.1–97.3× | 14% |
| 20,000 | 90.3 | 3.34 | 1.00 | **28.1×** | **85.5×** | 19.80 | 22.2–34.6× | 74.7–111× | 10% |
| 50,000 | 229 | 8.15 | 1.89 | **27.9×** | **125×** | 49.22 | 25.0–33.5× | 102–178× | 15% |
| 100,000 | 461 | 17.6 | 3.65 | **26.0×** | **127×** | 98.45 | 26.0–33.2× | 96.9–229× | 11% |
<!-- /results:scaling -->

Brute force is linear. The trees are O(log n + k) and here **k dominates**: at
100,000 zones about a hundred zones really do overlap each query, and every
index must return all of them. That caps the speedup — no index returns fewer
results than exist — and it is why the quadtree's ratio levels off around
25–35× instead of growing. The R-tree keeps gaining because STR packing lets it
reach those k results through fewer nodes. The candidate counts are identical
across indexes by construction; the checksum gate verifies it on every row.

![Query latency vs zone count](../bench/plots/index_scaling.svg)

## 2. Scaling at constant density — k held near 1

The same experiment with the district's area growing in proportion to n, so
each query overlaps about one zone at every size.

<!-- results:density -->
| zones | brute µs | quadtree µs | R-tree µs | quadtree × | R-tree × | k / query | quadtree × range | R-tree × range | worst IQR |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 3.90 | 0.233 | 0.208 | **16.5×** | **18.1×** | 0.97 | 16.1–33.6× | 18.0–31.2× | 20% |
| 10,000 | 37.6 | 0.503 | 0.380 | **73.5×** | **98.4×** | 1.03 | 73.5–144× | 93.4–193× | 10% |
| 100,000 | 426 | 1.45 | 0.802 | **294×** | **531×** | 0.99 | 269–530× | 531–1008× | 21% |
<!-- /results:density -->

With k fixed, what is left is the tree's depth, and the speedup keeps rising
with n — hundreds of times at 100,000 zones. Sections 1 and 2 together are the
honest answer to "how does it scale?": the index removes the O(n) scan; it
cannot remove the answer.

## 3. End to end — "which zones contain this point?"

The question the engine actually asks for each GPS fix, answered four ways that
must return the same set: **naive** ray-casts every polygon (O(n·V));
**bbox scan** tests every zone's box and ray-casts the hits (the brute-force
index plus exact geometry); **quadtree** and **R-tree** filter with the tree
first. 5,000 star-shaped polygons in the district, 300 probe points, V vertices
per polygon.

<!-- results:e2e -->
| V | naive µs | bbox scan µs | quadtree µs | R-tree µs | quadtree vs naive | range | quadtree vs scan | range | candidates / q | hits / q |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 59.2 | 20.0 | 0.325 | 0.191 | **184×** | 174–188× | 61.5× | 54.9–97.0× | 0.18 | 0.11 |
| 32 | 224 | 19.5 | 0.358 | 0.248 | **616×** | 515–616× | 54.6× | 48.7–86.5× | 0.30 | 0.18 |
| 128 | 851 | 18.1 | 0.487 | 0.384 | **1746×** | 1530–1746× | 39.4× | 34.0–58.1× | 0.28 | 0.16 |
| 512 | 3636 | 21.8 | 1.50 | 1.22 | **2542×** | 2361–2697× | 14.5× | 14.3–26.3× | 0.34 | 0.16 |
<!-- /results:e2e -->

The large factors against *naive* are what "check every zone against every
tourist" really costs. A plain bounding-box scan already removes the V; the
tree then removes the n. The exact test on the few candidates is identical in
every column, which is why the tree's advantage over the scan shrinks as V
grows — it accelerates the filter, not the refinement.

## 4. Build, memory, updates

Build is the median of 5. Memory is `IndexStats::bytes`: nodes plus the
capacity of every vector they own, allocator overhead excluded. Insert and
remove are the median over 7 rounds of 1,000 operations on a freshly built
index.

<!-- results:costs -->
| zones | index | build ms | memory KB | bytes / zone | insert µs | remove µs |
|---:|---|---:|---:|---:|---:|---:|
| 10,000 | brute-force | 0.03 | 391 | 40 | 0.006 | 10.8 |
| 10,000 | quadtree | 1.86 | 699 | 72 | 0.214 | 12.8 |
| 10,000 | r-tree | 2.74 | 527 | 54 | 1.06 | 13.5 |
| 100,000 | brute-force | 0.56 | 3,906 | 40 | 0.006 | 133 |
| 100,000 | quadtree | 22.14 | 6,618 | 68 | 0.209 | 199 |
| 100,000 | r-tree | 39.50 | 5,255 | 54 | 2.04 | 288 |
<!-- /results:costs -->

Removal is O(n) in all three indexes: none keeps an id → node map, so the entry
is found by search, and the trees then pay again to collapse or condense. That
is a deliberate simplification for a read-heavy workload, measured rather than
hidden; an id → leaf hash map is the first change for a churn-heavy one.

## 5. Equivalence

`make bench` §5 checks every index against brute force on 18,000 queries at
three radii and three densities; `tests/index/differential_test.cpp` runs about
275,000 more over seven hostile workload profiles with structural audits after
every operation ([TESTING.md](TESTING.md)). Zero mismatches in both.

## 6. Two point-in-polygon implementations

Ray casting vs winding number: 100,000 points against 200 random concave
polygons, **0 disagreements** (`make bench` §6; holes in every orientation are
covered by `tests/geo/polygon_holes_test.cpp`).

## 7. R-tree bulk loading — STR vs repeated insertion

Same data, same queries; only the way the tree was assembled changes.

<!-- results:str -->
| zones | insertion build ms | STR build ms | insertion nodes | STR nodes | insertion µs / query | STR µs / query | STR query gain |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.5 | 0.2 | 214 | 155 | 0.456 | 0.208 | **2.14×** |
| 10,000 | 6.2 | 3.5 | 2,133 | 1,456 | 3.01 | 0.642 | **4.89×** |
| 100,000 | 91.7 | 39.7 | 21,368 | 14,383 | 19.6 | 3.11 | **6.27×** |
<!-- /results:str -->

Both builds are O(n log n). STR produces a smaller tree whose sibling boxes
overlap less, so a query enters fewer branches — the structure, not the machine.

## 8. Interval tree — stabbing vs a linear scan

"Which validity windows contain instant t?", in two regimes. **Selective**:
windows of 1–30 minutes spread over 30 days. **Dense**: a tenth as many start
times as windows, each lasting up to 4 hours, so hundreds contain any instant.
Churn: ten rounds of deleting and reinserting a tenth of the set.

<!-- results:interval -->
| regime | windows | scan µs | tree µs | tree × | k / stab | height | AVL bound | height after churn | delete µs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| selective | 1,000 | 0.975 | 0.067 | **14.8×** | 0.3 | 12 | 14.0 | 12 | 0.230 |
| selective | 10,000 | 10.2 | 0.131 | **64.6×** | 3.6 | 16 | 18.8 | 16 | 0.354 |
| selective | 100,000 | 109 | 2.93 | **37.7×** | 36.0 | 20 | 23.6 | 20 | 0.711 |
| dense | 1,000 | 3.67 | 3.94 | **0.94×** | 363.2 | 12 | 14.0 | 12 | 0.253 |
| dense | 10,000 | 23.9 | 23.7 | **1.04×** | 980.5 | 16 | 18.8 | 16 | 0.364 |
| dense | 100,000 | 127 | 73.7 | **1.68×** | 1192.3 | 20 | 23.6 | 20 | 0.763 |
<!-- /results:interval -->

Selective windows are where the `max_high` augmentation earns its keep. With
hundreds of windows containing every instant the tree must still visit and
report each one, and a sequential scan is as fast: the same O(k) ceiling as the
spatial indexes. Height stays under the AVL bound for the *live* size after
churn. (This is also why the engine does **not** use the interval tree per tick:
it checks validity in O(1) on the few candidates the spatial index returned.)

## 9. Persistent quadtree — path copying

Zones added one version at a time; "if full-copied" is what a copy-the-tree-per-
version scheme would have allocated. Then the cost of each kind of mutation on a
5,000-zone index.

<!-- results:persistent -->
| versions | nodes allocated | if full-copied | sharing | query @ past µs | query @ now µs |
|---:|---:|---:|---:|---:|---:|
| 51 | 523 | 1,903 | **3.6×** | 0.222 | 0.202 |
| 201 | 2,582 | 14,633 | **5.7×** | 0.297 | 0.271 |
| 1,001 | 14,025 | 158,257 | **11.3×** | 0.555 | 0.537 |
| 5,001 | 71,314 | 930,257 | **13.0×** | 1.79 | 2.63 |

| mutation (5,000-zone index) | nodes allocated, mean | max |
|---|---:|---:|
| add (new zone) | 14.7 | 15 |
| replace (moved zone) | 29.0 | 30 |
| validity change | 0.0 | 0 |
| remove | 14.3 | 15 |
<!-- /results:persistent -->

Sharing grows with history, a query against the past costs what one against the
present does (a different root pointer, no replay), and each mutation copies one
root-to-node path — a validity change copies nothing.

---

## Extensions (§10–17)

Built on the core; not part of the headline claim. Full tables are in the CSVs
named after each section.

<!-- results:extensions -->
| Extension | § | Measured |
|---|---|---|
| Hysteresis A/B | §10 | white noise (rho=0): filter removes 100.9% of the naive run's excess transitions and reports 343 vs 521 that noise-free fixes give under the same policy; realistic drift (rho=0.9): filter removes 100.1% of the naive run's excess transitions and reports 502 vs 521 that noise-free fixes give under the same policy |
| A* vs Dijkstra | §11 | 77.7% fewer nodes settled at 1,024 nodes, same path |
| Hungarian vs greedy dispatch | §12 | 15.7% less total travel at 40 responders; never worse in any of 200 layouts |
| Adaptive GPS sampling | §13 | 28,800 → 257 fixes over an 8 h trek (99.1% fewer) at 100.0% near-zone recall |
| Index churn (20 × add/remove 2,000) | §14 | quadtree 1.00×, r-tree 1.57×, geohash 1.00×, hash_table 1.00× node count vs fresh |
| Geohash serialisation | §15 | 44 bytes / zone; 100,000 zones read in 1.39 ms |
| Shamos–Hoey vs pairwise validation | §16 | sweep first wins at 80 vertices; 7.52× at 2,048; verdicts agree on every ring |
| k-d tree vs linear snap | §17 | 37.8× at 10,000 junctions, same node every time |
<!-- /results:extensions -->

## Test suite and verification

Not benchmarks, but the numbers people ask for next — see
[TESTING.md](TESTING.md) for what each means. The suite is
`make test`; the whole of it runs under UBSan locally and ASan + UBSan in Linux
CI; `make mutation` kills 22 of 22 injected bugs; `make determinism` checks
byte-identical output for a fixed seed.
