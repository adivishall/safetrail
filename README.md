# SafeTrail

**An efficient geofencing engine in C++17, built on hand-written spatial and
temporal data structures, measured against brute force, and tested until the
fast answer is provably the same as the slow one.**

**What it is.** Given up to 100,000 hazard-zone polygons and a stream of noisy
GPS fixes, SafeTrail decides, fix after fix, whether each person is **inside,
outside or uncertain** relative to every zone near them, and emits an event only
when that changes. Zones switch on and off over time, and every past
configuration stays queryable.

**Why.** Checking every zone against every fix is O(n) per fix — 711 million
box tests in one simulated hour of the demo scenario. Repeated containment is the
whole cost of geofencing, and a spatial index is the data-structures answer to it.

**What I built.** Five structures behind one interface, with no spatial library
and no dependencies beyond a C++17 compiler: a brute-force scan (kept as the
oracle), a **quadtree**, an **STR-packed R-tree**, an **AVL interval tree** for
"which rules are in force at t", and a **persistent quadtree** for "what were the
rules at 14:32". Around them: exact point-in-polygon geometry under GPS
uncertainty, a hysteresis state machine that turns noisy positions into
trustworthy transitions, a benchmark harness, and a one-file dashboard.

**What makes it interesting.** The index is only allowed to make the answer
faster, never different. Every structure is compared with a brute-force oracle on
randomized hostile workloads with its invariants audited after every operation;
the whole engine must emit bit-identical events under all four indexes; and 22
injected bugs must each be caught by a test (22/22). That process found **ten
real defects** in code that was passing its tests.

