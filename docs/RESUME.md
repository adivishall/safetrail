# Résumé material

Bullet candidates for SafeTrail, each built from verified facts. The rule: **every
number has a source one line below it** — a command, a CSV, or a test. If you
cannot rerun it, do not use it. Benchmark numbers are from one laptop and move
between runs, so quote the ranges given here, not a single run's value, and
re-check them against [RESULTS.md](RESULTS.md) after any `make bench`.

---

## Bullets

**1. The system**

> Built a C++17 geofencing engine on hand-written spatial and temporal data
> structures (quadtree, STR-packed R-tree, AVL interval tree, persistent
> path-copying quadtree) with a brute-force correctness oracle, cutting
> bounding-box query time at 100,000 zones by ~100–230× (R-tree) and ~26–33×
> (quadtree) while returning results identical to a linear scan.

<sub>Source: `make bench` §1 (`bench/results/index_scaling.csv`, ranges over the
runs in `bench/results/variation/`); identical results enforced by per-query
checksums in the benchmark and by `tests/index/differential_test.cpp`.</sub>

**2. Correctness engineering**

> Validated the engine with randomized differential testing (~275,000 queries
> over seven adversarial workload profiles), structural invariant audits after
> every operation, and mutation testing (22 of 22 injected bugs caught), finding
> and fixing 10 latent defects (8 in the engine, 2 in its benchmarks) — including a
> state-machine bug that silently dropped zone exits after GPS gaps.

<sub>Source: `make test` (45 files), `make mutation`,
[DEFECT_LOG.md](DEFECT_LOG.md) pass 6, `tests/fence/state_reconciliation_test.cpp`.</sub>

**3. A bug with a number on it**

> Diagnosed a hysteresis leak that let uncertain GPS readings bypass the flap
> filter, producing about 60% of all enter/exit events in the reference run
> (2,440 → 987 entries); replaced a before/after metric that could not detect
> missed crossings with a noise-free ground-truth comparison, showing the filter
> recovers 96% of the transitions perfect GPS would yield under realistic drift,
> while an unfiltered geofence reports ~8× more transitions than real crossings.

<sub>Source: `make dashboard` configuration run on the code before and after the
fix; `make bench` §10 (`bench/results/hysteresis_ab.csv`);
`tests/fence/hysteresis_test.cpp`.</sub>

**4. The advanced structure**

> Designed a persistent quadtree using path copying and an append-only validity
> log, answering "which rules were in force at time t" at the cost of a present-
> time query, allocating ~15 nodes per change (13× structural sharing at 5,001
> versions) and verified against a full-replay oracle.

<sub>Source: `make bench` §9 (`versioned_index.csv`, `versioned_mutations.csv`);
`tests/index/persistence_differential_test.cpp`.</sub>

**5. Measurement**

> Rebuilt the benchmark harness — calibrated samples, interleaved rounds, paired
> speedup ratios, result checksums, recorded environment — after finding the
> previously published headline speedup irreproducible; documentation tables are
> generated from the benchmark CSVs and CI fails if they disagree.

<sub>Source: `apps/safetrail_bench.cpp` (protocol comment), `tools/render_results.py
--check` in `.github/workflows/deploy.yml`, [DEFECT_LOG.md](DEFECT_LOG.md).</sub>

**6. Build and CI**

> Enforced `-Werror` on gcc and clang from one warning list shared by Make and
> CMake, whole-suite AddressSanitizer + UBSan, the Clang Static Analyzer (0
> findings), mutation testing and a byte-for-byte determinism check in CI.

<sub>Source: `tools/build/*.flags`, `Makefile`, `CMakeLists.txt`,
`.github/workflows/deploy.yml`, `make analyze`, `make determinism`.</sub>

---

## Short forms

- *Hand-built quadtree / STR R-tree / AVL interval tree / persistent quadtree in
  C++17; ~100–230× faster range queries than a linear scan at 100k zones with
  identical results.*
- *Differential + mutation testing (22/22 injected bugs caught) found 10 latent
  defects in code that was already passing its tests.*
- *Persistent path-copying quadtree: historical queries at present-query cost,
  ~15 nodes per change.*

## Choosing bullets

For a systems or infrastructure role lead with 2 and 5 (correctness and
measurement discipline); for an algorithms-heavy role, 1 and 4; bullet 3 is the
best story in an interview because it has a clear before/after and a lesson about
metrics. Two or three bullets are plenty.

## Do not claim

- **"Faster than PostGIS / Boost."** Every speedup is against this project's own
  brute force, not an external library.
- **A single speedup figure as a constant.** Quote the range; one laptop, and
  ratios moved by up to 2× between runs.
- **"Tested on real users."** Real OpenStreetMap geography, simulated movement.
- **"Production-ready" or "scales to millions".** Single-threaded, in-memory,
  O(n) removal; see the limitations in [ARCHITECTURE.md](ARCHITECTURE.md).
- **Numbers from [docs/course/](course/README.md).** They predate the audit
  (e.g. "~240–260×", "94% of false transitions removed") and are superseded.

## Technologies actually used

C++17 with no external dependencies; Make and CMake reading one warning list;
AddressSanitizer, UndefinedBehaviorSanitizer and the Clang Static Analyzer; GitHub
Actions (gcc and clang, Linux and macOS); Python 3 for data preparation, plotting,
the mutation check and the docs checks; a self-contained HTML/Canvas dashboard (no
Leaflet, no tiles, no network).

Concepts: quadtree, R-tree with STR bulk loading, AVL interval tree, persistent
path-copying trees with structural sharing, ray casting and winding-number
point-in-polygon, Shamos–Hoey sweep line, filter-then-refine query processing,
hysteresis state machines, differential and mutation testing, determinism
engineering; in the extensions, Dijkstra/A*, Hungarian assignment, rollback
union-find, Lamport clocks, Merkle logs, k-d trees.
