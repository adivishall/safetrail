# safetrail — Team Brief

Everything you need to understand this project, get it running, and pick up a
task. Read this first; the deeper documents are linked at the end.

**Repo:** https://github.com/adivishall/safetrail
**Course:** Data Structures — course project
**Origin:** Smart India Hackathon 2025 problem statement `SIH25002` (Ministry of
Development of North Eastern Region), deliberately extended beyond it

---

## 1. What we're building, in plain terms

A system that watches where tourists are on a map and warns when someone walks
into a dangerous area — a landslide slope, a restricted border zone, a
deep-water lakeshore. Northeast India: difficult terrain, patchy mobile coverage,
tourists who don't know the local hazards.

**The one line that matters:** every other team who built this handed the hard
part to a database. They installed PostGIS and asked it *"is this person inside
this polygon?"* We wrote that engine ourselves. **The data structures are the
deliverable**, not plumbing wrapped around a library.

That's not a stylistic choice. It's what makes this a data structures project
instead of a web app, and it's also what lets us fix eleven things that
outsourcing to a server-side database makes impossible.

---

## 2. Why we didn't just build the obvious version

We researched the existing implementations before writing any code. Two are
public: **SafeVoyage** (a GitHub project) and **STSMIRS** (a published paper).
They converge on an identical shape:

| Component | What they use |
|---|---|
| Containment testing | PostGIS `ST_Contains` on a server |
| Spatial index | PostGIS's GiST tree (someone else's code) |
| Tamper-proof identity | Ethereum smart contract via Hardhat |
| Anomaly detection | An imported LSTM model |

So the finished product is a competent React Native app around four libraries.
Two consequences, and both are ours to take:

1. **For our course it's the wrong project** — nothing to analyse, since the
   interesting work happens inside PostGIS.
2. **It creates real functional gaps**, because depending on a server-side
   spatial database breaks in exactly the terrain this problem targets.

Full research writeup with sources: [docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md).

---

## 3. Get it running (about two minutes)

You need a C++17 compiler. **Nothing else** — no cmake, no libraries, no package
manager, no internet.

```bash
git clone https://github.com/adivishall/safetrail
cd safetrail
make test        # 765 assertions across 39 files, all pass
make demo        # run the simulation, watch the event stream
make bench       # the measurements — this is the money shot
make dashboard   # writes dashboard.html, open it in any browser
```

`make dashboard` produces one self-contained HTML file — animated map, live
counters, event stream, timeline scrubber. No server needed, no internet. Open it
over `file://` and it works.

---

## 4. How it works

### The plain version

Every tick (once a second in the demo), for each tourist:

1. **Narrow down** — which zones are even *near* this person? A tree answers this
   without checking all 100,000, like using a book's index instead of reading
   every page.
2. **Check precisely** — are they actually inside? Real polygon geometry.
3. **Report only changes** — "entered" and "exited", never "still inside"
   repeated forever. That distinction is the difference between a usable alert
   list and an unreadable scrolling wall.

### The technical version

All of it lives in one function: `fence::Evaluator::evaluate()` in
`src/fence/evaluator.cpp`. Everything else in the codebase is scaffolding around
it.

```
1. usability gate     reject fixes too noisy to mean anything        O(1)
2. adaptive sampling  should we even take a GPS fix right now?       O(1)
3. spatial prune      100,000 zones → ~5 candidates            O(log n + k)
4. temporal prune     of those, which are in force right now?        O(1)
5. exact geometry     three-valued containment per candidate         O(k·V)
6. hysteresis         filter out GPS-drift-induced flapping          O(1)
7. transition diff    states → EVENTS, the actual output             O(k)
8. prediction         where will they be, and does it cross?         O(k·V)
```

**Step 3 is the entire performance story.** Without it this is
`O(tourists × zones × vertices)` = 200 × 100,000 × 40 = 800 million operations
per tick. With it, about 27,000.

**Step 7 is where the bugs live.** We diff against the previous tick rather than
reporting current state.

### Data flow

```
data/zones/shillong_osm.geojson   (real OpenStreetMap data)
    → ZoneStore (validates geometry, rejects self-intersecting polygons)
    → SpatialIndex (quadtree / R-tree) + VersionedIndex (history)
    → Simulator: mobility models → GPS error injection
    → Evaluator: containment → hysteresis → transitions → Events
    → Correlator (group related alerts) + CohesionMonitor (group splits)
    → dashboard.html
```

---

## 5. The eleven things nobody else does

Each traces to a documented gap. Details in
[docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md).

