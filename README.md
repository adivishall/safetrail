# safetrail

A geofencing and incident-response engine for tourist safety in low-connectivity
terrain.

Data Structures course project. Derived from Smart India Hackathon 2025 problem
statement `SIH25002` (Ministry of Development of North Eastern Region), but
deliberately not an implementation of it as written — see
[docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md).

> **What this is — and what it isn't.** A **data-structures course project**, not a
> shipped product. It reimplements by hand — no libraries — the spatial engine a
> real tourist-safety system would normally delegate to PostGIS, so that the
> *structures themselves* are the graded work. The geography is real
> (OpenStreetMap); the tourists, their GPS noise, and the incidents are
> **simulated on purpose** — simulation is what gives the ground truth to measure
> against. The simulator, dashboard, and CI are scaffolding to exercise and see
> the structures work, not the deliverable. There is **no mobile app and no live
> server**: the engine is a C++ program that emits one self-contained HTML replay.
> If you're evaluating the data-structures work, the graded core is `geo/`,
> `index/`, `ds/`, and `graph/` (≈8k lines); read those first.

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
Correlates floods of related alerts into single incidents. Assigns responders to
incidents over a road network — shortest paths by Dijkstra/A*, and a provably
optimal global assignment by the Hungarian algorithm, not just greedy nearest-first.
Runs the whole evaluation locally against a serialised index — no server in the
loop — and reconciles event logs on reconnect. Keeps a tamper-evident record of
everything. (All of this exercised in simulation; there is no device or app.)

## Quickstart

No dependencies beyond a C++17 compiler. No cmake required, no libraries to fetch.
(`make cmake-build` uses CMake if you have it; both build systems glob the same
source set, so they cannot drift.)

```bash
make demo        # run the simulation, print event stream + counters
make bench       # brute force vs quadtree vs R-tree, + correctness gates
make test        # unit, golden and integration tests (~25 s)
make dashboard   # writes dashboard.html — open it, no server needed

make determinism # same seed twice, assert byte-identical output
make asan        # the whole suite under AddressSanitizer + UBSan
make ubsan       # the whole suite under UBSan only (see below)
make check       # every header compiles standalone
make manifest    # print the source/test set both build systems see
```

The default dataset is **real OpenStreetMap geography** — actual reservoirs,
forests, and landmarks (Wards Lake, Sonapani Waterfall Cliff) around Shillong,
Meghalaya, fetched via the Overpass API and converted by `tools/osm_to_zones.py`.
Regenerate it any time with that script; see
[docs/DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md).

> **On `make ubsan`.** AddressSanitizer's runtime is currently broken on macOS 26
> with Apple clang 17 — an empty `int main(){}` linked with `-fsanitize=address`
> hangs in dyld before reaching `main`, so no ASan binary runs on such a host.
> ASan is therefore authoritative in CI (Linux/g++, where it works) and `make ubsan`
> exists so macOS developers have a sanitizer they can actually run. Both run the
> **whole** suite, and UB aborts rather than printing and continuing.

`make dashboard` produces a single self-contained HTML file: animated map, live
counters, event stream, timeline scrubber, an incident-investigation panel showing
which zone rules were in force at any moment, and toggles for the real
spatial-index node boxes and GPS uncertainty discs.

**Zero network requests** — no fetch, no XHR, no socket, no CDN, no tile server, no
font import. The engine serialises its output straight into the file (99.3% of the
1.6 MB is data; the viewer is 11.7 KB). Open it over `file://` with the network
physically off and it behaves identically. See
[docs/DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md) for the full pipeline and the
commands to verify all of it.

### Measured, on this machine

Timing is the **median of 7 passes after a warmup**, on one machine, one `-O2`
build. The speedup is a ratio to **our own brute force** — the correctness
oracle, not an external library — so it measures the structure, not a vendor.
`make bench` prints the run-to-run spread and writes it to the CSVs (about ±5% at
100k zones, larger at small n where the times are sub-microsecond).

```
index scaling, 100,000 zones, 450 m query (median of 7, ±16% spread on this run):
  brute force  ~238 us/query
  quadtree      ~7.1 us/query    ~33x
  R-tree        ~1.0 us/query   ~240x    (STR bulk packing; see below)

R-tree bulk load:        STR packing vs repeated insertion — 6.5x faster queries,
                         33% smaller tree, from the SAME data and query code
self-intersection:       Shamos-Hoey sweep vs the O(V^2) reference — 1.6x at 128
                         vertices, ~9x at 2048 (8.9-9.1 across runs); crossover at ~56, which is where
                         `Polygon::validate()` switches between them
node snapping:           k-d tree vs linear scan — 43x at 10,000 road junctions,
                         same junction returned on every probe
hysteresis A/B [GAP 8]:  93% removed under realistic drift, 94% under white noise (simulated GPS)
equivalence:             18,000 queries, 0 mismatches vs brute force
ray cast vs winding:     100,000 points, 0 disagreements
persistent index [GAP 3]: 13.0x structural sharing at 5,001 versions
A* vs Dijkstra:          78% fewer nodes settled, same optimal path
dispatch (Hungarian):    16% less total travel than greedy, never worse in 200/200
adaptive sampling [GAP 7]: 28,800 fixes -> 257, 99% battery saved at 100% near-zone recall
alert correlation [GAP 5]: scenario-dependent — a clustered incident compresses
                           ~450:1, a scattered run ~9:1 (see the caveat below)
determinism:             same seed, two runs, byte-identical output (`make determinism`)
unit tests:              769 assertions across 39 files, each fast structure vs a brute-force
                         oracle; whole suite clean under UBSan locally, ASan+UBSan in CI
```

