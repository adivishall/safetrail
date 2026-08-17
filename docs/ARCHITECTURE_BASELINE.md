# safetrail — Geo-Fenced Tourist Safety & Incident Response

> `SIH25002` · Ministry of Development of North Eastern Region
> Smart Tourist Safety Monitoring & Incident Response System

Tracks tourists across a region, fires alerts the moment one enters a restricted
or hazardous zone, triages those alerts by severity, and dispatches the nearest
responder along a real road route.

**What we deliberately do not build:** an ML risk model, an actual blockchain, or
a mobile app. The project is a computational geometry engine, a spatial index,
and a real-time event system. The digital ID is a hand-written Merkle tree,
which is the data structure content the "blockchain" framing is pointing at.

---

## 1. The user's journey

There are two users. Both matter for the demo.

### User A — the control room operator

**Step 1 — Open the dashboard.** A map of the region fills the screen. A left
rail holds zones, a right rail holds alerts, and a bottom strip holds simulator
controls.

**Step 2 — Draw the zones.** They click "New Zone", then click points on the map
to trace a polygon around, say, a landslide-prone stretch of highway. On closing
the shape they pick a type — `Restricted`, `Caution`, or `Safe` — and a severity
from 1 to 5. The zone fills with colour. They can drag vertices to adjust, cut
holes for exempt areas, and delete zones. Everything saves to a GeoJSON file.

**Step 3 — Start the simulation.** The bottom strip has a tourist count, a
mobility model dropdown (`random waypoint`, `follows roads`, `guided group`), and
play / pause / speed controls. They set 200 tourists at 4× speed and hit play.

**Step 4 — Watch it run.** Dots move across the map. When a dot crosses into a
red polygon, three things happen at once: the dot turns red, the zone pulses,
and an alert card slides into the right rail:

```
⚠  RESTRICTED ZONE ENTRY              severity 5
    Tourist  TID-00147
    Zone     Border Buffer — Sector 4
    At       09:42:18   ·   2.3 km inside
    [ Dispatch ]  [ Acknowledge ]  [ Dismiss ]
```

**Step 5 — Alerts sort themselves.** The rail is ordered by a priority score
combining severity, time inside the zone, and distance from help — not by
arrival time. A severity-5 alert that has been open ninety seconds outranks a
fresh severity-2. Unacknowledged alerts escalate on a deadline and change colour.

**Step 6 — Dispatch.** They click `Dispatch`. The map draws a route from the
nearest available responder to the tourist, with distance and ETA. If several
alerts are open at once, an "Auto-assign all" button solves the whole
assignment at once rather than greedily one at a time — and the two produce
visibly different total response times, which is the point.

**Step 7 — Anomaly alerts appear unprompted.** Separate from zone entries: a
tourist who has not moved for twenty minutes, or who has drifted far off their
declared route, or whose pings stopped, generates its own alert type. This is
what makes the system feel alive rather than a polygon test.

**Step 8 — Replay and report.** A timeline scrubber replays the session. A
reports tab shows incidents per zone, mean response time, a dwell-time heatmap,
and which zones generate the most traffic.

### User B — the tourist

A phone-sized page showing their own position, the zones near them, and a
warning banner when they are approaching or inside a risky area. Their digital
ID is a QR code; scanning it verifies the record against a Merkle root and
displays a green tick with the inclusion proof path. Simple, but it closes the
loop for the demo.

### What neither user sees

Ray casting, quadtree subdivision, geohash prefixes, interval trees, the
Hungarian algorithm. One deliberate exception: a "Diagnostics" overlay that
draws the live quadtree grid over the map and shows candidate-count-per-query.
Toggle it on when the grader asks how it scales.

---

## 2. The builder's architecture

### Layer cake

```
┌────────────────────────────────────────────────────────────┐
│  web/       Leaflet map · zone editor · alert rail · replay │
├────────────────────────────────────────────────────────────┤
│  server/    HTTP API (CRUD) + WebSocket (live state deltas) │
├────────────────────────────────────────────────────────────┤
│  sim/       tick loop, mobility models, scenario loader     │
├────────────────────────────────────────────────────────────┤
│  DOMAIN                                                     │
│  fence/ evaluator   alert/ triage   dispatch/ assigner      │
│  track/ trajectory + anomaly detection                      │
├────────────────────────────────────────────────────────────┤
│  geo/  ★ geometry     index/  ★ spatial     graph/ routing  │
│  ds/   ★ circular buffer · heap · interval tree · Merkle    │
└────────────────────────────────────────────────────────────┘
```

