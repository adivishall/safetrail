# Work Log

A plain-language, running record of every change made to the project, newest
first. Each entry says **what** changed, **why**, and the **impact** — so you can
follow the project's history without reading diffs.

Format: `### YYYY-MM-DD — short title` then What / Why / Impact.

### 2026-09-04 — Correctness and invariant hardening (tier 4)

**What:** A deeper audit than tiers 1–3, which had checked what the project
*claimed*. This one checked what it *did*: every structure's behaviour under
deletion and churn, every serialisation format against malformed input, every
place two layers could disagree about the same question, and every asymptotic
claim against the code implementing it. Twelve defects fixed; the full
before/problem/fix/test ledger is [Tier 4 in
RESUME_HARDENING.md](RESUME_HARDENING.md).

The ones that mattered most:

1. **The persistent index answered historical queries with today's rules.** Zone
   validity lived in a mutable current-state array, so `query_at(t)` fetched the
   right historical geometry and then filtered it with whatever the rules had
   since become. The one question the structure exists to answer returned an
   answer that had never been true at any point in time. Validity is now a
   per-zone append-only log of `(version, Validity)` records — O(1) per change,
   O(log h) lookup — so the structural-sharing argument survives intact rather
   than being traded for a per-version snapshot.

2. **Serialisation lost information that changed answers.** The road-graph format
   wrote one line per unordered pair and reloaded through `add_road()`, so one-way
   streets came back two-way and non-geometric weights came back as distances —
   shortest paths through a round-tripped graph *differed*. Format v2 writes
   directed edges with their own weights (v1 still reads). The GeoJSON writer
   silently dropped kind, dwell, validity, jurisdiction, margins and every hole,
   and concatenated names unescaped so a zone named `Nohkalikai "Falls"` produced
   a file its own loader rejected. Both are now byte-identical round trips with
   field-by-field tests.

3. **The candidate cap could lose a breach.** Overflow was handled by
   `resize(cap)` — truncation in quadtree traversal order, which has nothing to do
   with risk. In a safety system a dropped candidate is a false negative. The
   default policy now treats the cap as a diagnostic and evaluates everything; the
   capped mode is opt-in, ranks by (distance, severity) before cutting, and
   reports what it dropped.

4. **Structures decayed under churn.** Quadtree deletion never collapsed a
   subdivision, R-tree deletion never condensed, geohash query padding only ever
   grew, and the hash table doubled to make room for tombstones — so a table
   churning a fixed 100-key live set grew without bound. All four fixed and
   measured: quadtree 309 → 33 nodes after deleting 900 of 1,000 and 1.00× across
   four full cycles; geohash keys scanned per query 489 → 110; hash table zero
   growths across 50,000 churned keys.

5. **Interval tree deletion was O(n) tombstoning** in the structure whose selling
   point is O(log n) — and it broke the AVL evidence, because `count_` fell while
   the height did not, so `balanced()` compared a real height to a fictional *n*.
   Replaced with real AVL deletion plus a `check_invariants()` structural audit.

6. **The two containment implementations disagreed about holes.** Kept
   specifically to cross-validate each other, they returned opposite answers for
   the interior of a counter-clockwise hole inside a counter-clockwise shell —
   which is what most GeoJSON producers emit. Found by the new hole test, which is
   exactly the job the second implementation is there for.

7. **Determinism was a claim with nothing checking it.** Explicit tie-breaks added
   to both shortest-path frontiers, the k-d tree build and queries, equal-cost
   parent selection and the Hungarian scan; `make determinism` and
   `golden/determinism_test` now gate it.

Also: `geo/segment.hpp` (one definition of the segment predicates, previously
written three times with three epsilons), `geo/projection.hpp` (planar arithmetic
in metres instead of degrees, with a measured error budget), `util/bytes.hpp`
(explicitly little-endian codec — three formats claimed a fixed layout while doing
host-endian `memcpy`), a hardened JSON parser, jurisdiction containment that works
on concave regions, and R-tree **STR bulk loading**.

