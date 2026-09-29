# Résumé material

Bullet candidates for SafeTrail, each built from verified facts. The rule: **every
number has a source one line below it** — a command, a CSV, or a test. If you
cannot rerun it, do not use it. Benchmark numbers are from one laptop and move
between runs, so quote the ranges given here, not a single run's value, and
re-check them against [RESULTS.md](RESULTS.md) after any `make bench`.

---

## Bullets

Each is about thirty words — two lines on a résumé. The source line under each
is not part of the bullet.

1. **Built a C++17 geofencing engine on hand-written spatial and temporal indexes
   (quadtree, STR R-tree, AVL interval tree, persistent quadtree); on a synthetic
   100k-zone benchmark, range queries ran ~100–115× faster than a linear scan,
   with identical results.**
   <sub>`make bench` §1, `bench/results/index_scaling.csv` and
   `bench/results/variation/` (R-tree 102–114× over four runs); identical results
   enforced by result checksums on every timed pass and
   `tests/index/differential_test.cpp`.</sub>

2. **Tested correctness with randomized differential tests against a
   brute-force oracle, invariant audits after every operation, and mutation
   testing (22/22 injected bugs caught), uncovering 8 latent defects in code that
   passed its tests.**
   <sub>`make test`, `make stress`, `make mutation`;
   [DEFECT_LOG.md](DEFECT_LOG.md) pass 6.</sub>

3. **Traced ~60% of a simulation's enter/exit events to a hysteresis flaw that let
   uncertain GPS readings bypass the flap filter; after the fix, transition counts
   came within 4% of noise-free ground truth under simulated GPS drift.**
   <sub>Dashboard configuration before/after (2,440 → 987 entries);
   `make bench` §10, `bench/results/hysteresis_ab.csv` (502 vs 521);
   `tests/fence/hysteresis_test.cpp`.</sub>

4. **Found a sweep-line (Shamos–Hoey) bug by fuzzing 800,000 small degenerate
   polygons against an O(V²) oracle, and made the spatial filter conservative by
   deriving query boxes from exact spherical-cap bounds.**
   <sub>`tests/geo/sweep_line_test.cpp`, `tests/geo/bbox_around_test.cpp`;
   [DEFECT_LOG.md](DEFECT_LOG.md) pass 6. The 800,000-ring run (94 disagreements)
   was a one-off; its failures are pinned as regression cases, and the checked-in
   fuzz loop covers 12,000 rings per `make test`, 120,000 under `make stress`.</sub>

5. **Designed a persistent path-copying quadtree that answers "rules at time t"
   queries about as fast as present-time ones, copying ~15 nodes per insert or
   removal (13× structural sharing at 5,001 versions), verified against a
   full-replay oracle.**
   <sub>`make bench` §9 (`versioned_index.csv`, `versioned_mutations.csv`);
   `tests/index/persistence_differential_test.cpp`.</sub>

6. **Rebuilt the benchmark harness — calibrated interleaved sampling, paired
   ratios, result checksums, recorded environment — after the published headline
   proved irreproducible; docs tables are generated from the CSVs and checked in
   CI.**
   <sub>`apps/safetrail_bench.cpp` (protocol comment), `tools/render_results.py`,
   `.github/workflows/deploy.yml`.</sub>

## Short forms

- *Hand-built quadtree, STR R-tree, AVL interval tree and persistent quadtree in
  C++17; ~100–115× faster range queries than a linear scan on a synthetic
  100k-zone benchmark, with identical results.*
- *Differential and mutation testing (22/22 injected bugs caught) found 8 latent
  defects in code that already passed its tests.*

## Choosing bullets

For a systems or infrastructure role, lead with 2 and 6 (correctness and
measurement discipline); for an algorithms-heavy role, 1, 4 and 5. Bullet 3 is the
best interview story: a clear before/after, and a lesson about metrics. Two or
three bullets are plenty.

## Do not claim

- **"Faster than PostGIS / Boost."** Every speedup is against this project's own
  brute force, not an external library.
- **A single speedup figure as a constant, or as a guarantee.** Quote the range;
  one laptop, synthetic zones, and ratios moved by more than 2× between sessions
  (97–229× in one set of runs). The bounds are O(log n + k) expected, O(n) worst
  case.
- **Mutation score as proof of correctness.** 22/22 means those 22 hand-written
  bugs are caught; it says nothing about bugs unlike them.
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
