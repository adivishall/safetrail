# Concepts beyond the Data Structures syllabus

Each concept below is something the code really does. For each: what it is,
why it goes beyond a first data-structures course, the practical problem it
solves here, and where to find it.

| Concept | Where | Problem it solves |
|---|---|---|
| Computational geometry | `src/geo/` | is a fix inside a polygon; is a polygon valid |
| Coordinate systems and projections | `src/geo/projection.cpp`, `geo::distance_m`, Web Mercator in the web map | distances in metres from degrees |
| GPS uncertainty modelling | `geo::Containment`, `sim::GpsErrorModel` | a fix is a disc, not a point |
| Hysteresis | `src/fence/hysteresis.cpp` | drift should not flap a boundary |
| State machines | hysteresis phases; alert and incident life cycles | each transition reported once |
| Persistent data structures | `index::VersionedIndex` | every past version, cheaply |
| Temporal (bitemporal) history | `TrailPoint::recorded_ms`, `Platform::position_at` | what happened vs what the system knew |
| Lamport clocks, offline-first reconciliation | `sync/`, `Platform::go_offline/reconnect` | ordering events from phones with wrong clocks |
| Cryptographic integrity | SHA-256, Merkle proofs (`evidence/`) | proving the log was not edited |
| Password hashing and session security | `app/crypto.cpp`, `app/api.cpp` | accounts that resist offline guessing and CSRF |
| Event-driven, single-writer architecture | `net::Server::run`, `app::Platform` | one total order, no locks |
| Deterministic replay | `-ffp-contract=off`, seeded RNGs, total-order tie breaks | the same run gives byte-identical evidence |
| Spatio-temporal correlation | `alert::Correlator` | forty alerts at one landslide are one incident |
| Battery-aware computation | `power::AdaptiveSampler` | sample less often far from hazards |
| Performance engineering and benchmark methodology | `apps/safetrail_bench.cpp` | numbers that survive scrutiny |
| Differential and mutation testing | `tests/index/differential_test.cpp`, `tools/mutation_check.py` | evidence that the tests can catch bugs |
| Numerical tolerance | `geo::` on-edge tolerance, `-ffp-contract=off` | floating point flips inside/outside |

---

### Computational geometry
**What:** ray casting (count crossings of a ray with polygon edges; odd = inside), the winding number (an independent second answer used as an oracle), segment intersection with orientation tests, and the Shamos–Hoey sweep line that detects a self-intersecting polygon in O(V log V) with its own AVL status tree.
**Why beyond the syllabus:** it needs robustness reasoning — collinear and touching edges, vertices exactly on the ray, holes.
**Here:** every candidate zone is tested exactly after the index prunes; zone creation and import refuse self-intersecting polygons, so the store never holds geometry that would make ray casting return arbitrary results. [../GEOMETRY_EDGE_CASES.md](../GEOMETRY_EDGE_CASES.md) lists the edge cases and tolerances.

### Three-valued containment under GPS uncertainty
**What:** a fix with accuracy r is a disc. If the whole disc is inside → *inside*; wholly outside → *outside*; straddling → *uncertain*.
**Here:** `geo::evaluate(poly, fix)` returns all three; the platform raises *uncertain* warnings for serious zones. A real finding from the OSM data: the river and cliff polygons are only 23–40 m deep, so with a 15 m entry margin and 5–18 m fixes "uncertain" is often the honest answer.

### Hysteresis and state machines
**What:** a Schmitt trigger — enter only 15 m inside, exit only 25 m outside, three agreeing fixes, a 5 s minimum dwell; an *ambiguous* phase for uncertain readings.
**Problem:** GPS drift near a boundary otherwise produces an enter/exit pair every few seconds. The A/B benchmark (section 10) measures the filter removing the flood while keeping the real transitions.

### Persistent data structures and structural sharing
**What:** data structures whose old versions remain valid after updates (Driscoll, Sarnak, Sleator and Tarjan, 1989). Path copying shares every untouched node.
**Here:** each zone change is a new version of the quadtree; the sharing ratio is measured in section 9 and shown live on the Analytics page.

### Bitemporal history
**What:** two times per fact — when it happened (valid time) and when the system recorded it (transaction time).
**Here:** a fix synced from an offline phone happened at 10:02 but arrived at 10:12. The Investigation page answers "what did the system know at 10:05?" using both, so it never shows knowledge the system did not yet have (`tests/app/scenario_test.cpp` asserts this).

### Lamport logical clocks and offline reconciliation
**What:** counters that respect causality without trusting wall clocks (Lamport, 1978). Local event: counter + 1. Receive: max(own, received) + 1. Order: (counter, device).
**Here:** an offline phone keeps evaluating zones locally from an index shipped as bytes, queues Lamport-stamped events, and on reconnect the server's reconciler admits each event once (retries are rejected by a hash-set lookup), orders by Lamport stamp, estimates and corrects the clock skew, and adopts the phone's engine state so no transition is repeated. The offline scenario demonstrates a phone 7 minutes fast.
**Finding:** a phone with a wrong clock judges zone validity windows on that wrong clock — recorded as a limitation.

### Cryptographic integrity: SHA-256 and Merkle proofs
**What:** a Merkle tree over an append-only log (RFC 6962): any entry can be proven present with O(log n) hashes against a published root; editing any entry changes the root.
**Here:** every platform log entry is a leaf. Entries carry ids, never coordinates, so erasing a person's movement history (a privacy right) never conflicts with the evidence log. Verified independently in the browser (WebCrypto) and in `tools/api_e2e.py` (Python hashlib).

### Password hashing and session security
**What:** PBKDF2-HMAC-SHA256 (RFC 8018) with a random salt and 120,000 iterations; HMAC per RFC 2104; random 256-bit session tokens from the OS; only the SHA-256 of a token is stored; constant-time comparisons. Checked against the RFC test vectors.
**Why beyond:** security engineering, not data structures — but it uses the project's own SHA-256.

### Single-writer event loop
**What:** one thread multiplexes sockets with `poll(2)` and interleaves the simulation clock. Every mutation happens in one total order with no locks.
**Trade-off:** no parallelism; measured throughput is still about 300,000 fixes per second on one core (section 19).

### Deterministic replay
**What:** the same seed must give byte-identical output on Apple clang and Linux g++. Fused multiply-add contraction is disabled (`-ffp-contract=off`); every sort that a cap cuts through has a total-order tie-break; scenarios are seeded.
**Here:** CI compares the dashboard built on macOS and Linux byte for byte; `tests/app/scenario_test.cpp` asserts identical event logs and Merkle roots across runs.

### Spatio-temporal correlation
**What:** connected components of alerts under "within 200 m and 10 minutes" (single linkage), via union-find; new alerts near an open incident join it.
**Here:** a breach and its dwell overrun are one incident; an injured hiker's stationary and straggler alerts are one incident.

### Battery-aware computation
**What:** the sampling interval depends on distance to the nearest zone (2 s under 200 m … 5 min beyond 5 km), forced to full rate while a serious alert is open.
**Here:** the evaluator feeds the sampler the distance it already computed; a battery-aware device in the platform sends fixes only when due, and the signal-lost detector stretches its threshold to 2.5× the expected interval so silence by design is not mistaken for a lost signal. The energy figure is a stated linear model, not a measurement.

### Benchmark methodology, differential and mutation testing
See [TESTING_AND_BENCHMARKS.md](TESTING_AND_BENCHMARKS.md): interleaved rounds, paired ratios, checksums as correctness gates, oracles for every fast structure, invariant audits after every operation, and 23 injected bugs that the suite must catch.