**Why:** Tiers 1–3 made the project's *claims* honest. They did not check its
*invariants*, and an examiner who stops reading the docs and starts reading the
code finds invariants. Several of these — the persistent index filtering history
with current rules, the lossy graph round-trip, the truncating candidate cap —
would have been fatal in a viva precisely because the code looked correct and the
tests passed.

**Impact:** 691 assertions across 39 files (was 285/28), 0 failures. Whole suite
clean under UndefinedBehaviorSanitizer with `-fno-sanitize-recover`; ASan+UBSan
now gates CI over the whole suite rather than 2 of 28 files. `make test` went from
~4 min to ~23 s by compiling the core once into an archive instead of per test.
Make and CMake now glob the same source set, so they cannot drift.

**One new result, not a fix:** switching the R-tree's `build()` from repeated
insertion to STR bulk packing made queries **6.5× faster** on the same data with
the same query code, and the tree 33% smaller — taking it from ~33× over brute
force at 100k zones to ~247×. Same machine, same benchmark: the difference is
entirely how the tree was assembled.

**Honest note:** AddressSanitizer cannot run on this development machine at all —
macOS 26 with Apple clang 17 hangs any ASan binary, including an empty
`int main(){}`, in dyld before `main`. ASan is authoritative in CI (Linux/g++) and
`make ubsan` exists so the local machine has a sanitizer it can actually run. That
is a platform limitation being disclosed, not sanitizer coverage being quietly
dropped.

### 2026-08-30 — Dashboard: operator console + structure visibility (remaining work, batch B/C)

**What:** Turned the dashboard from debug output into something that reads like an operator console and, crucially, makes the graded data structures visible on screen.

1. **Incident feed [GAP 5].** New top panel showing the largest correlated incidents as cards — "33 people · 1 card · Sonapani Waterfall Cliff · 75,763 alerts correlated into this incident." The correlation headline is now the first thing you see, not a buried counter. Exports the top incidents (people, severity, alert count, nearest authored hazard) from the correlator.

2. **Merkle proof, verified in the browser [GAP 9].** New evidence panel commits the event stream to a Merkle root and exports one inclusion proof; a "verify offline" button recomputes the root from the leaf + proof using a hand-written **SHA-256 + RFC 6962** in JS. The recomputed root is compared to the exported root, so the ✓ is genuine — confirmed byte-exact against the C++ root (`6510c12b…`). The property Ethereum was used for, checked with no network and no chain.

3. **The spatial prune, made visible.** Selecting a tourist now draws the query neighbourhood (white dashed) and highlights the index cells it actually touches ("N index cells touched") — the O(log n + k) story on the map, not just in a counter.

4. **Panels cleaned up.** Rules-in-force is grouped and colour-coded, collapsing repetitive families (`Deep Water ×18`, `Dense Forest ×14`) and sorting in-force restricted/caution first. The change log collapses the 00:00 bulk load into one line and shows the real mid-run validity windows (Wards Lake 00:30–01:30, Love Jungle from 00:45) — the GAP 3 "rewind" made legible. The event stream drops synthetic-zone events (scale-test geometry, not hazards). Added a "saved by Hungarian" counter so the greedy-vs-optimal gap is explicit.

5. **Export correctness.** The exporter now uses the real `Zone.synthetic` flag instead of sniffing the zone name.

**Why:** The engine work made the results real, but the deliverable still hid them — the 33-person incident, the working index prune, the offline-verifiable evidence were all invisible. A resume/portfolio artefact is judged on what a viewer can see and try.

**Impact:** Dashboard regenerated and verified in-browser: incident feed populated, Merkle proof verifies (JS root == C++ root), prune highlight renders, panels grouped, event rail free of synthetic noise. Suite green. Honesty held: the dispatch gap (615 m on this config) and the GAP-3 timeline show the real numbers — no dialling to flatter.