**Read the numbers honestly.** Everything is measured on **simulated** tourists
under our own GPS-noise model — real geography (OpenStreetMap), simulated people,
by design (see below). The alert-correlation and responder-dispatch figures are
**scenario-dependent**: they reflect a scripted incident where a cohort converges
on one hazard. That is the case the feature exists for, but the ratio is a
property of how clustered the incident is, not a fixed law — a scattered
population compresses far less, and we report both.

See [docs/DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) for why the quadtree's
ceiling is ~33x and not the ~29,000x originally estimated — at 100k zones over a
district, **98.8 zones genuinely intersect each 450 m query box**, and no index can
return fewer results than the query actually has. That output-size bound is the
most interesting result in the project. The R-tree beats it not by returning less
but by visiting fewer nodes to find the same answers, which is what STR bulk
packing buys.

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
web/                    viewer assets (dashboard is generated by viz/)
  zone_editor.html      hand-authored zone authoring tool — draw zones, live
                        self-intersection + overlap warnings, exports GeoJSON
                        ZoneStore loads directly (open it, no server needed)
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
| [TEAM_BRIEF.md](TEAM_BRIEF.md) | ★ Start here — full onboarding: what, why, how to run, status, and open tasks |
| [PRESENTATION.md](docs/PRESENTATION.md) | ★ A-to-Z walkthrough to present to your guide (prose) |
| [slides.html](slides.html) | ★ The same, as a self-contained slide deck — [present it live](https://adivishall.github.io/safetrail/slides.html) |
| [WALKTHROUGH.md](docs/WALKTHROUGH.md) | ★ Start-to-finish trace of one run with diagrams — how the whole thing works |
| [GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) | ★ Research on existing systems, the eleven gaps, and what we deliberately skip |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layers, the hot loop, data flow, module responsibilities |
| [DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md) | Every structure: invariants, complexity targets, why it's here |
| [ROADMAP.md](docs/ROADMAP.md) | Phased build order with milestones |
| [GEOMETRY_EDGE_CASES.md](docs/GEOMETRY_EDGE_CASES.md) | What breaks in point-in-polygon and how we handle it |
| [DATA_PROVENANCE.md](docs/DATA_PROVENANCE.md) | ★ Where every dashboard number comes from, with re-runnable verification |
| [DESIGN_DEFENSE.md](docs/DESIGN_DEFENSE.md) | ★ Answers to the hard viva questions — worst case, noise model, hand-written rule, scope |
| [DEPLOYMENT.md](docs/DEPLOYMENT.md) | How to deploy — dashboard to Pages, engine as binaries, and the on-device story |

## Ground rules

**Every data structure is hand-written.** No `std::unordered_map`, no `std::set`,
no `std::priority_queue`, no Boost.Geometry, no PostGIS. `std::vector` and
`std::string` are permitted as raw storage. If a container does something
interesting, we wrote it. This is a **deliberate learning constraint for the
course** — the point is to implement and analyse the structures the course is
about — **not** a production recommendation. In real software you would reach for
`std::unordered_map` and a mature spatial library; here, reimplementing them is
the assignment.

**The naive baseline is a deliverable.** `BruteForceIndex` stays in the codebase
forever behind the same interface as the fast implementations. It is the
correctness oracle every index is validated against, and the denominator in every
speedup number we report.

**Determinism is non-negotiable.** Fixed seeds, stable tie-breaking, byte-identical
output across runs of the same scenario. Without it, the replay harness is
worthless and timing-dependent bugs are unfindable.

Three things are needed for that and only two are obvious. A fixed-seed integer
PRNG (`sim/mobility.hpp`) gives a reproducible random stream. Explicit tie-breaks
in every structure that orders by a key with ties -- the k-d tree's median
partition, Dijkstra's and A*'s frontiers, the Hungarian assignment's equal-cost
choice -- stop two standard libraries returning different-but-equally-valid
answers. The third is `-ffp-contract=off`: by default the compiler may fuse
`a*b + c` into an FMA, and whether it does depends on the optimisation level and
the compiler, so the same source gave three different runs (clang -O0, clang -O2,
g++ -O2). One last bit in a distance flips an inside/outside test and every later
event diverges. With it off, a local build and the one CI publishes agree exactly
-- 81,202 events, Merkle root `59736c1b...`, on both.

**Where that stops, stated precisely.** Comparing the published dashboard against a
local build byte for byte: 1,553,101 bytes, of which **173 differ, all of them in
the dispatch section** -- the greedy/optimal travel totals and the responder ->
incident lines. The evaluation core is identical: same events, same Merkle root,
same alerts, flaps, anomalies, incidents and index statistics. Dispatch differs
because it takes an argmin over accumulated haversine path costs, and haversine
calls `asin`/`sin`/`cos`, whose last-ulp results are not identical between Apple's
libm and glibc. `-ffp-contract=off` fixes contraction; it cannot make two libm
implementations agree. One last-ulp difference flips one argmin, a different
responder is chosen, and the totals move by ~3%. Closing that would mean shipping
our own transcendental functions, which is not a trade worth making here -- so it
is documented instead. Explicit tie-breaks do not help: the values genuinely
differ, so the tie-break never fires.

**Ground truth before optimisation.** Scenarios have known expected outcomes.
"It runs" is not a result; "33x faster with byte-identical output" is.

## Stack

C++17, no external dependencies anywhere — not in the core, not in the viewer, not
in the build. The dashboard is generated as one self-contained HTML file with a
hand-drawn canvas map: no Leaflet, no tile server, no CDN, no network. Python 3 is
used only for optional tooling scripts.
