# Architecture

Start with [GAP_ANALYSIS.md](GAP_ANALYSIS.md) — it explains why this is not a
straight implementation of `SIH25002`.
[ARCHITECTURE_BASELINE.md](ARCHITECTURE_BASELINE.md) has the original design this
was extended from.

## Layers

```
┌────────────────────────────────────────────────────────────────┐
│ web/        Leaflet map · zone editor · alert rail · replay     │
├────────────────────────────────────────────────────────────────┤
│ server/     HTTP API (CRUD) + WebSocket (state deltas, 10 Hz)   │
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
              VersionedIndex.query_at(now, bbox) ──► candidates  [GAP 3]
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
                     WebSocket delta ──► browser
                                   │
                     offline? ──► OfflineQueue ──► Reconciler  [GAP 6]
```

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
