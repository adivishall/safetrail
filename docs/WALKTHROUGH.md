# Walkthrough — How safetrail Works, Start to Finish

A complete trace of one `make demo` run, from the compiler to the animated
dashboard. Every file and function named here is real; the numbers are from an
actual run on the real OpenStreetMap dataset (60 tourists, 438 zones, 2 simulated
hours).

If you read one document to understand the project, read this one.

---

## The 30-second version

```
Real map  +  simulated-but-realistic people
        │
        ▼   each tick (once per simulated second):
   a noisy GPS fix is checked against nearby, currently-in-force zones
   using hand-written spatial data structures
        │
        ▼
   state CHANGES (not states) become Events
        │
        ▼
   Events become spatially-correlated Incidents
        │
        ▼
   the whole run serialises into ONE static HTML dashboard
```

The rest of this document expands each arrow.

---

## The shape of a run

```
┌─ BUILD ─────────────────────────────────────────────────────────────────────┐
│  make demo → compile 20 modules → build/safetrail_headless → run it           │
└───────────────────────────────────────────────────────────────────────────────┘
                                     │
         ┌───────────────────────────┼───────────────────────────┐
         ▼                           ▼                           ▼
   PHASE 1: SETUP              PHASE 2: THE LOOP           PHASE 3: OUTPUT
   (happens once)             (repeats 7,200×)             (happens once)
   load zones                 move → sense → evaluate      capture frames
   build indexes              → correlate → cohesion       serialise to JSON
   spawn tourists                                          write dashboard.html
```

---

## Phase 0 — Build

```bash
make demo
```

`clang++` (or `g++`) compiles ~20 source modules — geometry, spatial indexes, the
evaluator, the simulator, the dashboard exporter — into a single binary
`build/safetrail_headless`. No libraries, no cmake, no package manager. Then the
binary runs.

The driver is `apps/safetrail_headless.cpp` and its `main()` does exactly three
setup calls, then the loop, then the output — mirrored below.

---

## Phase 1 — Setup (once)

### ① Load the zones — `Simulator::load_zones()`

```
data/zones/shillong_osm.geojson   (real OpenStreetMap geography)
        │
        ▼  util::Json::parse_file()          hand-written JSON parser, no library
        ▼  ZoneStore::load_geojson()
        │     ├─ strip the duplicate closing vertex GeoJSON adds
        │     ├─ Polygon::validate()  ── REJECTS self-intersecting shapes [GAP 10]
        │     └─ read validity window (active_from_s / active_to_s)       [GAP 3]
        ▼
   ZoneStore: 38 real zones, each a validated polygon
        │
        ▼  fit the tourist roam area to the loaded zones
           (so people spawn AMONG the geography, not in a fixed box)
```

Why validation matters: ray casting on a self-intersecting polygon returns
arbitrary answers. Rejecting bad geometry here means nothing downstream ever has
to defend against it. This is GAP 10, and it is a load-time gate, not a runtime
check.

### ② Build the indexes — `Simulator::reindex()`

Two structures are built from the same zone set:

```
      every zone's bounding box
        ├──────────────────────────────┐
        ▼                              ▼
   SpatialIndex                   VersionedIndex                    [GAP 3]
   (quadtree / R-tree)            (persistent quadtree)
   "which zones are near X?"      "what were the rules at time T?"
   O(log n + k) per query         one version per zone, time-stamped
```

The spatial index is the reason the whole thing is fast (Phase 2, step ④.2). The
versioned index is the time-travel history the investigation panel queries.

### ③ Spawn the tourists — `spawn_tourists()`

```
60 tourists  →  6 groups of 10
   each tourist gets: a start position, a walking speed (~1–2 m/s),
                      and a mobility model (today: random-waypoint)
   each group is declared to the CohesionMonitor, so a split can be detected
```

