# Complete technical documentation

Everything needed to build, run, test, operate and extend SafeTrail. For
the design see [ARCHITECTURE.md](ARCHITECTURE.md); for the structures see
[DATA_STRUCTURES_EXPLAINED.md](DATA_STRUCTURES_EXPLAINED.md).

## 1. Requirements

| Need | Version used | Notes |
|---|---|---|
| C++17 compiler | Apple clang 17 (macOS), g++ 13 and clang on Linux (CI) | nothing else to install on a Mac with the Command Line Tools |
| make | macOS `/usr/bin/make` 3.81 or GNU make | |
| Python 3 | 3.13 (any 3.8+) | tests' end-to-end check, benchmark rendering, mutation check — standard library only |
| A browser | Chrome, Safari, Firefox | for the operations console |
| Optional | CMake ≥ 3.16 | `make cmake-build` builds the same sources |

On a MacBook Air M4: `xcode-select --install` once, then everything below.
No database, map service, API key or network connection is needed.

## 2. Install, build, run

```bash
git clone https://github.com/adivishall/safetrail.git
```

```bash
cd safetrail && make app
```

`make app` compiles the library, the server and its dependencies (about a
minute the first time) and starts the server on **http://127.0.0.1:8080**.
The first start prints the four accounts (`admin`, `operator`, `responder`,
`tourist`) and their generated passwords, and saves them in
`var/demo-credentials.txt` (mode 0600, git-ignored). The walkthrough scenario
is loaded and paused at T+00:00:00.

| Mode | Command | What differs |
|---|---|---|
| Development / demo (default) | `make app` | demo responders (labelled DEMO), scenarios enabled, clock driven by the scenario runner |
| Production | `make app APP_ARGS="--mode production"` | no fixtures, scenarios refused, real-time clock, only the `admin` account |
| Another port | `make app APP_ARGS="--port 9000"` | |
| Choose the preloaded scenario | `make app APP_ARGS="--scenario entry"` | `walkthrough`, `normal`, `entry`, `separation`, `offline`, `none` |
| Fixed passwords | `SAFETRAIL_ADMIN_PASSWORD=... make app` | likewise `_OPERATOR_`, `_RESPONDER_`, `_TOURIST_`; at least 10 characters |
| Reset | `make app-reset` | deletes `var/`; passwords regenerate on the next start |
| Behind a TLS proxy | `make app APP_ARGS="--secure-cookies"` | adds `Secure` to the session cookie; see [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) before exposing the server |

Demo data: there is nothing to initialise. The world is built at start-up from
`data/zones/shillong_osm.geojson` (38 OSM hazard zones) and `data/osm/roads.txt`
(144,090 junctions) in about 0.4 s. Reloading a scenario (Live Monitoring →
Scenario → Load, or **Reset** in the top bar) rebuilds the world from scratch.

## 3. Every make target

| Target | Does |
|---|---|
| `make` | build the three programs |
| `make app` / `make app-reset` | run the platform / delete local credentials |
| `make test` | 51 test files, 12,137 checks (about 20 s once built) |
| `make e2e` | start the server on a free port and drive it over HTTP (30 checks, about 22 s: one check waits out the 20 s request deadline) |
| `make bench` | every benchmark (≈ 5 min); rewrites `bench/results/*.csv` and the tables in README and RESULTS |
| `make validate` | docs links, results tables, `-Werror`, tests, determinism, e2e, sanitizers, CMake + ctest |
| `make mutation` | inject 28 known bugs; each must be caught |
| `make sanitize` | UBSan (macOS) or ASan + UBSan (Linux) over the whole suite |
| `make demo` / `make dashboard` / `make review` | the original CLI demo, the static dashboard, the scripted review walkthrough |

## 4. Configuration (`app::PlatformConfig`)

| Setting | Default | Editable at run time |
|---|---|---|
| trail retention | 6 h | yes (Settings) |
| breach → incident at severity ≥ | 3 | yes |
| warnings become alerts at severity ≥ | 4 | yes |
| approach alert if ETA ≤ | 180 s | yes |
| GPS jump threshold | 70 m/s | yes |
| auto-assign recommended responder | off | yes |
| escalation timeout | 120 s | start-up |
| cohesion pass interval | 10 s | start-up |
| hysteresis (enter / exit margin, samples, min dwell) | 15 m / 25 m / 3 / 5 s | start-up |
| correlation radius / window | 200 m / 10 min | start-up |
| PBKDF2 iterations | 120,000 | start-up |
| limits | 5,000 users, 20,000 zones, 4,096 vertices per zone | start-up |

## 5. HTTP API

All responses are JSON unless noted. Mutating requests need the header
`X-SafeTrail-Request: 1` and the session cookie set by `/api/login`.
Roles: **A** admin, **O** operator, **R** responder, **T** tourist; "self" means
the user named in the path.

