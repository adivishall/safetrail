# Individual contribution

> Fields marked **[fill in]** are for the student to complete. Nothing in this
> document invents institutional details, approvals or co-authors.

| | |
|---|---|
| Student | Adi Vishal (GitHub `adivishall`) |
| Roll number | [fill in] |
| Programme, semester | [fill in] |
| Course | Data Structures — course project |
| Institution | [fill in] |
| Guide / faculty | [fill in] |
| Repository | https://github.com/adivishall/safetrail |
| Origin of the problem | Smart India Hackathon 2025, problem statement SIH25002 (Ministry of DoNER) |
| Period | 17 August 2026 – 30 October 2026 (111 commits, all under the author's name) |

## Work carried out

| Area | What was built | Where |
|---|---|---|
| Problem analysis | study of existing implementations and eleven functional gaps | `docs/GAP_ANALYSIS.md` |
| Spatial indexes | brute force, quadtree, R-tree with quadratic split and STR bulk loading, geohash, k-d tree; one interface | `src/index/`, `include/safetrail/index/` |
| Temporal structures | AVL interval tree; persistent quadtree with path copying | `include/safetrail/ds/interval_tree.hpp`, `src/index/versioned_index.cpp` |
| Supporting structures | binary heap, open-addressing hash table, timer wheel, ring buffer, rollback union-find | `include/safetrail/ds/`, `src/ds/` |
| Geometry | point in polygon under uncertainty, polygon validation by sweep line, projections | `src/geo/` |
| Geofencing engine | the nine-step evaluator, hysteresis, prediction, reconciliation | `src/fence/` |
| Graphs and dispatch | road graph from OpenStreetMap, Dijkstra, A*, Hungarian assignment | `src/graph/`, `src/dispatch/` |
| Alerts, groups, sync, evidence, power | correlator, escalation, triage, cohesion, Lamport sync, Merkle log, adaptive sampler | `src/alert/`, `src/group/`, `src/sync/`, `src/evidence/`, `src/power/` |
| Platform | users, consent, sessions, multi-user tracking, incidents workflow, responders, zone management, history, offline devices, scenarios | `src/app/` |
| Server and web app | HTTP server, API, authentication, operations console, Data Structures Lab | `src/net/`, `src/app/api.cpp`, `web/app/` |
| Evaluation | benchmark harness (19 sections), differential, golden, platform, API and HTTP tests, mutation testing | `apps/safetrail_bench.cpp`, `tests/`, `tools/` |
| Data | OpenStreetMap extraction of hazard zones and roads around Shillong | `tools/osm_to_zones.py`, `tools/osm_to_roads.py`, `data/` |
| Documentation | design, results, testing, defect log, review and final academic material | `docs/` |

The commit history (`git log`) records the order in which this work was done.

## Tools used

C++17 (Apple clang 17, GCC 13 in CI), make and CMake, Python 3 (standard
library; python-pptx and matplotlib for the deck and figures), Git and GitHub
Actions, OpenStreetMap data via Overpass, Chrome for testing and PDF export.

Other assistance used, to be declared as your institution requires: **[fill in]**.

## Declaration

[fill in, in the wording your institution requires]

Signature: ______________________ Date: ______________
