# Phase-zero audit — SafeTrail as found on 2026-10-30

This is the audit that came before any of the final-product work. It records
what the repository on `main` at `0f4b877` **actually did**, traced through the
code rather than taken from the README or the header comments. Everything the
later phases changed is measured against this baseline.

Method: every header in `include/safetrail/`, every source file in `src/` and
`apps/`, the test harness and all 46 test files, the Makefile, CMake, CI
workflow and every document in `docs/` were read; each module was then traced
from an executable (`apps/safetrail_headless.cpp`, `apps/safetrail_bench.cpp`)
or a test to decide whether it is on a running path. The suite was built and
run from clean: **46 test files, all passing** (`make test`, Apple clang 17,
M4).

## 1. Architecture map (as found)

```
 data/zones/*.geojson ─┐                    data/osm/roads.txt
                       ▼                            │
             fence::ZoneStore ──► index::SpatialIndex (brute / quadtree / R-tree / geohash)
                       │                 │                        │
                       │                 ▼                        ▼
 sim::Simulator ──► fence::Evaluator (9 steps) ──► fence::Event   graph::RoadGraph (+ k-d tree snap)
 (synthetic          │  usability gate · prune · validity ·          │
  tourists,          │  containment · hysteresis · transitions ·     ▼
  mobility, GPS      │  prediction · reconciliation           dispatch::assign_greedy / _optimal
  error model)       ▼                                        (Dijkstra cost matrix, Hungarian)
             alert::Alert ──► alert::Correlator (union-find) ──► incidents ── (end of run only)
             group::CohesionMonitor (RollbackDSU) ──► counted only
             index::VersionedIndex (persistent quadtree + interval tree) ── history
                       │
                       ▼
 viz::export_html ──► dashboard.html (one static file: a replay of one pre-computed run)
 apps/safetrail_headless (CLI)   apps/safetrail_bench (benchmarks → bench/results/*.csv)
```

There was **no server, no user model, no authentication, no API and no
interactive state**. The only user interface was `dashboard.html`: a replay of
a single simulation run baked into the page as JSON, with a time slider and an
index switch. It is deployed as a static page to GitHub Pages and Vercel.

## 2. Feature inventory and classification

Legend — **V**: fully implemented and verified (on a running path, tested);
**I**: implemented, insufficiently verified or not on any running path;
**P**: partially implemented; **M**: missing; **N/A**: not applicable.

### The fifteen capabilities the product brief asks for

| # | Capability | Status | Evidence (traced) |
|---|---|---|---|
| 1 | Manage multiple registered users | **M** | `track::Tourist` is an engine record (id, fix, zone states). There is no registration, profile, role, activation or account of any kind. |
| 2 | Track each participating user during an active session | **P** | The simulator evaluates N synthetic tourists independently each tick, but there is no notion of a tracking *session*, consent, start/stop, or live ingestion of fixes from outside the simulator. |
| 3 | Evaluate positions against many hazard zones | **V** | `fence::Evaluator`, four indexes, differential + index-independence tests. |
| 4 | Entry and exit detection | **V** | Transitions only, hysteresis, `tests/fence/*`, golden replays. |
| 5 | Uncertain GPS readings | **V** | Three-valued containment, `Ambiguous` hysteresis phase, `tests/geo/containment_uncertainty_test.cpp`. |
| 6 | Predicting boundary crossings | **V** | Evaluator step 8 emits `ZoneApproaching` with an ETA. |
| 7 | Prolonged presence (dwell) | **V** | `DwellExceeded` when `max_dwell_ms` is set. |
| 8 | Separated group members | **P** | `group::CohesionMonitor` detects fragmentation and stragglers, but its events are only **counted** by the simulator — they never become alerts. `CohesionConfig::confirm_ms` and `widening_rate_mps` are declared and **never read**: a straggler fires on the first minute it is seen, with no separation-duration check. |
| 9 | Correlating alerts into incidents | **V** | `alert::Correlator`, union-find, `tests/alert/*`. |
| 10 | Identifying and assigning responders | **P** | Greedy vs Hungarian assignment runs **once at the end of a run** (`Simulator::finalize`) to produce two totals for the report. Responders have no names, no availability changes, no acknowledgement, no reassignment. |
| 11 | Routes over a road network | **P** | Dijkstra and A* are implemented and tested; A* is only used by the benchmark. No route is ever produced for an assignment — the dashboard draws a straight line from responder to incident. |
| 12 | Operating during network outages | **I** | `sync::OfflineQueue` / `sync::Reconciler` are implemented and unit-tested (`tests/sync/*`), but nothing on a running path uses them. |
| 13 | Synchronising after reconnect | **I** | As 12. Reconciler de-duplication is an O(n) scan per event (O(n²) per merge). |
| 14 | Historical records and auditable evidence | **P** | `VersionedIndex` answers "which zones were in force at t" and is on the running path. The Merkle log is built only inside the HTML exporter, to print one root. No user movement history, no alert/incident history store, no inclusion-proof endpoint. |
| 15 | Interface for users, responders and administrators | **M** | One read-only replay page for an observer. No per-role views, no controls that change state. |

