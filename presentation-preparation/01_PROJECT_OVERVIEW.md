# 01 — Project overview

> What SafeTrail is, what is real and what is simulated, the repository map, and
> the vocabulary used in every other document. Read this first.

## 1. SafeTrail in one paragraph

SafeTrail is a **geofencing engine and tourist-safety operations platform written
in C++17 with hand-written data structures**. Given hazard-zone polygons (real
OpenStreetMap data around Shillong, Meghalaya) and a stream of noisy GPS fixes
from people who have opted in, it decides — fix after fix — whether each person is
**inside, outside or uncertain** relative to every zone near them, emits an event
only when that changes, turns serious events into alerts and incidents, and
recommends and routes the nearest responder over the real road network. A web
console (plain JavaScript, no framework) shows all of it, and a **Data Structures
Lab** runs the project's own C++ structures on the server and draws what they did.

The project started as a Data Structures course project derived from Smart India
Hackathon 2025 problem SIH25002 (Ministry of DoNER), as the README says.

## 2. The core problem (say this first in the viva)

"Is this person inside a hazard zone?" asked naively means testing the fix against
**every** zone: O(n) per fix. With 100,000 zones and one fix per second per person,
that scan *is* the whole cost of geofencing. A **spatial index** answers the range
query "which zone boxes meet this box?" by pruning whole regions that cannot
overlap — that is the data-structures heart of the project.

The second half of the problem is honesty about GPS: a fix is a **disc** (a centre
plus an accuracy radius), not a point. Near a boundary the true answer may be
unknowable, so the engine reports a third value, **uncertain**, and a hysteresis
state machine stops noisy fixes from producing a storm of enter/exit events.

## 3. What is real, what is simulated, what is only a model

| Category | Examples | How to say it |
|---|---|---|
| **Real data** | 38 hazard-zone polygons from OpenStreetMap (`data/zones/shillong_osm.geojson`); the road network, 144,090 junctions (`data/osm/roads.txt`) | "The geography is real OpenStreetMap data." |
| **Real algorithms on real inputs** | every index query, every point-in-polygon test, Dijkstra/A* on the OSM road graph, Merkle proofs, the Lab experiments | "Everything you see computed is computed by the C++ engine." |
| **Simulated people** | every tourist in a scenario (named `SIM · …`, badged **SIM**) | "No real person is tracked. The tourists are scripted by the scenario runner." |
| **Demo fixtures** | six responders named `Demo …`, badged **DEMO**, at fixed points | "Responders are demonstration fixtures." |
| **Models, not measurements** | battery drain (energy ∝ fixes taken), the device-side "phone" | "The battery number is a model." |
| **Measured on one laptop** | every benchmark number (Apple M4, committed in `bench/results/`) | "Ratios are measured; they move between sessions." |
| **Implemented but not wired into the website** | jurisdiction hierarchy (`src/jurisdiction/hierarchy.cpp`), QR digital-ID verification (`src/evidence/digital_id.cpp`), Kuhn's matching | "Implemented and unit-tested library code; the console does not use it." |

Never claim a real tourist was tracked, and never present a benchmark ratio as a
guarantee. Both rules are repeated where they matter.

## 4. The two public deliverables (do not confuse them)

| | What it is | URL |
|---|---|---|
| **Integrated application** | `safetrail_server` + the web console, in public-demo mode | https://safetrail-v0bh.onrender.com |
| **Static dashboard** | one HTML file replaying a headless run (no server, no accounts) | https://adivishall.github.io/safetrail/ and https://safetrail-adivishall.vercel.app |

The integrated application runs on Render's **free** plan (Singapore): it sleeps
after 15 idle minutes (a measured cold start took 13.5 s), and **every restart
starts a fresh world**. Accounts are held by the project owner; the passwords are
in the Render dashboard (service **safetrail → Environment**). Details:
[docs/final/DEPLOYMENT_GUIDE.md](../docs/final/DEPLOYMENT_GUIDE.md).

