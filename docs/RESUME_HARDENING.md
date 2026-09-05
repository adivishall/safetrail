# Resume Hardening — Flaws and Before/After

A judged critique of this project (as if by a demanding data-structures examiner,
for a resume context) and the tier-by-tier record of what was fixed. Each tier is
committed and pushed separately; this file is the running before/after ledger.

The flaws are grouped by how badly they would hurt under interview questioning:
**Tier 1** = credibility killers (a reviewer catches these first), **Tier 2** =
substance/rigor, **Tier 3** = positioning for a resume, **Tier 4** =
correctness and invariant hardening (what an examiner finds when they stop reading
the docs and start reading the code), **Tier 5** = claim–code alignment (what is
left once the invariants hold: complexity claims that are true in general but not
for this project's data, a module built and never called, a reference
implementation that answers a different question from the thing it references).

**Tiers 4 and 5 are not victory laps.** Each records a later, deeper audit that
found real defects the earlier ones had not looked for — tier 4 in the project's
flagship structure, tier 5 in a complexity claim and in a brute-force oracle that
had been quietly invalid for as long as it existed. Nothing in the earlier tiers
was wrong about what it fixed; each was simply looking at a different layer.

**How to read this file.** Every tier's table is a dated record of what was true
*then*. The current-state numbers are the ones in the most recent tier and in
`README.md`; earlier figures ("285 assertions across 28 files") are history, not
claims, and are left in place because the point of the ledger is the trajectory.

---

## Tier 1 — Credibility killers  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 1 | **Claim contradicts code** | `TEAM_BRIEF.md` advertised the hash table as *"Robin Hood probing"*; the implementation is linear probing with tombstones. | Claim corrected to *"linear probing, tombstone deletes."* No `Robin Hood` reference remains anywhere in the repo. |
| 2 | **Vanity test metric** | *"10,941 checks across 27 files"* — a runtime count of `t::ok` calls inflated ~40× by fuzz loops; two randomized tests supplied 79% of it. Docs also disagreed with themselves (222/11, 233/12, 117). | Every current-state doc now reads *"285 assertions across 28 files, each fast structure vs a brute-force oracle."* One honest number, consistent everywhere. |
| 3 | **Flagship feature silently broken; tests didn't catch it** | Alert correlation (GAP 5) clustered alerts only *within one tick* — it never merged into an open incident, so the running product compressed ~1.19:1 while the pitch is "40→1." The unit tests only ever called `ingest()` once, so they passed. | Correlator now keeps incidents open across their time window and folds nearby alerts in. Added **3 cross-tick unit cases** + a new **`golden/incident_formation_test`** that drives the whole simulator and asserts the mass incident forms (≥15 people, ≥20:1) and does **not** form on a scattered run. Both fail against the old batch-only code. |
| 4 | **Red CI on `main`** | `priority_queue_test` was missing `<string>`/`<cstdint>` — passed under Apple clang, failed under GNU g++ on the runner, failing every Pages deploy. | Includes added; the other 27 tests confirmed to pull those headers transitively (CI had already compiled them). Suite green, deploy unblocked. |

**Measured after Tier 1:** suite green — 285 assertions / 28 files / 0 failures.
The new integration test records **33-person mass incident, 453:1 compression**
with the scripted scenario on, versus **2 people, 9.2:1** with it off — so GAP 5
is now guarded by a test of the real pipeline, not a fixture.

*Note:* the correlator cross-tick fix and the scripted "incident day" that make
those numbers real landed just before this tier (see
[WORKLOG](WORKLOG.md) — "Scripted incident day + persistent alert correlation").

---

