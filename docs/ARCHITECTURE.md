# Architecture

## The pipeline

One GPS fix, one tourist, one tick. Everything else in the repository either
feeds this path or consumes its output.

```
 GPS fix (position ± accuracy)
   │
   │ 1. usability gate          reject fixes too noisy to mean anything          O(1)
   ▼
 query box = Bbox::around(fix, accuracy + speed × horizon)
   │
   │ 2. spatial filter          SpatialIndex::query → candidate zone ids     O(log n + k)
   │                            (quadtree by default; brute force / R-tree /
   │                             geohash are drop-in; ids sorted afterwards)
   ▼
 candidates (a handful out of up to 100,000)
   │
   │ 3. temporal filter         zone.validity.active_at(now), per candidate      O(1)
   │ 4. exact geometry          three-valued containment vs the accuracy disc    O(V)
   │ 5. hysteresis              Schmitt trigger + N-fix confirmation + dwell      O(1)
   │ 6. transition diff         an event only when the confirmed state changes   O(1)
   │ 7. dwell / prediction      dwell limit; project forward, retest              O(V)
   │ 8. reconciliation          observe every zone with OPEN state that the
   │                            index did not return, or that went out of force  O(s)
   │                            (s = open states, bounded by the neighbourhood;
   │                             a box distance each, no geometry; RESULTS.md §18)
   ▼
 events: ZoneEnter / ZoneExit / ZoneUncertain / ZoneApproaching / DwellExceeded
   │
   └──► extensions: alert correlation → incidents → responder dispatch;
        group cohesion; Merkle evidence log; the self-contained dashboard
```

The code is `fence::Evaluator::evaluate()` in `src/fence/evaluator.cpp`; its
header walks the same steps.

**The spatial filter is the performance story.** Everything after it runs on k
candidates instead of n zones. RESULTS.md §1–3 measures how much that buys and
where it stops buying (k itself).

**The state machine is the correctness story.** Two decisions carry it:

- *Transitions, not states.* The evaluator never reports "is inside"; it reports
  that containment changed. That is what makes an alert mean something — and it
  means per-(tourist, zone) state must stay truthful across ticks.
- *Every zone with open state is observed every tick* (step 8). The index only
  returns zones near the current fix, so a tourist who leaves a zone's window in
  one step — a GPS gap, a vehicle ride — would otherwise never be observed against
  it again: no exit, and a later re-entry silently ignored. That was a real
  defect ([DEFECT_LOG.md](DEFECT_LOG.md), pass 6). A zone the index did not return
  is a *certain* Outside, because `Bbox::around` bounds a disc at least as large as
  the fix's uncertainty; that observation still goes through hysteresis, so one
  wild fix cannot force an exit. A zone that went out of force is closed at once.

**The index is invisible in the output.** Candidates are evaluated in zone-id
order, so the event stream depends only on the zones and the fixes.
`tests/fence/index_independence_test.cpp` runs the whole engine — through alert
correlation and dispatch — under all four indexes and requires bit-identical
results.

## Where the temporal structures are, and are not

The per-tick temporal filter is **not** the interval tree. It is an O(1)
`validity.active_at(now)` on each of the few candidates the spatial filter
returned — cheaper than any temporal index, because a tourist is near a handful
of zones while many zones are in force at any instant.

The interval tree and the persistent quadtree serve the **historical** questions,
through `index::VersionedIndex`:

- `query_at(t, box)` — the zones in force here at time t, *by the rules of time
  t* (spatial filter on the version current at t, then each candidate's validity
  as of that version);
- `active_at(t)` — every zone in force at t, no spatial filter: this is where the
  interval tree's stab — O(min(n, (k + 1) log n)), a guarantee — is the right tool;
- `changes_between`, `history_for`, `validity_at` — the audit trail.

The simulator builds it at load time and the dashboard's investigation panel
reads it; it is not consulted per tick.

## Layers

```
 apps/         safetrail_headless (engine + dashboard export) · safetrail_bench
 viz/          one self-contained HTML file: canvas map, index overlay, replay
 sim/          tick loop, mobility models, GPS error model, scenario
 ───────────────────────────────────────────────────────────────────────────
 fence/        ★ evaluator (the hot loop), hysteresis, zone store + GeoJSON I/O
 track/        tourist state, trajectory, anomaly detection
 alert/ dispatch/ group/ sync/ power/ evidence/ jurisdiction/   extensions
 ───────────────────────────────────────────────────────────────────────────
 index/        ★ SpatialIndex: brute force · quadtree · R-tree · geohash;
               ★ VersionedIndex (persistent quadtree); k-d tree
 geo/          ★ points, boxes, polygons, containment, predicates, sweep line
 ds/           ★ interval tree · heap · hash table · timer wheel · union-find
 graph/        road graph, Dijkstra, A*, assignment
 types.hpp     ids and timestamps
```