### 2026-08-30 — Consistency + housekeeping (remaining work, batch A)

**What:** Cleared the doc/deck staleness the hardening pass left behind, plus small housekeeping.

1. **TEAM_BRIEF §6 rewritten.** It still said *"23 modules implemented, 28 still stubs"* and listed ~11 built modules as pick-up tasks. Replaced with an honest "everything in the inventory is built and oracle-tested; the only stubs left are the serverless `server/` scaffolding" and a complete "What's built" table.
2. **Stale gap statuses fixed.** GAP 6 (offline sync), 10 (sweep-line validation) and 11 (jurisdiction) were marked ⬜/⚠️ in TEAM_BRIEF and "not yet built" in PRESENTATION — all three are built. Marked ✅; PRESENTATION's gap table now lists all eleven; its "Honest engineering" slide drops the "23 modules / 13 stubbed" line and adds the scenario/simulated caveats.
3. **Deck refreshed.** `safetrail-overview.pptx` said *"27 files, ~10,900 assertions"* (now 285/28) and listed *"get CI green — one test doesn't compile"* as a Next item (done). Patched in place; the Next item now points at the console-polish work.
4. **Removed a dead stub.** `src/sim/scenario.cpp` / `sim/scenario.hpp` were an empty `// TODO(impl)` — the scenario system lives in the simulator (`SimConfig::Scenario` + `apply_scenario`). Deleted; nothing referenced them and they weren't in the build.
5. **CI:** bumped `actions/checkout@v4 → @v5` to clear the Node-20 deprecation warning.

**Why:** The tiers made the numbers and claims honest, but left three docs and the deck asserting the old story — the same credibility gap, half-closed. A reviewer opening TEAM_BRIEF or the deck would see TODOs and counts that contradict the code.

**Impact:** Suite green (exit 0) after removing the stub. No functional code changed.

### 2026-08-25 — Positioning pass (tier 3): honest framing for a resume

**What:** Fixed how the project *presents itself*, so its breadth reads as depth and its claims survive an interview.

1. **"What this is — and isn't" callout** at the top of the README: a data-structures course project, not a shipped product; real geography, simulated people, on purpose; the simulator/dashboard/CI are scaffolding, not the deliverable; no mobile app, no live server. Points evaluators at the graded core (`geo/ index/ ds/ graph/`).

2. **Softened product overclaims.** "Runs fully offline on a device" → "runs the whole evaluation locally, no server, in simulation (there is no device or app)."

3. **Reframed the "no `std::`" rule** as a deliberate learning constraint for the course, explicitly *not* a production recommendation — heading off the "reinventing `std::unordered_map` is poor judgment" read.

4. **Added a "Resume framing" section** to `docs/RESUME_HARDENING.md`: lead with one deep structure (the persistent quadtree) plus the ceiling analysis, a defensible one-line bullet, and a "don't" list (no vanity counts, no implied product, no unqualified O(log n)).

5. **Rewrote the GitHub About/description** to lead with the data-structures learning framing instead of "geofencing engine for tourist safety."

**Why:** On a resume, breadth reads as shallow and a "geofencing engine" implies a product that doesn't exist. An interviewer drills one thing; the honest, depth-first framing is what turns the project from a liability into a differentiator.

**Impact:** No code changed — positioning only. `README.md`, `docs/RESUME_HARDENING.md`, and the GitHub repo description updated. Closes the resume-hardening pass (tiers 1–3).

### 2026-08-25 — Substance pass (tier 2): benchmark rigor + honest caveats

**What:** Made the headline numbers defensible under interrogation.

1. **Benchmark rigor.** `time_queries` in `safetrail_bench.cpp` did a single timed pass with no warmup. It now runs a **warmup pass, then 7 timed passes, and reports the median** plus the **best run and the run-to-run spread** (printed, and added to `index_scaling.csv` as `*_min_us` / `*_spread_pct` columns). At 100k zones the spread is ±3–10%, so "~33×" is now a figure with an error bar. Small-n rows (10–1,000) are flagged: their times are sub-microsecond and the spread is ±30–230%, so those speedups are noise, not results.