## Tier 2 — Substance / rigor  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 5 | **Headline results are scenario-engineered** | 453:1 compression and the Hungarian-vs-greedy gap depend on a hand-placed cohort; a neutral run was degenerate. Not stated. | Every results table now labels correlation/dispatch **scenario-dependent** and reports **both** numbers: ~450:1 on a clustered incident, **~9:1 scattered**. The distinction is guarded by `golden/incident_formation_test`. |
| 6 | **No benchmark rigor** | Scaling numbers were a single timed pass, no warmup, no repeats, no variance; "33×" had no error bars. | `time_queries` now does a **warmup pass + median of 7 timed passes**, and reports **best run + spread** (printed and in the CSV). At 100k the spread is ±~5%; small-n rows show ±30–230% and are flagged as noise. Docs state "single machine, ratio to our own brute force — no external baseline." |
| 7 | **All data simulated** | Every "impact" number is synthetic under our own noise model; disclosed in places, but impact phrasing survived. | README results block and PRESENTATION now say "simulated GPS / simulated tourists" inline next to each figure; the ratio-to-own-brute-force caveat makes the internal baseline explicit. |
| 8 | **Worst case cuts against the headline** | Benchmarked structure is O(n) worst case; only interval tree/heap guaranteed. Disclosed in DATA_STRUCTURES but summaries risked unqualified "O(log n)." | Verified: the structure tables already say **"O(log n + k) avg"**, DATA_STRUCTURES keeps a dedicated worst-case table, and PRESENTATION §10 ("Honest engineering") states the O(n) worst case outright. No unqualified claim remained; no change needed beyond confirming it. |

**Measured after Tier 2:** `make bench` green, correctness gates pass. Headline
holds under the stricter method — **100k: quadtree ~32×, R-tree ~35×, spread
±3–10%** — so "~33×" is now a figure with an error bar, not a lucky single run.

## Tier 3 — Positioning for a resume  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 9 | **Breadth reads as shallow** | 14 structures + 17 algorithms + simulator + dashboard + CI; an interviewer drills one. | Added the **Resume framing** section below: lead with depth (the persistent quadtree + the honest ceiling analysis), one disciplined bullet, and what *not* to quote. The README callout points evaluators at the graded core first. |
| 10 | **"No `std::`" reads as NIH** | Framed as pure rigor; a senior engineer reads it as poor production judgment. | README ground rule now states it is a **deliberate learning constraint for the course, not a production recommendation** — in real software you'd use `std::unordered_map` and a mature spatial library. |
| 11 | **Category inflation** | Most of the repo mass is systems/sim/viz, not data structures. | README "What this is" callout names the graded core (`geo/ index/ ds/ graph/`, ≈8k lines) and calls the simulator/dashboard/CI **scaffolding, not the deliverable**. |
| 12 | **Product framing oversells** | "The engine every team imports," "runs offline on a device" — reads as a deployable product. | README callout states plainly: **course project, not a shipped product; no mobile app, no live server; real geography, simulated people.** "on a device" softened to "locally, no server, in simulation." GitHub About/description rewritten to lead with the data-structures framing. |

---

## Tier 4 — Correctness and invariant hardening  ·  status: ✅ done

Tiers 1–3 audited what the project *said*. This tier audited what it *did*: every
structure's behaviour under deletion and churn, every serialisation format's
behaviour on malformed input, every place two layers could disagree about the same
question, and every asymptotic claim against the code implementing it.

Twelve defects, ordered roughly by how much they would cost under questioning.

### The flagship structure was answering the wrong question

| | |
|---|---|
| **Before** | `VersionedIndex` path-copied geometry, but zone validity lived in a mutable `std::vector<Validity>` indexed by `ZoneId`. `query_at(t)` fetched the correct historical quadtree root and then filtered its results with *whatever the validity is now*. |
| **Problem** | The one question the persistent index exists to answer — "what were the rules at 14:32 on the day of the incident?" — returned an answer that had never been true at any point in time. Edit a closure window today and every historical query about that zone silently changed. The structure was persistent in its geometry and amnesiac in its rules, which is worse than not being persistent at all, because it looks correct. |
| **Fix** | Validity became a **per-zone append-only log of `(version, Validity)` records**; lookup at a version is a binary search, O(log h) in that zone's own change count. The obvious alternative — snapshot the validity array per version — would have restored correctness while destroying the point (O(Z) copied state per mutation is exactly the O(n) full copy path copying exists to avoid). The interval tree now retains **every historical** interval and `active_at(t)` filters a stab by which record was in force at that version. Two time axes (transaction time → version, valid time → `Validity{from,to}`) are now named explicitly in the header. |
| **Test** | `tests/index/versioned_index_test.cpp` +22 assertions: original vs edited window, historical query before and after an edit, a query between two windows, four successive edits each visible only from its own version on, remove-then-re-add showing absence in the gap, and `active_at` never double-reporting a zone with overlapping historical intervals. Every one fails against the old implementation. |

