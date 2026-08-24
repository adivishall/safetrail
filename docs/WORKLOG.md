# Work Log

A plain-language, running record of every change made to the project, newest
first. Each entry says **what** changed, **why**, and the **impact** — so you can
follow the project's history without reading diffs.

Format: `### YYYY-MM-DD — short title` then What / Why / Impact.

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
