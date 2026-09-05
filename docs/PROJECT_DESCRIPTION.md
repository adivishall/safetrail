# Project Description

**Title:** safetrail — A Geofencing and Incident-Response Engine for Tourist
Safety in Low-Connectivity Terrain

**Course:** Data Structures — Course Project
**Repository:** https://github.com/adivishall/safetrail
**Problem origin:** Smart India Hackathon 2025, problem statement `SIH25002`
(Ministry of Development of North Eastern Region)

---

## Short abstract

Tourist safety in remote terrain depends on knowing, in real time, when someone
has entered a restricted border area, a landslide-prone stretch, or a zone with no
mobile coverage. This project implements the computational engine behind such a
system: a hand-written spatial indexing and computational geometry layer that
evaluates a population of tracked positions against a large set of time-varying
hazard zones, raises and prioritises alerts, and dispatches the nearest responder
over a road network.

Existing implementations of this problem delegate every non-trivial computation to
external libraries — containment testing to a PostGIS database, identity records to
an Ethereum contract, anomaly detection to a pre-trained model. This project
inverts that: we implement the engine itself. That choice gives the project genuine
data structure content, and it also removes the architectural dependency on a live
server connection, which is the precise failure mode in the terrain the problem
targets.

## Objective

Evaluate up to 200 tracked positions against up to 100,000 time-varying zones at
10 Hz, on commodity hardware, with output provably identical to a brute-force
reference implementation.

## Scope

- Point-in-polygon containment with support for concave rings and holes
- Three-valued containment that accounts for GPS measurement uncertainty, rather
  than reporting a binary answer the data cannot support
- Predictive alerting — estimated time to boundary crossing, not only entry
- Zones with time-varying validity, queryable at any past instant for incident
  investigation
- Group cohesion and straggler detection via connected components
- Correlation of many related alerts into a single incident
- Fully offline evaluation on-device, with logical-clock reconciliation on
  reconnect
- Battery-aware sampling driven by proximity to risk
- Tamper-evident incident log with offline-verifiable inclusion proofs
- Responder assignment and routing

## Data structures implemented from scratch

Quadtree · R-tree (quadratic split and STR packing) · geohash / Z-order curve ·
k-d tree · persistent path-copying quadtree · interval tree · union-find with
rollback · binary heap · circular buffer · open-addressed hash table · Merkle tree
· adjacency-list graph · timer wheel

No standard-library associative containers, no external geometry or spatial
libraries. `std::vector` is used only as raw contiguous storage.

## Algorithms

Ray casting and winding number for containment (implemented independently and
cross-validated against each other) · circle–polygon and segment–polygon
intersection · Shamos–Hoey sweep line for self-intersection detection ·
Douglas–Peucker simplification · Dijkstra and A* · Kuhn's and Hungarian bipartite
matching · Lamport logical clocks

## Evaluation

The project is evaluated on measured results, not feature count. A brute-force
implementation is retained permanently behind the same interface as every optimised
one, serving both as the correctness oracle and as the baseline in every
performance comparison.

**Primary target.** Naive evaluation costs O(T · Z · V) — approximately 8 × 10⁸
operations per tick at the stated scale, which is not feasible at 10 Hz. With
spatial indexing and bounding-box rejection the target is O(T · (log Z + k · V)),
approximately 2.7 × 10⁴ operations, a reduction of roughly four orders of
magnitude with identical output.

**Secondary measurements.** Query time and pruning effectiveness across all four
index implementations as zone count scales from 10 to 100,000; false-alert
reduction from hysteresis under injected GPS noise; battery projection against
fixed-interval polling at matched alert recall; alert-to-incident compression
ratio; structural sharing in the persistent index.

## Deliverables

Engine library · headless simulator with deterministic replay · operator dashboard
with live map and diagnostics overlay · benchmark suite producing the measurements
above · unit tests per data structure and end-to-end golden replays · design
documentation

## Implementation

C++17, no external dependencies in the core. Approximately 12 phases across the
semester, sequenced so that a correct end-to-end path exists early and each
subsequent phase replaces a slow component with a fast one.