### Serialisation lost information that changed answers

| | |
|---|---|
| **Before** | `RoadGraph::save_file` wrote one line per unordered node pair and `load_file` replayed it through `add_road()`. `ZoneStore::save_geojson` wrote name, severity and the outer ring. |
| **Problem** | `add_road()` inserts **both** directions and re-derives the weight from geometry, so a one-way street came back two-way and any weight that was not a distance came back as a different number — **shortest paths through a round-tripped graph differed from the original**. The zone writer silently dropped kind, dwell limit, validity window, jurisdiction, hysteresis margins and every hole, so a restricted night-closure zone with an exempt enclave reloaded as a plain caution zone. It also concatenated names into JSON unescaped, so a zone named `Nohkalikai "Falls"` produced a file its own loader rejected. |
| **Fix** | Road file **format v2**: one line per *directed* edge with its own weight, version-gated, with v1 still readable (and read as what v1 meant — undirected, derived weights). GeoJSON writer now emits every property the loader understands plus all holes, with `Json::escape()` for strings and `Json::number()` for doubles. |
| **Test** | `tests/graph/road_graph_io_test.cpp` (60 assertions) asserts direction, asymmetric weights, unchanged shortest paths, byte-identical re-save, v1 back-compat, and nine malformed-file rejections that leave the loaded graph intact. `tests/fence/zone_roundtrip_test.cpp` (30) checks every field, the hole, a name containing quote/backslash/newline/tab, byte-identical re-save, and that the emitted file is valid JSON by the parser's own strict rules. |

### The evaluator redefined the geometry it was applying policy to

| | |
|---|---|
| **Before** | `fence::Evaluator` called `geo::signed_distance_m()` and then re-derived Inside/Uncertain/Outside with its own threshold comparisons, duplicating `geo::evaluate()`. |
| **Problem** | Two copies of the containment semantics, only one of which the geometry tests pin. A policy layer may add hysteresis and dwell rules on top of a verdict; it may not quietly redefine the verdict. |
| **Fix** | One `classify()` in `containment.cpp` behind both `evaluate()` overloads; the evaluator calls the overload that also returns the signed distance it needs. |
| **Test** | `tests/fence/evaluator_agreement_test.cpp` cross-checks both overloads and the documented rule on 4,000 randomised (polygon, fix) pairs, and guards against a vacuous pass by requiring all three verdicts to occur. |

### The candidate cap turned a safety system into a lossy one

| | |
|---|---|
| **Before** | `if (candidates.size() > max_candidates) candidates.resize(max_candidates);` |
| **Problem** | Truncation in quadtree traversal order — an ordering with no relationship to risk. In a system whose output is safety alerts, a dropped candidate is a **missed breach**: a false negative, silently, with no signal beyond a counter. |
| **Fix** | An explicit `CandidatePolicy`. The default, `ExactAlways`, treats the cap as a *diagnostic*: it counts the overflow and evaluates every candidate anyway, because a bounded tick is not worth a missed breach at this scale. `NearestFirstCapped` is opt-in for a hard real-time bound and ranks by (distance, then descending severity) before cutting, reporting `candidates_dropped` so the approximation is measurable rather than silent. |
| **Test** | 80 decoy zones plus one tight high-severity hazard, cap of 8: `ExactAlways` examines all 81 and drops 0; `NearestFirstCapped` examines 8, reports exactly 73 dropped, and still keeps the hazard. |

### Structures decayed under churn

