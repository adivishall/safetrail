# Limitations and future scope

Stated plainly, so that nothing on the slides claims more than the code does.

## Limitations of the data and the evaluation

1. **Simulated people, real geography.** Hazard zones and roads are real
   OpenStreetMap data around Shillong, Meghalaya; every tourist is simulated.
   No field trial has been run and no real GPS trace has been evaluated.
2. **The OSM hazard polygons are thin.** The rivers ("Deep Water") are at most
   25–40 m deep and the Sonapani cliff about 23 m. With a 15 m entry margin and
   5–18 m GPS accuracy, a person is rarely *confirmed* inside them; the engine
   correctly reports "uncertain". The entry scenario therefore uses a larger
   restricted closure registered by the administrator, labelled SIM.
3. **Benchmarks come from one laptop** (Apple M4). Ratios transfer better than
   absolute times, and even ratios move between sessions — the R-tree's
   100,000-zone ratio has ranged roughly 100–235×. They are quoted as ranges.
4. **The battery figure is a model** (energy proportional to fixes taken,
   1 fix/s = 10 %/hour), not a measurement on a phone.
5. **No travel-time estimates.** The road file has segment lengths, not speeds
   or road classes, so routes report distance only.

## Limitations of the engine

6. **Removal is O(n)** in every spatial index (no id → node map).
7. **Worst-case queries are O(n)** for the quadtree and R-tree on adversarial
   data; the bounds are expected, not guaranteed.
8. **Tolerance-based geometry**, not exact predicates (on-edge tolerance ~0.1 mm).
9. **No antimeridian support**: zones crossing ±180° are refused.
10. **A GPS jump inflates the query radius** for about a minute (the speed
    window), reducing pruning for that person — correct, but slower.
11. **Prediction is a straight-line extrapolation** over at most 5 minutes.

## Limitations of the platform

12. **In-memory only.** Accounts, zones, alerts and the evidence log are lost
    when the server stops. A production system needs a durable store and signed,
    published Merkle roots.
13. **Single process, single thread.** About 300,000 fixes per second on one
    core is far beyond the demo, but there is no horizontal scaling, no
    replication and no failover.
14. **Late fixes are recorded, not evaluated.** Re-evaluating them would mean
    rewinding the hysteresis state machine; an event-sourced design could
    replay a user's window instead.
15. **A phone judges zone validity on its own clock.** A phone whose clock is
    wrong may treat a zone as in force too early or too late while offline;
    hysteresis timers handed back from a phone whose clock was fast confirm
    late by up to the skew. Server-side skew correction applies only to the
    events, after reconnect.
16. **One Dijkstra gives road distance to every responder** only because the
    road graph is undirected; a directed graph (one-way streets) would need a
    reverse-graph search.
17. **Group cohesion is O(g²)** per group per pass (all pairs); fine for tour
    parties, not for crowds. A grid bucket would make it O(g) expected.
18. **No push notifications.** The console polls once a second; tourists have
    no phone app — the "simulated phone" on the tourist page stands in for one.
19. **Security** — see the open items in [SECURITY_AND_PRIVACY.md](SECURITY_AND_PRIVACY.md)
    (no in-process TLS, PBKDF2 not Argon2, no external penetration test).

## Future scope

| Area | Next step |
|---|---|
| Field data | evaluate on recorded GPS traces with ground truth; tune hysteresis margins per zone type |
| Persistence | write-ahead event log replayed on start; durable Merkle log with signed roots and consistency proofs published to responders |
| Mobile | a phone app that runs the same evaluator offline from the shipped geohash bytes (the platform already simulates exactly this) |
| Scale | partition users across worker threads by user id (users are independent), keeping one writer per user; grid bucketing for cohesion |
| Indexes | id → leaf maps for O(log n) removal; Hilbert-ordered R-tree packing; a centred interval tree for O(log n + k) stabbing |
| Routing | travel-time weights from road classes; contraction hierarchies for city-scale graphs; multi-objective dispatch (time, unit type) |
| Jurisdiction | resolve each alert's owning district through the existing `jurisdiction::Hierarchy` (tested, not yet wired in) |
| Privacy | differential-privacy aggregates for analytics; per-zone consent scopes |
| Security | TLS, Argon2id, per-user request quotas, external security review |
