# Live demo script (7–10 minutes)

Exact actions, what should appear, and what to do if it does not. Rehearse it
twice from a clean start. Times in **[brackets]** are cumulative targets.

## Before the examiner arrives (5 minutes)

```bash
cd ~/Projects/"Data Structures CP" && git switch final/product-completion && make app-reset && make app
```

Expected terminal output:

```
SafeTrail demo mode — http://127.0.0.1:8080  (world built in 0.4 s: 38 zones, 144090 road junctions)
Accounts (passwords from the environment or var/demo-credentials.txt):
  admin      admin     <generated>
  operator   operator  <generated>
  ...
Scenario "walkthrough" is loaded and paused at 00:00 — press Start in Live Monitoring.
```

1. Open **http://127.0.0.1:8080** in Chrome, full screen, at 100 % zoom.
2. Sign in as **admin** (password from the terminal, or `cat var/demo-credentials.txt`).
3. In a second terminal, have `make test` already run once (so it finishes in seconds when shown).
4. Keep `docs/presentation/SafeTrail_review_slides.pdf` or the new deck open in another window as the fallback.

## The demo

| # | Say (short) | Do | Expect |
|---|---|---|---|
| 1 **[0:00]** | "Tourists in remote areas walk into hazards; help arrives late. SafeTrail tracks only people who opt in, and turns positions into alerts, incidents and dispatch." | Show the Overview page. | KPI strip, map with 38 OSM zones, 6 DEMO responders, scenario panel. |
| 2 **[0:45]** | "Architecture: a C++17 engine with hand-written structures; a platform layer for users, consent and incidents; this single-threaded server; no external services." | One slide (architecture), back to browser. | — |
| 3 **[1:30]** | "Everyone you will see is simulated and marked SIM; the zones and roads are real." | Top bar: speed **10×**, press **Run**. Go to **Live Monitoring**. | Clock advancing; 16 markers appear as each tourist opts in; group labels. |
| 4 **[2:15]** | "Each user has independent state; every fix goes through the same endpoint a phone uses." | Click one tourist (e.g. Anika) on the map. | Selected tab: ID, coordinates, last update, accuracy, nearest zones, transitions, battery model. |
| 5 **[3:00]** | "Ravi walks toward a restricted closure the administrator registered." | Watch Waterfall Viewpoint (zoom with the wheel). Scenario tab shows the narration. | Around T+00:05:50: approaching (amber), then uncertain at the edge, then **inside restricted** (red) and an incident ring **#1**. Alerts badge increases. |
| 6 **[4:00]** | "One breach → alert → incident, with the nearest responder by road distance — one Dijkstra from the incident." | **Pause.** Go to **Incidents**, click Ravi's incident. | Recommended: a Demo unit and its road distance. |
| 7 **[4:30]** | "Acknowledge, assign: the route is A* on the OpenStreetMap graph." | **Acknowledge**, then **Assign**. | State *dispatched*; white route on the mini-map; "A* settled ~770 junctions; Dijkstra needs ~5,500". |
| 8 **[5:00]** | "Group separation: Nisha stopped; union-find over the group's proximity graph says the group split; after 60 s it is a Straggler alert." | **Groups** (or Live map near Ridge Hikers); **Run** to ≈ T+00:10. | Ridge Hikers "split into 2"; dashed line to Nisha with the separation; her incident has Stationary + Straggler alerts. |
| 9 **[5:45]** | "Offline: Tenzin's phone kept evaluating locally; on reconnect the queue is merged once by Lamport order." | Live → **Scenario** tab after ≈ T+00:12:30. | Reconciliation report: N uploaded, N admitted, N duplicates rejected, skew corrected, late alert raised; Alerts shows **LATE**. |
| 10 **[6:15]** | "Investigation: what did the system know at a past time? And is the log intact?" | **Investigation** → drag the slider to ≈ T+00:06:00 → Query; scroll to the event log → **Verify** on a row. | Positions known then; zones in force from the persistent index's version; "✓ verified" — recomputed in the browser. |
| 11 **[7:00]** | "The Data Structures Lab runs the real C++ structures." | **Data Structures Lab → Brute force · Quadtree · R-tree**; drag a new query box; press Play. | Teal (descended) and red (pruned) nodes; table: every index 0 mismatches vs brute force; timings. |
| 12 **[7:45]** | "AVL rotations and graph search, from the same code." | Lab → **AVL** (Play), then **Graph** (switch Dijkstra / A*). | Balance factors, rotations; settled nodes and heap contents. |
| 13 **[8:15]** | "Benchmarks are measured, not typed." | **Analytics**, scroll to the charts. | Index scaling chart; multi-user table (≈ 3 µs per fix up to 5,000 users). |
| 14 **[8:45]** | "Tests: 51 files, 12,137 checks; 28 injected bugs all caught." | Second terminal: `make test` (and point at the last line). | `ALL TESTS PASS`. |
| 15 **[9:15]** | "Syllabus: arrays and ring buffers, trees and recursion, BST and AVL, hashing, heaps, graphs, Dijkstra/A*, union-find, binary search; beyond it: persistence, Merkle proofs, Lamport clocks. Limits: simulated people, in-memory server, thin OSM polygons." | Slide: syllabus map + limitations. | — |

## Fallbacks

| Problem | Fallback |
|---|---|
| Port 8080 busy | `make app APP_ARGS="--port 8090"` |
| The scenario has run past the moment you need | top bar **Reset**, then **Step 10 s** / speed 60× to jump |
| Incident list refreshes under the cursor | click the incident in the Overview table instead, or pause first |
| Browser problem | the static dashboard: `make dashboard` then open `dashboard.html`, or the live site https://adivishall.github.io/safetrail |
| Laptop problem | the PDF report and deck in `docs/final/`, and the screenshots in `docs/final/images/` |
| Lab request error | any experiment's **Run/Build** button resubmits; the server log shows nothing sensitive |
| Forgotten password | `cat var/demo-credentials.txt` |

## Clean-start check (run once before the day)

```bash
make app-reset && make -s && make test && make e2e
```

Expected: `ALL TESTS PASS` and `[ ok ] api/e2e 30 checks, 0 failed` (about 22 s).
