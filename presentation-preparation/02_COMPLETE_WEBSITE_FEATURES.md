# 02 — Every website feature, page by page

> Every page, control and workflow of the console in `web/app/`, traced from the
> click to the JavaScript handler, the HTTP endpoint, the C++ function and the
> data structure underneath. Built by reading every file in `web/app/js/` and the
> matching handlers in `src/app/api.cpp` and `src/app/platform.cpp`.

## 0. How to read the traces

Every page module exports `render(root, ctx)` and returns `{ destroy() }`.
All HTTP goes through `web/app/js/api.js` (`get`, `post`, `patch`, `del`), which adds
`X-SafeTrail-Request: 1` to every non-GET request (the server's CSRF check) and
sends the HttpOnly session cookie automatically. Every endpoint is dispatched in
`Api::route()` in `src/app/api.cpp`, which calls a `Platform::` method in
`src/app/platform.cpp`. Errors come back as `{"error": "..."}` and are shown as a
red toast (`toast()` in `web/app/js/util.js`).

Notation in the traces: **UI** (what you click) → **JS** (function) → **HTTP**
(method and path, body) → **C++** (function) → **DS/algorithm**.

---

## 1. Application shell (sign-in, navigation, top bar, polling)

File: `web/app/js/app.js`.

### A. Purpose
Signs the user in, shows only the pages their role may use, hosts the global
scenario controls, and runs the one-second poll every page reads from.

### B. UI breakdown
| Element | What it does |
|---|---|
| **Sign-in form** (Username, Password, **Sign in**) | `showLogin()` → `post('/api/login', {username, password})`. On success stores `ctx.me` and calls `startApp()`; on failure shows the server's message in a red box. |
| **Public notice** under the button | Shown when `/api/session` reports `public: true` (the Render deployment): every tourist is simulated, everything resets on restart, accounts are issued by the owner. Locally it shows a hint about `var/demo-credentials.txt` instead. |
| **Brand** "SafeTrail operations" | static. |
| **Mode tag** | "public demo · resets", "demo data" or "production", from `/api/session`'s `mode` and `public`. Hover shows an explanation. |
| **Clock** `T+hh:mm:ss` | Platform clock from `/api/live` `now_ms` (`clock()` in `util.js`). Shows "offline" if a poll fails. |
| **Run / Pause**, **Step 10 s**, **speed** (0.5×–60×), **Reset** | `runnerControls()` in `web/app/js/runner.js`; only for admin/operator in demo mode. See §16. |
| **User name · role** and **Sign out** | `logout()` → `post('/api/logout')`, then shows the sign-in form. |
| **Sidebar** | Built from `ROUTES` filtered by role. Section labels: Me, Operations, Analysis, Learn, System. |
| **Badges** on Alerts and Incidents | `renderTopbar()`: Alerts = `open_alerts`, Incidents = incidents not resolved; red ("hot") when > 0. |

### Role → pages (from `ROUTES` in `app.js`)
| Role | Pages |
|---|---|
| admin, operator | Overview, Live Monitoring, Users, Groups, Hazard Zones, Alerts, Incidents, Responders, Investigation, Analytics, Data Structures Lab, Settings |
| responder | My Assignments, Data Structures Lab, Settings |
| tourist | My Safety, Data Structures Lab, Settings |

The first page in the list is the landing page. The server enforces the same
rules independently: hiding a menu item is convenience, the 403 is the control.

### C. Behind the scenes
1. `boot()` → `GET /api/session` → `Api::route` "session": returns `user` (or
   null), `mode`, `public`, `client_address`.
2. `POST /api/login` → rate-limit and lock checks → `Platform::authenticate()`
   (PBKDF2-HMAC-SHA256, constant-time compare) → a 256-bit random token; the
   server stores only SHA-256(token) in a `ds::HashMap<std::string, Session>`
   and sets the cookie `HttpOnly; SameSite=Strict` (plus `Secure` on the public
   host).
3. `startApp()` starts `setInterval(poll, 1000)`. `poll()` → `GET /api/live`,
   stores `ctx.live`, and calls every page's listener (`ctx.onLive(fn)`).
4. Routing is by URL hash (`#/incidents/3`): `route()` destroys the old page,
   clears listeners and renders the new one. A page that throws shows "This page
   failed to render" instead of breaking the shell.
5. Any 401 (except on login) triggers "Your session ended. Please sign in again."

### D. Technical notes
- `/api/live` is the shared snapshot: users (filtered by role), responders
  (staff and responders only), incidents, groups, runner state, open-alert count,
  log size. One request a second for the whole console, not one per widget.
- Sessions expire after 30 minutes idle or 8 hours (see
  `docs/final/SECURITY_AND_PRIVACY.md` §3).

### E. Presentation
1. **Simple:** "After sign-in the console shows only the pages your role allows,
   and it refreshes itself once a second from one endpoint."
2. **Detailed:** "The shell polls `/api/live` every second and fans the snapshot
   out to the current page's listeners. Sessions are random 256-bit tokens; the
   server keeps only their SHA-256 in my own open-addressing hash map, and every
   POST must carry a custom header the browser will not send cross-site."
3. **Example:** sign in as admin → twelve menu items; sign in as tourist → three.
4. **Viva:**
   - *Is hiding pages security?* No. Each endpoint checks the role on the server
     (e.g. `if (!staff) return error(403, ...)` in `api.cpp`).
   - *Why a hash map for sessions?* O(1) expected lookup by token hash on every
     request; it is `ds::HashMap`, not `std::unordered_map`.

---

## 2. The map component (used on most pages)

File: `web/app/js/map.js`, class `MapView`.

### B. UI breakdown
| Control / layer | Behaviour |
|---|---|
| **+ / −** buttons | `zoomBy(1.6)` / `zoomBy(1/1.6)` around the centre. |
| **⤢ Fit to data** | `fitData()`: fits users and responders; if fewer than two, the zone boxes. |
| Mouse wheel | Zoom around the cursor. |
| Drag | Pan. A move under 3 px counts as a click. |
| Double-click | Zoom ×2 (or finishes a polygon while drawing). |
| Hover | Tooltip: user (name, status, ± accuracy), responder (available/assigned), incident (id, state, people, severity), zone (name, kind, severity, "not in force"). |
| Click | `pick()`: nearest user/responder/incident within 12 px; otherwise the topmost zone whose box and ring contain the point (`pointInRing`). Calls the page's `onPick`. |
| Roads | Fetched in 0.02° tiles from `GET /api/roads?min_lat&min_lon&max_lat&max_lon`, cached in `roadCache`, drawn only when zoomed in (≤ 18 m per pixel and ≤ 64 tiles). |
| Zones | Filled polygons coloured by kind (restricted red, caution amber, advisory violet, safe green); faded when not in force. |
| Users | Dots coloured by status (`STATUS_COLOUR` in `util.js`). |
| Responders | Light squares. |
| Incidents | Rings at the incident centroid. |
| Groups | Centre marker and dashed lines to stragglers. |
| Trails / routes | Polylines (trail teal; route white). |
| Legend | Colour key (hidden on narrow screens). |

### C/D. Behind the scenes
- Projection: spherical Web Mercator implemented in a few lines (`mx`, `my`,
  `toPx`, `toLL`). No tile server, no Leaflet, no Google Maps.
- `/api/roads` (staff and signed-in users) scans road nodes inside the box and
  returns segments, capped so a huge box is truncated rather than slow.
- Rendering is batched with `requestAnimationFrame`.
- The map's own `pointInRing` is only for *clicking* a zone in the browser. It
  is **not** the engine's containment test; the engine's test is
  `geo::evaluate()` in C++. Do not confuse the two.

### E. Viva
- *Why draw your own map?* No external service, works offline, and the base
  layer is exactly the graph routing runs on.
- *Is the browser deciding containment?* No. The browser only draws; all
  containment, routing and alerting happens on the server.

---

## 3. Overview (staff)

File: `web/app/js/pages/overview.js`.

### A. Purpose
The operations-room summary: how many people are tracked, what is open, where
everyone is, the scenario narration and the latest events.

### B. UI breakdown
| Component | Content | Source |
|---|---|---|
| KPI **Tracked now** | tourists sharing / registered | `/api/live` users |
| KPI **No contact** | sharing but silent > 2 min | users with status "no contact" |
| KPI **Open alerts** | unacknowledged | `open_alerts` |
| KPI **Open incidents** | not resolved / total | incidents |
| KPI **Responders free** | available / total ("demo fixtures in demo mode") | responders |
| KPI **Eval cost** | mean µs per fix, measured live | `GET /api/stats` → `evaluator.mean_us` |
| KPI **Events logged** | log size ("Merkle-backed evidence") | `log_size` |
| **Situation map** | zones, users, responders, open incidents, groups; link "Open live monitoring →" | `GET /api/zones` once, then `/api/live` |
| **Open incidents** table | #, Sev, People, State, Responder (or "recommended: …"), Opened; click a row → `#/incidents/<id>` | `/api/live` incidents |
| **Scenario** panel (demo only) | scenario picker, **Load**, summary, narration, device view, reconciliation reports | `GET /api/scenario` (§16) |
| **Latest events** | last 14 log entries, LATE badge; link "Investigate →" | `GET /api/events?limit=14` every 3 s |

All numbers are read from the running platform ("nothing is a placeholder").

### C. Behind the scenes
- `/api/stats` → `Platform::stats()`, `eval_counters()`, `spatial_index().stats()`,
  `correlator().stats()`, `reconciler().stats()`, `versioned().share_stats()`.
- **Eval cost** is wall time accumulated in `Platform::evaluate_fix()` around
  `Evaluator::evaluate()` (`std::chrono::steady_clock`), divided by the call count.
- `/api/events` reads `Platform::log()`, the append-only event log whose every
  entry is also a leaf of the Merkle log (`Platform::append_log()`).

### E. Presentation
1. **Simple:** "This is the control room. Every number here is live."
2. **Detailed:** "Eval cost is the measured mean time of the whole per-fix
   pipeline: index query, exact geometry and hysteresis. Events logged counts
   entries in the evidence log, each hashed into a Merkle tree."
3. **Example:** start the walkthrough at 10× and watch Tracked now climb to 16
   and Open alerts appear.
4. **Viva:** *Where does Eval cost come from?* — a timer around
   `Evaluator::evaluate()` in `Platform::evaluate_fix()`.

---

## 4. Live Monitoring (staff)

File: `web/app/js/pages/live.js`.

### A. Purpose
The map is the page: watch people move, click anything for details, filter.

### B. UI breakdown
**Map** (full height) plus a side panel with four tabs:

| Tab | Contents and controls |
|---|---|
| **Selected** | Nothing selected: "Tracked users (n)" table (click a row to select). **User selected:** name, SIM badge, Tracking active / Not sharing, status chip, group; User ID, Coordinates, Last update, GPS accuracy, Speed, Session, Connection, Open alerts, Incidents (links), Fixes (accepted · duplicate · late · jumps); **Nearest hazard zones** (name, severity, state, distance or "m inside"); **Battery model** (mode, next interval, fixes sent vs fixed 1 Hz, estimated drain); **Geofence transitions** (last 25 zone/dwell/alert/session/sync events). The user's trail is drawn. **Incident selected:** severity, state, people, opened, alert count, recommended responder with road distance, assigned, route (A* vs Dijkstra settled counts), link to Incidents; the route is drawn. **Zone selected:** category, in force, validity window, dwell limit, users inside, vertices, incidents; link "Manage this zone →". **Responder selected:** type, status, incident, position, road junction. |
| **Filters** | Hazard category checkboxes (restricted/caution/advisory/safe), Minimum zone severity (1–5), Group, Tracking status (everyone sharing / with open alerts / no contact), User name (text), Incident status (open / all / dispatched / resolved), Show responders, Show selected user's trail. All filters are client-side (`map.filter`). |
| **Scenario** | Same panel as Overview (§16). |
| **Events** | Last 60 log entries with **Lamport stamp** `L<counter>@<device>` (device 0 is the server) and LATE badges. |

### C. Behind the scenes
| Action | Requests | C++ |
|---|---|---|
| Click a user | `GET /api/users/:id`, `GET /api/users/:id/trail`, `GET /api/events?limit=40&user=:id` | user JSON with `Platform::status_label()`, nearest zones, power model; trail from `Tracked::trail`; **each view is written to the access audit log** (`Platform::audit_access`). |
| Click an incident | `GET /api/incidents/:id` | `IncidentCase` + correlator incident + route |
| Click a zone | `GET /api/zones/:id` | zone + persistent-index history |
| Every second | `/api/live` | positions, statuses |

Detail refreshes every 2 s while something is selected.

### D. Data structures visible here
- **Status** ("inside restricted", "approaching", "uncertain", "no contact"…) comes
  from the engine's per-(tourist, zone) hysteresis state
  (`track::ZoneState`, `fence::HysteresisState`).
- **Nearest zones** and "m inside" are signed distances from `geo::signed_distance_m()`.
- **Fixes counters** come from the ingestion boundary (`Platform::ingest_fix()`).
- **Trail** is a time-sorted vector; late fixes are inserted with
  `std::upper_bound` (binary search) and flagged.
- The ping window behind speed and anomaly detection is a `ds::CircularBuffer` of 64.

### E. Presentation
1. **Simple:** "Click any tourist: the system shows where they are, how accurate
   the GPS is, which hazards are near and what transitions happened."
2. **Detailed:** "The status is the confirmed output of a hysteresis state machine
   per tourist and zone. The fix counters show the ingestion boundary at work:
   duplicates dropped, late fixes recorded but not evaluated, jumps flagged."
3. **Example:** at about T+00:05:50 in the walkthrough, select *SIM · Ravi*: the
   chip goes approaching (amber) → uncertain → inside restricted (red).
4. **Viva:**
   - *Why is a late fix not evaluated?* Evaluating an older fix after a newer one
     would run the state machine backwards in time and could produce a false
     exit/enter; it is stored in history only.
   - *Is viewing a tourist logged?* Yes, `audit_access()` writes an access record
     that is itself hashed into a separate Merkle log.

---

## 5. Users (staff; registration admin-only)

File: `web/app/js/pages/users.js`.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| **Register a user** (admin only): Username, Display name, Role, Password, **Register** | `POST /api/users {username, display_name, role, password}` → `Platform::create_user()` (admin only; the API checks the role before validating input). Toast "User registered". |
| **Accounts** table: search name, All roles, Any status (sharing / with open alerts / no contact / not sharing) | Client-side filters over `GET /api/users`. Columns: ID, Name (+SIM badge, username), Role, Tracking (active / consented, off / no consent), Status, Last update, Accuracy, Group, Alerts (worst severity), Active. |
| Click a row → **user drawer** (`userDrawer`) | Shows consent, tracking, sessions, status, location (or "not visible to you"), group, alerts, incidents, fix counters, battery model, nearby zones, a movement-history map, the event history. **Close** or Esc. |
| Drawer **Deactivate / Activate** (admin, not self) | `PATCH /api/users/:id {active}` → `Platform::set_active()`; deactivation ends that user's sessions on their next request. |
| Drawer **Erase movement history** (admin, tourists) | `DELETE /api/users/:id/history` → `Platform::erase_history()`; positions on still-open alerts are kept until they close. |

The list reloads every second while no drawer is open.

### C/D. Behind the scenes
- `Platform::create_user()` hashes the password with PBKDF2 and indexes the
  username in a `ds::HashMap` (O(1) expected lookup at sign-in).
- Location visibility is decided by `Platform::may_view_location()`: staff yes;
  a responder only while assigned to an incident involving that person; a tourist
  only themself. Each location read by someone else is audited.

### E. Viva
- *Why open addressing?* No per-node allocation, one cache-friendly array;
  tombstones keep probe chains intact after deletion (`include/safetrail/ds/hash_table.hpp`).
- *What does erasure keep?* Positions attached to open alerts, so a responder on
  the way can still find the person (documented exception).

---

## 6. Groups (staff)

File: `web/app/js/pages/groups.js`.

### A. Purpose
Tour parties are declared groups; the system detects when a party splits and
raises a **Straggler** alert for someone left behind.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| Name field + **Create group** | `POST /api/groups {name}` → `Platform::create_group()`. |
| Group card header | name, SIM badge, chip "split into N" (fragmented) or "together" / "too few tracked". |
| Summary line | members · tracked now · centre; **Show on map / Show all** focuses the map on that group. |
| Straggler notes | "X is d m from the group, separated since T — alert raised / confirming". |
| Members table | Member, Status, Tracking, **Remove** → `POST /api/groups/:id/members {user, action:"remove"}`. |
| "Add a tourist…" + **Add** | `POST /api/groups/:id/members {user, action:"add"}`. |
| Proximity m, Straggler m, Confirm s + **Save thresholds** | `PATCH /api/groups/:id {proximity_m, straggler_m, confirm_s}` → `Platform::set_group_thresholds()`; "monitoring restarts for this group". |
| **Group map** | users and group overlays. |

### C/D. Behind the scenes — union-find on a proximity graph
Every 10 s (`cohesion_interval_ms`) `Platform::tick()` calls
`Platform::run_cohesion()`, which runs `group::CohesionMonitor::update()`:
1. Build the proximity graph: link two tracked members if they are within
   `proximity_m` (default 150 m) — an O(g²) pair scan per group.
2. Join linked members with `ds::RollbackDSU::unite()`; the number of
   components tells whether the group is fragmented.
3. A member outside the largest component and farther than `straggler_m`
   (default 400 m) from the group centre for longer than `confirm_ms` (60 s)
   raises a Straggler alert.

The DSU is rebuilt fresh on every pass; **rollback is not used at runtime**
(stated in `include/safetrail/ds/dynamic_connectivity.hpp`).

### E. Presentation
1. **Simple:** "Each group is a graph; people close together are connected; if
   the graph splits, someone is left behind."
2. **Detailed:** "Every 10 seconds I build the proximity graph and find connected
   components with union-find, union by rank. A separation must last 60 seconds
   before it alerts, because people stop to take photos."
3. **Example:** walkthrough, Ridge Hikers: Nisha stops (simulated injury); the
   card shows "split into 2" and later a Straggler alert.
4. **Viva:**
   - *Why not BFS/DFS?* Either works for one snapshot; union-find is the
     incremental structure, and the same DSU class is used for incident
     correlation.
   - *Complexity per pass?* O(g²) to build edges, plus O(g log g) DSU operations
     (union by rank without path compression gives O(log g) per find).

---

## 7. Hazard Zones (staff view; editing admin-only)

File: `web/app/js/pages/zones.js`.

### A. Purpose
See every zone; for administrators, create, edit, schedule, delete and import
zones — with every change kept as history in the **persistent quadtree**.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| **Map** | Zones (filterable by category), sharing users. Click a zone to select it. |
| **Zones** table: search, All categories, Any validity / In force now / Not in force | Columns: ID, Name (+badge), Category, Sev, In force, Validity (`from → to`), Dwell, Inside (users), Vertices. Click to select. |
| **Zone** detail panel | Category, In force now, Users inside, Area (m²), Vertices. |
| Admin edit form: Name, Category, Severity, Dwell limit (s), In force from (s), until (s) + **Save** | `PATCH /api/zones/:id {...}` → `Platform::update_zone()`; toast "Zone saved as a new version". |
| **Deactivate now / Activate now** | `POST /api/zones/:id/validity {active}` → `Platform::set_zone_validity()` (closes or reopens the validity window at the current time). |
| **Delete** (confirm) | `DELETE /api/zones/:id` → `Platform::delete_zone()`; "It stays in the history." |
| **Version history (persistent quadtree)** | table Version, Time, Change from `VersionedIndex::history_for(zone)`. |
| **Changes by people** | log entries for this zone. |
| **Incidents involving this zone** | links. |
| **Create a zone** (admin): Name, Category, Severity, Dwell, **Draw**, **Finish**, **Cancel** | Draw: click the map to add vertices; double-click or Finish → `POST /api/zones {name, kind, severity, max_dwell_s, ring}` → `Platform::create_zone()`. Toast "Zone created (validated: simple polygon, finite coordinates)". |
| **Import GeoJSON** (admin): file picker (≤ 3 MB) or paste + **Import** | `POST /api/zones/import` (body is the GeoJSON text) → `Platform::import_geojson()`: **all or nothing**. |

### C/D. Behind the scenes
On create/update: `Platform::validate_zone()` checks finite coordinates, ranges
and vertex caps, then `geo::Polygon::validate()` rejects self-intersecting rings —
with an O(V²) pairwise scan below 80 vertices and the **sweep line**
(`geo::ring_self_intersects_sweep()`, `src/geo/sweep_line.cpp`) from 80 vertices
up (`kSweepThresholdVertices` in `include/safetrail/geo/polygon.hpp`). Then
`Platform::index_zone()`:
```
index_->remove(id); index_->insert(id, bbox);          // live R-tree
vindex_->add_zone(id, bbox, validity, now);             // persistent quadtree: new version
```
A validity-only change appends one record to the zone's append-only validity
log and copies no tree nodes.

### E. Presentation
1. **Simple:** "Zones can be edited, switched on and off by time, and every
   past version is still queryable."
2. **Detailed:** "Each edit is a new version of a path-copying quadtree: only the
   root-to-leaf path changes; everything else is shared. Validity windows switch
   a zone off without deleting it, so 'what were the rules at 14:32' stays
   answerable."
3. **Example:** select the rockfall closure, change its severity, Save; the
   version table gains a row.
4. **Viva:**
   - *Why validate with a sweep line?* A self-intersecting polygon makes
     point-in-polygon meaningless; the sweep checks all edges in O(n log n)
     instead of O(n²) pairs. Below 80 vertices the pairwise scan is used because
     its constants win (measured in `bench/results/self_intersection.csv`).
   - *Is the live index the persistent one?* No: the live per-fix index is the
     R-tree; the persistent quadtree serves history.

---

## 8. Alerts (staff)

File: `web/app/js/pages/alerts.js`.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| State select: **Open (triage order)** / All, including closed | `GET /api/alerts?state=open` or `GET /api/alerts`. |
| severity ≥ N select | client-side filter. |
| Kind select (zone breach, approaching, uncertain, dwell exceeded, straggler, group split, stationary, signal lost, SOS) | client-side filter. |
| Table | Rank (position in triage order for open alerts), Sev (with ↑ if escalated), Kind (+LATE), User (+SIM), Zone (or note), Raised, State, **Priority**, Incident link, actions. |
| **Acknowledge** (open) | `POST /api/alerts/:id/ack` → `Platform::acknowledge_alert()`. |
| **Resolve** / **Dismiss** (open/ack/dispatched) | `POST /api/alerts/:id/resolve` or `/dismiss` → `Platform::resolve_alert()`. |

Reloads every 2 s.

### C/D. Triage = binary max-heap; escalation = timing wheel
- `Platform::triage_order()` pushes every open alert into `alert::TriageQueue`
  (a `ds::BinaryHeap` ordered by priority) and pops them:
  `priority = severity + 0.5 × minutes open + 0.1 × km to nearest free responder (+100 for SOS)`
  (`alert::triage_score()` in `include/safetrail/alert/triage.hpp`). Building the
  order is O(n log n) for n open alerts.
- Escalation: when an incident-worthy alert is raised, `EscalationTracker::arm()`
  schedules its deadline (raised + 120 s) on a `ds::TimerWheel` (1 s ticks, 512
  slots). Every tick `Platform::escalate()` advances the wheel; only alerts still
  **Open** escalate one severity level (max 5) and log "unacknowledged for 120 s".
  Acknowledging cancels the timer.
- Warnings (approaching, uncertain) resolve themselves when the person enters,
  leaves, or after 10 minutes (`settle_warnings`, `expire_warnings`).

### E. Viva
- *Why a heap and not sorting?* Equivalent for one snapshot (O(n log n)); the
  heap is the structure for "always give me the most urgent", the same class as
  Dijkstra's frontier.
- *Why a timing wheel?* Checking "which deadlines expired" costs one slot per
  tick instead of scanning every open alert.

---

## 9. Incidents (staff)

File: `web/app/js/pages/incidents.js`.

### A. Purpose
Turn related alerts into one card, recommend the nearest responder by **road**
distance, dispatch with an A* route, and resolve.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| **Incidents** table | #, Sev, People (+LATE), State, Recommended (name · road distance, or "none free"), Assigned, Opened. Click → detail (URL `#/incidents/<id>`). |
| Detail header | `Incident #n`, severity, state, LATE. |
| Progress chips | 1. open → 2. acknowledged → 3. dispatched → 4. resolved (reached steps highlighted). |
| Facts | People affected, Opened, Last alert, Area (radius), Recommended (road metres), Assigned, Route (metres, junctions), **Search effort** ("A* settled X junctions; Dijkstra needs Y for the same pair"), Travel time ("not estimated: the road file has lengths, not speeds"), Resolution. |
| **Acknowledge** | `POST /api/incidents/:id/ack` → `Platform::acknowledge_incident()`; also acknowledges its open alerts. |
| Responder select + **Assign / Reassign** | `POST /api/incidents/:id/assign {responder}` → `Platform::assign_incident()`. |
| Resolution note + **Resolve** | `POST /api/incidents/:id/resolve {note}` → `Platform::resolve_incident()`; frees the responder, resolves the alerts. |
| Mini-map | zones, incident ring, responders, route, affected users. |
| **Alerts in this incident** | Alert, Kind, Sev, User, Raised, State. |
| **Timeline (evidence log)** | each log entry with its evidence index. |
| **Batch dispatch: greedy vs optimal** — **Compare**, **Apply optimal plan** (confirm) | `POST /api/dispatch/plan {apply}` → `Platform::plan_dispatch()`: Greedy total, Hungarian total, longest, Saved, Uncovered. |

### C/D. Behind the scenes
1. **Correlation** (`Platform::correlate()` → `alert::Correlator::ingest()`):
   incident-worthy alerts in a batch are clustered with `ds::RollbackDSU` (pairs
   within 200 m and 10 minutes), then each cluster joins a live incident whose
   centroid is within 200 m and recent enough, or opens a new one.
2. **Recommendation** (`Platform::recommend()`): snap the centroid to the nearest
   road junction (`RoadGraph::nearest_node()`, k-d tree), run **one Dijkstra**
   (`graph::dijkstra()`) from there, read the distance to every available
   responder's junction, pick the minimum. Valid because the road graph is
   undirected.
3. **Assignment** (`Platform::assign_incident()` → `route_between()`):
   `graph::astar()` responder → incident; for the "Search effort" figure it also
   runs `graph::dijkstra(g, from, to)` on the same pair.
4. **Batch plan**: road-distance cost matrix (Dijkstra) for free responders ×
   unassigned incidents; `graph::hungarian()` (O(n³)) vs greedy cheapest-first.

### E. Presentation
1. **Simple:** "Alerts close together become one incident. The system recommends
   the nearest responder by road and draws the route."
2. **Detailed:** "One Dijkstra from the incident gives road distance to every
   responder at once. Assigning runs A* with a straight-line heuristic, which is
   admissible, so the path is still shortest but A* settles far fewer junctions —
   about 770 against about 5,500 in the demo."
3. **Example:** incident #1 (Ravi in the rockfall closure): recommended Demo
   Medical Unit A, 1.54 km by road; Acknowledge, Assign; the route appears.
4. **Viva:**
   - *Why Dijkstra for recommendation but A* for the route?* Recommendation
     needs distances to *all* responders (one Dijkstra); a route needs one
     source–target pair (A* is faster).
   - *Is A* still optimal?* Yes; edge weights are great-circle lengths, so the
     straight-line heuristic never over-estimates.
   - *Why compare with Hungarian?* Greedy can strand a later incident with a far
     responder; Hungarian minimises the total. The benchmark shows 6.5–15.7 %
     saved (`bench/results/dispatch.csv`).

---

## 10. Responders (staff; registration admin-only)

File: `web/app/js/pages/responders.js`.

| Component | Behaviour |
|---|---|
| **Units** table | ID, Name (+DEMO), Type, Status (available / assigned / unavailable), Incident link, Position, Junction. **Set unavailable / Set available** (when not assigned) → `PATCH /api/responders/:id {available}` → `Platform::set_responder_available()`. |
| **Map** | responders; open incidents; routes of active incidents (blue). |
| **Register a responder** (admin): Name, Type (medical/rescue/police/fire), Latitude, Longitude, **Pick on map**, **Register** | Pick: next map click fills the coordinates. Register → `POST /api/responders` → `Platform::create_responder()`, which **snaps the unit to its nearest road junction** with the k-d tree. |

**DS:** `index::KdTree` inside `graph::RoadGraph::nearest_node()` — O(log n)
expected nearest neighbour over 144,090 junctions instead of a linear scan
(`bench/results/node_snap.csv` section 14 checks both agree).

**Viva:** *Why a k-d tree for points and an R-tree for zones?* Points need
nearest-neighbour; zones are boxes needing range queries.

---

## 11. Investigation (staff) — "History" in the code

File: `web/app/js/pages/history.js`.

### A. Purpose
Answer "what did the system **know** at time t?" and prove the evidence log was
not edited.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| Time slider, seconds field, clock label, **Query**, **Now** | `GET /api/history?t=<ms>`. |
| **What the system knew** map | users at their last position known at t; zones coloured by "in force at t". |
| **At T+…** panel | Users known, Zones in force (index version vN), Alerts open then, Incidents then. |
| **Alerts as of then** | id, kind, severity, state then vs state now. |
| **Events in the 5 minutes before** | feed. |
| **Event log and evidence**: kind filter, **arrival order / Lamport order**, **Refresh** | `GET /api/events?limit=300&order=...&kind=...`; columns Seq, Lamport, Time, Received, Kind, Who, Detail, **Verify**. |
| **Verify** | `GET /api/evidence/:index` → browser recomputes the Merkle root with WebCrypto SHA-256 (`verifyInclusion()` in `history.js`) → "✓ verified" or "✗ FAILED". |

### C/D. Behind the scenes
| Question | C++ | Structure |
|---|---|---|
| Where was user u at t? | `Platform::position_at(u, t)` | binary search (`std::upper_bound`) on the time-sorted trail, ignoring fixes *received* after t |
| Which zones were in force at t? | `VersionedIndex::version_at(t)`, `active_at(t)` | persistent quadtree version + **AVL interval tree** stabbing query + per-zone validity log |
| Alert/incident state at t | `Platform::alert_state_at`, `case_state_at` | per-record state history |
| Is entry i intact? | `MerkleLog::prove(i)` | Merkle inclusion proof, O(log n) hashes |

Events synced late from an offline phone are excluded until the time they
arrived — the page shows what was *known*, not what is known now.

### E. Viva
- *Why verify in the browser?* So nobody has to trust the server: given the
  published root, the browser recomputes it from O(log n) sibling hashes.
- *What is the difference between arrival and Lamport order?* Arrival is when the
  server received it; Lamport is the causal order, correct even for a phone with a
  wrong clock.

---

## 12. Analytics (staff)

File: `web/app/js/pages/analytics.js`.

| Component | Source | Meaning |
|---|---|---|
| **Live platform** KPIs: Fixes accepted (duplicates, late), Evaluation (µs/fix, calls), Candidates/fix (after the R-tree prune), Flaps suppressed (by hysteresis), Alerts → incidents (compression), Escalations (timer-wheel deadlines fired), Cohesion pass (µs), History sharing (×, versions, nodes), Sync (merged, duplicates rejected), Road graph (nodes, edges) | `GET /api/stats` every 3 s | live counters |
| **Events by kind** bar chart | `stats.events_by_kind` | this run |
| **Benchmark environment** | `GET /api/bench/environment.txt` | machine the CSVs came from |
| **Measured benchmarks**: index scaling (log–log chart + table), constant density chart, R-tree insertion vs STR table, multi-user table, AVL interval tree vs scan, persistent quadtree sharing, Dijkstra vs A* chart, greedy vs Hungarian, adaptive sampling vs 1 Hz | `GET /api/bench/<file>.csv` (read-only whitelist) | **committed** results from `make bench`, not computed live |

Charts are drawn with SVG by `lineChart()` in `analytics.js`. **Two kinds of
numbers, never mixed:** live counters and measured benchmarks.

**Viva:** *Are these benchmarks run on the server when I open the page?* No.
They are the CSVs `make bench` wrote; the page only draws them. The live Lab
times (§13) are separate.

---

## 13. Data Structures Lab (all roles)

File: `web/app/js/pages/lab.js`; server: `src/app/lab.cpp`. Fifteen experiments,
each `POST /api/lab/<name>` running the real C++ class. Covered in full in
[06_DATA_STRUCTURES_LAB.md](06_DATA_STRUCTURES_LAB.md).

---

## 14. My Safety (tourist)

File: `web/app/js/pages/mine.js`.

### A. Purpose
The tourist's own control: consent, the tracking switch, a simulated phone,
SOS, alerts, battery saving and erasure.

### B. UI breakdown
| Component | Behaviour |
|---|---|
| "Location sharing is ON/OFF" chip + status chip | from `GET /api/users/:me`. |
| **I consent to location tracking** checkbox | `POST /api/users/:me/consent {consent}` → `Platform::set_consent()`; withdrawing consent stops sharing. |
| **Start sharing / Stop sharing** | `POST /api/users/:me/sharing {active}` → `start_sharing()` / `stop_sharing()`; Start is disabled without consent. Stopping discards zone state (not turned into a false exit). |
| **SOS** (confirm) | `POST /api/users/:me/sos` → `Platform::raise_sos()`: severity-5 alert at the last fix; triage adds +100. Needs an active session and a position. |
| Map click (**Simulated phone**) | `POST /api/users/:me/fix {lat, lon, accuracy_m}` → `Platform::ingest_fix()`; toast "Fix accepted, N transition(s)". |
| Accuracy select ±5/8/15/30/60 m | accuracy of the simulated fix. |
| **My status**: position, last fix, nearest zones, battery (model) | |
| **Battery saving** checkbox | `POST /api/users/:me/battery {aware}` → `set_battery_aware()`; adaptive sampling model (`power::AdaptiveSampler`). |
| **My alerts** | the tourist's own alerts only (server filters). |
| **Erase my history** (confirm) | `DELETE /api/users/:me/history`. |

**Important honesty point:** there is no phone app. The map click *stands in*
for a phone, but it uses the **same endpoint** a phone would and goes through the
full engine. A fix can only come from the user's own session
(`by.id == u` in `ingest_fix`), and nothing is stored without consent and an open
session.

**Viva:** *Can an operator start tracking a tourist?* No:
`Platform::set_consent()` and `start_sharing()` accept only the user themself
(a mutation test, "operator can give consent for a tourist", checks this).

---

## 15. My Assignments (responder)

File: `web/app/js/pages/assignments.js`.

| Component | Behaviour |
|---|---|
| **My unit**: name and status; **Go off duty / Go on duty** (when not assigned) | `PATCH /api/responders/:unit {available}`. "Your account is not linked to a responder unit" if unlinked. |
| One card per assigned, unresolved incident: people, opened, route length; **Acknowledge**, **Resolve** (prompt for a note) | `POST /api/incidents/:id/ack` / `resolve`; allowed because the incident is assigned to the responder's own unit. |
| **Route** map | own unit, the incident, the people involved, the A* route. |

A responder sees a person's location **only while assigned** to their incident,
and each view is audited (`Platform::may_view_location()`). A mutation test
("responder keeps location access after the incident resolves") checks that
access ends.

---

## 16. Scenario controls (top bar and Scenario panels; staff, demo mode)

Files: `web/app/js/runner.js`; server: `src/app/scenario.cpp` (`ScenarioRunner`).

| Control | Request | Effect |
|---|---|---|
| **Run** | `POST /api/scenario/start` | the runner advances the platform clock each tick |
| **Pause** | `POST /api/scenario/pause` | stops advancing |
| **Step 10 s** | `POST /api/scenario/step {ticks:10}` | advance ten simulated seconds while paused |
| Speed select 0.5×…60× | `POST /api/scenario/speed {speed}` | simulated seconds per real second |
| **Reset** (confirm) | `POST /api/scenario/reset` | rebuild the world and reload the scenario at 00:00 |
| Scenario picker + **Load** (confirm) | `POST /api/scenario/load {id}` | replace the world with another scenario, paused |
| Narration list | — | scripted lines; done ones dimmed, the next highlighted |
| Device-side view | — | offline phones: queued events, clock skew; battery-aware phones: fixes sent/skipped |
| Reconciliation reports | — | after an offline phone reconnects |

Scenarios (`ScenarioRunner::catalogue()`): **Complete walkthrough** (15 min, all
stories at once), **A. Normal movement**, **B. Restricted-zone entry**, **C. Group
separation and dispatch**, **D. Offline device and sync**.

**Simulation vs live:** the people's *movements* are scripted; everything the
platform does with those fixes (indexing, geometry, hysteresis, alerts,
correlation, routing) is the real engine. Simulated fixes enter through
`Platform::ingest_fix()` — "nothing bypasses the engine". In production mode
scenarios are refused (409).

---

## 17. Settings (all roles; content by role)

File: `web/app/js/pages/settings.js`.

| Component | Who | Behaviour |
|---|---|---|
| **Platform settings**: Trail retention (min), Breach → incident at severity ≥, Warnings alert at severity ≥, Approach alert if ETA ≤ (s), GPS jump above (m/s), Auto-assign the recommended responder + **Save** | view: staff; edit: admin | `GET /api/settings`; `PATCH /api/settings` → updates `PlatformConfig`. |
| **Fixed at start-up** table | staff | escalation timeout, cohesion interval, stationary confirm, hysteresis margins (15 m enter, 25 m exit), confirm samples (3), min dwell (5 s), prediction horizon (300 s), correlation radius (200 m) and window (600 s), PBKDF2 iterations, spatial index ("r-tree"). |
| **Change my password** (two fields) | everyone | `PATCH /api/users/:me {password}`. |
| **Location-access audit log** | admin | `GET /api/audit`: size, Merkle root, records (time, who, whose data, what, allowed/DENIED). |
| **Privacy commitments** | everyone | static list of the privacy rules the code enforces. |

---

## 18. Cross-cutting behaviour

| Topic | What the code does |
|---|---|
| **Real-time updates** | Polling once a second (`app.js`), not WebSockets. Pages reload heavier data every 2–3 polls. Stated as a limitation (no push). |
| **Notifications** | Toasts (bottom right) for results and errors; nav badges for open alerts and incidents. No browser push notifications. |
| **Loading and errors** | "Loading…" placeholders; red error boxes with the server's message; "offline" clock if the server is unreachable; a page that throws shows a contained error. |
| **Session end** | 401 → sign-in form with "Your session ended". |
| **Security** | CSRF header on every mutating request; role checks on the server; every text node via `textContent`; CSP forbids inline scripts. |
| **Responsive** | ≤ 1100 px: side columns stack, the brand shrinks; ≤ 760 px: the sidebar becomes a horizontal scroller, the top bar wraps, the map legend hides (`web/app/css/app.css`). |
| **Simulated vs real** | SIM and DEMO badges everywhere (`sourceBadge()` in `util.js`); the public deployment labels everything simulated and temporary. |
| **Static vs interactive** | Interactive: everything above. Static: the Privacy commitments list, the Lab's explanatory "facts" cards, page descriptions. The Analytics benchmark charts are **recorded** results, not live. |