### Modules

| Module | Status | Notes |
|---|---|---|
| `geo/` (projection, containment, polygon validation, sweep line) | V | Oracles + edge-case tests. |
| `index/` brute force, quadtree, R-tree, geohash | V | Differential tests with invariant audit after every op; mutation-tested. |
| `index/kd_tree.hpp` | V | Used for road-graph snapping; tested. |
| `index/versioned_index` (persistent quadtree) | V | On the running path (simulator mirrors zones into it). |
| `ds/interval_tree.hpp` (AVL) | V/I | Used inside `VersionedIndex`; tested; directly benchmarked. |
| `ds/hash_table.hpp` (open addressing) | I | Tested, benchmarked; used by `EscalationTracker`, which nothing runs. |
| `ds/priority_queue.hpp` (binary heap) | V | Dijkstra/A* frontier; `TriageQueue` (not run). |
| `ds/timer_wheel.hpp` | I | Only inside `EscalationTracker`, which nothing runs. |
| `ds/circular_buffer.hpp` | V | Per-tourist ping window. |
| `ds/dynamic_connectivity.hpp` (rollback DSU) | V | Cohesion monitor. Rollback itself only exercised by tests. |
| `fence/hysteresis` | V | Pinned by A/B golden test. |
| `alert/escalation.hpp`, `alert/triage.hpp` | I | Header-only, tested, **never instantiated** outside tests. |
| `alert/correlator` | V | On the running path. |
| `dispatch/assigner` | P | End-of-run comparison only (above). |
| `graph/astar` | I | Bench only. |
| `group/cohesion` | P | See capability 8. |
| `sync/lamport` | I | Tests only. |
| `evidence/merkle_log`, `digital_id` | I/P | Merkle root printed by the exporter; digital-ID QR flow tested only. |
| `power/adaptive_sampler` | P | Fed by the evaluator every evaluation, but gates nothing: every simulated tourist still produces a fix every tick. Measured on its own in bench §13. |
| `jurisdiction/hierarchy` | I | Tested; no zone or alert is ever resolved through it. |
| `track/anomaly` | V | Signal-lost / stationary / route-deviation alerts in the simulator. |
| `viz/html_export` | V | The static dashboard. |
| `web/zone_editor.html` | P | A standalone polygon sketcher that downloads GeoJSON; not connected to anything. |

## 3. Partially implemented features

1. Group monitoring — detection without alerts, without separation duration, without configurable thresholds at run time.
2. Dispatch — an offline comparison, not an operation: no live assignment, no route, no acknowledgement, no reassignment, no availability.
3. Evidence — a Merkle root, no proofs served, no event log feeding it continuously.
4. Battery-aware tracking — the policy is computed, never applied.
5. Zone authoring — a disconnected editor page; validation exists in `ZoneStore::load_geojson` but only from a file path.
6. Incident lifecycle — `Correlator::close` exists; nothing calls it; no acknowledged/dispatched states.

## 4. Missing features

