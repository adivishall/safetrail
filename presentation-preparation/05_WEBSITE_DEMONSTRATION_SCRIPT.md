# 05 — Website demonstration script (live, about 12–15 minutes)

> An ordered walkthrough on the deployed site, using only buttons that exist.
> Timings come from the **Complete walkthrough** scenario
> (`ScenarioRunner::catalogue()` in `src/app/scenario.cpp`) and from a live run on
> the deployed service on 2026-11-02. A shorter, rehearsed version is in
> `docs/final/LIVE_DEMO_SCRIPT.md`.

## Before you start (5 minutes before)

| Step | Action | Why |
|---|---|---|
| 1 | Open **https://safetrail-v0bh.onrender.com** two minutes early. | The free host sleeps after 15 idle minutes; the first request then waits while it starts (13.5 s measured; up to about a minute). |
| 2 | Get the admin password: Render Dashboard → **safetrail** → **Environment** → `SAFETRAIL_ADMIN_PASSWORD` → copy icon. | Never put it on a slide or in a chat. |
| 3 | Sign in as `admin`. You should land on **Overview** with the clock at `T+00:00:00` and the tag **public demo · resets**. | Fresh world after every start. |
| 4 | If anyone else may be signed in, press **Reset** in the top bar first. | The world is shared. |
| 5 | Keep the local fallback ready: `make app` → http://127.0.0.1:8080 (passwords printed in the terminal). | No network needed. |
| 6 | Use a window at least 1,100 px wide. | Below that, side columns stack (still usable). |

**Live vs scripted, say it once at the start:** "The people are simulated by a
scenario script and marked SIM. Everything the system does with their GPS fixes —
indexing, geometry, alerts, correlation, routing — is the real engine."

## The walkthrough

### Step 1 — Introduce the project (0:00–0:45)
- **Say:** "Tourists in remote areas walk into hazards and help arrives late.
  SafeTrail tracks only people who opt in, decides inside / outside / uncertain
  for every hazard zone near them, and turns that into alerts, incidents and
  dispatch. The core is a C++ engine with hand-written spatial and temporal data
  structures; the question is how to avoid checking every zone for every fix."
- **DS:** the motivating O(n) per fix.

### Step 2 — Sign in (already done; mention it)
- **Show:** the sign-in note: "Public demonstration. Every tourist here is simulated…".
- **Say:** "Passwords are PBKDF2-hashed; the session is a random 256-bit token
  whose hash is stored in my own open-addressing hash table."
- **If it fails:** the password is wrong or the server is waking — wait 30 s,
  retry once; five wrong attempts from one address lock that account for 60 s.

### Step 3 — The main dashboard (Overview) (0:45–1:30)
- **Click:** nothing yet. **Point at** the KPI strip: Tracked now 0, Responders
  free 6 / 6, Events logged; the situation map; the Scenario panel.
- **Say:** "Every number is read from the running platform. Eval cost will show
  the measured microseconds per GPS fix once people move."
- **DS:** Merkle-backed event log (Events logged).

### Step 4 — The map (1:30–2:00)
- **Click:** **+** twice on the map, or scroll to zoom; drag to pan.
- **Expect:** grey road lines appear once you are zoomed in (≤ 18 m per pixel).
- **Say:** "No map library or tile server: I draw the OpenStreetMap road network
  myself — the same 144,090-junction graph routing runs on — and the 38 hazard
  polygons are real OSM data."
- **Hover** a coloured polygon: tooltip with name, kind, severity.

### Step 5 — Live tracking (2:00–3:30)
- **Click:** top bar speed **10×**, then **Run**. Then sidebar **Live Monitoring**.
- **Expect:** the clock advances; 16 markers appear as tourists opt in; within
  about 40 simulated seconds the **SIM · rockfall closure** is created.
- **Click** one tourist on the map (e.g. *SIM · Anika*).
- **Show:** the Selected tab: coordinates, GPS accuracy, Fixes counters,
  Nearest hazard zones, Geofence transitions, Battery model; the trail on the map.
- **Say:** "Each user has independent state per zone. Every fix goes through the
  same endpoint a phone would use: validation, duplicate and late-fix handling,
  then the R-tree query, exact geometry and hysteresis."
