# Work Log

A plain-language, running record of every change made to the project, newest
first. Each entry says **what** changed, **why**, and the **impact** — so you can
follow the project's history without reading diffs.

Format: `### YYYY-MM-DD — short title` then What / Why / Impact.

### 2026-08-24 — True O(n log n) sweep-line (AVL status)

**What:** Replaced the sorted-vector active set in the Shamos-Hoey sweep with a hand-written AVL-balanced BST, giving real O(log n) insert/erase/neighbour queries instead of O(n). Fixed a subtle bug this surfaced: two ring-adjacent edges meeting at a shared vertex tie exactly at that x, which can flip their recorded tree order and break later erase/neighbour lookups -- fixed by evaluating the ordering comparator a hair before the sweep position instead of exactly at it.

**Impact:** Sweep is now genuinely O((n+k) log n). Verified against Polygon::validate() on 1000+ random polygons and an O(n^2) all-pairs oracle on 300 random segment sets, all passing.


### 2026-08-24 — Real OSM roads + dispatch shows greedy-vs-optimal gap

**What:** Fetched real Shillong road network (144,090 nodes / 147,039 edges) via osm_to_roads.py; the demo now routes on it. Dispatch now targets the top ~major incidents (capped near responder count), so greedy and optimal diverge.

**Impact:** Demo shows e.g. 2,080 m saved by Hungarian over greedy, on real roads. All tests pass.


### 2026-08-24 — Per-tourist tracking in the dashboard

**What:** Click any tourist dot to track them — a panel shows their digital ID, group, live status, speed, accuracy, and position, and their trail is drawn on the map. Serialized per-tourist identity into the export.

**Impact:** Each individual tourist can now be tracked in the end product. Verified in-browser.


### 2026-08-24 — Battery-projection benchmark (GAP 7)

**What:** Added `make bench` measurement driving the adaptive sampler over an 8h trek.

**Impact (measured):** 99.1% battery saved vs continuous 1 Hz polling, at 100% near-zone recall. Writes power.csv.


---

### 2026-08-24 — Dashboard surfaces dispatch + anomalies

**What:** Added a "dispatch" overlay (responder markers + optimal responder→incident lines on the map) and new counters (anomalies, responders dispatched, greedy vs optimal travel). Fixed the export path to run dispatch (finalize()).

**Impact:** The new graph/dispatch and anomaly work is now visible in dashboard.html. Verified in-browser, no console errors.

---

### 2026-08-24 — Anomaly detection wired into the live pipeline

**What:** The simulator now runs anomaly detection each tick and raises
stationary / signal-lost / off-route alerts (with confirmation debounce). A small
fraction of tourists "collapse" (stop moving) mid-run to exercise it.

**Impact:** Anomalies now appear in a run (≈115 in the default demo) and flow into
incidents/dispatch. All tests still pass.

---

### 2026-08-23 — Real OSM roads loadable (closes the synthetic caveat)

**What:** Added `RoadGraph::save_file/load_file` (a plain-text road format) and
`tools/osm_to_roads.py`, which fetches real Shillong highways from OpenStreetMap
and writes that format. The simulator loads a roads file if given, else the grid.

**Why:** Removes the one honesty caveat in dispatch — routing can now run on real
roads, not just a synthetic grid. (Run the script online to fetch; nothing shipped.)

**Impact:** +5 checks (save/load round-trip preserves shortest paths). All pass.

---

### 2026-08-23 — Benchmarks for routing and dispatch

**What:** Added two measurements to `make bench`: A* vs Dijkstra (node
expansions) and greedy vs optimal dispatch (total travel), each writing a CSV.

**Why:** Report material — quantifies the new graph/dispatch work.

**Impact (measured):** A* expands ~77% fewer nodes than Dijkstra; Hungarian saves
6.5–15.7% travel over greedy and is never worse (200/200 layouts).

---

