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
make test        # 117 checks, should all pass
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
data/zones/meghalaya.geojson
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
| 6 | "Offline-first" that isn't | Theirs queues requests. Can't reach PostGIS = can't check a single zone. We ship the index to the device | ⬜ todo |
| 7 | Continuous GPS = 8–12% battery/hour | Sample based on how close the danger is | ✅ done |
| 8 | Drift makes fences fire constantly | Hysteresis filter. **Removes 87.6% of false alerts** | ✅ done |
| 9 | Ethereum for a tamper-proof log | A Merkle log gives the same property offline, in ~200 lines, no chain | ⬜ todo |
| 10 | No validation on hand-drawn zones | Self-intersecting polygons make the geometry return garbage. We reject them | ⚠️ basic version done |
| 11 | Nobody owns the alert across district lines | Resolve jurisdiction from nested boundaries | ⬜ todo |

---

## 6. Where the project actually stands

Honest status. **21 modules implemented, 28 still stubs.**

### Working

| Area | Files | What's in it |
|---|---|---|
| Geometry | `geo/point, bbox, polygon, containment` | Haversine distance, polygons with holes, ray casting, winding number, signed distance, three-valued containment |
| Spatial indexes | `index/brute_force, quadtree, rtree` | All three behind one interface |
| Persistent index | `index/versioned_index` | Path-copying quadtree — query any past moment |
| Data structures | `ds/dynamic_connectivity, circular_buffer, interval_tree` | Rollback union-find, ring buffer, AVL interval tree |
| Zone evaluation | `fence/zone, hysteresis, evaluator` | GeoJSON loading, drift filter, the hot loop |
| Tracking | `track/tourist` | Noise-tolerant speed/heading, trajectory projection |
| Alerts | `alert/alert, correlator` | Spatio-temporal clustering into incidents |
| Groups | `group/cohesion` | Fragmentation and straggler detection |
| Power | `power/adaptive_sampler` | Risk-proximity sampling |
| Simulation | `sim/mobility, simulator` | Deterministic RNG, GPS error model, mobility models |
| Support | `util/json`, `viz/html_export` | Hand-written JSON parser, dashboard generator |

### Still stubs — pick one

Headers exist with full interfaces and comments explaining what to build, so it's
fill-in-the-blanks rather than a blank page.

| Task | Files | Difficulty | What you'd learn |
|---|---|---|---|
| **Merkle evidence log** (GAP 9) | `evidence/merkle_log` + SHA-256 | Medium | Hash trees, inclusion proofs. Self-contained, good first task |
| **Geohash index** | `index/geohash` | Medium | Z-order curves, bit interleaving. Slots straight into the benchmark |
| **k-d tree** | `index/kd_tree` | Medium | Nearest-neighbour queries |
| **Sweep line** (GAP 10) | `geo/sweep_line` | Hard | Bentley–Ottmann segment intersection |
| **Road routing** | `graph/road_graph, dijkstra, astar` | Medium | Graphs, A* heuristics |
| **Responder assignment** | `graph/bipartite_match`, `dispatch/` | Hard | Hungarian algorithm, matching |
| **Offline sync** (GAP 6) | `sync/lamport, delta_sync` | Hard | Logical clocks, distributed ordering |
| **Alert triage** | `alert/triage, escalation` | Easy | Binary heap, interval tree usage |
| **Hash table** | `ds/hash_table` | Easy | Open addressing, Robin Hood probing |
| **Anomaly detection** | `track/anomaly, trajectory` | Easy | Sliding windows, Douglas–Peucker |
| **Jurisdiction** (GAP 11) | `jurisdiction/hierarchy` | Medium | Nested polygon containment |

Suggested split: whoever wants graph algorithms takes routing + matching; whoever
wants strings/hashing takes the Merkle log + hash table; whoever wants geometry
takes sweep line + anomaly detection.

Full phase plan with ordering: [docs/ROADMAP.md](docs/ROADMAP.md).

---

## 7. Results we can actually claim

All reproducible with `make bench`. Numbers from an Apple clang -O2 build.

### Spatial index scaling — 100,000 zones, 450 m query

| | time per query | speedup |
|---|---|---|
| Brute force | 242.23 µs | baseline |
| Quadtree | 7.41 µs | **32.7×** |
| R-tree | 7.45 µs | **32.5×** |

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
| Hysteresis filter (GAP 8) | **87.6%** of false transitions removed — 733 → 91, same seed and noise |
| Persistent index (GAP 3) | **13.0×** node sharing at 5,001 versions; querying the past costs the same as the present |
| Index equivalence | 18,000 queries — quadtree and R-tree both **0 mismatches** vs brute force |
| Ray casting vs winding number | 100,000 points, 200 polygons, **0 disagreements** |
| Alert correlation (GAP 5) | 833 operator cards suppressed |
| Unit tests | **117 checks**, all pass |

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

1. Pick a row from the stub table in section 6.
2. Read its header in `include/safetrail/…` — the interface and the reasoning are
   already written.
3. Read the matching phase in [docs/ROADMAP.md](docs/ROADMAP.md).
4. Write the test first if it's a data structure.
5. `make test` before pushing.
6. Add a benchmark row if it's an index.

---

## 12. Documents, in reading order

| Doc | Why |
|---|---|
| **This file** | Start here |
| [GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) | The research: what exists, the eleven gaps, what we deliberately skip |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layers, the hot loop, data flow |
| [DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) | Every structure, complexity, and all measured results |
| [ROADMAP.md](docs/ROADMAP.md) | Phase plan and what to build next |
| [GEOMETRY_EDGE_CASES.md](docs/GEOMETRY_EDGE_CASES.md) | The ten ways point-in-polygon breaks |
| [DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md) | Where every number comes from, verifiable |
| [DEPLOYMENT.md](docs/DEPLOYMENT.md) | How to deploy — dashboard to Pages, engine as binaries, and the on-device story |
| [PROJECT_DESCRIPTION.md](docs/PROJECT_DESCRIPTION.md) | The formal writeup for our guide |

---

## The 30-second version

We built the geofencing engine everyone else imports, and fixed eleven things that
importing it makes impossible. Hand-written quadtree, R-tree, persistent quadtree,
rollback union-find, interval tree, plus real computational geometry. It runs,
there's a dashboard, 117 tests pass, and the index is 33× faster than brute force
with provably identical output.