| | |
|---|---|
| **Before** | Quadtree deletion never collapsed a subdivision. R-tree deletion never condensed underfull nodes. Geohash query padding only ever grew. Hash-table rehash always **doubled**, even when the table was full of tombstones rather than entries. |
| **Problem** | Every one is invisible to a build-then-query test and unbounded over time. A quadtree that had held 1,000 items kept that shape after 900 were deleted; a hash table churning a fixed 100-key live set doubled forever, growing without limit to make room for corpses. |
| **Fix** | Quadtree: subtree collapse when a node's whole subtree fits in one node, with an early-exit count so the check is O(cap). R-tree: Guttman-style condense (detach underfull nodes, reinsert their entries) plus root collapse. Geohash: exact extent recomputation on removal — affordable because `remove()` is already O(n). Hash table: the rehash **trigger** still counts tombstones (probe length depends on occupied slots) but the **decision** is made on live count alone, so tombstones cause a same-size rebuild and only real growth doubles. |
| **Measured** | Quadtree 309 → 33 nodes after deleting 900 of 1,000, and **1.00×** node count across four full insert-900/delete-900 cycles. R-tree 155 → 22. Geohash padding 0.08° → 0.0005°, keys scanned per query **489 → 110**. Hash table: 50,000 churned keys, **zero** growths, bucket count unchanged. |
| **Test** | `tests/index/churn_test.cpp` (41 assertions, brute-force-checked at 20 randomised checkpoints), plus churn blocks in the hash-table and interval-tree tests. Section 10 of `make bench` reports the ratios. |

### Interval tree deletion was O(n) and broke its own balance evidence

| | |
|---|---|
| **Before** | `remove()` linear-scanned the node array and set a `dead` flag. |
| **Problem** | O(n) deletion in the structure whose entire selling point is O(log n) — and worse, `count_` fell while the height did not, so `balanced()` compared a real height against a fictional *n*. The evidence the report cites for the AVL invariant was measuring something else. Dead nodes also kept inflating every subtree's `max_high`, loosening the very pruning bound they exist to tighten. |
| **Fix** | Real AVL deletion: leaf / one-child / two-child cases, successor promotion, rotation and `max_high` repair on the way back up, freed slots on a free list. |
| **Test** | +52 assertions including a `check_invariants()` structural audit (BST order, height, balance factor, `max_high` at every node) run at 20 checkpoints across 4,000 mixed operations, duplicate low-keys removed individually, and a 200-cycle churn showing slots are reused. |

### Two geometry layers disagreed about holes

| | |
|---|---|
| **Before** | `Polygon::validate()` checked the outer ring only. `signed_area()`, `centroid()` and `perimeter_m()` ignored holes. `contains_winding()` summed hole windings without normalising orientation. |
| **Problem** | A hole could self-intersect, sit outside the shell, cross its boundary, or overlap another hole and the zone loaded clean — while ray casting's parity rule assumes none of those, making containment arbitrary in exactly the way a self-intersecting ring makes it arbitrary (which the code already refused to accept). A ring-shaped zone reported the area of a disc and a centroid sitting *in the hole*, i.e. outside itself. And the two containment implementations — kept specifically to cross-validate each other — **returned opposite answers** for the interior of a counter-clockwise hole inside a counter-clockwise shell, which is what most GeoJSON producers emit. |
| **Fix** | Full hole validation (six new `Validity` cases). Region-aware metrics with hole winding normalised away. `contains_winding()` normalises hole orientation the same way, so the two agree however a file was authored. |
| **Test** | `tests/geo/polygon_holes_test.cpp`, 40 assertions: metrics with and against hole winding, six adversarial rejections, two accepted disjoint holes, and both containment implementations checked on the same six boundary cases. |

### One predicate, three definitions

| | |
|---|---|
| **Before** | `orientation` / segment-intersection was written three times — in `polygon.cpp`, in `sweep_line.cpp`, and (as a point-on-edge test) in `containment.cpp` — with three slightly different epsilons. |
| **Fix** | `geo/segment.hpp`: one `orientation`, one `point_on_segment`, one `segments_intersect`, plus `segments_properly_cross` for the case where *touching is legal*. All four callers share them. |
| **Why the second crossing predicate** | Polygon validation wants "do these touch at all" — a ring edge grazing a non-adjacent edge is malformed. Jurisdiction nesting wants transversal crossing only, because real administrative boundaries **share edges constantly**: a block whose northern limit is its district's northern limit is correct data, and rejecting it would break the stricter containment rule on exactly the input it was written for. |