### The hot loop — this is the whole project

Everything else is scaffolding around this function, which runs every 100 ms:

```cpp
void Simulator::tick(Millis now) {

  for (Tourist& t : tourists) {

    // 1. move
    t.pos = mobility.step(t, dt);
    t.pings.push(Ping{t.pos, now});          // CircularBuffer, fixed window

    // 2. narrow the candidate set  ← the reason this scales
    auto bbox = Bbox::around(t.pos, MAX_ZONE_RADIUS);
    auto candidates = zoneIndex.query(bbox);  // Quadtree or R-tree
                                              // 10,000 zones → ~3 candidates

    // 3. exact test only on candidates
    ZoneSet nowInside;
    for (ZoneId z : candidates)
      if (RayCasting::contains(zones[z], t.pos))
        nowInside.insert(z);

    // 4. transitions, not states — this is what generates events
    for (ZoneId z : nowInside - t.lastInside) emit(ZoneEnter{t.id, z, now});
    for (ZoneId z : t.lastInside - nowInside) emit(ZoneExit {t.id, z, now});
    t.lastInside = nowInside;

    // 5. behavioural anomalies, independent of zones
    if (auto a = anomaly.check(t.pings, t.plannedRoute)) emit(*a);
  }

  // 6. triage
  for (Event& e : pending) {
    Alert a = Alert::from(e);
    a.priority = score(a.severity, a.ageMs, distanceToNearestResponder(a));
    alertQueue.push(a);                       // MinHeap on -priority
    escalation.schedule(a.id, now + deadlineFor(a.severity));  // IntervalTree
  }

  // 7. dispatch
  if (autoAssign) {
    auto pairs = BipartiteMatch::solve(openAlerts, freeResponders, costMatrix);
    for (auto& [alert, responder] : pairs)
      routes[alert] = AStar::path(roadGraph, responder.pos, alert.pos);
  }

  // 8. push only what changed
  ws.broadcast(stateDelta(now));
}
```

**Step 2 is the entire performance story.** Without the spatial index it is
O(T × Z × V) — 200 tourists × 10,000 zones × 40 vertices = 80 million point
operations per tick, at 10 ticks per second. Impossible. With the index it is
O(T · log Z + hits), which is a few thousand operations. That contrast is your
benchmark chapter and your defence when asked "why a quadtree?"

### Transitions, not states

Note that step 4 diffs against `lastInside` rather than reporting current
containment. This is not a detail — it is what turns a geometry query into an
event stream, and it is where the bugs live. A tourist walking along a zone
boundary with noisy GPS will flap between inside and outside dozens of times
per minute unless you add hysteresis (a small buffer distance, or requiring N
consecutive confirmations). Handle this in `evaluator.hpp`, test it explicitly,
and mention it in the report — it is the kind of real-world detail that
distinguishes a working system from a toy.

---

## 3. File and folder layout