| # | Gap | What we do | Status |
|---|---|---|---|
| 1 | GPS is treated as an exact point | It's ±4 m open sky, ±35 m in hills. We answer **inside / outside / can't tell** instead of faking certainty | ✅ done |
| 2 | Alerts fire on entry — too late | Predict it: *"4 minutes from the border zone at current pace"* | ✅ done |
| 3 | Zones are static; risk isn't | A road closes after rain. We can also rewind: *"what were the rules at 14:32?"* — what an investigation actually needs | ✅ done |
| 4 | Tourists tracked individually | They travel in groups. Someone 800 m behind and falling further behind is the incident | ✅ done |
| 5 | One landslide, forty alert cards | Correlate them into **one incident** with forty people on it | ✅ done |
| 6 | "Offline-first" that isn't | Theirs queues requests. Can't reach PostGIS = can't check a single zone. We serialise the index for local evaluation and reconcile event logs with Lamport clocks on reconnect | ✅ done |
| 7 | Continuous GPS = 8–12% battery/hour | Sample based on how close the danger is | ✅ done |
| 8 | Drift makes fences fire constantly | Hysteresis filter. **Removes 93% of false alerts** (measured under realistic correlated drift) | ✅ done |
| 9 | Ethereum for a tamper-proof log | A Merkle log (RFC 6962, SHA-256 from scratch) gives the same property offline, no chain | ✅ done |
| 10 | No validation on hand-drawn zones | Self-intersecting polygons make the geometry return garbage. We reject them — pairwise check plus an O((n+k) log n) Shamos–Hoey sweep-line, and the zone editor warns live | ✅ done |
| 11 | Nobody owns the alert across district lines | Resolve jurisdiction from nested administrative boundaries | ✅ done |

---

## 6. Where the project actually stands

Honest status. **Every structure and algorithm in the inventory is built,
exercised, and cross-checked against a brute-force oracle.** Nothing in the
data-structures inventory is designed-but-unbuilt any more, and there is no
`TODO(impl)` anywhere in `include/`, `src/`, `apps/` or `tests/`.

Seven headers exist with no implementation, and each now says in its own file
exactly why: `server/http_api.hpp` and `server/ws_stream.hpp` (the project is
serverless by design — the engine emits one HTML file), `geo/haversine.hpp` and
`geo/predict.hpp` (the logic lives beside its callers, in `geo/point.cpp` and
`fence/evaluator.cpp`), `geo/douglas_peucker.hpp` (simplification runs offline in
`tools/osm_to_zones.py`), and `sim/recorder.hpp` / `sync/delta_sync.hpp`
(superseded by `viz/html_export.cpp` and `sync/lamport.hpp`). They survive so that
the comments referring to them lead somewhere honest rather than to a missing
file. Full inventory with complexity and status, including a "what is deliberately
NOT built" table: [docs/DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md).

### What's built

| Area | Files | What's in it |
|---|---|---|
| Geometry | `geo/point, bbox, polygon, containment, sweep_line` | Haversine, polygons with holes, ray casting, winding number, signed distance, three-valued containment, Shamos–Hoey sweep-line (AVL status) |
| Spatial indexes | `index/brute_force, quadtree, rtree, geohash, kd_tree` | Five indexes behind one interface; geohash serialises for offline |
| Persistent index | `index/versioned_index` | Path-copying quadtree — query any past moment |
| General structures | `ds/dynamic_connectivity, circular_buffer, interval_tree, priority_queue, hash_table, timer_wheel` | Rollback union-find, ring buffer, AVL interval tree, binary heap, open-addressed hash table, hashed timing wheel |
| Zone evaluation | `fence/zone, hysteresis, evaluator` | GeoJSON loading + validation, drift filter, the hot loop |
| Tracking | `track/tourist, anomaly, trajectory` | Noise-tolerant speed/heading, stationary/signal-lost/off-route detection |
| Alerts | `alert/alert, correlator, triage, escalation` | Cross-tick spatio-temporal clustering into incidents, heap triage, timer-wheel escalation |
| Groups | `group/cohesion` | Fragmentation and straggler detection |
| Graph + dispatch | `graph/road_graph, dijkstra, astar, bipartite_match; dispatch/` | Real OSM road graph, Dijkstra/A*, Kuhn's + Hungarian, greedy-vs-optimal assignment |
| Offline + evidence | `sync/lamport, delta_sync; evidence/merkle_log, digital_id` | Lamport clocks + reconciler, RFC 6962 Merkle log + SHA-256, offline-verifiable QR ID |
| Jurisdiction | `jurisdiction/hierarchy` | Nested-polygon ownership resolution |
| Power | `power/adaptive_sampler` | Risk-proximity sampling |
| Simulation | `sim/mobility, simulator, recorder` | Deterministic RNG, GPS error model, mobility models, scripted incident scenario |
| Support | `util/json`, `viz/html_export` | Hand-written JSON parser, dashboard generator |