Dependencies point downward. `geo/` and `ds/` depend only on `types.hpp` and
each other's headers; `index/` on `geo/`, `ds/` and `util/` (byte encoding); `fence/` on those. ★ marks
the core.

## Design decisions

| Decision | Why | Cost |
|---|---|---|
| Hand-written structures, no spatial library | The structures are the subject; building them is what makes their invariants, bounds and failure modes something I can demonstrate rather than cite | Slower to build, and an obligation to test harder than a library would need |
| Brute force kept forever, behind the same interface | It is the oracle every index is compared with and the denominator of every speedup | None worth mentioning |
| Filter-then-refine: indexes store boxes, `ZoneStore` owns polygons | The index gives a conservative superset cheaply; exact geometry runs on the survivors; geometry lives in one place | The filter must be provably conservative (it was not quite — see pass 6) |
| One `SpatialIndex` interface | Tests, benchmark and engine written once; indexes are interchangeable, so equivalence is testable end to end | A virtual call per query, unmeasurable next to traversal |
| Three-valued containment | A fix with 30 m accuracy 10 m outside a boundary is not "outside"; Uncertain is reported, not guessed | Distance-to-boundary per candidate, O(V) |
| Hysteresis over the verdict | GPS drift near a boundary otherwise produces a flood of false transitions (RESULTS.md §10) | Latency of N fixes on genuine transitions |
| Persistence by path copying + append-only validity log | History of both geometry and rules at O(depth) nodes per change, instead of O(n) per version | `shared_ptr` refcounting; removal needs the zone's box |
| Determinism: fixed seeds, explicit tie-breaks, id-ordered candidates, `-ffp-contract=off` | Byte-identical replays make A/B comparisons and bug reproduction possible | A few FMAs not emitted |
| Caller-owned, reused buffers in the hot loop | No allocation per tourist per tick in steady state | Slightly less convenient APIs |
| One self-contained HTML output, no server | Reproducible, shareable, works offline; the engine is the subject | No live streaming |

## Limitations

Stated here so they are not discovered:

- **Removal is O(n)** in every spatial index (no id → node map). Measured in
  RESULTS.md §4; fine for a read-heavy zone set, not for heavy churn.
- **The quadtree and R-tree have O(n) worst-case queries.** Every zone with the
  same box defeats the quadtree; heavily overlapping boxes defeat the R-tree. The
  interval tree's guarantee is O(min(n, (k + 1) log n)) per stab, not
  O(log n + k).
- **A speed estimate poisoned by a GPS jump inflates the query radius** (up to the
  10 km clamp) for as long as the jump sits in the 64-fix window, and pruning
  collapses for that tourist meanwhile. Correct, and slow; a robust estimator
  (median of segment speeds) is the fix.
- **Tolerance-based geometry**, not exact predicates; see
  [GEOMETRY_EDGE_CASES.md](GEOMETRY_EDGE_CASES.md). Antimeridian-spanning zones are
  refused.
- **Single-threaded.** Tourists are independent within a tick, so the loop in
  `evaluate_all` is the natural place to parallelise; nothing is shared except
  the read-only index and zone store.
- **Simulated tourists.** Real OSM geography, synthetic movement with a GPS error
  model. No field data.
- **Cross-OS determinism has one known gap:** dispatch travel totals can differ
  across operating systems because libm's `asin`/`sin`/`cos` differ in the last
  ulp. The evaluation core agrees.

## What would change at production scale

In order of what would bite first:

1. **Id → node maps** in the indexes, making removal O(log n) and zone edits
   cheap; then incremental index updates instead of rebuilds.
2. **Robust motion estimation** for the query radius (see limitations).
3. **Parallel evaluation** across tourists, sharded by region so each worker's
   candidate zones stay cache-resident.
4. **A spatial partition of the zone set** (per district or per cell) so no
   single index holds everything, and so updates lock a shard, not the world.
5. **Exact predicates** (Shewchuk-style adaptive precision) for validation, where
   authored geometry can be arbitrarily close to degenerate.
6. **A durable event log** in place of the in-memory stream, with the Merkle
   log's root published per batch.