| Method and path | Who | Purpose |
|---|---|---|
| `GET /api/health` | anyone | mode, clock |
| `POST /api/login` `{username, password}` | anyone | sets the session cookie |
| `GET /api/session` | anyone | current user or null |
| `POST /api/logout` | signed in | ends the session |
| `GET /api/live` | signed in (filtered by role) | users, responders, incidents, groups, runner, counts |
| `GET /api/users` | A O (all), R (assigned subjects), T (self) | accounts |
| `POST /api/users` `{username, display_name, role, password}` | A | register |
| `GET /api/users/{id}` | A O, R if assigned, self | profile, location, zones, battery model (audited) |
| `PATCH /api/users/{id}` `{active?, display_name?, password?}` | A; self for name/password | |
| `GET /api/users/{id}/trail?from=&to=` | A O, R if assigned, self | movement history (audited) |
| `POST /api/users/{id}/consent` `{consent}` | self | give / withdraw |
| `POST /api/users/{id}/sharing` `{active}` | self | start / stop the session |
| `POST /api/users/{id}/fix` `{lat, lon, accuracy_m, t_ms?, seq?}` | self | ingest a GPS fix |
| `POST /api/users/{id}/sos` | self | severity-5 SOS |
| `POST /api/users/{id}/battery` `{aware}` | self, A | adaptive sampling |
| `DELETE /api/users/{id}/history` | self, A | erase movement history |
| `GET /api/groups` · `POST /api/groups` `{name}` | A O | |
| `POST /api/groups/{id}/members` `{user, action}` · `PATCH /api/groups/{id}` `{proximity_m, straggler_m, confirm_s}` | A O | |
| `GET /api/zones` · `GET /api/zones/{id}` | list: signed in; detail: A O | geometry; detail adds version history, change log, incidents |
| `POST /api/zones` · `PATCH /api/zones/{id}` `{name, kind, severity, ring, max_dwell_s, active_from_ms, active_to_ms}` | A | validated polygon |
| `DELETE /api/zones/{id}` · `POST /api/zones/{id}/validity` `{active}` or `{from_ms, to_ms}` | A | |
| `POST /api/zones/import` (GeoJSON body) | A | all-or-nothing import |
| `GET /api/alerts?state=open` | A O (all), T (own) | open alerts in triage order |
| `POST /api/alerts/{id}/ack\|resolve\|dismiss` | A O | |
| `GET /api/incidents` · `GET /api/incidents/{id}` | A O, assigned R, affected T (own part only) | detail adds alerts and evidence timeline; a tourist sees only their own alerts and entries, no centroid or route |
| `POST /api/incidents/{id}/ack` | A O, assigned R | |
| `POST /api/incidents/{id}/assign` `{responder?}` | A O | default = recommended; A* route |
| `POST /api/incidents/{id}/resolve` `{note}` | A O, assigned R | |
| `POST /api/dispatch/plan` `{apply}` | A O | greedy vs Hungarian over unassigned incidents |
| `GET /api/responders` · `POST /api/responders` `{name, type, lat, lon}` · `PATCH /api/responders/{id}` `{available?, lat?, lon?}` | read: A O R; create: A; update: A O / own unit | |
| `GET /api/roads?min_lat=&min_lon=&max_lat=&max_lon=` | signed in | road segments in a box (map base layer) |
| `GET /api/history?t=` | A O | what the system knew at t (audited) |
| `GET /api/events?limit=&user=&kind=&order=lamport` | A O (all), T (own) | event log |
| `GET /api/evidence` · `GET /api/evidence/{i}` | A O | Merkle root; one entry with its inclusion proof |
| `GET /api/audit` | A | location-access audit log |
| `GET /api/stats` | A O | live counters |
| `GET /api/settings` · `PATCH /api/settings` | A O read; A write | |
| `GET /api/scenario` · `POST /api/scenario/load\|start\|pause\|resume\|reset\|step\|speed` | A O, demo mode | |
| `GET /api/lab` · `POST /api/lab/{experiment}` | signed in | spatial, kdtree, zorder, avl, persistent, graph, heap, assignment, dsu, hash, wheel, ring, merkle, lamport, geometry |
| `GET /api/bench` · `GET /api/bench/{file}` | signed in | committed benchmark CSVs (text) |

Errors: `{"error": "..."}` with 400 (bad input, including a number that is not
finite or is beyond 2^53, and a query parameter that is not entirely numeric),
401 (no session), 403 (role or CSRF), 404, 409 (state conflict, e.g. responder
busy), 411/413/431/501 (HTTP limits), 429 (login throttled), 500 (a request
that failed inside the server; the server keeps running). A connection that
has not delivered a complete request 20 s after opening is closed.

## 6. Data formats

- **Zones:** GeoJSON FeatureCollection, `[lon, lat]` order, closed rings; properties `name`, `kind` (`restricted`/`caution`/`advisory`/`safe`), `severity` 1–5, `max_dwell_s`, `active_from_s`, `active_to_s`, `synthetic`, `jurisdiction`, `enter_margin_m`, `exit_margin_m`.
- **Roads:** `safetrail-roads 1|2`, node count, `lat lon` lines, edge count, `u v weight_m` lines (v2 directed).
- **Evidence entry (canonical form hashed into the Merkle log):** `safetrail-log-v1|seq|lamport|device|t_ms|recorded_ms|kind|user|actor|zone|alert|incident|group|responder|late|live|detail`.
- **Platform clock:** milliseconds since the platform started (demo: simulated).

## 7. Extending

- A new index: implement `index::SpatialIndex`, add it to `make_index`, and to `tests/index/differential_test.cpp` and `tests/fence/index_independence_test.cpp`.
- A new alert rule: raise it in `Platform` with `raise_alert`; decide in `incident_worthy` whether it opens incidents.
- A new Lab experiment: a function in `src/app/lab.cpp` returning the structure's real state, and a view in `web/app/js/pages/lab.js`.
- A new scenario: a builder in `src/app/scenario.cpp` plus a test in `tests/app/scenario_test.cpp` asserting the story it tells.

## 8. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `cannot listen: Address already in use` | another server on 8080: `make app APP_ARGS="--port 8090"` |
| Forgot the passwords | `cat var/demo-credentials.txt`, or `make app-reset` and restart |
| Login says "too many failed attempts" | wait 60 s |
| Map shows no roads | zoom in: roads are drawn below about 18 m per pixel |
| "Scenarios are disabled" | the server was started in production mode |
| A page says the server is not reachable | the server stopped; restart `make app` (state is in memory) |