Every fast structure has a unit test that checks it against a slow, obviously
correct oracle, and the whole pipeline has an end-to-end test
(`tests/golden/incident_formation_test.cpp`). Build order and history:
[docs/ROADMAP.md](docs/ROADMAP.md).

---

## 7. Results we can actually claim

All reproducible with `make bench`. Each timing is the **median of 7 passes after
a warmup**, one machine, one Apple clang `-O2` build; `make bench` prints the
run-to-run spread (about ±5% at 100k). The speedup is a ratio to **our own brute
force** — the correctness oracle — not to an external library, and it is measured
on **simulated** data. State those three caveats before quoting the number.

### Spatial index scaling — 100,000 zones, 450 m query

| | time per query (median of 7) | speedup |
|---|---|---|
| Brute force | ~230 µs | baseline |
| Quadtree | ~7.3 µs | **~33×** |
| R-tree (STR bulk build) | ~1.0 µs | **~240×** |

Quadtree and R-tree converge at 100k; which one leads is within the ±5% spread,
The R-tree pulled ahead once `build()` switched from repeated insertion to STR
bulk packing — same data, same query code, 6.5× faster queries and a 33% smaller
tree purely from how it was assembled.

### The most interesting thing in the project

Our design doc originally predicted **~29,000×**. Measurement brought that down to
33×, and understanding *why* is worth more marks than the big number would have
been.

At 100,000 zones in ~40 km², **98.78 zones genuinely intersect each query box**.
Those are correct answers — no index can return fewer results than the query
actually has. The ceiling is output size, not the tree. That's exactly what
`O(log n + k)` predicts once `k ≈ 99` dominates `log n`.

**If anyone asks about the speedup, lead with this explanation, not the number.**

### Other measurements

| Result | Measured |
|---|---|
| Hysteresis filter (GAP 8) | **91.2%** removed under realistic drift, 92.3% under white noise — both models, same seed |
| Persistent index (GAP 3) | **13.0×** node sharing at 5,001 versions; querying the past costs the same as the present |
| Index equivalence | 18,000 queries — quadtree and R-tree both **0 mismatches** vs brute force |
| Ray casting vs winding number | 100,000 points, 200 polygons, **0 disagreements** |
| Alert correlation (GAP 5) | **Scenario-dependent.** A scripted cohort on one hazard collapses to a single incident of ~33 people (~450:1 compression); a scattered run still compresses ~9:1. The ratio reflects how clustered the incident is, not a fixed number — we report both |
| Unit tests | **765 assertions across 39 files**, every fast structure vs a brute-force oracle, all pass |

### Three bugs the measurements caught

Worth knowing, because they're the kind that ship silently:

1. **The index pruned nothing (1×).** The evaluator queried a fixed 20 km box for
   2 km zones, so every query returned every zone. Results were still *correct*,
   just pointless.
2. **The quadtree wasted 11 levels of depth.** It was rooted at the whole planet,
   so its cells were kilometres across where the zones actually are. **The
   dashboard's index overlay is what revealed it** — the subdivision lines were
   visibly enormous. Fixing it took 100k performance from 14.1× to 32.7×, from a
   five-line change.
3. **`validate()` misdiagnosed every bowtie.** It checked zero-area before
   self-intersection, and a bowtie's two lobes cancel to exactly zero area.

Bug 2 is the strongest argument we have for building the visualisation early.

---

## 8. Is the data real?

You will be asked this. The answer has two halves.

**Real:** the zone geography is **fetched from OpenStreetMap** (Overpass API) —
38 actual reservoirs, forests, and landmarks around Shillong, including **Wards
Lake** and **Sonapani Waterfall Cliff**, at their true coordinates. Reproduce with
`curl ... | python3 tools/osm_to_zones.py`. The converter validates and drops
self-intersecting polygons so the engine loads them cleanly (GAP 10).

**Simulated:** the tourists, the GPS noise, and the extra filler zones that pad
the index to 100k for the scaling benchmark. The validity *windows* (Wards Lake's
spillway hours, etc.) are illustrative rules layered onto the real geometry.

**This split is deliberate.** The geography is real. The tourists are simulated
because no real tourist-tracking dataset exists for this problem, and simulation
is what gives us *ground truth* — we know exactly where every tourist truly was,
so we can measure whether the engine got the right answer. A recording of real GPS
traces couldn't tell us that.

Everything between input and output is genuinely computed. Nothing is mocked.