## 5. Repository map

```
apps/
  safetrail_server.cpp     HTTP server entry point: flags, world build, poll loop
  safetrail_headless.cpp   headless run that writes dashboard.html
  safetrail_bench.cpp      every benchmark (19 sections) -> bench/results/*.csv
include/safetrail/ + src/  the C++ library, one folder per concern:
  geo/        points, bounding boxes, polygons, containment, sweep line, projection
  index/      brute force, quadtree, R-tree, geohash, k-d tree, persistent quadtree
  ds/         interval tree, rollback DSU, binary heap, hash map, timer wheel, ring buffer
  fence/      zones, hysteresis, the Evaluator (the per-fix pipeline)
  track/      tourist state, anomaly detection, trajectory statistics
  alert/      alerts, triage score, escalation, incident correlation
  graph/      road graph (adjacency list), Dijkstra, A*, Hungarian + Kuhn matching
  dispatch/   responder pool, greedy vs optimal assignment
  group/      group cohesion (proximity graph + DSU)
  sync/       Lamport clocks, offline queue, reconciler
  evidence/   SHA-256, Merkle log, digital ID
  power/      adaptive GPS sampling model
  sim/        mobility models and the simulator (used by bench and headless)
  jurisdiction/  containment forest of regions (library only)
  net/        the HTTP/1.1 server (poll(2), one thread)
  app/        Platform (users, consent, alerts, incidents...), Api, ScenarioRunner, Lab
  util/, viz/ JSON parser, HTML export for the static dashboard
web/app/      the console: index.html, css/app.css, js/{app,api,map,runner,util}.js,
              js/pages/*.js (14 pages)
tests/        51 test files (unit, differential, golden, platform, API, HTTP)
bench/results/  committed benchmark CSVs + environment.txt
data/         OSM zones and roads
docs/         engine docs (ARCHITECTURE, DATA_STRUCTURES, RESULTS, TESTING, DEFECT_LOG...)
docs/final/   academic deliverables (report, technical reference, viva guide, deck)
tools/        api_e2e.py, mutation_check.py, check_docs.py, build_pdfs.py, make_deck.js...
Dockerfile, render.yaml   public deployment
Makefile, CMakeLists.txt  two build systems over the same sources
```

## 6. Technology stack and why

| Layer | Choice | Why |
|---|---|---|
| Engine | C++17, no third-party libraries | The course is about data structures, so none is borrowed: no `std::map`, `std::set`, `std::unordered_map`, `std::priority_queue`, and no spatial library (README). Predictable performance for benchmarking. |
| Server | Own HTTP/1.1 server on `poll(2)`, one thread (`src/net/http.cpp`) | No dependencies; a single writer means no locks on the platform state. |
| Frontend | Plain ES modules, `<canvas>` and SVG, no build step (`web/app/`) | Nothing to install; every text node built with `textContent` (no `innerHTML`), so names cannot inject markup. |
| Map | Own Web-Mercator canvas renderer (`web/app/js/map.js`) | No tile server or map library: the base layer *is* the OSM road graph that routing uses. |
| Data | GeoJSON zones, a text road file | Real OpenStreetMap extracts converted by `tools/osm_to_zones.py` and `tools/osm_to_roads.py`. |
| Builds | `make` and CMake over the same sources | CI checks that both produce byte-identical output. |
| Deployment | Docker, Render free web service | Free; HTTPS terminated by Render's proxy. |

## 7. Headline numbers (verified against `bench/results/`)

