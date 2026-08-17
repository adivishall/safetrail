# safetrail

A geofencing and incident-response engine for tourist safety in low-connectivity
terrain.

Data Structures course project. Derived from Smart India Hackathon 2025 problem
statement `SIH25002` (Ministry of Development of North Eastern Region), but
deliberately not an implementation of it as written — see
[docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md).

---

## The one-sentence difference

Existing implementations of this problem statement are React Native apps that
call `ST_Contains` on a PostGIS server and an Ethereum contract for identity.
**We build the engine they import**, and in doing so fix eleven things that
delegating to a server-side spatial database makes impossible.

## What it does

Tracks a population of tourists across a region. Evaluates their position against
time-varying hazard zones with explicit GPS uncertainty. Predicts boundary
crossings before they happen. Detects group fragmentation and stragglers.
Correlates floods of related alerts into single incidents. Dispatches the nearest
responder along a real road route. Runs fully offline on a device and reconciles
event logs on reconnect. Keeps a tamper-evident record of everything.

## Quickstart

```bash
make            # build everything
make test       # run the test suite
make bench      # spatial index benchmark → bench/results/
./build/safetrail_server --scenario data/scenarios/quiet_day.json
# then open http://localhost:8080
```

## Layout

```
include/safetrail/      headers — the design lives here
  geo/                  ★ computational geometry
  index/                ★ spatial data structures
  ds/                   ★ general data structures
  graph/                road network + routing + matching
  track/                trajectory, anomaly detection
  group/                ★ group cohesion, straggler detection      [NEW]
  fence/                zone management, evaluation, hysteresis
  alert/                triage, escalation, correlation             [NEW]
  dispatch/             responder assignment
  sync/                 ★ offline operation, logical clocks         [NEW]
  power/                ★ risk-adaptive sampling                    [NEW]
  evidence/             ★ Merkle log, inclusion proofs              [NEW]
  sim/                  simulator, mobility models, replay
  server/               HTTP + WebSocket
src/                    implementations, mirrors include/
apps/                   server · headless · bench binaries
web/                    zero-build viewer, vendored Leaflet
data/                   zones, road graph, scenarios
tests/                  per-structure unit tests + golden replays
bench/                  CSV results and plots
docs/                   ★ start with GAP_ANALYSIS.md
tools/                  scenario generation, OSM preprocessing
```

`[NEW]` marks modules that exist because of the gap analysis and have no
counterpart in any existing implementation.

## Documents

| Doc | What's in it |
|---|---|
| [GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) | ★ Research on existing systems, the eleven gaps, and what we deliberately skip |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layers, the hot loop, data flow, module responsibilities |
| [DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) | Every structure: invariants, complexity targets, why it's here |
| [ROADMAP.md](docs/ROADMAP.md) | Phased build order with milestones |
| [GEOMETRY_EDGE_CASES.md](docs/GEOMETRY_EDGE_CASES.md) | What breaks in point-in-polygon and how we handle it |

## Ground rules

**Every data structure is hand-written.** No `std::unordered_map`, no `std::set`,
no `std::priority_queue`, no Boost.Geometry, no PostGIS. `std::vector` and
`std::string` are permitted as raw storage. If a container does something
interesting, we wrote it.

**The naive baseline is a deliverable.** `BruteForceIndex` stays in the codebase
forever behind the same interface as the fast implementations. It is the
correctness oracle every index is validated against, and the denominator in every
speedup number we report.

**Determinism is non-negotiable.** Fixed seeds, stable tie-breaking, byte-identical
output across runs of the same scenario. Without it, the replay harness is
worthless and timing-dependent bugs are unfindable.

**Ground truth before optimisation.** Scenarios have known expected outcomes.
"It runs" is not a result; "3,000× fewer operations with identical output" is.

## Stack

C++17, no external dependencies in the core. Python 3 for tooling only. Viewer is
plain HTML and vanilla JS with no build step. Leaflet is vendored, not CDN —
demo machines lose network.