**How the data reaches the dashboard: it doesn't travel.** Zero network requests —
no fetch, no XHR, no socket, no CDN, no tile server. The engine serialises its
output straight into the HTML file (99.3% of the 1.6 MB is data; the viewer is
11.7 KB). Verification commands:
[docs/DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md).

---

## 9. Repo layout

```
include/safetrail/      headers — the design lives here, read these first
  geo/                  computational geometry
  index/                spatial data structures (+ the persistent one)
  ds/                   general data structures
  fence/                zone evaluation, hysteresis, THE HOT LOOP
  track/ group/ alert/  tracking, cohesion, correlation
  graph/ dispatch/      routing and assignment (mostly stubs)
  sync/ power/ evidence/  offline, battery, tamper-evidence
  sim/ viz/ util/       simulation, dashboard, JSON
src/                    implementations, mirrors include/
apps/                   safetrail_headless (demo) · safetrail_bench
tests/                  unit tests per structure
data/zones/             the GeoJSON dataset
bench/results/          benchmark CSVs (committed — they're our evidence)
docs/                   design documents
```

---

## 10. Ground rules — please follow these

**Every data structure is hand-written.** No `std::unordered_map`, `std::set`,
`std::map`, `std::priority_queue`, no Boost.Geometry, no PostGIS. `std::vector`
and `std::string` are fine as raw storage. If a container does something
interesting, we wrote it. This is the entire premise of the project — breaking it
in one file undermines the whole thing.

**Never delete `BruteForceIndex`.** It's the correctness oracle every other index
is validated against, and the denominator in every speedup number we report.
Without it a benchmark can compare a correct slow thing against a fast wrong
thing.

**Determinism is non-negotiable.** Fixed seeds, stable tie-breaking, byte-identical
output across runs. The replay tests and the hysteresis A/B comparison are
worthless otherwise.

**New structure ⇒ new test.** Ideally cross-checked against a brute-force version.
That's how we caught all three bugs above.

**No allocation in the tick loop.** Buffers are caller-owned and reused.

---

## 11. Picking up a task

There is no stub table left to pick from — section 6 says why. Work now comes
from one of three places:

1. **A gap the tests do not cover.** The rule is `optimized result == simple
   oracle`; if a structure has no brute-force reference next to it in `tests/`,
   that is the task.
2. **A claim that has drifted from the code.** Every header states its own
   complexity and its own guarantees. When one stops being true, the code is the
   source of truth and the comment is the bug — that is how the k-d tree's
   metric mismatch was found (`docs/WORKLOG.md`).
3. **A phase in [docs/ROADMAP.md](docs/ROADMAP.md)** that is genuinely future
   work rather than a missing piece of what is already claimed.

Whichever it is:

- Read the header in `include/safetrail/…` first — the interface and the
  reasoning are already written there, not in a wiki.
- Write the test first if it's a data structure, and write the oracle with it.
- `make test`, `make asan` and `make determinism` before pushing. All three gate
  CI, so a red one is not a "fix it later".
- Add a benchmark row if it's an index or a structure whose cost you changed.

---

## 12. Documents, in reading order

| Doc | Why |
|---|---|
| **This file** | Start here |
| [PRESENTATION.md](docs/PRESENTATION.md) | ★ A-to-Z slide-by-slide walkthrough to present to your guide |
| [WALKTHROUGH.md](docs/WALKTHROUGH.md) | ★ Start-to-finish trace of one run with diagrams — how the whole thing works |
| [GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) | The research: what exists, the eleven gaps, what we deliberately skip |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layers, the hot loop, data flow |
| [DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) | Every structure, complexity, and all measured results |
| [ROADMAP.md](docs/ROADMAP.md) | Phase plan and what to build next |
| [GEOMETRY_EDGE_CASES.md](docs/GEOMETRY_EDGE_CASES.md) | The ten ways point-in-polygon breaks |
| [DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md) | Where every number comes from, verifiable |
| [DESIGN_DEFENSE.md](docs/DESIGN_DEFENSE.md) | ★ Answers to the hard viva questions — worst case, noise model, hand-written rule, scope |
| [DEPLOYMENT.md](docs/DEPLOYMENT.md) | How to deploy — dashboard to Pages, engine as binaries, and the on-device story |
| [PROJECT_DESCRIPTION.md](docs/PROJECT_DESCRIPTION.md) | The formal writeup for our guide |

---

## The 30-second version

We built the geofencing engine everyone else imports, and fixed eleven things that
importing it makes impossible. Hand-written quadtree, R-tree, persistent quadtree,
rollback union-find, interval tree, plus real computational geometry. It runs,
there's a dashboard, 765 assertions across 39 files pass, and the index is 33-240× faster than brute force
with provably identical output.