**This is the only place "human data" enters, and it is entirely synthetic.**
The tourists are 60 dots driven by a seeded random number generator. See
[DATA_PROVENANCE.md](DATA_PROVENANCE.md) for the full honesty statement and
[the movement model below](#how-a-tourist-actually-moves).

---

## Phase 2 — The loop (`Simulator::step()`, ×7,200)

One tick = one simulated second. Two simulated hours = 7,200 ticks. Each tick runs
six things in this exact order:

```
  FOR EACH TOURIST:
    ① step_mobility()     advance the TRUE position one step
    ② apply_gps_error()   produce a NOISY fix — all the engine is allowed to see
    ③ pings.push()        append to the tourist's circular buffer (recent history)

  THEN, over all tourists:
    ④ evaluate_all()      turn positions into Events        ← the heart
    ⑤ correlator.ingest() cluster related alerts → Incidents          [GAP 5]
    ⑥ cohesion.update()   detect group splits / stragglers            [GAP 4]
```

### How a tourist actually moves (steps ① and ②)

This two-layer split is the most important idea in the simulation:

```
   step_mobility()  ─────────►  TRUTH      where the tourist REALLY is
                                             (we control this; the engine never sees it)
        │
        ▼
   apply_gps_error() ─────────► REPORTED   truth + realistic noise:
                                             4 m normally, 35 m multipath (25%),
                                             or a dropout (2%)  — a phone-shaped fix
        │
        ▼
   the Evaluator only ever receives REPORTED
```

Because *we* hold the truth and the engine doesn't, every accuracy claim is
checkable — that is what "ground truth" means, and it is why the project can
report *"93% of false alerts removed under realistic drift"* rather than just *"it seems to work."*
`apply_gps_error()` deliberately emits an `UncertainPoint` (lat, lon, accuracy,
time) — the exact shape a real phone GPS reading has, which is what makes the
real-deployment swap a one-line change.

### Inside ④ — `Evaluator::evaluate()`, the hot loop

Run once per tourist per tick. This is where the hand-written data structures do
their work. Eight steps:

```
  ┌─────────────────────────────────────────────────────────────────────────┐
  │ 1. usability gate    fix worse than 150 m? discard — it's only noise      │
  │ 2. SPATIAL PRUNE     ask the index: which zones are near me?              │
  │                      438 zones → ~2 candidates   ◄── the performance story│
  │ 3. temporal filter   of those, which are IN FORCE right now?      [GAP 3] │
  │ 4. exact geometry    ray casting + signed distance:                       │
  │                      Inside / Outside / UNCERTAIN                 [GAP 1] │
  │ 5. hysteresis        real crossing, or GPS jitter at the edge?    [GAP 8] │
  │ 6. transition diff   did my state CHANGE since last tick? → emit an Event │
  │ 7. prediction        heading toward a zone? → "ETA 4 min"         [GAP 2] │
  └─────────────────────────────────────────────────────────────────────────┘
```

**Step 2 is the entire point of the project.** Without the spatial index:

```
   naive:   200 tourists × 100,000 zones × 40 vertices = 800,000,000 ops / tick
   indexed: 200 × (log n + ~2 candidates × 40 vertices) ≈ 27,000 ops / tick
```

On the real 438-zone run (the `make dashboard` configuration), the index prunes
each query from 438 zones to **3.75 candidates on average — a 117× reduction** —
before any expensive geometry runs.

**Step 6 is the subtle one.** The evaluator emits *transitions* ("entered",
"exited"), never "still inside" repeated every tick. That single decision is the
difference between a usable alert feed and an unreadable scrolling wall — and it
is where the trickiest bugs live (a tourist flickering across a boundary in noisy
GPS), which is exactly what step 5's hysteresis exists to absorb.

### ⑤ and ⑥ — from events to incidents and group awareness

```
  Events ──► Alerts ──► Correlator.ingest()                            [GAP 5]
                        │  union-find over (space, time) proximity
                        ▼
                        40 tourists in one landslide → ONE Incident
                        (not 40 alert cards; on this run, 331 cards suppressed)

  All tourist positions ──► CohesionMonitor.update()                   [GAP 4]
                        │  rollback union-find: is each declared group
                        │  still one connected cluster?
                        ▼
                        someone 400 m behind and falling back → Straggler alert
```

---

## Phase 3 — Output (once, after the loop)

```
  every 10 simulated seconds:
    TraceRecorder::capture()   snapshot all positions + states → one frame
                               (2 h ÷ 10 s = 720 frames)
        │
        ▼  after the run:
    TraceRecorder::write_html()
        │   serialise zones + 720 frames + events + version history → JSON
        │   substitute that JSON into an HTML shell at the __DATA__ marker
        ▼
    dashboard.html            ONE self-contained file
                              99.3% data, 11.7 KB viewer, ZERO network calls
```

Open `dashboard.html` and the 720 frames animate: dots moving over the real
Shillong reservoirs and forests, zones lighting up on entry, the investigation
panel showing which rules were in force at each moment as you scrub the timeline.
It works over `file://` with the network physically off — see
[DATA_PROVENANCE.md](DATA_PROVENANCE.md).

---

## The whole pipeline on one page

```
  data/zones/shillong_osm.geojson        (REAL OpenStreetMap geography)
        │
        ▼  ZoneStore::load_geojson()      validate + reject bad polygons   [GAP 10]
        ▼  Simulator::reindex()           SpatialIndex + VersionedIndex    [GAP 3]
        ▼  spawn_tourists()               60 synthetic tourists, 6 groups
        │
        ▼  ── LOOP ×7,200 ──────────────────────────────────────────────
        │    step_mobility()      TRUTH   (simulated movement)
        │    apply_gps_error()    REPORTED = truth + noise  → UncertainPoint
        │    evaluate()           prune → in-force → geometry → hysteresis
        │                         → transitions → Events    [GAP 1,2,3,8]
        │    correlator.ingest()  Events → Incidents        [GAP 5]
        │    cohesion.update()    group splits / stragglers [GAP 4]
        │  ────────────────────────────────────────────────────────────
        │
        ▼  TraceRecorder          720 frames → JSON → HTML
        ▼
  dashboard.html                  static, self-contained, zero-network
```

---

## Where real humans would plug in

The simulator is a **stand-in for a fleet of phones**, nothing more. In a real
deployment exactly one seam changes:

```
   TODAY (simulation)                    REAL DEPLOYMENT
   ─────────────────                     ───────────────
   step_mobility()   → truth             a phone app / GPS unit reports a fix
   apply_gps_error() → UncertainPoint    that fix IS already an UncertainPoint
                                          (lat, lon, accuracy, timestamp)
        │                                      │
        └──────────────┬───────────────────────┘
                       ▼
              evaluate_all()  ── UNCHANGED
              indexes, hysteresis, correlation, cohesion  ── ALL UNCHANGED
```

Everything below the seam already consumes `UncertainPoint`, which is exactly the
shape of a real GPS reading — by design, not by luck. The phone app is UI work
outside a data-structures course, but the engine is already the right shape to
receive real data. See the roadmap for the `PositionSource` interface that would
formalise this seam.

---

## Map of the code, by phase

| Phase | Step | File · function |
|---|---|---|
| Setup | load + validate zones | `src/fence/zone.cpp` · `ZoneStore::load_geojson` |
| Setup | build indexes | `src/sim/simulator.cpp` · `Simulator::reindex` |
| Setup | spawn tourists | `src/sim/simulator.cpp` · `Simulator::spawn_tourists` |
| Loop | move + sense | `src/sim/mobility.cpp` · `step_mobility`, `apply_gps_error` |
| Loop | **the hot loop** | `src/fence/evaluator.cpp` · `Evaluator::evaluate` |
| Loop | correlate alerts | `src/alert/correlator.cpp` · `Correlator::ingest` |
| Loop | group cohesion | `src/group/cohesion.cpp` · `CohesionMonitor::update` |
| Output | capture + serialise | `src/viz/html_export.cpp` · `TraceRecorder::write_html` |

For *why* each structure was chosen and its complexity, see
[DATA_STRUCTURES.md](DATA_STRUCTURES.md). For the eleven gaps referenced by tag,
see [GAP_ANALYSIS.md](GAP_ANALYSIS.md).