- **Result (measured):** at 100,000 zones in one district,
  <!-- results:keyline -->the R-tree answers the index query **223× faster** than a linear scan (223–235× over 4 runs in one session) and the quadtree **33.5×** (33.5–34.4×), returning identical results<!-- /results:keyline -->.
  The R-tree's ratio moves between sessions: an earlier session on the same
  laptop measured 102–114×, so read ~100× as the floor
  ([why](docs/RESULTS.md#read-this-first)).
- **Run it:** `make review` — the whole story from real runs in about a minute,
  ending with the dashboard. Or piece by piece: `make test && make demo && make
  dashboard`. A C++17 compiler and `make`, nothing else.

> **Project review:** [REVIEW_DEMO.md](docs/REVIEW_DEMO.md) is the 5-minute live
> demo built on `make review`; [REVIEW_QA.md](docs/REVIEW_QA.md) answers the
> questions it raises from the code; [REVIEW_SLIDES.html](docs/REVIEW_SLIDES.html)
> is ten slides whose numbers are generated from the committed benchmark results.

| Structure | Answers | Bound (theoretical) | Implementation |
|---|---|---|---|
| **Brute force** | which zone boxes meet this box? (the oracle) | O(n) | a scanned vector, kept forever as the baseline |
| **Quadtree** | same, partitioning **space** | O(log n + k) expected; O(n) worst | root fitted to data, doubling expansion, collapse on delete |
| **R-tree** | same, partitioning **items** | O(log n + k) expected; O(n) worst | quadratic split, **STR bulk loading**, condensing delete |
| **AVL interval tree** | which rules are in force at t? | O(log n) to find one; O(min(n, (k + 1) log n)) to report k, worst case | max-high augmentation, total-order key |
| **Persistent quadtree** | what were the rules at 14:32? | O(depth) new nodes per change | path copying + append-only validity log |

k is the number of results. No spatial library, no
`std::map`/`set`/`unordered_map`/`priority_queue`, no dependencies beyond a C++17
compiler. [DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) covers each structure's
invariants and bounds.

![SafeTrail dashboard: simulated tourists over real OpenStreetMap hazard zones around Shillong, with the index-scaling table measured by make bench](docs/images/dashboard-main.png)

<sub>`make dashboard` writes one self-contained HTML file (no server, no network):
real OpenStreetMap zones, simulated tourists, the event stream, and a switch that
draws each index's actual structure on the same data.</sub>

| Quadtree — partitions **space** (disjoint cells) | R-tree — partitions **items** (overlapping envelopes) |
|---|---|
| ![Quadtree cells over the zones](docs/images/dashboard-quadtree.png) | ![R-tree envelopes over the zones](docs/images/dashboard-rtree.png) |

## Measured results (empirical)

Speedups are **paired ratios against this project's own brute force**, shown with
their range over independent runs. Absolute times are one laptop's
(<!-- results:machine -->Apple M4, Battery power, load average 3.63 at the start, commit f267de0<!-- /results:machine -->; see
[RESULTS.md](docs/RESULTS.md) for the protocol). Every number is generated from
`bench/results/*.csv`.

<!-- results:headline -->
| Workload | Brute force / naive | Quadtree speedup (range over runs) | R-tree speedup (range over runs) |
|---|---:|---:|---:|
| Range query, 100,000 zones in one district (k ≈ 98) | 244 µs | 33.5× (33.5–34.4×) | 223× (223–235×) |
| Range query, 100,000 zones at constant density (k ≈ 1) | 218 µs | 551× (546–575×) | 1048× (961–1048×) |
| Point-in-zone, 5,000 polygons × 128 vertices, vs checking every polygon | 262 µs | 1509× (1497–1540×) | 2100× (ratio of medians) |
<!-- /results:headline -->

What the numbers say, and why:

- **The speedup has a ceiling, and it is the answer size.** In one crowded
  district, about 100 zones really do overlap each query at 100,000 zones, and
  every index must return all of them, so the quadtree's ratio stops growing
  after a few thousand zones. Hold the density constant so each query overlaps
  about one zone, and the same code is hundreds of times faster at 100,000
  zones. O(log n + k), measured both ways. (§1–2)
- **The index accelerates the filter, not the geometry.** Against checking every
  polygon the tree wins by two to three orders of magnitude, more as polygons
  get more complex; against a plain bounding-box scan followed by the same exact
  test the margin is far smaller and shrinks as vertex counts grow, because the
  exact test on the few candidates is the same work in both.
  ([RESULTS.md](docs/RESULTS.md) §3)
- **STR packing matters**: a bulk-loaded R-tree is about 6× faster to query than
  the same R-tree built by insertion at 100,000 zones. (§7)
- **The interval tree wins only when the query is selective**: an order of
  magnitude or more over a linear scan for short windows; when hundreds of
  windows contain every instant it loses to the scan at 1,000 windows and is at
  most a small factor ahead at 100,000 — what its O(min(n, (k + 1) log n)) bound
  predicts. That is why the engine checks validity in O(1) per candidate per
  tick and uses the tree only for history queries. (§8)
- **Persistence is cheap**: one root-to-leaf path per change (≈15 nodes on a
  5,000-zone index), zero for a rule change, and a query against the past costs
  the same as one against the present. (§9)
- **Reconciliation does not undo the index**: <!-- results:evalfix -->the whole `evaluate()` — index, geometry, hysteresis and the observation of every zone with open state — costs **4.88 µs per fix at 5,038 zones and 5.46 µs at 50,038** when the zone density is held constant; packed into one district it costs 2.25 µs at 1,038 zones and 17.1 µs at 20,038, tracking the candidate count (k grows with n, as in §1). Reconciliation itself is 0.08–1.42 zones observed per fix outside the candidate set, a box distance each, no geometry<!-- /results:evalfix -->. (§18)

## How correctness is established (evidence)

| Method | What it covers |
|---|---|
| **Oracles** | every fast structure has an independent one: brute force for the indexes, a linear scan for the interval tree, a full-table replay for the persistent index, winding number for ray casting, an O(V²) scan for the sweep line |
| **Differential tests** | ~55,000 queries per index over seven hostile workload profiles (identical boxes, zero-area boxes, touching edges, both hemispheres, poles, past lon 180, far inserts), mixed with inserts, removals and rebuilds |
| **Invariant audits** | each core structure's `check_invariants()` runs after **every** operation in those tests |
| **End to end** | the whole engine under all four indexes must emit bit-identical event streams |
| **Mutation testing** | 22 realistic bugs injected into the core; the suite must fail on each (`make mutation`: 22/22) |
| **Sanitizers, warnings, analysis** | the whole suite is UBSan-clean on macOS and gated under ASan + UBSan in Linux CI; `-Werror` builds with clang, and with g++ 13 in CI; Clang Static Analyzer: 0 findings |

This process found **ten defects** in code that was already passing its tests —
eight in the engine, two in how it was measured. One was the state machine never
observing a zone again after it left the candidate window: a missed exit, then a
silent re-entry. Another, in the hysteresis filter, produced about 60% of the
demo's enter/exit events. Each is pinned: by a test that fails on the old code,
or, where the test now uses API the old code lacks, by a mutant that reverts the
fix; the measurement defect is pinned by the benchmark's own gates
([DEFECT_LOG.md](docs/DEFECT_LOG.md), [TESTING.md](docs/TESTING.md) §5).

## Run it

```bash
make review       # the project walkthrough: every index, identical answers,
                  # transitions, noise, history, a live benchmark, tests,
                  # dashboard (PAUSE=1 to step through it while presenting)
make test         # 45 test files, ~11,800 checks (~20 s once built)
make demo         # the engine on real OSM zones: event stream + counters
make dashboard    # writes dashboard.html; just open it
make bench        # every benchmark (~2–3 min), regenerates the tables above
make validate     # everything that gates a merge: -Werror, docs, tests,
                  # determinism, sanitizers, CMake + ctest
make mutation     # the mutation check (needs python3)
```

Plain `make` and a C++17 compiler. Developed with Apple clang 17; CI builds with
g++ 13 and clang on Linux and Apple clang on macOS. CMake builds the same source
set from the same flag files (`make cmake-build`). The
dashboard opens `#index=rtree&frame=400` style deep links.

## How it fits together

```
 GPS fix ──► query box ──► spatial index ──► per candidate: in force? ──► exact
 (± accuracy)   (provably      (quadtree /       O(1) validity check        three-valued
                conservative)   R-tree / …)                                  geometry
                                                                               │
 events ◄── transition only on change ◄── hysteresis ◄────────────────────────┘
   │        (+ every zone with open state observed each tick, returned or not)
   └──► extensions: alert correlation → incidents → responder dispatch (Dijkstra/A*,
        Hungarian) · group cohesion · Merkle evidence log · the dashboard
```

The interval tree and the persistent quadtree sit beside this path, answering
historical questions. [ARCHITECTURE.md](docs/ARCHITECTURE.md) has the full
pipeline, the design decisions with their costs, the limitations, and what would
change at production scale.

## Limitations

- **Simulated people, real geography.** OpenStreetMap zones around Shillong,
  Meghalaya; synthetic movement with a GPS error model. No field data.
- **Removal is O(n)** in every spatial index (no id → node map); the quadtree and
  R-tree have O(n) worst-case queries on adversarial data. The interval tree's
  guarantee is O(min(n, (k + 1) log n)) for reporting k results — a bound, but
  not the O(log n + k) a centred interval tree would give.
- **Tolerance-based geometry**, not exact predicates (on-edge tolerance ~0.1 mm).
- **No antimeridian support.** Zones crossing ±180° are refused at load, and
  query boxes do not wrap, so a fix at 179.99° cannot see a zone at −179.99°.
- **GPS jumps inflate the query radius.** A jump sits in the 64-fix speed window
  for about a minute, the radius clamps toward 10 km, and pruning collapses for
  that person meanwhile — still correct, just slow.
- **Single-threaded**, one process, in-memory.
- Benchmarks come from one laptop. Ratios transfer far better than absolute times,
  and even ratios move — under ~10% within a session at 100,000 zones, more
  than 2× between sessions for the R-tree — which is why they're quoted as
  ranges.

## Documentation

| | |
|---|---|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | pipeline, layers, design decisions, limitations, production changes |
| [DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) | each structure: design, invariants, bounds (guaranteed vs expected) |
| [RESULTS.md](docs/RESULTS.md) | every benchmark, the protocol, the environment, run-to-run ranges |
| [TESTING.md](docs/TESTING.md) | oracles, differential tests, invariants, mutation results, sanitizers |
| [GEOMETRY_EDGE_CASES.md](docs/GEOMETRY_EDGE_CASES.md) | what breaks point-in-polygon and polygon validation, and the tolerances |
| [DEFECT_LOG.md](docs/DEFECT_LOG.md) | every real defect found, its fix, and the test that pins it |
| [REVIEW_DEMO.md](docs/REVIEW_DEMO.md) · [REVIEW_QA.md](docs/REVIEW_QA.md) · [REVIEW_SLIDES.html](docs/REVIEW_SLIDES.html) | the 5-minute live demo on `make review`, the questions it raises, and ten slides |
| [INTERVIEW.md](docs/INTERVIEW.md) · [RESUME.md](docs/RESUME.md) | the project explained at 30 s / 60 s / 3 min, and résumé bullets with sources |
| [GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) · [DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md) · [DEPLOYMENT.md](docs/DEPLOYMENT.md) | why the extensions exist; where the dashboard's data comes from; CI and Pages |
| [docs/course/](docs/course/README.md) | the original course submission (viva prep, slides), archived |

SafeTrail started as a Data Structures course project derived from Smart India
Hackathon 2025 problem SIH25002 (Ministry of DoNER).