- **DS:** circular buffer of 64 pings (speed), binary search on the trail (late fixes).
- **If no markers:** the scenario is paused — press Run; if it shows "finished",
  press Reset, then Run.

### Step 6 — Zone management (3:30–4:15)
- **Click:** **Hazard Zones**; select *SIM · Rockfall closure (Sonapani trail)* in
  the table.
- **Show:** the detail panel (category restricted, severity 5) and **Version
  history (persistent quadtree)**.
- **Say:** "Zone changes never overwrite: each is a new version of a path-copying
  quadtree. Only the changed root-to-leaf path is copied; everything else is
  shared, so every past version stays queryable."
- **Optional (admin):** **Create a zone → Draw**, click 4 map points, **Finish**:
  "validated as a simple polygon, then inserted into the live R-tree and the
  persistent index." Avoid editing during the scenario if you don't need to.
- **DS:** persistent quadtree; sweep-line validation.

### Step 7 — Trigger an alert (4:15–5:30)
- **Click:** back to **Live Monitoring**; zoom near *Waterfall Viewpoint*; select
  *SIM · Ravi (viewpoint)*.
- **Expect (≈ T+00:05:20 to T+00:06:10):** status chip amber *approaching* →
  *uncertain* at the edge → red **inside restricted**; an incident ring; the
  Alerts badge increases.
- **Click:** **Pause** in the top bar.
- **Say:** "A fix is a disc. Inside means the whole disc is inside; straddling is
  uncertain. Entry is confirmed only after three agreeing fixes at least 15 m
  inside and 5 seconds of dwell — hysteresis. That is why this didn't flicker."
- **DS:** R-tree query → three-valued geometry → hysteresis state machine.
- **If you missed the moment:** it is fine — the incident persists. If the
  scenario finished, the incidents are still there to show.

### Step 8 — Incident management (5:30–6:30)
- **Click:** **Incidents** → row **#1** (*SIM · Ravi*).
- **Show:** progress chips (open → acknowledged → dispatched → resolved);
  Recommended *Demo Medical Unit A — 1.54 km by road*; the Alerts in this
  incident; the Timeline with evidence indices.
- **Say:** "Alerts within 200 m and 10 minutes are joined into one incident by
  union-find. The recommendation is one Dijkstra from the incident's nearest road
  junction — found with a k-d tree — which gives the road distance to every
  responder at once."
- **DS:** union-find, k-d tree, Dijkstra.

### Step 9 — Responder dispatch (6:30–7:15)
- **Click:** **Acknowledge**, then **Assign** (the recommended unit is preselected).
- **Expect:** state *dispatched*; a white route on the mini-map; "A* settled 772
  junctions; Dijkstra needs 5557 for the same pair".
- **Say:** "Assigning routes the responder with A*. The straight-line heuristic
  never over-estimates road distance, so the path is still shortest, but A*
  settles about seven times fewer junctions here."
- **Optional:** **Batch dispatch → Compare**: greedy total vs Hungarian total.
- **DS:** A*, binary heap, Hungarian algorithm.

### Step 10 — Groups and assignments (7:15–8:00)
- **Click:** **Groups**. (Run the scenario to about T+00:09:00 at 10× first if
  the split has not happened.)
- **Show:** *Ridge Hikers* "split into 2"; the straggler note for *Nisha*.
- **Say:** "Every 10 seconds I build each group's proximity graph and count
  connected components with union-find. A separation must last 60 seconds before
  it becomes a Straggler alert."
- **Assignments:** the **My Assignments** page exists only for a responder
  account. Say: "A responder sees their incidents, the route, and the people's
  locations only while assigned — every view audited." Show it only if you have
  the responder password ready.
- **DS:** union-find on a proximity graph.

### Step 11 — Analytics (8:00–8:45)
- **Click:** **Analytics**.
- **Show:** live KPIs (Fixes accepted, Evaluation µs, Candidates/fix after the
  r-tree prune, Flaps suppressed, Alerts → incidents compression), then the
  **index scaling** chart.
- **Say:** "Two kinds of numbers here, never mixed: live counters from this
  server, and benchmark results committed from my laptop runs."
- **If a chart is missing:** the bench files are part of the image; reload once.