| Claim | Source row | Value |
|---|---|---|
| R-tree vs linear scan, 100,000 zones in one district | `index_scaling.csv`, zones=100000 | 223.67× (README rounds to 224×) |
| Quadtree, same workload | same row | 32.71× |
| Results per query in that workload (k) | same row, `candidates` | 98.45 |
| Constant density, 100,000 zones | `index_density.csv` | quadtree 555.74×, R-tree 1026.86×, k ≈ 0.99 |
| Point-in-zone, 5,000 polygons × 128 vertices vs testing every polygon | `end_to_end.csv`, vertices=128 | quadtree 1498.4×; R-tree 254.6365 / 0.1229 ≈ 2072× |
| STR vs insertion-built R-tree, query time | `index_build.csv`, 100000 | 6.264× |
| Multi-user ingestion | `multi_user.csv` | ≈ 2.7–3.7 µs per fix up to 5,000 users; ≈ 6.9 KB state per user |
| Tests | `make test` | 51 files, 12,166 checks; `make e2e` 38 checks; mutation 28/28 |

Machine for the benchmark CSVs (`bench/results/environment.txt`): Apple M4, Apple
clang 17, `-O2`, commit `9d33bb6`, on battery power. An earlier session on the
same laptop measured the R-tree ratio at 102–114×, which is why the README says
"read ~100× as the floor".

## 8. Glossary

| Term | Meaning in this project |
|---|---|
| **Fix** | One GPS reading: latitude, longitude, accuracy radius (metres), timestamp. `geo::UncertainPoint`. |
| **Accuracy (± m)** | Radius of the disc the true position is believed to lie in. A fix with accuracy ≥ 150 m is "unusable" and rejected (`UncertainPoint::usable()`). |
| **Bounding box (bbox)** | The smallest latitude/longitude rectangle around a polygon. `geo::Bbox`. Indexes store boxes, not polygons. |
| **Range query** | "Which stored boxes intersect this query box?" — what every spatial index answers. |
| **Candidate** | A zone the index returned; it *might* contain the fix and still needs the exact geometry test. |
| **k** | The number of results a query returns. Bounds like O(log n + k) mean the cost depends on n *and* on how many answers there are. |
| **Oracle** | A slow, obviously correct implementation used to check a fast one (brute force for indexes, a linear scan for the interval tree, etc.). |
| **Differential test** | Run the fast structure and the oracle on the same random, hostile inputs and require identical answers. |
| **Invariant** | A property a structure must always satisfy (e.g. AVL balance, quadtree tiling); `check_invariants()` audits it. |
| **Mutation testing** | Inject a realistic bug; the test suite must fail. `tools/mutation_check.py`, 28 mutants. |
| **Three-valued containment** | `Inside`, `Outside`, `Uncertain` (`geo::Containment`): inside only if the whole accuracy disc is inside. |
| **Signed distance** | Distance from the fix to the polygon boundary, negative inside. |
| **Hysteresis** | A state machine that requires several agreeing fixes (and margins) before confirming entry or exit; suppresses flapping. |
| **Transition / event** | A *change* of confirmed state: `ZoneEnter`, `ZoneExit`, `ZoneUncertain`, `ZoneApproaching`, `DwellExceeded`. |
| **Validity window** | `[from, to)` time interval during which a zone is in force. |
| **Persistent structure** | Every past version stays queryable after updates (path copying). |
| **Rollback** | Undo the last operations back to a saved mark (different from persistence: only one version is live at a time). |
| **Alert** | A record raised from an event or anomaly, with a severity 1–5 and a state (open, acknowledged, dispatched, resolved, dismissed). |
| **Incident** | A cluster of related alerts (within 200 m and 10 minutes) shown as one card, with a recommended responder. |
| **Triage** | Ordering open alerts by urgency using a binary max-heap. |
| **Escalation** | Raising an unacknowledged alert's severity after 2 minutes, scheduled on a timing wheel. |
| **Lamport clock** | A logical counter ordering events consistently with causality, independent of wall clocks. |
| **Merkle log** | Append-only log whose entries are hashed into a tree; an inclusion proof shows an entry is present and unedited. |
| **SIM / DEMO** | Badges for simulated people and demonstration fixtures. |
| **Platform clock** | `T+hh:mm:ss`: milliseconds since the platform's own start (in demo mode, driven by the scenario runner). |
