# Gap Analysis — What Existing Tourist Safety Systems Miss

Research basis for every feature in this project that is not in the original
problem statement. Nothing here is speculative: each gap is either documented in
published work on `SIH25002`, visible in a shipped implementation, or measured in
the geofencing literature.

---

## 1. What everyone already built

Multiple teams have implemented `SIH25002`. The implementations converge on a
near-identical shape.

**SafeVoyage** ([GitHub](https://github.com/Anoint2612/tourist-safety)) — a
representative shipped implementation:

| Component | Technology |
|---|---|
| Mobile | React Native, TypeScript, Redux |
| Backend | Node.js, Express, MongoDB |
| Dashboard | React.js |
| **Geospatial** | **FastAPI + PostgreSQL/PostGIS** |
| **Blockchain** | **Hardhat (Ethereum)** |
| Notifications | Twilio SMS, WebSockets |

Features: SOS trigger, multilingual e-FIR filing, admin dashboard, inspector
assignment, geofence breach notifications, role-based access control, "offline-first
with cached sessions and retry logic", ML anomaly detection.

**STSMIRS** ([published paper](https://ijirt.org/publishedpaper/IJIRT202990_PAPER.pdf))
— the academic version, six modules: blockchain ID, tourist mobile app, command
dashboard, LSTM anomaly detection (91.4% precision in simulation), optional IoT
wearable, automated e-FIR. Future work lists ABHA health record integration,
federated learning, and SATCOM chipsets.

### The exploitable pattern

**Every one of these delegates the actual computation.**

Containment testing is a `ST_Contains` call to PostGIS. The spatial index is
PostGIS's GiST tree, written by someone else. The tamper-evident ID is an
Ethereum smart contract. The anomaly detection is an imported LSTM. What remains
is a well-built CRUD application wrapped around four libraries.

This has two consequences, and both are ours to take:

1. **For a data structures course it is the wrong project.** There is no
   structure to analyse — the interesting work happens inside PostGIS.
2. **It creates real functional gaps**, because delegating to a server-side
   spatial database forces architectural choices that break in the exact terrain
   this problem statement targets.

Our position: **we build the engine they import.** Every gap below follows from
that inversion.

---

## 2. The gaps

### Gap 1 — Position is treated as a point. It isn't.

GPS is accurate to **3–5 m in open sky**, but **20–50 m error from multipath
reflections** in dense or steep terrain, and one documented pilot lost **12.7% of
GPS signal** to urban canyon effects ([Radar](https://radar.com/blog/how-accurate-is-geofencing)).
Northeast India is deep valleys and dense forest — the bad case, not the good one.

Every existing system asks `is_inside(point, polygon)` and gets back `true` or
`false`. A tourist 10 m outside a restricted border zone with 30 m position
uncertainty is not outside it. The honest answer is *unknown*.

**What we add:** position carries an accuracy radius, and containment is
three-valued — `Inside`, `Outside`, `Uncertain`. Requires circle–polygon
intersection rather than point-in-polygon, and the operator UI gets a genuinely
useful third state instead of a coin flip presented as a fact.

**Structures:** circle–polygon intersection, nearest-edge distance query.

---

### Gap 2 — Alerts fire on entry, which is already too late

Universal across existing systems: the alert is raised when the breach has
happened. For a restricted border zone or an active landslide slope, an alert
that a tourist is **four minutes from entering at current heading and speed** is
a different product from one that says they are already in.

**What we add:** trajectory extrapolation with time-to-boundary estimation.
Predictive `Approaching` alerts alongside reactive `Entered` alerts.

**Structures:** segment–polygon intersection, ray casting against edges for
first-crossing time, priority queue keyed on time-to-impact.

---

### Gap 3 — Zones are static. Risk is not.

Every implementation stores a fixed polygon set. But a river crossing is safe in
dry season and lethal after rain; a mountain road is fine at noon and closed at
night; a border buffer has different rules on different days.

Worse, a static store cannot answer the one question an incident investigation
needs: **what were the zone rules at the moment this happened?** Overwriting a
polygon destroys the evidence.

**What we add:** zones carry validity intervals and a version history. The
spatial index is queryable at any past timestamp.

**Structures:** interval tree over the time dimension, **persistent/versioned
spatial index** (path-copying quadtree). This is the most advanced structure in
the project and it exists for a concrete reason, not for show.

---

### Gap 4 — Tourists are tracked individually. They travel in groups.

No existing system models group structure. But the signal that matters is rarely
absolute position — it is **separation**. A tourist 800 m behind their group and
falling further behind is the incident, and they may be nowhere near a zone
boundary.

**What we add:** groups as connected components under a proximity threshold.
Detect component splits, straggler emergence, and group fragmentation as
first-class alert types.

**Structures:** dynamic connectivity — union-find with rollback, since groups
split as well as merge and plain DSU cannot un-merge.

---

### Gap 5 — One event, forty alerts

A landslide near a viewpoint with forty tourists produces forty separate alert
cards in every existing dashboard. The operator is now the bottleneck, and alert
fatigue is a documented cause of missed real incidents.

**What we add:** spatio-temporal correlation. Alerts close in space and time
collapse into a single **Incident** with forty affected people, one card, one
dispatch decision.

**Structures:** union-find over a space-time proximity graph, grid-bucketed
candidate generation.

---

### Gap 6 — "Offline-first" that isn't

SafeVoyage documents "offline-first capabilities with cached sessions and retry
logic." That is **request queuing**, not offline operation. If the device cannot
reach PostGIS, it cannot evaluate a single zone. The published paper concedes the
point — its proposed fix for connectivity is *satellite hardware*
(Iridium 9603N).

There is a software fix nobody applied: **ship the spatial index to the device.**
A serialised quadtree over a district's zones is small. Evaluate locally, queue
events, reconcile on reconnect.

The reconciliation is the interesting part. Events arrive from multiple
disconnected devices with unreliable wall clocks, and you need a defensible
ordering.

**What we add:** compact serialisable index, local evaluation, store-and-forward
event log, and logical-clock ordering on merge.

**Structures:** index serialisation format, **Lamport timestamps**, merge with
deterministic tie-breaking.

*Also worth knowing:* iOS Core Location caps a single app at **20 monitored
regions** ([Radar](https://radar.com/blog/limitations-of-ios-geofencing)). Any
system leaning on OS geofencing is hard-capped at 20 zones. Ours has no such
ceiling — a point in our favour worth stating in the report.

---

### Gap 7 — Continuous GPS polling, which nobody can afford

Measured: **continuous GPS polling drains 8–12% of battery per hour**
([Glance](https://thisisglance.com/blog/geofencing-done-right-7-mistakes-that-kill-battery-life)).
A tourist on a full-day trek with a dead phone at 3pm is less safe than one who
was never tracked.

Existing systems poll at a fixed interval. But a tourist 20 km from any zone
needs a fix every five minutes; one 50 m from a restricted boundary needs one
every five seconds.

**What we add:** sampling rate as a function of distance to the nearest relevant
boundary, computed from the spatial index. Report projected battery life against
fixed-interval polling — a clean, quantified result.

**Structures:** nearest-boundary distance query, per-device rate controller.

---

### Gap 8 — Boundary flapping treated as an unavoidable false positive

Documented plainly: a 10 m geofence "will fire constantly if GPS drift nudges a
parked tracker in and out of the boundary all day." Existing systems list false
positives as a limitation and move on.

**What we add:** hysteresis as an explicit state machine — separate enter and
exit thresholds, N-consecutive-confirmation requirements, and a dwell minimum
before an alert is raised. Then **measure it**: false alert rate with and without
hysteresis, on the same replay with injected GPS noise. That is a real
experimental result.

**Structures:** per-tourist-per-zone state machine, confirmation counters over
the ping ring buffer.

---

### Gap 9 — Ethereum for a tamper-evident log

Hardhat and a smart contract to prove an ID record was not altered. This brings
gas costs, block latency, and a hard dependency on connectivity — into a system
whose defining constraint is that connectivity is absent.

The requirement is tamper-evidence. That is an **append-only Merkle log with
inclusion proofs**: O(log n) proof size, verifiable offline, no network, no
chain, about 200 lines.

**What we add:** hand-written Merkle log covering both digital IDs and the
incident event stream, so an investigation can prove the log was not edited after
the fact.

**Structures:** Merkle tree, inclusion proof generation and verification.

**Status: ✅ built.** RFC 6962 tree with SHA-256 implemented from scratch (checked
against NIST vectors), O(log n) inclusion proofs, and consistency proofs verified
across all prefix pairs. Wired into the demo: every run commits its event stream to
a Merkle root and verifies a sample inclusion proof. See `src/evidence/`.

---

### Gap 10 — Zone authoring with no validation

Operators are police and tourism staff, not GIS analysts. They will draw
self-intersecting polygons, near-duplicate overlapping zones, and 400-vertex
shapes traced by hand. No existing system validates any of this, and a
self-intersecting polygon makes ray casting return arbitrary results.

**What we add:** authoring-time validation — self-intersection detection,
degenerate geometry rejection, overlap warnings, and vertex-count reduction with
an accuracy bound.

**Structures:** **Bentley–Ottmann sweep line** for segment intersection,
Douglas–Peucker simplification.

---

### Gap 11 — Nobody owns the alert when a tourist crosses a district line

Tourists cross administrative boundaries constantly. Meghalaya to Assam is a
different police jurisdiction, a different control room, a different responder
pool. No existing system models this, so alerts silently belong to whoever was
watching.

**What we add:** nested administrative boundary hierarchy, automatic jurisdiction
resolution, and explicit alert handoff with an audit entry in the Merkle log.

**Structures:** polygon containment hierarchy (nesting tree), point-to-region
resolution.

---

## 3. What we deliberately do not build

Scope discipline matters more than feature count. Existing systems have these; we
skip them on purpose, and the reason is defensible in a viva.

| Not building | Why |
|---|---|
| Mobile app | Simulated tourists give reproducible, replayable scenarios. A real app adds zero data structure content and a month of work. |
| e-FIR workflow | Forms and paperwork routing. No algorithmic content. |
| LSTM anomaly detection | A black box we cannot analyse or defend. We do geometric and statistical anomaly detection instead — inspectable, explainable, and with stated complexity. |
| Real blockchain | Merkle log gives the actual property required (see Gap 9) with none of the cost. |
| Twilio / SMS | Integration plumbing. |
| Auth and RBAC | Standard web engineering, irrelevant to the course. |
| PostGIS | **This is the entire point.** We implement the spatial index. |

---

## 4. What this buys us

| | Existing implementations | This project |
|---|---|---|
| Containment | `ST_Contains`, binary | Own engine, three-valued with uncertainty |
| Spatial index | PostGIS GiST | Quadtree, R-tree, geohash — all three, benchmarked |
| Zone capacity | 20 (iOS) or server-bound | Unbounded, measured to 100k |
| Time model | Static polygons | Versioned, queryable at any past instant |
| Alerting | Reactive on entry | Predictive time-to-boundary + reactive |
| Social model | Individuals | Groups via dynamic connectivity |
| Alert volume | One per tourist per event | Correlated into incidents |
| Offline | Request queuing | Local evaluation + logical-clock merge |
| Power | Fixed interval | Risk-adaptive sampling |
| Drift | Listed as a limitation | Hysteresis, with measured reduction |
| Tamper evidence | Ethereum | Merkle log, offline-verifiable |
| Zone validation | None | Sweep-line self-intersection detection |

Eleven added capabilities, every one traceable to a documented gap, and every one
carrying data structure content that a course project can be graded on.

---

## Sources

- [SafeVoyage implementation](https://github.com/Anoint2612/tourist-safety)
- [STSMIRS published paper (IJIRT)](https://ijirt.org/publishedpaper/IJIRT202990_PAPER.pdf)
- [Geofencing accuracy in practice — Radar](https://radar.com/blog/how-accurate-is-geofencing)
- [iOS geofencing limitations — Radar](https://radar.com/blog/limitations-of-ios-geofencing)
- [Battery cost of geofencing — Glance](https://thisisglance.com/blog/geofencing-done-right-7-mistakes-that-kill-battery-life)
- [Geofencing methodology and challenges — Behavior Research Methods](https://link.springer.com/article/10.3758/s13428-023-02213-2)