### Jurisdiction containment was not containment

| | |
|---|---|
| **Before** | "Every vertex of the inner ring is inside the outer one." |
| **Problem** | Not sufficient for concave regions. A C-shaped district and a block drawn as a bar across the mouth of the C: both ends sit inside the arms, the middle lies in the gap, every vertex passes. The bar lands in the wrong branch of the tree, so every alert raised in it routes to the wrong authority. |
| **Fix** | All vertices inside **and** no inner edge properly crossing the outer boundary. Nesting also keys on **outer** area (`outer_signed_area()`) now that region area subtracts holes. |
| **Test** | The C-and-bar case, a genuinely nested block in the same C, a block sharing two whole edges with its district (must still nest), and a region inside a district's exempt enclave (must **not** be owned by it). |

### Serialisation claimed an endianness it did not have

| | |
|---|---|
| **Before** | Three formats — geohash blob, offline queue, Merkle log — documented themselves as "fixed little-endian" while doing `memcpy(&value, bytes, sizeof)`, which is **host**-endian. `MerkleLog::load` then did `std::vector<uint8_t> e(len)` on an unvalidated 64-bit length. |
| **Problem** | The claim was never falsified because every machine it ran on was little-endian. A corrupt or hostile length field was an out-of-memory abort rather than a parse error — in the module whose entire purpose is tamper *evidence*. |
| **Fix** | `util/bytes.hpp`: values assembled and disassembled with shifts (which have no endianness), doubles through their IEEE-754 bit pattern, every read bounds-checked. All three formats gained a magic number, a length sanity bound checked **before** allocating, trailing-garbage rejection, and parse-into-a-temporary so a failed load leaves existing state untouched. |
| **Test** | `tests/index/serialization_test.cpp`, 40 assertions: round trip, byte-identical re-serialisation, the on-disk byte order asserted directly, and eleven malformed blobs (wrong magic, truncated, one byte short, one trailing byte, two concatenated, absurd count, unsorted keys, NaN coordinate, inverted box) each refused *and* each leaving the previously-loaded index intact. |

### The JSON parser accepted things that are not JSON

| | |
|---|---|
| **Before** | `strtod` for numbers, `\u` escapes skipped four bytes and emitted `?`, unknown escapes passed through, no trailing-content check, no depth limit, raw control characters accepted. |
| **Problem** | `strtod` accepts `0x1f`, `inf`, `nan` and a leading `+`. Every non-ASCII zone name was silently corrupted. Two concatenated documents, or a truncated file, parsed "successfully" as whatever the first value happened to be — the failure mode where half a zone set loads and nobody notices until an alert does not fire. |
| **Fix** | The RFC 8259 number grammar scanned explicitly; full `\uXXXX` decoding to UTF-8 including surrogate pairs; invalid escapes and raw control characters rejected; trailing content is an error; a documented `kMaxDepth` so a file of open brackets is a parse error rather than a stack overflow; duplicate-key behaviour pinned (all retained, `find()` returns the first). Plus `Json::escape()` / `Json::number()` so the writer is the parser's inverse. |
| **Test** | `tests/util/json_test.cpp`, 54 assertions, roughly half of them things that must be **rejected**. |

### Determinism was a claim, not a property