### 2026-08-23 — Dispatch wired into the simulator

**What:** After a run, the simulator now builds a road grid, places responders,
and assigns them to the correlator's incidents — greedy vs optimal (Hungarian)
side by side. The headless demo prints both totals.

**Why:** Makes the README's "assigns responders over a road network" claim real
in the running engine, not just in unit tests. Completes Phase 8's exit goal.

**Impact:** New Summary fields (dispatched / greedy_response_m / optimal_response_m);
all tests still pass (replay unaffected — dispatch runs after the event loop).

---

### 2026-08-23 — Alert triage/escalation + anomaly detection

**What:** Built `alert/triage` (orders alerts by urgency using the binary heap),
`alert/escalation` (fires unacknowledged-alert deadlines using the timer wheel +
hash table), and `track/anomaly` + `track/trajectory` (detects signal-loss,
stationary, and route-deviation from the GPS ping history).

**Why:** These app modules were the remaining designed stubs, and they put the
heap/wheel/hash-table structures to real use — a clean story for the report.

**Impact:** +48 checks (now 10,910 across 25 files), all passing. Each checked
against a brute-force oracle.

---

### 2026-08-23 — All remaining data structures built (batch 2)

**What:** Implemented the last 8 designed-but-stubbed structures/algorithms, each
with a test that checks it against a slow-but-obviously-correct "oracle":
- `index/kd_tree.hpp` — k-d tree for nearest-neighbour (nearest responder / nearest hazard)
- `index/geohash.hpp` — Morton/Z-order spatial index, and it serialises to a blob (offline story, GAP 6)
- `geo/sweep_line.hpp` — Shamos–Hoey sweep-line, a faster way to detect self-intersecting zones
- `sync/lamport.hpp` — Lamport clocks + offline queue + reconciler (correct event ordering across offline devices, GAP 6)
- `jurisdiction/hierarchy.hpp` — polygon nesting; find the deepest jurisdiction owning a point (GAP 11)
- `ds/hash_table.hpp` — open-addressed hash table (fast entity lookup by id)
- `ds/timer_wheel.hpp` — hashed timing wheel (alert escalation deadlines)

**Why:** These were the remaining `◻ designed` rows in the data-structures
inventory. The course is graded on hand-written structures, so finishing them
completes the core deliverable.

**Impact:** Zero designed-but-unbuilt structures remain. Test suite grew from
2,005 to **10,862 checks across 23 files**, all passing. Docs
([DATA_STRUCTURES.md](DATA_STRUCTURES.md), [ROADMAP.md](ROADMAP.md)) updated to
match. Committed as `ae3f946` on branch `feat/graph-dispatch-stack`
([PR #1](https://github.com/adivishall/safetrail/pull/1)).

---

### 2026-08-23 — Graph + dispatch stack built (batch 1)

**What:** Implemented the entire routing/dispatch stack, previously empty stubs:
- `ds/priority_queue.hpp` — binary min-heap (used by the routing below)
- `graph/road_graph.hpp` — weighted road graph (adjacency list) + a synthetic road-grid generator
- `graph/dijkstra.hpp` / `graph/astar.hpp` — shortest paths (A* with a map-distance heuristic)
- `graph/bipartite_match.hpp` — Kuhn's (max matching) + Hungarian (cheapest assignment)
- `dispatch/assigner.hpp` — assign responders to incidents: greedy vs provably-optimal, side by side

**Why:** This was the biggest data-structures gap — classic graded content
(graphs, Dijkstra, matching) that the project claimed but didn't have.

**Impact:** Test suite grew from 233 to 2,005 checks. Dijkstra checked against
Floyd–Warshall; A* verified to never do more work than Dijkstra; matching/
assignment checked against exhaustive search. Committed as `01bd53b`.

*Note: the road graph runs on a documented **synthetic** grid — no real
OpenStreetMap road extract ships yet (`data/osm/` is empty).*
