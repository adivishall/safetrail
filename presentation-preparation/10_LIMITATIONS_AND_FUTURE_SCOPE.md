# 10 — Limitations and future scope

> The project's genuine limitations, why each exists, and what fixing it would
> take. Source of record: `docs/final/LIMITATIONS_AND_FUTURE_SCOPE.md` and
> `docs/final/SECURITY_AND_PRIVACY.md` §9. Saying these first, in your own words,
> is stronger than having them found.

## 1. The limitations, grouped

### 1.1 Data and realism
| Limitation | Why it exists | What would fix it |
|---|---|---|
| **Simulated people.** Every tourist is scripted (SIM); responders are fixtures (DEMO). | No consent process, privacy notice or data-protection review exists for real users; a course project must not track real people. | Recorded GPS traces with ground truth; a consent and privacy process before any real use. |
| **Thin OSM hazard polygons.** Rivers ("Deep Water") are narrow, so a ±20 m fix near them is often honestly *uncertain*. | Real data has this shape. | Buffered hazard polygons from domain experts; per-zone hysteresis margins. |
| **Battery figure is a model** (energy ∝ fixes taken). | No phone to measure. | Measure on devices; include radio/screen/CPU. |
| **No travel time.** Routes are in metres; the road file has no speeds. | OSM extract stores lengths only. | Speeds from road classes; A* heuristic divided by max speed to stay admissible. |
| **Benchmarks from one laptop.** Ratios moved ≈ 100×–236× between sessions for the R-tree. | Microsecond timings are sensitive to machine state. | Repeat on several machines; quote ranges (already done). |

### 1.2 Data structures and algorithms
| Limitation | Why | Fix |
|---|---|---|
| **Removal is O(n)** in every spatial index. | No id → node map; the item must be found by search. | Keep an id → leaf map (hash map) for O(log n) removal. |
| **Worst-case queries O(n)** (quadtree, R-tree). | Adversarial data: everything straddles splits or all boxes overlap. | Inherent to these structures; priority R-trees give worst-case bounds; or cap and rank (the engine has a capped policy, off by default because a dropped candidate is a missed breach). |
| **Interval tree reporting is O(min(n, (k+1) log n)),** not O(log n + k). | The augmentation prunes only on `max_high`. | Centred interval tree or priority search tree. Not needed: the per-fix path does not use it. |
| **Rollback DSU's rollback is unused at runtime.** | Rebuilding per pass is cheaper at these sizes (≈ 200 people). | Use rollback for incremental "what if this edge is removed" queries at larger scale, or full dynamic connectivity (Holm–de Lichtenberg–Thorup). |
| **Group cohesion is O(g²)** per group per pass. | All-pairs proximity is fine for tour parties. | Grid bucketing → O(g) expected. |
| **Tolerance-based geometry** (on-edge tolerance ~0.1 mm), not exact predicates. | Simpler, measured error budget. | Exact/adaptive predicates (Shewchuk). |
| **No antimeridian support** — zones crossing ±180° are refused. | Query boxes do not wrap. | Split boxes at ±180° (tested refusal today). |
| **A GPS jump inflates the query radius** for about a minute (speed window). | Speed estimate from the 64-ping window. | Robust speed estimate (median, outlier rejection). Still correct, only slower. |
| **Prediction is a straight-line extrapolation** over ≤ 5 minutes. | Simple, bounded look-ahead; the code says it does not claim to predict. | Path-aware prediction along trails/roads. |
| **One Dijkstra gives distance to every responder** only because the graph is undirected. | OSM roads stored both ways. | Run Dijkstra on the reverse graph for one-way streets. |

### 1.3 System and deployment
| Limitation | Why | Fix |
|---|---|---|
| **In-memory state; no persistence.** A restart loses accounts created in the console, zones edited, alerts and the evidence log. | No database by design; free hosting has no disk. | Write-ahead event log replayed on start; durable Merkle log with signed roots and consistency proofs (a paid disk or database on a host). |
| **Free host sleeps after 15 idle minutes;** every wake is a fresh world; 0.1 CPU. | Render free plan; zero cost was a requirement. | A paid instance with a disk. |
| **One shared world** on the public demo: any signed-in operator can run/reset the scenario for everyone. | Single demo instance. | Per-session sandboxes or read-only demo accounts. |
| **Single process, single thread** (about 300,000 fixes/s on one core in the benchmark). | Simplicity: one writer, no locks. | Partition users across worker threads by user id (users are independent). |
| **Late fixes are recorded, not re-evaluated.** | Re-evaluating would require rewinding each zone's state machine and replaying. | Per-user event-sourced state with replay. |
| **A phone judges zone validity on its own clock** while offline. | It has no other clock; skew is corrected on reconnect. | Signed time from the server when online; validity margins. |
| **Polling, no push notifications**; no phone app. | Simplicity. | WebSockets/SSE; a mobile app running the same evaluator from the shipped geohash bytes. |
| **Security open items:** no in-process TLS (relies on the proxy), PBKDF2 not Argon2, throttles per process, no external penetration test. | Scope. | TLS, Argon2id, shared rate-limit store, external review. |
| **Library modules not wired in:** jurisdiction hierarchy, QR digital ID, Kuhn's matching, k-NN. | Built and tested; no page uses them yet. | Resolve each alert's owning district with `jurisdiction::Hierarchy`. |
| **No browser-automation tests.** | Checked by hand and scripts. | Playwright/Selenium suite. |

## 2. Answers ready for "why does this limitation exist?"

- **"Why no database?"** — "The engine's job is per-fix computation; I kept state
  in memory so every structure is my own and the behaviour is deterministic and
  testable. The consequence is no persistence, which I document. The next step is
  a write-ahead log replayed on start — the event log is already append-only and
  Merkle-hashed, so it is the natural thing to persist."
- **"Why simulated people?"** — "Tracking real people needs consent, a privacy
  notice and a data-protection review. The system enforces opt-in and audits
  every location read, but I did not have a lawful basis to collect real traces.
  The geography and every algorithm are real."
- **"Why single-threaded?"** — "One writer means no locks and no races. Users are
  independent, so the scaling path is to shard users across threads, one writer
  per user. At about 3 µs per fix a single core handles about 300,000 fixes per
  second, far beyond the demo."
- **"Your quadtree is O(n) in the worst case — isn't that bad?"** — "Yes, on
  adversarial data. That is why the bound I quote is *expected* O(log n + k), and
  why brute force stays as the oracle. The benchmark also shows the k term: at
  100,000 zones in one district about 98 results per query bound the speedup."
- **"Why is removal O(n)?"** — "I did not keep an id → node map. Adding a hash map
  from zone id to leaf would make it O(log n). Zone edits are rare compared with
  fixes, so I prioritised queries."

## 3. Future scope (prioritised)

1. Persistence (write-ahead log, signed Merkle roots).
2. Field evaluation on real recorded traces; tune hysteresis per zone type.
3. Mobile app running the same offline evaluator (geohash bytes already ship).
4. O(log n) removal (id → leaf map); Hilbert-packed R-tree; centred interval tree.
5. Travel-time routing; contraction hierarchies for city-scale graphs.
6. Wire in the jurisdiction hierarchy for alert ownership.
7. Multi-threading by user shard; grid-bucketed cohesion.
8. Security: TLS in process, Argon2id, per-user quotas, external review.