User accounts, roles, authentication, sessions, consent and opt-in tracking,
stop-sharing, retention and deletion, access audit, live location ingestion,
duplicate/out-of-order handling at an ingestion boundary, per-user history,
an operator API, a scenario runner with pause/step/speed, zone CRUD at run
time, alert acknowledgement, responder management, route visualisation,
historical "what did the system know at time t" for users and alerts, an
interactive data-structures lab, multi-user benchmarks.

## 5. Bugs and integration gaps found

| # | Finding | Severity | Disposition |
|---|---|---|---|
| A1 | `CohesionConfig::confirm_ms` and `widening_rate_mps` are never read; a straggler is reported the first time it is seen. | Medium — a documented threshold has no effect | Separation duration implemented in the platform's group service (the simulator's pinned golden output is left unchanged and the reason is recorded). |
| A2 | Cohesion events never become alerts or incidents. | High for the product brief | Fixed in the platform layer: `Straggler` / `GroupFragmented` alerts. |
| A3 | `EscalationTracker`, `TriageQueue`, `TimerWheel`, `HashMap` (outside the bench), `astar`, `Reconciler`, `OfflineQueue` are tested but on no running path. | Integration gap | Wired into the platform. |
| A4 | `Reconciler::merge` de-duplicates with a linear scan of the whole timeline per event — O(n²) per merge. | Low at demo scale, real at scale | Replaced with an O(1)-expected hash-set lookup using the project's own `ds::HashMap`; same output, tested. |
| A5 | Dispatch lines are drawn straight, not along roads. | Misleading visual | Routes now come from A* over the road graph. |
| A6 | The adaptive sampler gates nothing, so the battery figures in the dashboard describe a policy the simulation never applied. | Honesty | The platform applies the sampling decision for users in battery-aware mode and counts fixes taken vs skipped. |
| A7 | `make test` passes, but no test exercises two users in the same zone, a user leaving a session, duplicate or out-of-order fixes at an ingestion boundary — because there was no ingestion boundary. | Coverage | New tests (Phase P6). |

## 6. UI/UX assessment

The static dashboard is a good *explainer* — its legend is honest, the index
overlay is a strong visual, and it works offline — but it is not an
application: there is nothing to operate. Every control is a view control.
There is no per-user view, no alert list that can be acted on, no zone
management, no history query beyond the persistent-index panel, and no notion
of who is looking at the page. It cannot be used to answer "what happened to
user X between 10:02 and 10:15".

## 7. Testing and security assessment

Testing of the engine is unusually strong: differential tests against oracles,
invariant audits after every operation, 23/23 mutants killed, sanitizers,
cross-platform byte-identical output. The gap is everything above the engine,
which did not exist.

Security: with no server and no accounts there was little attack surface; the
GeoJSON and JSON parsers already bound depth, reject non-finite coordinates
and validate polygons. A product with accounts and location data needs: password
hashing, session expiry, role checks on every endpoint, CSRF protection,
request-size and rate limits, audit of every location read, retention and
deletion, and secrets kept out of the repository. None of that existed.

## 8. Prioritised roadmap (what the later phases did)

| Priority | Work |
|---|---|
| P0 | Keep every existing structure, test and output intact; build beside the engine, not over it. |
| P1 | A platform layer (`src/app/`): users, roles, consent, tracking sessions, ingestion with duplicate/out-of-order/jump handling, per-user isolated engine state. |
| P2 | Alerts with acknowledgement and escalation (timer wheel + hash map), triage (binary heap), incidents with a workflow, groups with separation duration, responders with A* routes and Hungarian batch assignment, zone CRUD with history. |
| P3 | A dependency-free C++ HTTP server, authentication, the API, and a scenario runner (start/pause/resume/reset/step/speed). |
| P4 | A map-first operations web app; a Data Structures Lab that calls the real C++ structures. |
| P5 | Offline devices (Lamport stamps, offline queue, reconciliation), history at time t, Merkle inclusion proofs. |
| P6 | Tests for every case in the brief, multi-user benchmarks, security review. |
| P7 | Academic deliverables. |

`docs/final/PROJECT_REPORT.md` §"Results" and `docs/final/TESTING_AND_BENCHMARKS.md`
record what was finally verified against this baseline.