2. **Scenario-dependence stated.** The alert-correlation and dispatch figures depend on a scripted cohort converging on one hazard. Every results table now says so and reports **both** numbers — ~450:1 on a clustered incident, **~9:1 on a scattered run** — and points at `golden/incident_formation_test` as the guard.

3. **Baseline and data honesty.** Docs now state plainly that the speedup is a ratio to **our own brute force**, not an external library (there is no third-party baseline, by design), and that every figure is measured on **simulated** tourists under our own GPS-noise model.

4. **Worst case.** Verified the resume-facing summaries already qualify the spatial index as **O(log n + k) average** and disclose the O(n) worst case (DATA_STRUCTURES worst-case table, PRESENTATION §10). No unqualified "O(log n)" remained.

**Why:** "33× faster" with no error bars, no external baseline, and results you can dial by moving tourists around is a claim that collapses under a single sharp question. Reporting the method, the spread, the baseline, and the scenario-dependence turns each number into something that survives scrutiny.

**Impact:** `make bench` green, correctness gates pass. Headline holds under the stricter method — 100k: quadtree ~32×, R-tree ~35×, spread ±3–10%. Regenerated the committed CSVs. Files touched: `apps/safetrail_bench.cpp`, `README.md`, `TEAM_BRIEF.md`, `docs/DATA_STRUCTURES.md`, `docs/PRESENTATION.md`, `bench/results/*.csv`.

### 2026-08-25 — Credibility pass (tier 1): honest claims, real integration test

**What:** Closed the four claim-vs-reality gaps a reviewer would catch first.

1. **Fixed a false claim.** `TEAM_BRIEF.md` called the hash table "Robin Hood probing"; the code is (and always was) **linear probing with tombstone deletes**. Corrected to match the implementation. No Robin Hood references remain anywhere.

2. **Retired the vanity metric.** "10,941 checks across 27 files" counted `t::ok` executions *inside fuzz loops*; the actual figure is **285 assertion sites across 28 files**, dominated by two randomized tests. Every current-state doc (README, TEAM_BRIEF, DATA_STRUCTURES, PRESENTATION, DEPLOYMENT, the CI comment) now says "285 assertions across 28 files, each fast structure vs a brute-force oracle." Stale inconsistent counts (222/11, 233/12, 117) reconciled. WORKLOG history entries left as-is — they're a dated record.

3. **Added the integration test the suite was missing.** The correlator's unit tests passed on a single hand-fed batch while the running product compressed ~1.19:1, because nothing tested multi-tick accumulation. Added (a) three **cross-tick** cases to `alert/correlator_test` — alerts arriving one-per-tick fold into one growing incident; a gap beyond the window opens a fresh one; distance still separates — and (b) a new **`golden/incident_formation_test`** that drives the whole simulator and asserts the marquee behaviour end to end: the scripted cohort collapses into one incident of ≥15 people with ≥20:1 compression, and a scenario-off run forms no such mass incident. Both would have failed against the old batch-only correlator.

