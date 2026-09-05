# Architecture

Start with [GAP_ANALYSIS.md](GAP_ANALYSIS.md) — it explains why this is not a
straight implementation of `SIH25002`.
[ARCHITECTURE_BASELINE.md](ARCHITECTURE_BASELINE.md) has the original design this
was extended from.

## Layers

```
┌────────────────────────────────────────────────────────────────┐
│ viz/        ONE self-contained HTML file: canvas map · zone     │
│             editor · alert rail · index overlay · replay        │
│             (no Leaflet, no tile server, no network)            │
├────────────────────────────────────────────────────────────────┤
│ (no server layer — see "What is not here", below)               │
├────────────────────────────────────────────────────────────────┤
│ sim/        tick loop · mobility models · scenarios · replay    │
├────────────────────────────────────────────────────────────────┤
│ DOMAIN                                                          │
│   fence/    zone eval, hysteresis          [GAP 8]              │
│   group/    cohesion, stragglers           [GAP 4]              │
│   alert/    triage, escalation, correlate  [GAP 5]              │
│   dispatch/ matching, routing                                   │
│   track/    trajectory, anomaly                                 │
│   sync/     offline queue, reconciliation  [GAP 6]              │
│   power/    adaptive sampling              [GAP 7]              │
│   evidence/ Merkle log                     [GAP 9]              │
├────────────────────────────────────────────────────────────────┤
│ geo/    ★ geometry    index/ ★ spatial (+versioned [GAP 3])     │
│ graph/    routing     ds/    ★ general structures               │
└────────────────────────────────────────────────────────────────┘
```

Layers depend downward only. `ds/` and `geo/` depend on nothing but `types.hpp`.

## The hot loop

`fence::Evaluator::evaluate()` — see the header comment in
`include/safetrail/fence/evaluator.hpp` for the full ordered breakdown. In short,
per tourist per tick:

```
usability gate → adaptive sample? → spatial prune → temporal prune
   → exact three-valued geometry → hysteresis → transition diff → prediction
```

Two things carry the design:

**The spatial prune is the whole performance story.** 100,000 zones down to ~3
candidates before any O(V) geometry runs. Everything else is rounding.

**Transitions, not states.** The evaluator emits `ZoneEnter`, never "is currently
inside". Existing systems report containment every tick and drown the operator.
Diffing against the previous tick is what makes the alert rail mean something —
and it is where the subtle bugs live, which is why hysteresis and deterministic
replay both exist.

## Data flow

```
scenario.json ──► Simulator ──► positions
                                   │
                      AdaptiveSampler decides: take a fix?   [GAP 7]
                                   │
                            UncertainPoint (pos + accuracy)  [GAP 1]
                                   │
       SpatialIndex.query(bbox) ──► candidates      (Quadtree by default;
                                   │            --rtree / --brute swap it)
                      per candidate: validity.active_at(now)     [GAP 3]
                                   │
                      three-valued containment per candidate
                                   │
                          Hysteresis (drift filter)          [GAP 8]
                                   │
                          transition diff ──► Events
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        ▼                          ▼                          ▼
  CohesionMonitor            Triage (heap)              MerkleLog
  groups, stragglers    ──►  Correlator (DSU)     ──►   evidence
      [GAP 4]                  incidents [GAP 5]          [GAP 9]
                                   │
                            Dispatcher (matching + A*)
                                   │
                     TraceRecorder ──► one static dashboard.html
                                   │
                     offline? ──► OfflineQueue ──► Reconciler  [GAP 6]
```

## What is not here, and where the temporal structures actually live

Two corrections that this diagram used to get wrong, kept explicit because the
difference is exactly what a reader would otherwise assume:

**There is no server and no Leaflet.** `server/http_api.hpp` and
`server/ws_stream.hpp` carry no implementation and are not planned;
`apps/safetrail_server.cpp` exists only to fail loudly if something still
references the target. The engine writes one self-contained HTML file that opens
over `file://`. See `viz/html_export.hpp`.

**`VersionedIndex` is not in the tick loop.** The per-tick spatial query goes to
`index::SpatialIndex` (a Quadtree by default), and the per-tick temporal filter is
a direct `z->validity.active_at(now_ms)` — O(1) per candidate, on a candidate set
the spatial prune has already cut to a handful. `VersionedIndex` is built at load
time (`Simulator::reindex`) and read by the dashboard export for the history
panel: `changes_between()`, `version_count()`, `share_stats()`.

That is a deliberate ordering, not an oversight. The spatial filter prunes far
harder than the temporal one — a tourist is near a handful of zones, while MANY
zones are temporally active at any instant — so running the cheap O(1) validity
check over the already-pruned candidates beats running a temporal index first.
`VersionedIndex::query_at()` composes both filters and is what the historical /
audit path uses; `active_at()` is where the interval tree earns its place, on the
temporal-only question where no spatial prune is available.

## Non-negotiables

**The brute-force index is permanent.** `tests/index/equivalence_test.cpp` asserts
every index returns identical results to it. Without that, a benchmark can compare
a correct slow thing against a fast wrong thing.

**Determinism.** Fixed seeds, stable tie-breaking, byte-identical output across
runs. The replay harness is worthless otherwise, and timing bugs become
unfindable.

**No allocation in the tick loop.** Candidate buffers and event vectors are
caller-owned and reused. 200 tourists × 10 Hz is 2,000 allocations per second
otherwise.

**Geometry lives in one place.** The index stores ids and boxes only; `ZoneStore`
owns polygons. Filter-then-refine, never duplicated.