```
safetrail/
├── README.md
├── CMakeLists.txt
├── Makefile
├── .gitignore                     build/, data/osm/*.pbf
│
├── include/safetrail/
│   │
│   ├── geo/                       ★ COMPUTATIONAL GEOMETRY
│   │   ├── point.hpp              LatLon and projected Meters, kept as
│   │   │                          distinct types so you cannot mix them
│   │   ├── bbox.hpp               axis-aligned box, intersects, contains
│   │   ├── polygon.hpp            outer ring + holes, cached bbox,
│   │   │                          signed area, centroid
│   │   ├── ray_casting.hpp        point-in-polygon, the primary test
│   │   ├── winding_number.hpp     second implementation, benchmarked and
│   │   │                          cross-validated against ray casting
│   │   ├── projection.hpp         WGS84 → Web Mercator and local ENU
│   │   ├── haversine.hpp          great-circle distance, A* heuristic
│   │   └── douglas_peucker.hpp    polyline simplification, recursive
│   │
│   ├── index/                     ★ SPATIAL DATA STRUCTURES
│   │   ├── spatial_index.hpp      pure virtual interface — the key file,
│   │   │                          it makes the benchmark possible
│   │   ├── brute_force.hpp        the O(Z) baseline you must beat
│   │   ├── quadtree.hpp           point-region quadtree, configurable
│   │   │                          capacity and max depth
│   │   ├── rtree.hpp              R-tree with quadratic node split
│   │   ├── geohash.hpp            Morton/Z-order bit interleaving,
│   │   │                          prefix-match proximity
│   │   └── kd_tree.hpp            nearest-responder queries
│   │
│   ├── ds/
│   │   ├── circular_buffer.hpp    fixed-window GPS ping history
│   │   ├── priority_queue.hpp     binary heap, alert triage
│   │   ├── interval_tree.hpp      escalation deadlines, O(log n) queries
│   │   ├── timer_wheel.hpp        alternative scheduler, benchmarked
│   │   ├── merkle_tree.hpp        digital ID, build + inclusion proof
│   │   │                          + verify — no external crypto library
│   │   └── hash_table.hpp         entity lookup by id
│   │
│   ├── graph/
│   │   ├── road_graph.hpp         OSM extract → adjacency list
│   │   ├── dijkstra.hpp
│   │   ├── astar.hpp              haversine heuristic, benchmarked
│   │   │                          against Dijkstra for node expansions
│   │   └── bipartite_match.hpp    Kuhn's, then Hungarian for weighted
│   │
│   ├── track/
│   │   ├── tourist.hpp            entity, state, declared route
│   │   ├── trajectory.hpp         ping buffer + simplification
│   │   └── anomaly.hpp            stationary / deviation / silence
│   │
│   ├── fence/
│   │   ├── zone.hpp               polygon + type + severity + metadata
│   │   ├── zone_manager.hpp       CRUD, GeoJSON load/save, reindex
│   │   └── evaluator.hpp          ★ the hot loop, hysteresis handling
│   │
│   ├── alert/
│   │   ├── alert.hpp              types, severity, lifecycle states
│   │   ├── triage.hpp             priority scoring + heap
│   │   └── escalation.hpp         deadline tracking via interval tree
│   │
│   ├── dispatch/
│   │   ├── responder.hpp          position, status, capability
│   │   └── assigner.hpp           greedy vs optimal matching, both kept
│   │
│   ├── sim/
│   │   ├── simulator.hpp          tick loop, event emission
│   │   ├── mobility.hpp           random waypoint · road-following ·
│   │   │                          guided group models
│   │   ├── scenario.hpp           JSON scenario definition + loader
│   │   └── recorder.hpp           event log for deterministic replay
│   │
│   └── server/
│       ├── http_api.hpp           zones, tourists, scenarios, reports
│       └── ws_stream.hpp          state deltas, ~10 Hz
│
├── src/                           .cpp mirrors
│
├── apps/
│   ├── safetrail_server.cpp       serve web + run sim
│   ├── safetrail_headless.cpp     run scenario → event log, no UI
│   └── safetrail_bench.cpp        ★ index shootout, emits CSV
│
├── web/
│   ├── index.html                 operator dashboard
│   ├── tourist.html               phone-sized tourist view
│   ├── js/
│   │   ├── map.js                 Leaflet setup, tile layer, layers
│   │   ├── zone_editor.js         draw / edit / delete polygons
│   │   ├── live.js                WebSocket consumer, dot rendering
│   │   ├── alerts.js              priority rail, escalation colours
│   │   ├── dispatch.js            route drawing, ETA display
│   │   ├── replay.js              timeline scrubber over event log
│   │   ├── diagnostics.js         ★ draws the quadtree over the map
│   │   └── reports.js             heatmap, response-time charts
│   ├── css/style.css
│   └── vendor/leaflet/            vendored, not CDN — demos fail offline
│
├── data/
│   ├── zones/
│   │   └── meghalaya.geojson      hand-drawn demo zones
│   ├── osm/
│   │   └── region.graph           preprocessed road graph
│   └── scenarios/
│       ├── quiet_day.json
│       ├── festival_surge.json    500 tourists, stress test
│       └── landslide_event.json   mass alert scenario
│
├── tests/
│   ├── geo/
│   │   ├── ray_casting_test.cpp   ★ the edge cases that matter:
│   │   │                          point on edge, ray through vertex,
│   │   │                          concave polygon, polygon with hole,
│   │   │                          antimeridian crossing
│   │   └── douglas_peucker_test.cpp
│   ├── index/
│   │   └── equivalence_test.cpp   ★ every index must return identical
│   │                              results to brute force on random input
│   ├── ds/
│   │   └── merkle_proof_test.cpp  valid proofs verify, tampered fail
│   └── golden/
│       └── scenario_replay_test.cpp  fixed seed → identical event log
│
├── bench/
│   ├── run_all.sh
│   ├── results/index_scaling.csv  Z = 10 … 100,000
│   └── plots/
│
└── docs
    ├── ARCHITECTURE.md            this document
    ├── SPATIAL_INDEX_COMPARISON.md  ★ the report chapter
    ├── GEOMETRY_EDGE_CASES.md     what breaks and how you handle it
    └── API.md
```