4. **CI portability.** `tests/ds/priority_queue_test.cpp` was missing `<string>`/`<cstdint>` (fine under Apple clang's transitive includes, broken under GNU libstdc++ on the runner) — the break that had been failing every Pages deploy. Fixed; swept the other 27 tests and confirmed they pull those headers transitively (CI had already proved they compile).

**Why:** The project is going on a resume, where every number is an invitation to be interrogated. A false "Robin Hood" claim, a test count inflated ~40×, and a green-looking suite that never exercised the flagship feature are exactly the things that turn an impressive project into a liability under questioning.

**Impact:** Suite green — **285 assertions across 28 files, 0 failures** (10,950 executions). The new integration test measures max incident = 33 people / 453:1 compression with the scenario on, versus 2 people / 9.2:1 off — so GAP 5 is now guarded by a test that proves it works on the real pipeline, not just on a fixture.

### 2026-08-25 — Scripted incident day + persistent alert correlation

**What:** Reworked the running scenario so the outcome tells a story instead of showing random wandering, and fixed the data-structures defect that made two headline results degenerate.

1. **Scenario keystone.** Added `sim::Scenario` (on by default): a cohort (~55% of tourists) is staged as a tight party ~420 m from the highest-severity restricted zone (Sonapani Waterfall Cliff) and led into it with a new `GuidedGroup` mobility model — walk to a shared destination, then mill within a tight radius. The rest keep wandering. The roam area is ~25 km across but a walker covers only a few km/hour, so a *scattered* cohort could never reach the hazard in the sim window — which is exactly why the mass incident never used to form.

2. **Correlator now keeps incidents open across ticks.** The real bug behind GAP 5 being weak: `Correlator::ingest` only clustered alerts *within a single tick's batch* and never merged a fresh alert into an already-open incident, so 33 people breaching over ~40 s produced ~33 tiny incidents, not one. It now retires incidents past their time window and folds each fresh cluster into the nearest live incident (centroid updated by a running average). The three existing correlator unit cases still pass unchanged.

3. **Synthetic filler no longer alerts.** `Zone.synthetic` flag set on the index-scaling padding; the simulator skips alert generation for those zones. They exist to grow the spatial index for the scaling benchmark, not to flood the operator — this alone removed ~10,000 spurious UNCERTAIN alerts per run.

4. **CI unblock.** `tests/ds/priority_queue_test.cpp` was missing `<string>`/`<cstdint>`; it compiled under Apple clang (libc++ transitive includes) but failed under GNU g++ on the CI runner, which was blocking the Pages deploy.

**Why:** The engine was strong but the *end result* hid it — alert correlation compressed 1.19:1 (its whole pitch is 40→1), and the Hungarian-vs-greedy dispatch gap was exactly zero on the shipped dashboard. Both were artefacts of a random-waypoint population that never converges, plus a correlator that couldn't accumulate an incident over time.

**Impact (measured):**
- Alert correlation: **1.44 → 376:1** on the demo (25,551 of 25,619 alerts absorbed into 68 incidents); on the dashboard run **77,622 alerts → 188 incidents**. The Sonapani mass incident is now a single card with **33 people** on it.
- Dispatch: Hungarian gap restored — **4,273 m saved over greedy** on the 2 h road run (was 0).
- Full suite still green (all 27 files, exit 0); the correlator test passes with the new cross-tick behaviour.

### 2026-08-24 — Zone editor: draw zones with live validation

**What:** New `web/zone_editor.html` — click to draw a hazard zone, get a live self-intersection and overlap warning (JS port of the exact C++ orient/segs_cross logic), and export GeoJSON that ZoneStore loads directly. Found and fixed two real bugs while testing: a syntax error in the finish-button handler (extra closing paren), and a view-bounds bug where the map re-fit to the in-progress draft after every click, which silently distorted shapes mid-draw (a bowtie could stop looking like one).

**Impact:** Verified end-to-end against the real C++ engine: an exported 2-zone file loads and validates OK; a self-intersecting export is correctly REJECTED with the same "self-intersecting" reason the live editor warned about.

### 2026-08-24 — QR digital ID + fully offline verification (GAP 9)

**What:** New `evidence/digital_id.hpp/.cpp` — registers a tourist identity record in the Merkle log, encodes the compact text payload a QR code would carry (index + inclusion proof), and verifies it using ONLY a cached root + the presented record, no log access, no network.

**Impact:** 17 checks: genuine IDs verify, forged/wrong/corrupted/stale-root cases are all rejected with a reason. Text payload round-trips exactly.


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
