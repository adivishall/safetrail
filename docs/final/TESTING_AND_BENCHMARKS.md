# Testing and benchmarks

The project's rule is that a faster structure may never give a different
answer, and that every claim has a measurement or a test behind it. The engine
methodology is described in depth in [../TESTING.md](../TESTING.md) and
[../RESULTS.md](../RESULTS.md); this document summarises it and adds the
platform, API and multi-user evidence produced in the final phase.

## 1. Results at a glance (2026-10-30, Apple M4, Apple clang 17)

| Check | Command | Result |
|---|---|---|
| Unit, property, differential, golden, platform, API and HTTP tests | `make test` | **51 files, 12,066 checks, 0 failed** |
| End to end over real HTTP | `make e2e` | **29 checks, 0 failed** |
| Mutation testing | `make mutation` | **28/28 injected bugs caught** (23 engine + 5 platform) |
| Full gate | `make validate` | passes: docs, results tables, `-Werror`, headers, tests, determinism, e2e, UBSan, CMake + ctest, Make-vs-CMake byte-identical output |
| Benchmark correctness gates | `make bench` | all 8 gates pass (scaling, density, end to end, equivalence, containment, interval, evaluator, multi-user) |
| Browser | manual, recorded in the work log | sign-in, scenario at 20×, incident acknowledge/assign with road route, Lab views, all 220 evidence entries re-verified in the browser, a forged entry rejected |

## 2. How correctness is established

| Layer | Method | Where |
|---|---|---|
| Spatial indexes | ~55,000 randomised queries per index against brute force over seven hostile workloads, with `check_invariants()` after every operation | `tests/index/differential_test.cpp`, `churn_test.cpp`, `equivalence_test.cpp` |
| Interval tree | against a linear scan; AVL and augmentation invariants after every insert and delete | `tests/ds/interval_tree_test.cpp` |
| Persistent index | every past version against a full-table replay | `tests/index/persistence_differential_test.cpp` |
| Geometry | ray casting vs winding number; sweep line vs O(V²) pairwise; holes, poles, wraparound | `tests/geo/` |
| Engine end to end | the full simulation under all four indexes emits byte-identical events | `tests/fence/index_independence_test.cpp` |
| Determinism | same seed, same bytes; Make and CMake builds agree; macOS and Linux dashboards compared in CI | `make determinism`, CI |
| Crypto | HMAC-SHA256 and PBKDF2-HMAC-SHA256 against RFC 4231 and RFC 7914 vectors | `tests/app/crypto_test.cpp` |
| Multi-user isolation | two users entering the same zone independently; one leaving while the other stays; one stopping tracking; alerts attributed to the right person | `tests/app/multi_user_test.cpp` |
| Ingestion boundary | duplicate (identical and by sequence number), out-of-order (recorded, not evaluated, trail stays sorted), GPS jump, inaccurate fix, invalid coordinates, future timestamps, fixes without consent (refused, not stored), missing fixes → SignalLost → resolved on contact | `tests/app/multi_user_test.cpp` |
| Consent and access | nobody but the user grants consent or starts sharing; responders see a location only while assigned; reads are audited | `multi_user_test`, `tests/app/api_test.cpp` |
| Evidence | every log entry's inclusion proof verifies; an edited entry does not; no coordinates in evidence | `multi_user_test`, `tools/api_e2e.py`, browser |
| Scenarios | each demo tells its story: no serious alert on the normal walk; approach → uncertain → enter → incident → dwell → exit for the entry; a group split with confirmed straggler and one incident; offline entry arrives late and exactly once, the retry is rejected event for event, Lamport stamps preserved, history shows only what was known; identical event log and Merkle root across runs | `tests/app/scenario_test.cpp` |
| Incidents and dispatch | acknowledge, assign (A* route; A* settles no more than Dijkstra), resolve frees the responder and resolves the alerts; reassignment | `scenario_test`, `api_e2e` |
| Groups | split and reunite; straggler confirmed only after the separation lasts | `scenario_test` |
| Zones | self-intersecting polygon rejected; import all-or-nothing; validity changes | `api_test` |
| HTTP | limits enforced before buffering; chunked, duplicate Content-Length, malformed request lines and headers refused | `tests/net/http_parse_test.cpp` |
| API security | 401 without session, 403 without CSRF header, throttling after 5 failures, idle expiry, logout, role matrix, path traversal (plain and encoded), bounded lab inputs | `api_test`, `api_e2e` |

### Mutation testing (do the tests catch bugs?)