| | |
|---|---|
| **Before** | The README called determinism non-negotiable. Nothing checked it, and the places it breaks are not obvious: a binary heap is not stable, `std::nth_element` guarantees nothing among equal elements, `std::sort` is not stable. |
| **Problem** | Dijkstra's and A\*'s frontiers, the k-d tree's median partition and NN candidates, and the Hungarian assignment's equal-cost choices were all free to return a *different but equally valid* answer between two builds, two standard libraries, or two optimisation levels — and every one of those answers feeds the golden replay. |
| **Fix** | Explicit tie-breaks throughout: `(distance, node)` on both frontiers, `(axis value, id)` in the k-d tree build and `(distance, id)` in its queries, deterministic equal-cost parent selection (guarded against zero-weight edges, which could otherwise make two nodes each other's parent and send `path_to()` round a cycle forever), and first-minimum scanning in the Hungarian potentials method. The k-d tree's pruning test also had to admit the far subtree when the splitting plane is *exactly* as far as the current best — with the obvious strict comparison, the tie-break guarantee is one the function cannot keep. |
| **Test** | `tests/golden/determinism_test.cpp`: the whole simulator run twice and compared event-by-event; a lattice of equal-weight edges where the parent trees must match, not merely the distances; a k-d tree over deliberately coincident points; an all-equal Hungarian cost matrix; the full dispatch plan; and a quadtree built in reverse order answering every query with the same set. Plus `make determinism`, which runs the binary twice and `cmp`s the output. |

### Smaller defects fixed in the same pass

| Defect | Fix |
|---|---|
| `BruteForceIndex::query` added `out.size()` — the whole accumulating buffer — to its candidate counter | Count only what the call appended. This is the *oracle*: it is the denominator of every speedup figure and the source of the candidates column, so the bug inflated the project's numbers about itself while every correctness test still passed. |
| `SpatialIndex::nearest()` was an O(n log n) scan in all four implementations, sitting next to an O(log n + k) range query and documented as driving the adaptive sampler (it never did) | Removed from the interface. Range queries are what these structures do; nearest-neighbour over points is `index/kd_tree.hpp`, which is what `RoadGraph::nearest_node` now uses — O(V) → O(log V), with the linear scan kept as its oracle. |
| `Correlator::close()` had an empty body and `open_incidents()` returned every incident ever created | Real `IncidentStatus`; closing removes an incident from the merge set so a later alert opens a new card rather than silently reopening one an operator signed off. |
| Incident radius was max'd only against *newly arriving* alerts while the centroid moved | Member positions retained; radius recomputed over all of them. The operator map draws that circle and the dispatcher sizes the response from it. |
| `hungarian()` read past the end of a short row on a ragged matrix and let a NaN poison its dual potentials | Validated with a typed `Status`; the assigner checks `ok()` before indexing. |
| `RoadGraph::add_edge` accepted negative, infinite and NaN weights | Rejected at the boundary. A NaN weight is corrosive precisely because every comparison against it is false, so it neither relaxes nor fails to relax and the distances are silently wrong. |
| `offset()` could return a longitude outside (-180, 180] | Normalised. (`distance_m` and `bearing_deg` needed no change — the delta enters only through periodic functions — and the header now says why, so nobody "fixes" it into being wrong.) |
| Segment projection computed its parameter `t` in degree space, stretching the east-west axis by 1/cos(lat) = 1.108 at Shillong | `geo/projection.hpp`: a local tangent plane in metres, deliberately the **linearisation of the same spherical metric `distance_m` uses**. A first attempt used the WGS84 ellipsoidal series and disagreed with haversine by 0.37% — 2.8 m over a 750 m segment, comparable to the GPS noise the design is built around, from nothing but two files modelling the Earth differently. Error budget measured at three anchor radii and printed every test run. |
| `CircularBuffer<T, 0>` compiled | `static_assert(N > 0)`. |
| `make test` recompiled the entire core for every test file and hid compiler output with `2>/dev/null` | Core compiled once into an archive; suite went from ~4 min to ~23 s, and build failures are visible again. |
| Makefile carried a hand-written source list that CMake's glob did not match | Both now glob the same patterns; drift is impossible by construction. 16 empty translation units (files containing only `TODO(impl)` for header-only templates) removed. |
| Sanitizers covered 2 of 28 test files and did not gate CI | Whole suite under ASan+UBSan with `-fno-sanitize-recover`, gating deployment. |

**Measured after Tier 4:** 691 assertions across 39 files, 0 failures;
`make determinism` byte-identical; benchmarks green with the R-tree's STR bulk
build a **6.5× query improvement** over insertion-built (a new result, not a
fix); the whole suite clean under UndefinedBehaviorSanitizer locally with
`-fno-sanitize-recover`, and gated under ASan+UBSan in CI.

**Honest note on local sanitizer coverage.** AddressSanitizer's runtime is
currently broken on macOS 26 with Apple clang 17 — an empty `int main(){}` linked
with `-fsanitize=address` hangs in dyld's `__malloc_init` before reaching `main`,
so no ASan binary of any kind runs on such a host. ASan is therefore authoritative
in CI (Linux/g++, where it works) and `make ubsan` exists so macOS developers have
a sanitizer they can actually run locally. Saying so is better than quietly
shipping a sanitizer target nobody on the team can execute.

---

## Tier 5 — Claim–code alignment  ·  status: ✅ done

Tier 4 audited behaviour against claims. This pass re-audited the tree tier 4 left
behind, hunting the specific residue a large correctness pass tends to leave: a
complexity claim that holds for typical data but not for *ours*, a module built and
never wired to a caller, an oracle that is not an oracle, and prose describing the
project as it was two tiers ago.

| # | Flaw | Before | After |
|---|---|---|---|
| 1 | **Interval-tree deletion was O(n) on exactly this project's data** | The BST key was the interval's `low` alone. Intervals sharing a start time — every zone whose closure begins at midnight — formed a block of equal keys with no internal order, rotations scattered it to both sides, and `remove()` had to search the right subtree and then the left. The header and the complexity table both said O(log n). | Total order `(low, high, value, seq)`, `seq` a per-insert counter so no two live nodes tie. Removal compares on the `(low, high, value)` triple, which is monotone in that order, so matching nodes form one contiguous in-order run and a single descent lands in it. The structural audit checks the composite order, not `low` — which is what would have caught the original. Benchmarked: **0.12–0.22 µs per delete at 1k–50k live entries** with a tenth of the entries sharing each endpoint. |
| 2 | **The k-d tree's brute-force oracle minimised a different distance** | `nearest_node_linear()` scanned with haversine; the k-d tree minimises its own tangent-plane metric. A disagreement therefore meant either a search bug or nothing at all. The test ran one 40×40 grid and passed; a new benchmark at 64×64 reported *same node: **NO***. | Both use the tree's metric, so they agree node-for-node and a mismatch is a real bug. The modelling difference is measured on its own instead: plane and great circle pick different junctions on **1 probe in 4,000**, and the plane's is **2 mm** further. The test pins the exact configuration that exhibits it and asserts the case occurs, so the bound cannot go vacuous. |
| 3 | **The sweep line was built, tested, and called by nothing** | `polygon.hpp` claimed "Bentley-Ottmann sweep line, O((n+k) log n)"; `validate()` ran the O(V²) pairwise scan; `sweep_line.cpp` had no production caller. Wrong algorithm name, wrong complexity, wrong code running — three claims failing at once. | `validate()` dispatches to the Shamos–Hoey sweep at `kSweepThresholdVertices = 56`, outer ring and holes alike. Threshold read off a measurement (§12 of `make bench`: 0.89× at 48 vertices, 1.04–1.11× at 64), not guessed. The pairwise version stays as the oracle and as the faster path below the threshold, and the two are asserted to give identical verdicts on rings either side of it. **1.6× at 128 vertices, ~9× at 2048 (8.9–9.1 across runs).** Name corrected in all six places: existence, not enumeration. |
| 4 | **Timing wheel claimed unconditional O(1) and could not cancel** | `advance()` steps tick by tick — it must, or the rounds comparison is wrong — so a jump of D ticks walks D slots. "O(1) amortised" in the table hid the only surprising thing about the structure. Cancellation was lazy, so `pending()` counted timers nobody awaited and the wheel's memory tracked *total* alerts raised, not live ones. | Claim is now `schedule O(1) · cancel O(b) · advance O(Δticks + fired + held)`, with the reason spelled out in the header and the worst-case table. `cancel()` removes the entry; `EscalationTracker::acknowledge()` uses it, and `tracked()` became a true count. |
| 5 | **Hole validation had no stated boundary-contact policy** | A hole flush against the outer ring, or touching another hole at a vertex, was rejected by the code and by nothing that said so — while containment uses the *opposite* convention (a point on a boundary is inside). Read in separate files, that is an inconsistency. | Both rules documented together, with why they differ: validation refuses ambiguous geometry, containment resolves a point in geometry already known to be good. Tested — including the near-miss that must still be accepted, so the rule is not just "reject everything". |
| 6 | **Determinism was asserted on the event stream, not on what ships** | The golden test compared event sequences. The deliverables — the exported dashboard HTML, the serialised index blob — were never compared. | Both compared **byte for byte** across two runs, plus SHA-256 digests (the project's own implementation) so a failure is one line rather than a megabyte of diff. Blob round-trip is asserted byte-stable too, which is stronger than "loads without error". |
| 7 | **Stale prose across six documents** | `DESIGN_DEFENSE.md` called the binary heap an unbuilt stub and "~15 modules" designed-not-built; `DEPLOYMENT.md` said `sync/` was a stub and 28 test files gated the deploy; `slides.html` reported 233 checks across 12 files and "23 modules built, ~13 stubbed"; `PRESENTATION.md` called the sanitizers advisory when they gate; `adaptive_sampler.hpp` cited a `SpatialIndex::nearest()` removed in tier 4. | All corrected against the code, which is the source of truth. Benchmark figures requoted from a fresh run, with the R-tree headline given as a **230–250× band** because that is how much it moves between runs — a single-figure claim there would be false precision. |
| 8 | **CI could not detect Make/CMake source-set drift, and macOS did not gate** | Both build systems glob the same patterns, so drift "should be" impossible — the phrase that precedes every drift, and this repo has drifted before. The macOS job ran but nothing depended on it, while the README claims the project builds on both platforms. | An explicit CI step diffs each build system's discovered source and test lists against the tree. `macos` joined `test`, `sanitize` and `cmake` as a deploy gate. |

**Measured after Tier 5:** **765 assertions across 39 files**, 0 failures.
`make check`, `make test`, `make ubsan`, `make determinism`, `make bench` and
`make dashboard` all pass on a clean tree. Three new benchmark sections
(self-intersection crossover, interval-tree churn, node snapping). The ASan
situation was re-verified rather than carried forward: an empty `int main(){}`
linked with `-fsanitize=address` still hangs on macOS 26 / Apple clang 17, so ASan
stays CI-only and `make ubsan` stays the local path.

**The interview-usable line from this tier**, because it is the one that shows
judgment rather than effort: *a brute-force oracle that answers a slightly
different question is not an oracle*. Two structures agreed on every test and
disagreed the moment a benchmark ran them on a larger input — not because the fast
one was wrong, but because the reference was minimising a different metric. The fix
was to make them share a metric and then measure the modelling difference
separately, so one number stopped standing for two questions.

---

## Resume framing — how to present this without getting exposed

The project's breadth is a liability in an interview: an interviewer picks **one**
thing and drills to bedrock. Lead with depth, not the catalogue.

**Do:**

- **Lead with one advanced structure you can defend to the floor** — the
  persistent path-copying quadtree. It is genuinely uncommon, it is real
  (`shared_ptr<const Node>`, immutable nodes, measured 13× structural sharing),
  and the "why the speedup ceilings at ~33×, not 29,000×" analysis shows judgment,
  not just coding.
- **Quote numbers with their caveats built in:** "≈33× over a brute-force oracle
  (median of 7 runs, single machine), bounded by output size — analysed, not just
  measured."
- **Call it what it is:** a data-structures course project / simulation study.

**Suggested one-liner** (defensible end to end):

> Built a persistent (path-copying) quadtree in C++17 with reference-counted
> immutable nodes for O(log n)-per-update historical queries; measured 13×
> structural sharing over 5,000 versions and validated every index against a
> brute-force oracle. Analysed why the spatial-index speedup is output-bound
> (~33×), not the ~29,000× first predicted.

**Don't:**

- Don't quote a big test-count or "eleven gaps" as if breadth proves depth.
- Don't imply a deployed product or real-world safety impact — the data is simulated.
- Don't claim "O(log n) spatial index" unqualified — say "average-case; O(n) worst
  case, which is why the AVL interval tree is the one with a real guarantee."
- Don't list all 14 structures; name two or three you can whiteboard on demand.
