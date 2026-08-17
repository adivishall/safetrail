# Roadmap

Ordered so that something runs end-to-end as early as possible. Replacing a slow
stage with a fast one is easy; integrating twelve finished modules in the last
week is not.

`[GAP n]` marks work that exists because of [GAP_ANALYSIS.md](GAP_ANALYSIS.md) and
has no counterpart in existing implementations.

---

## Phase 1 — Geometry (week 1–2)

Nothing else works if containment is wrong, and containment is wrong in more ways
than anyone expects. **Write the tests before the implementation here.**

- [ ] `geo/point.hpp`, `bbox.hpp`, `polygon.hpp`, `projection.hpp`, `haversine.hpp`
- [ ] `geo/containment.hpp` — ray casting, all edge cases
- [ ] `contains_winding()` — the independent second implementation
- [ ] `tests/geo/ray_casting_test.cpp` — every case in GEOMETRY_EDGE_CASES.md
- [ ] `signed_distance_m()` — needed by three later phases, build it now

**Exit:** ray casting and winding number agree on 10⁶ randomised points against
polygons with holes and concavities.

## Phase 2 — Brute force, end to end (week 2–3)

- [ ] `index/spatial_index.hpp` interface + `brute_force.hpp`
- [ ] `fence/zone.hpp`, `ZoneStore`, GeoJSON load
- [ ] `track/tourist.hpp`, `ds/circular_buffer.hpp`
- [ ] `fence/evaluator.hpp` — the hot loop, brute force behind it
- [ ] `apps/safetrail_headless.cpp` — one tourist, straight line, events to stdout

**Exit:** enter/exit events printed, correct, slow. This is now the correctness
oracle for everything that follows.

## Phase 3 — Make it visible (week 3–4)

Deliberately early. The visual feedback loop makes every later bug an order of
magnitude easier to find, and it is the highest-morale week of the project.

- [ ] `web/` — Leaflet (vendored), zone rendering, moving dots
- [ ] `server/ws_stream.hpp` — state deltas at ~10 Hz
- [ ] `sim/simulator.hpp`, `mobility.hpp`, `scenario.hpp`
- [ ] `data/zones/*.geojson`, `data/scenarios/quiet_day.json`

**Exit:** dots move on a map, zones light up on entry.

## Phase 4 — Spatial indexes (week 4–7) ★ core course content

- [ ] `index/quadtree.hpp`
- [ ] `index/rtree.hpp` — quadratic split, then STR packing
- [ ] `index/geohash.hpp` — Morton encoding
- [ ] `index/kd_tree.hpp`
- [ ] `tests/index/equivalence_test.cpp` — **every index must match brute force**
- [ ] `apps/safetrail_bench.cpp` + `bench/results/index_scaling.csv`
- [ ] `web/js/diagnostics.js` — draw the live quadtree over the map

**Exit:** the scaling plot. Brute force goes vertical, the others stay flat.

## Phase 5 — Uncertainty and prediction (week 6–8) `[GAP 1, 2]`

Can overlap phase 4 — different people, different files.

- [ ] `UncertainPoint`, three-valued `Containment`
- [ ] Circle–polygon intersection in `evaluate()`
- [ ] `tests/geo/containment_uncertainty_test.cpp`
- [ ] `geo/predict.hpp` — segment–polygon, time-to-boundary
- [ ] `ZoneApproaching` alerts with ETA
- [ ] UI: third state rendered distinctly; predicted paths drawn

**Exit:** an operator can see "possibly inside" and "will enter in 4 min".

## Phase 6 — Event system and hysteresis (week 7–9) `[GAP 8]`

Bugs here are timing-dependent, so the deterministic replay harness earns its
cost before you need it.

- [ ] `alert/alert.hpp`, `triage.hpp` (heap), `escalation.hpp` (interval tree)
- [ ] `fence/hysteresis.hpp` — Schmitt trigger, confirmation, dwell minimum
- [ ] `sim/recorder.hpp` — deterministic replay, fixed seeds
- [ ] `track/anomaly.hpp` — stationary, deviation, signal loss
- [ ] `tests/golden/hysteresis_ab_test.cpp` — **the A/B measurement**

**Exit:** false alert rate with hysteresis on vs off, under injected GPS noise.
A number, not a feature.

## Phase 7 — Groups and correlation (week 9–10) `[GAP 4, 5]`

- [ ] `ds/dynamic_connectivity.hpp` — rollback DSU + brute-force oracle
- [ ] `group/cohesion.hpp` — fragmentation, stragglers
- [ ] `alert/correlator.hpp` — spatio-temporal clustering into incidents
- [ ] `data/scenarios/landslide_event.json` — 40 tourists, one event
- [ ] UI: group hulls, stragglers outside them, one incident card

**Exit:** the landslide scenario produces one incident card, not forty.

## Phase 8 — Dispatch (week 10–11)

- [ ] `graph/road_graph.hpp` from OSM, `dijkstra.hpp`, `astar.hpp`
- [ ] `graph/bipartite_match.hpp` — Kuhn's, then Hungarian
- [ ] `dispatch/assigner.hpp` — greedy vs optimal, both kept
- [ ] `jurisdiction/hierarchy.hpp` `[GAP 11]`

**Exit:** greedy vs optimal total response time, side by side.

## Phase 9 — Time travel `[GAP 3]`  ✅ DONE

- [x] `index/versioned_index.hpp` — path-copying quadtree, immutable shared nodes
- [x] `ds/interval_tree.hpp` — AVL-balanced, subtree-max-high augmentation
- [x] Validity intervals; `active_at(t)` uses the interval tree
- [x] `query_at(timestamp, box)` — spatial prune first, then per-candidate validity
- [x] Structural sharing metric + benchmark section 5
- [x] Cross-checked against brute force at 120 historical versions, 0 mismatches
- [ ] UI: incident investigation view — "the rules at 14:32"  *(still open)*

**Measured:** 13.0x sharing at 5,001 versions; querying the past costs the same as
the present; a validity-only change allocates zero nodes.

## Phase 10 — Offline and power (week 12–13) `[GAP 6, 7]`

- [ ] Index serialise/deserialise
- [ ] `sync/lamport.hpp`, `OfflineQueue`, `Reconciler`
- [ ] `power/adaptive_sampler.hpp`
- [ ] `tests/sync/lamport_test.cpp` — idempotent merge, skewed clocks
- [ ] Scenario: device offline 2 h, reconnects, timeline stays correct

**Exit:** battery projection vs fixed polling at matched recall.

## Phase 11 — Evidence and authoring (week 13) `[GAP 9, 10]`

- [ ] `evidence/merkle_log.hpp` + `sha256.cpp` — inclusion and consistency proofs
- [ ] QR digital ID, offline verification
- [ ] `geo/sweep_line.hpp` — Bentley–Ottmann self-intersection
- [ ] Zone editor validation, overlap warnings

## Phase 12 — Measure everything (week 14+)

All ten measurements in [DATA_STRUCTURES.md](DATA_STRUCTURES.md#measurements-to-produce).
Budget real time — this is what the report is made of.

---

## If the timeline compresses

Cut in this order, from the bottom:

1. Phase 11 (evidence, authoring) — self-contained, drops cleanly
2. Phase 9 (time travel) — the most advanced, but also the most droppable
3. Phase 8 (dispatch) — a static responder list still demos
4. Phase 10 (offline) — keep the serialisation, drop the reconciler

**Never cut:** phases 1, 2, 4, 6. Geometry, the brute-force oracle, the indexes,
and hysteresis are the project. Everything else is the case for it.