---

## 4. Complexity budget

| Operation | Structure | Naive | Indexed |
|---|---|---|---|
| Which zones contain point | brute force → quadtree | O(Z·V) | O(log Z + k·V) |
| Zone insert / delete | quadtree | — | O(log Z) amortized |
| Nearest responder | linear scan → k-d tree | O(R) | O(log R) average |
| Point-in-polygon | ray casting | O(V) | O(V), bbox reject first |
| Route to incident | Dijkstra → A* | O(E log V) | same bound, ~3–10× fewer expansions |
| Alert triage | sorted list → heap | O(n) insert | O(log n) |
| Escalation due check | scan → interval tree | O(n) | O(log n + k) |
| Path simplification | Douglas-Peucker | — | O(n log n) average, O(n²) worst |
| Merkle proof verify | — | — | O(log n) hashes |
| **Full tick** | | **O(T·Z·V)** | **O(T·(log Z + k·V))** |

Concrete: 200 tourists, 10,000 zones, 40 vertices each. Naive is ~80M
operations per tick. Indexed is roughly 200 × (14 + 3×40) ≈ 27,000. A factor
of about 3,000 — and it is not theoretical, you will feel the difference the
moment you load the festival scenario.

---

## 5. Build order

**Phase 1 — geometry first, nothing else (week 1–2).**
`Point`, `Polygon`, `RayCasting`, and the edge-case test suite. Nothing else
works if containment is wrong, and containment is wrong in more ways than you
expect. Write the tests before the implementation here.

**Phase 2 — brute force everything (week 2–3).**
`BruteForceIndex` behind the `SpatialIndex` interface, a fixed set of zones
loaded from GeoJSON, a hardcoded tourist walking a straight line, printing
enter/exit events to stdout. End-to-end and correct, just slow. This is your
correctness oracle for the rest of the project.

**Phase 3 — the map (week 3–4).**
Leaflet, zone rendering, WebSocket, moving dots. Do this early rather than
late: the visual feedback loop makes every subsequent bug ten times easier to
find, and it is the single highest-morale week of the project.

**Phase 4 — spatial indexes (week 4–7).**
Quadtree, then R-tree, then geohash. Each one ships with the equivalence test
against brute force and a benchmark row. This is the core course content.

**Phase 5 — the event system (week 7–9).**
Triage heap, escalation interval tree, anomaly detection, hysteresis. Bugs
here are subtle and timing-dependent; the deterministic replay harness earns
its cost.

**Phase 6 — dispatch (week 9–10).**
Road graph, A*, then bipartite matching. Show greedy versus optimal side by
side in total response time — a small, clean, convincing result.

**Phase 7 — Merkle IDs, replay, reports (week 10–12).**

**Phase 8 — benchmark sweep and polish (week 12+).**

---

## 6. Risks and scope guards

**Scope creep is the primary threat.** This problem statement invites you to
build a product: mobile apps, real blockchain, ML risk scoring, SMS gateways,
authentication. Every one of those is a week you do not have, and none of them
add data structure content. The scope is: geometry engine, spatial index,
event system, simulator, visualisation. Write that sentence at the top of your
README and defend it.

**Vendor Leaflet, do not use a CDN.** Demo machines lose network. Tile caching
matters too — keep an offline tile set for your demo region.

**GPS noise will humiliate you if you ignore it.** Boundary flapping is the
most common failure in real geofencing. Add hysteresis in phase 5, not as an
afterthought.

**Keep the `SpatialIndex` interface honest.** Every implementation must pass
the same equivalence test against brute force. Without that discipline your
benchmark compares a correct slow thing against a fast wrong thing.

**Do not model real terrain.** Landslide risk, weather, elevation — all out.
Zones are polygons an operator drew. That is enough.