### Step 12 — History / Investigation (8:45–9:45)
- **Click:** **Investigation**; drag the slider to about T+00:06:00; **Query**.
- **Show:** users known then, zones in force (index version vN), alerts as of then.
- **Scroll** to *Event log and evidence*; click **Verify** on any row.
- **Expect:** "✓ verified" and a toast "browser recomputed the root from N
  sibling hashes".
- **Say:** "This answers what the system knew at that time — trails are
  binary-searched, zones in force come from the persistent index and an AVL
  interval tree. And the browser verifies the Merkle proof itself, so you don't
  have to trust my server."
- **DS:** binary search, persistent quadtree, interval tree, Merkle tree.

### Step 13 — Data Structures Lab (9:45–11:30)
- **Click:** **Data Structures Lab** → *Brute force · Quadtree · R-tree · Geohash*.
- **Drag** a box on the canvas. Use **Play** on the stepper.
- **Show:** teal = nodes descended, red dashed = pruned subtrees, amber = results;
  the table: every index **0 mismatches** vs brute force.
- **Then:** *AVL interval tree* → preset *Zig-zag (forces double rotations)* →
  **Run** → step through rotations; *Adjacency list · Dijkstra · A* · priority
  queue* → switch Dijkstra / A* and compare settled counts; *Union-find with
  rollback* → step to the rollback.
- **Say:** "Each experiment runs the real C++ class on the server and returns its
  internal trace; the JavaScript only draws."
- **If a request is refused** (red toast): an input is out of range — the panel
  keeps its last good result; press the button again.

### Step 14 — Compare the spatial indexes (11:30–12:15)
- **Stay** on the spatial experiment: change *Show structure* to *R-tree (STR
  bulk-loaded)* and *R-tree (built by insertion)*; point at Visited, Nodes, Depth.
- **Say:** "The quadtree partitions space into fixed quadrants; the R-tree
  partitions the items into overlapping boxes that hug the data. STR packing
  builds a better R-tree than inserting one by one."
- **Note:** one small query can go either way; the committed benchmark averages
  many queries.

### Step 15 — Benchmark results (12:15–13:00)
- **Click:** Analytics → *R-tree: insertion-built vs STR* table, constant density chart.
- **Say:** "At 100,000 zones in one district, the R-tree is about 224 times faster
  than a linear scan and the quadtree about 33 times — with identical results.
  The speedup flattens because about 98 zones genuinely overlap each query; no
  index can avoid returning them. At constant density the R-tree is about
  1,000 times faster. These are one laptop's measurements, quoted as ranges."

### Step 16 — Architecture (13:00–13:45)
- **Show:** the architecture diagram (slide, or
  [09_SYSTEM_ARCHITECTURE.md](09_SYSTEM_ARCHITECTURE.md) §1).
- **Say:** "Browser console in plain JavaScript; a single-threaded C++ HTTP server;
  the platform layer for users, consent, alerts and incidents; the engine with the
  data structures; real OSM data. No database: state is in memory."

### Step 17 — Conclude with the DS contribution (13:45–14:30)
- **Say:** "Five spatial and temporal structures behind one interface, each
  checked against a brute-force oracle with invariant audits after every operation;
  28 injected bugs all caught; 51 test files, 12,166 checks. The honest limits:
  simulated people, in-memory state on a free host, and benchmarks from one
  laptop."

## Fallbacks

| Problem | What to do |
|---|---|
| Public URL slow | It is waking from sleep: wait, or switch to the local server. |
| Public URL unreachable | `make app`; http://127.0.0.1:8080. |
| Someone else is driving the shared world | Local server. |
| Scenario ran past a moment | **Reset**, then **Step 10 s** or 60× to jump. |
| Incident list refreshes under the cursor | Pause first, or open the incident from the Overview table. |
| Browser problem | The static dashboard: https://adivishall.github.io/safetrail/. |
| Laptop problem | PDFs and deck in `docs/final/`, screenshots in `docs/final/images/`. |

## What is live, simulated or recorded in this demo

| Live (computed now on the server) | Simulated | Recorded |
|---|---|---|
| index queries, geometry, hysteresis, alerts, correlation, Dijkstra/A*, Hungarian, Merkle proofs, every Lab experiment, live KPIs | the tourists' movements and phones, the injury, the offline trekker, the responders' positions | benchmark charts in Analytics (from `bench/results/`) |