`tools/mutation_check.py` copies the tree, injects one realistic bug at a time,
rebuilds, and requires a named test to fail. The 23 engine mutants (index
pruning, R-tree box growth and reinsertion, AVL rotation and augmentation,
persistent-index history, evaluator reconciliation, hysteresis, containment,
sweep line) were joined by five platform mutants: fixes accepted without
consent, out-of-order fixes evaluated, responder access surviving resolution,
an operator able to grant consent, and the reconciler admitting a retried
batch twice. All 28 are caught.

### Defects found during this phase

| Defect | How it was found | Fix |
|---|---|---|
| `ZoneStore::load_geojson` left a half-loaded store when a later feature was invalid | writing the import API | all-or-nothing parse |
| Group separation never alerted; `confirm_ms` never read | audit | separation-duration rule in the platform |
| Stationary alerts re-raised every tick (each fix reset the confirmed anomaly) | scenario test (1,246 alerts) | only a lost signal clears on contact |
| People resting together flagged as stationary | scenario test | stationary counts only without a group member nearby |
| Scenario "inside" target fell outside concave/thin polygons | scenario test | interior-point grid search; thin OSM polygons documented |
| A phone with a slow clock saw no zones in force | scenario test | documented limitation; demo phone clock is fast instead |
| Quadratic alert bookkeeping (every state change scanned every alert) | benchmark section 19 | per-user alert index |
| Reconciler de-duplication was O(n) per event | audit | hash set of (counter, device) |

## 3. Benchmarks

Protocol (unchanged from the engine): work precomputed; each sample ≥ 20 ms;
11 interleaved rounds with rotated order; medians and IQR; paired speedups;
result checksums as correctness gates. Environment recorded in
`bench/results/environment.txt` (this run: commit 9d33bb6, Apple M4, battery).

| Comparison | Result (this run) | Source |
|---|---|---|
| Range query, 100,000 zones in one district (k ≈ 98) | brute 245 µs; quadtree 32.7×, R-tree 224× faster | `index_scaling.csv` |
| Same at constant density (k ≈ 1) | quadtree 556×, R-tree 1027× | `index_density.csv` |
| R-tree, STR vs insertion at 100,000 zones | 6.3× faster queries, 14,383 vs 21,368 nodes, depth 5 vs 6 | `index_build.csv` |
| Interval tree vs scan (selective windows) | 14–57× faster; **slower** than the scan for dense windows at 1,000 intervals (0.61×) | `interval_tree.csv` |
| Persistent quadtree | 11–13× fewer nodes than full copies at 1,000–5,000 versions; past queries cost the same as present | `versioned_index.csv` |
| A* vs Dijkstra | 71–78 % fewer nodes settled on grids of 64–1,024 junctions | `routing.csv` |
| Hungarian vs greedy dispatch | 6.5–15.7 % less total travel; never worse | `dispatch.csv` |
| Adaptive sampling (8 h trek model) | 257 fixes instead of 28,800 at 100 % near-zone recall (energy is a model) | `power.csv` |
| Hysteresis on realistic drift | 16,168 naive transitions → 502 (target 521) | `hysteresis_ab.csv` |
| Whole evaluator per fix | 5.0 µs at 5,038 zones → 5.7 µs at 50,038 at constant density | `evaluator_cost.csv` |
| **Multi-user platform ingestion** | 2.7–3.8 µs per fix, flat from 10 to 5,000 users (≈ 300,000 fixes/s on one core); ≈ 6.9 KB per tracked user; noisy stream counters exact | `multi_user.csv` |

Single-user versus multi-user: with one user the per-fix cost includes cold
caches and fixed overhead; from 10 users up it is flat, which is the evidence
that users are independent and nothing in the path scales with the number of
other users. The three update patterns cost the same within noise; the noisy
stream records fewer transitions (5,452 vs 5,581 at 5,000 users) because late
fixes are recorded but not evaluated, by design.

### Why theory and measurement differ

See [ALGORITHM_COMPLEXITY.md](ALGORITHM_COMPLEXITY.md) §7: the answer size k
caps the speedup; small inputs favour the contiguous scan; laptop timings drift
between sessions (ratios are quoted as ranges in RESULTS.md).

## 4. Reproduce

```bash
make test && make e2e
```

```bash
make bench
```

```bash
make validate
```

```bash
make mutation
```

## 5. What is not covered

Load across many processes or machines; real GPS traces and real phones;
browser-automation tests of the web app (it was verified by hand in the
browser, and its API by `api_e2e.py`); fuzzing of the parsers beyond the
targeted malformed-input tests; an external security review.
