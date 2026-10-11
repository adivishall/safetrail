# Defect log — what was wrong, and what fixed it

A running record of defects found in this project by successive self-review
passes, and what each fix changed. It exists because the interesting part of a
data-structures project is not that the structures work, but *how* the ways they
were quietly wrong were found.

Every entry is a real defect that shipped and was later caught: a flagship
structure that silently never merged across ticks, a brute-force oracle that had
been answering a different question than the thing it validated, a complexity
claim that held in general but not on this project's data, a null-pointer UB in
SHA-256 that a sanitizer gate surfaced, a state machine that lost zones once they
left the index's candidate window. Passes 1–5 fixed each defect in its own
commit; pass 6's engine fixes landed together, in one commit with their tests.

The passes are ordered by depth — each one looked at a layer the previous one
had not: **1** claims that contradicted the code, **2** benchmark rigor, **3**
framing and claim accuracy, **4** correctness and invariants (what you find when
you stop reading the docs and start reading the code), **5** claim–code alignment
(complexity claims true in general but not for this data, a module built and
never called, an oracle that had been quietly invalid), **6** a hostile audit
driven by differential tests against oracles, invariant audits after every
operation, and mutation testing. Pass 6, the most recent and the deepest, is
listed first.

**Passes 4, 5 and 6 are not victory laps.** Each records a later, deeper audit
that found real defects the earlier ones had not looked for — pass 4 in the
project's flagship structure, pass 5 in a complexity claim and in a brute-force
oracle that had been invalid for as long as it existed, pass 6 in the state
machine on the per-fix path and in the benchmark's own headline.

**Pass 7** is the release validation of the integrated branch: every target from
a clean build, the regression tests built against the pre-audit code, the
mutation check, the benchmark harness itself, and every complexity claim read
against the code that makes it. It found no defect in the engine's behaviour;
it found claims stronger than their evidence — a complexity bound, a
cross-platform guarantee, a one-session benchmark range — two measuring
instruments weaker than they said, and one regression test that did not test the
defect it was named for.

**How to read this file.** Every table is a dated record of what was true *then*.
Current-state numbers are the ones in the most recent pass, in `README.md` and in
[RESULTS.md](RESULTS.md) / [TESTING.md](TESTING.md); earlier figures are history,
not claims, and are left in place because the point of the ledger is the
trajectory. In pass 6, every "Test" cell names a test that was run against the
pre-fix code and **failed**, then passed after the fix; where a defect was found
by a probe rather than a test, the probe's output is quoted.

---

## Pass 7 — Release validation (2026-10-01)  ·  status: ✅ done

### The interval tree's query bound was overstated

| | |
|---|---|
| **Before** | README, DATA_STRUCTURES, INTERVIEW, ARCHITECTURE and `ds/interval_tree.hpp` gave the stabbing query as O(log n + k) worst case — "the only structure here with a guaranteed query bound". |
| **Problem** | The traversal is the augmented-BST one: prune a subtree when its `max_high <= low`, descend right while `low < high`. That finds *one* overlapping interval in O(log n), but reporting all k costs O(min(n, (k + 1) log n)) in the worst case: the prune admits a subtree as soon as *any* interval in it reaches past the query, so each reported interval can light up its own root-to-leaf path, and every node on those paths is visited whether or not it overlaps. O(log n + k) is the bound of a centred interval tree or a priority search tree, neither of which this is. The measurement already had the right shape — §8 shows an order of magnitude when k is tens and nothing when k is hundreds — the claim did not. |
| **Fix** | The bound corrected in all five places. It is still a guarantee (AVL height ≤ 1.44 log n) and still never worse than the scan; what changed is the exponent on k. |
| **Test** | A claim, not behaviour: no test. `make bench` §8 is the evidence either way. |

### The benchmark's result checksum could not tell some different answers apart

| | |
|---|---|
| **Before** | `fold()` summed `(id + 1) × c` over the result ids, order-independently. |
| **Problem** | Two result sets of equal size and equal id-sum collide — {1, 4} and {2, 3} — so the "identical results" gate on every timed section could pass two indexes returning different candidates. The exact set-for-set comparison in §5 and `tests/index/differential_test.cpp` were unaffected; the gate the headline tables rely on was weaker than the protocol said. |
| **Fix** | Each id goes through a 64-bit bit mixer (splitmix64's finaliser) before the sum. The checksum stays inside the timed pass, as it must to keep the work alive; it costs the same few instructions for every contender. |

### The reconciliation step had an argument and no measurement

| | |
|---|---|
| **Before** | Step 9 of the evaluator — observe every zone with open state, whether or not the index returned it — was described as "O(open states), no geometry". Nothing measured it, and a reviewer could reasonably ask whether a per-fix loop over open states gives back what the index wins. |
| **Fix** | `make bench` §18 times the whole `evaluate()` per fix on fixed trajectories against 1,000–50,000 zones in two regimes. Packed into one district the cost tracks the candidate count (the §1 crowding ceiling: k grows with n); at constant density it barely moves while n grows 10×. Reconciliation is a fraction of a zone per fix, a box distance and a hysteresis update each. [RESULTS.md](RESULTS.md) §18. |

### The hysteresis regression test still used the metric pass 6 had discredited

| | |
|---|---|
| **Before** | Pass 6 found that the hysteresis A/B scored filter-off against filter-on, which cannot tell a removed flap from a suppressed real crossing, and gave `make bench` §10 a noise-free target. `tests/golden/hysteresis_ab_test.cpp` was not updated: it still asserted "removes >60% of false transitions", and its header promised a check ("never suppress a genuine sustained crossing") that it did not make. |
| **Problem** | The ground-truth comparison lived only in a benchmark, which nothing gates. Built against the pre-audit code (`6656b74`), the old test passes 6 of 6 — on the very code whose Uncertain leak let the filtered run report more than twice what noise-free fixes give. |
| **Fix** | The test now runs the noise-free target as well and requires the filtered noisy run to stay within 115% of it under both noise models, and at or above 85% under realistic drift (it measures 96%). White noise keeps no lower bound: losing a third of the target there is the documented price of suppressing ~20,000 flaps. |
| **Test** | On `6656b74` the new test fails 2 of 11 (filtered/target = 241% and 219%); on the current code it passes 11 of 11. |

### A regression claim that could no longer be reproduced as stated

| | |
|---|---|
| **Before** | TESTING.md said `state_reconciliation_test` fails 9 of 18 checks on the old code. |
| **Problem** | The current test reads counters the old code does not have and no longer compiles against it; the same holds for the hysteresis, index-independence and persistence-differential tests, which use the `Ambiguous` phase and `check_invariants`. Built against commit `6656b74` (pre-audit): `bbox_around` 7 of 10, `ray_casting` 1 of 35, `sweep_line` 9 of 56, `interval_tree` 7 of 185 and `zone_roundtrip` 7 of 37 fail exactly as documented; the other four do not compile, and their evidence is the mutants that revert each fix. |
| **Fix** | TESTING.md §5 now says which tests were run against the old code and which rely on the mutation check. |

### Smaller findings in the same pass

| Finding | Resolution |
|---|---|
| The README's headline range ("102–114× over 4 runs", then "223–235×") was the min–max of one session's runs. Runs within a session agree to a few percent; between sessions the R-tree's ratio has moved more than 2× (102–114× on 2026-09-28, every contender 2–4× slower and tree-row IQRs of 16–58%; 223–235× on 2026-10-01, where the canonical run's IQRs on that row were 1–6%). A reader saw one session's spread as the whole uncertainty. | The generated keyline says "in one session"; the README states the earlier session's figure and reads ~100× as the floor; RESULTS.md "Read this first" lists every session on record. Hand-typed figures in README/RESULTS/INTERVIEW prose that went stale with the new run (quadtree "23–28×", interval tree "14–44×" and "0.9–1.3×", end-to-end "~50×") now describe the shape and leave the numbers to the generated tables, or quote the range across sessions. |
| ARCHITECTURE, TESTING and RESULTS said "the evaluation core agrees across operating systems" in the present tense. The evidence is one comparison of a macOS and a Linux build on 2026-09-05 (WORKLOG), before the pass-6 fixes; nothing gates it. | Stated as what it is: one comparison, not a gate. CI checks determinism within each platform. |
| Make vs CMake: the Make build (library `-O2`, tests `-O1`) and the CMake Release build (`-O3`, `NDEBUG`; the tree has no `assert`) had never been compared. | Compared on the same seed: byte-identical. `make validate` and the CI CMake job now check it, so the determinism claim covers both build systems rather than two runs of one binary. |

---

## Pass 6 — Hostile audit of the core (2026-09-28)  ·  status: ✅ done

A full re-audit with one question per structure: *what input would make this
disagree with its oracle?* Answered with randomised differential tests over
deliberately hostile workloads, structural invariant audits after every
operation, and a mutation check that injects known bugs and requires a test to
fail for each. Ten findings: eight defects in the engine — four of them on the
per-fix path from GPS fix to event, one of which produced about 60% of the
demo's enter/exit events — and two in how it was measured.

### The state machine lost track of zones that left the candidate window

| | |
|---|---|
| **Before** | `fence::Evaluator` advanced a tourist's per-zone state only for zones the spatial index returned this tick. |
| **Problem** | A tourist confirmed **Inside** who then left the zone's candidate window in one step — a GPS gap, a vehicle ride, a fix after the phone was off — was never observed against that zone again: no `ZoneExit`, state stuck at Inside, and a later genuine re-entry raised **no** `ZoneEnter` because the state already said Inside. A zone whose validity window closed while someone was inside did the same. Probe: a walk across 60 zones in 1 km steps produced 60 entries and **zero** exits. Silent missed alerts, on the core path. |
| **Fix** | A reconciliation step: every zone with open state is observed on every usable fix. Not returned by the index → a *certain* Outside (the query box provably bounds a disc at least as large as the fix's uncertainty — see the next entry), fed through the same hysteresis machine so one wild fix cannot force an exit. Out of force or deleted → state closed at once, with a `ZoneExit` if it was Inside. Settled states outside the window are dropped, which also bounds `zone_states` by the neighbourhood instead of by every zone ever visited. Candidates are now evaluated in zone-id order, making the event stream independent of which index produced them. |
| **Test** | `tests/fence/state_reconciliation_test.cpp` — 9 of 18 checks fail on the old code (jump away, re-entry, zone expiry at t=60 s, reactivation, deletion, bounded state across 60 zones; plus "one wild fix is not an exit" and "a candidate dropped by the cap is not given a fabricated exit"). `tests/fence/index_independence_test.cpp` runs the whole simulation and a hostile teleport/validity workload under all four indexes and requires bit-identical event streams. |

### Hysteresis let the Uncertain band through — 60% of transitions were flaps

| | |
|---|---|
| **Before** | The hysteresis machine (asymmetric margins, N confirming fixes, minimum dwell) filtered Inside ↔ Outside only. An Uncertain verdict was reported as-is, and the next Outside verdict silently reset it. |
| **Problem** | Two leaks. (1) From a pending exit, one Uncertain fix was reported as Uncertain — which the evaluator emits as a `ZoneExit` — and the next inside fix re-entered: an exit/enter flap pair straight through the filter. (2) A fix whose accuracy alternates between open sky (4 m) and multipath (35 m) near a boundary emitted a fresh `ZoneUncertain` on every alternation: 150 in five minutes for one stationary tourist. On the dashboard's two-hour run, removing the leaks took entries from **2,440 to 987**, exits from 2,422 to 965, Uncertain events from 64,168 to 16,558 and operator alerts from 79,052 to 23,380. |
| **Fix** | "Reported Uncertain" became a phase of its own with the same exit rule as Inside: only a run of `confirm_samples` clearly-outside fixes leaves it. An Uncertain fix never ends Inside and cancels a pending exit instead of completing it; during a pending entry it restarts the confirmation count but keeps the dwell clock. |
| **Test** | `tests/fence/hysteresis_test.cpp`. Its first version failed 7 of 13 checks on the old code, including the evaluator-level count (150 `ZoneUncertain` events in five minutes; now 1); it has since grown checks that use the new phase and so cannot compile against the old code. |

### The hysteresis experiment could not tell a removed flap from a missed crossing

| | |
|---|---|
| **Before** | `make bench` compared transitions with the filter off and on, both under GPS noise, and reported the difference as "94% of false transitions removed". |
| **Problem** | Nothing compared either run with the truth, so suppressing *real* crossings would have scored as removing false ones — and the flap leak above was hiding inside the "filter on" count. |
| **Fix** | Four runs over identical trajectories (the GPS model draws the same random numbers at any sigma): noise-free without and with the filter (the raw truth, and the *target* — truth under the dwell/confirmation policy), and noisy without and with it. Under realistic correlated drift the filter reports 96% of the target's transitions, while the naive geofence reports 16,168 transitions for 1,934 real boundary crossings (about 8×); under white noise the filter recovers 66% of the target. That trade-off is now visible instead of hidden (RESULTS.md §10). |

### The index query box was not conservative

| | |
|---|---|
| **Before** | `Bbox::around(c, r)` used 110,574 m per degree of latitude and 111,320·cos(lat) m per degree of longitude. |
| **Problem** | Those are WGS84 constants; `distance_m` measures on a 6,371,008.8 m sphere (~111,195 m per degree). The box came out 0.11% too **narrow** east-west, so the filter could drop a zone the exact test would have called Uncertain — the filter-then-refine contract requires the filter to be a superset. Probe: 1,728 of 160,000 points sampled inside the radius fell outside the box. It also mishandled caps containing a pole. |
| **Fix** | The exact bounding box of the spherical cap on the same sphere: half-width `r/R` in latitude and `asin(sin(r/R) / cos(lat))` in longitude (a great circle bulges poleward), full longitude range when the cap reaches a pole, and a 1e-9 relative pad for rounding. |
| **Test** | `tests/geo/bbox_around_test.cpp` — conservative on 160,000 sampled points across both hemispheres and radii from 1 m to 100 km; tight (no edge measurably beyond the cap); poles; zero and negative radii. 7 of 10 checks fail on the old code. |

### "On the boundary" was thicker on short edges

| | |
|---|---|
| **Before** | `point_on_segment` compared the raw cross product against 1e-11. |
| **Problem** | The cross product is |edge| × perpendicular distance, so the boundary's thickness was inversely proportional to edge length: ~1 mm on a 100 m edge, ~11 cm on a 1 m edge. A fix 10 cm *outside* a small zone read as "on the boundary", which the containment rules define as inside. GEOMETRY_EDGE_CASES.md already promised a 1e-9° (~0.1 mm) tolerance; the code did not implement it. |
| **Fix** | Compare the perpendicular distance: `|cross| <= 1e-9 · |edge|`, degrading gracefully to "within 1e-9 of the point" for a zero-length edge. |
| **Test** | `tests/geo/ray_casting_test.cpp` — a point 10 cm outside 1 m, 100 m and 5 km edges is outside, the midpoint is on the boundary, and ray casting agrees with winding number. The 1 m case fails on the old code. |

### Sweep-line validation called some self-intersecting rings simple

| | |
|---|---|
| **Before** | Shamos–Hoey tested a newly inserted segment against its *first* non-exempt neighbour on each side (ring-adjacent edges are exempt: they share a vertex). Same-x events were ordered by `(x, type)` only. |
| **Problem** | Exempt pairs break the textbook adjacency argument, so a touching segment one step further out was never compared. Fuzzing on lattice rings — touches, shared vertices, collinear overlaps, the inputs a sweep is fragile on — found **94 disagreements in 800,000 rings** against the O(V²) pairwise oracle, all false "simple" verdicts; the existing test used jittered circles, which never produce degeneracies, and asserted "verdicts identical on every ring". Separately, `std::sort` on `(x, type)` leaves same-x events in unspecified order, free to differ between standard libraries. A first diagnosis blamed the removal step; restoring each half of the fix separately showed the insertion step was the whole cause, and the removal change was reverted. |
| **Fix** | Test every eligible segment among the nearest three on each side at insertion (an edge has at most two exempt partners); a total event order `(x, type, index)`. Still O(log n) per event. |
| **Test** | `tests/geo/sweep_line_test.cpp` — eight pinned rings (all fail on the old code) and a 12,000-ring degenerate lattice fuzz, 0 disagreements; the 800,000-ring version also shows 0. |

### Interval-tree deletion broke its own ordering invariant on exact duplicates

| | |
|---|---|
| **Before** | Deleting a node with two children copied its in-order successor up, then removed "the successor" by *searching* for its `(low, high, value)` triple. |
| **Problem** | With exact duplicates (legal: two rules with the same window and payload) the search can stop on a *different* node with the same triple. The real successor then survives in the right subtree while its payload and tie-break `seq` are also copied up: two live nodes with one total-order key. `check_invariants()` fails; queries stayed correct because the payloads are indistinguishable, which is how it hid — the existing test audited only after draining the tree. Also: `stabbing(INT64_MAX)` computed `at + 1`, a signed overflow (UB) reachable as `active_at(kForever)`, and an inverted query range `[10, 5)` "overlapped" `[0, 20)`. |
| **Fix** | Unlink the successor by position (`erase_min`, rebalancing on the way up). Guard `INT64_MAX` (no half-open interval contains it) and empty ranges. |
| **Test** | `tests/ds/interval_tree_test.cpp` — invariants audited after **every** removal of 50 identical entries, of a mixed duplicate block, and of 3,000 random ops over a 4×3×3 key space; the range and overflow cases. 7 checks fail on the old code; the overflow was reported by UBSan. |

### The persistent quadtree's mutations were not what they claimed

| | |
|---|---|
| **Before** | `remove_zone` searched the tree depth-first, copying every node it *visited*; `add_zone` of a present id inserted a second copy; commit times were trusted to be increasing; the world-sized root was never widened. |
| **Problem** | Removing one zone from a 2,000-zone index allocated **96** nodes (the claim was O(depth)); removing an id that never existed allocated 125 and still minted a version; re-adding an id made every later query return it twice; a change stamped earlier than the latest version left `version_times_` unsorted, and `version_at()` binary-searches it, so `query_at` silently returned the wrong version; a box past lon 180 was stored at the root but pruned from every query. |
| **Fix** | Removal follows the one path the insert rule put the zone on (a per-zone box makes it a guided descent) — O(depth) allocations. Absent-id removal and validity edits are no-ops that create no version. Re-adding replaces geometry and validity in a single version (`Change::Kind::Replaced`). Transaction time is monotone: a late change is recorded at the latest version's time. The root copy widens to cover out-of-domain boxes. A `check_invariants()` audits every retained version. |
| **Test** | `tests/index/persistence_differential_test.cpp` — 25 seeds × 160 random ops (adds, replacements, absent removals, out-of-order commits, zero-area / on-split-line / negative / past-180 / huge boxes) against a replay oracle that keeps a full table per version; every `query_at`, `active_at`, `validity_at`, `zone_count_at` and `query_now` must match; invariants after every op; no mutation may allocate more than two paths plus a split. The mutation check confirms each fix is load-bearing (see TESTING.md). |

### The zone loader converted untrusted numbers with undefined behaviour

| | |
|---|---|
| **Before** | `uint8_t(severity)`, `uint32_t(jurisdiction)`, `Timestamp(seconds * 1000)` straight from the GeoJSON; coordinates unchecked. |
| **Problem** | A double-to-integer conversion whose value does not fit is undefined behaviour in C++, not a wrap: `severity: 300`, `jurisdiction: -1` and `active_to_s: 1e300` each reached one (UBSan: "300 is outside the range of representable values of type 'unsigned char'"). A ring stored across the antimeridian as lon > 180 loaded and then half-worked, since nothing downstream wraps longitude. |
| **Fix** | Every numeric property is range-checked before conversion and a bad one rejects the file with a reason; coordinates must be finite and on the lat/lon domain. Geohash's Morton encoder also sends NaN to cell 0 instead of converting it to an integer. |
| **Test** | `tests/fence/zone_roundtrip_test.cpp` — seven hostile files, each refused with the right reason; all seven load on the old code. |

### The benchmark's headline was not reproducible

| | |
|---|---|
| **Before** | Each timing was the median of 7 passes of 2,000 queries — about 2 ms of work for a tree index — run index after index. The README quoted the R-tree at "~240–260×" and the quadtree at "~35×". |
| **Problem** | A 2 ms window is at the mercy of the scheduler; a baseline run for this audit measured 138× and 29× on the same machine, with ±40% spreads. Nothing recorded the conditions a number was measured under. |
| **Fix** | A calibrated, interleaved protocol (samples ≥ 20 ms, 11 rounds, contender order rotated, paired ratios, IQR reported, result checksums gated), fixed-density scaling beside fixed-area, an end-to-end filter+refine benchmark with a truly naive baseline, build/memory/update costs, the interval tree against a linear scan, and `bench/results/environment.txt` recording compiler, CPU, power source and load. See RESULTS.md. |

### Smaller findings in the same pass

| Finding | Resolution |
|---|---|
| Seven empty "tombstone" headers (`server/http_api.hpp`, `geo/predict.hpp`, …) and a stub `safetrail_server` that only printed an error | Deleted, with the one live `#include` repointed. Scaffolding that says "not implemented" is still scaffolding. |
| `-Wshadow` hits in four places (CMake enabled it, the Makefile did not), a real `-Wformat-truncation` in the headless app's time formatter (gcc), 14 sign conversions | Fixed. Both build systems read one warning list (`tools/build/*.flags`); CI builds everything with `-Werror`. |
| The quadtree's structural audit could not tell a quadtree from "some tree of boxes": a root expansion that put the old root in the wrong quadrant survived every test (found by the mutation check) | `Quadtree::check_invariants()` now requires every child to be exactly its parent's quadrant. |
| Index memory was reported as node count × `sizeof(Node)` | Measured: nodes plus the capacity of every vector they own. |
| The evaluator's header listed an "adaptive sampling" step that gated nothing, a `fixes_skipped_power` counter that nothing incremented, and the per-candidate validity check as O(log n + k) | The header now says the sampler is fed, not consulted (it is a device-side policy, measured on its own); the dead counter is gone; the validity check is O(1) per candidate. |
| The interval tree was presented as the per-tick temporal filter | It is not on the per-tick path — the evaluator checks validity in O(1) per candidate. README, ARCHITECTURE and DATA_STRUCTURES now say where it is used (history queries), and RESULTS.md §8 measures when it beats a scan. |
| Introduced and caught within this audit: `.gitignore`'s `build/` also matched `tools/build/`, so a fresh clone would have lacked the shared warning-flag files — Make would have built with no warnings and CMake would have failed | Found by validating a clean export of the branch before committing. Patterns anchored to the root; the Makefile now refuses to build if the flag file is missing. |
| Found while integrating with `origin/main`: `make bench` recorded "(uncommitted changes)" in `environment.txt` on every run, because the shell redirect truncates that tracked file before `git diff --quiet HEAD` inspects the tree | The dirty check now covers only the code that produces the numbers (`src`, `include`, `apps`, build files). |
| CI ran on `ubuntu-latest` / `macos-latest`, so the GCC version that `-Werror` is enforced against could change with no commit | Pinned to `ubuntu-24.04` (g++ 13) and `macos-15`. |
| Found while re-validating the branch from a clean clone: one mutation-check run reported the on-boundary tolerance mutant as a survivor, though that mutant's binary fails its test 20 times in 20. The script relied on make seeing a fresh mtime, and macOS's GNU make 3.81 compares mtimes to the second, so an edit made in the same second as the last build was never compiled. The same flaw could have credited a kill to the previous mutant | `tools/mutation_check.py` starts every write in a later second than the last build. That mutant alone: killed in 6 of 6 runs (2 of 5 before). The full check: 22/22 in three consecutive runs. |
| `ds/interval_tree.hpp` claimed a second use in alert escalation; escalation uses the timer wheel | Corrected: the persistent index's `active_at` is its only engine use. |

---

## Pass 1 — Claims that contradicted the code  ·  status: ✅ done

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

## Pass 2 — Substance / rigor  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 5 | **Headline results are scenario-engineered** | 453:1 compression and the Hungarian-vs-greedy gap depend on a hand-placed cohort; a neutral run was degenerate. Not stated. | Every results table now labels correlation/dispatch **scenario-dependent** and reports **both** numbers: ~450:1 on a clustered incident, **~9:1 scattered**. The distinction is guarded by `golden/incident_formation_test`. |
| 6 | **No benchmark rigor** | Scaling numbers were a single timed pass, no warmup, no repeats, no variance; "33×" had no error bars. | `time_queries` now does a **warmup pass + median of 7 timed passes**, and reports **best run + spread** (printed and in the CSV). At 100k the spread is ±~5%; small-n rows show ±30–230% and are flagged as noise. Docs state "single machine, ratio to our own brute force — no external baseline." |
| 7 | **All data simulated** | Every "impact" number is synthetic under our own noise model; disclosed in places, but impact phrasing survived. | README results block and PRESENTATION now say "simulated GPS / simulated tourists" inline next to each figure; the ratio-to-own-brute-force caveat makes the internal baseline explicit. |
| 8 | **Worst case cuts against the headline** | Benchmarked structure is O(n) worst case; only interval tree/heap guaranteed. Disclosed in DATA_STRUCTURES but summaries risked unqualified "O(log n)." | Verified: the structure tables already say **"O(log n + k) avg"**, DATA_STRUCTURES keeps a dedicated worst-case table, and PRESENTATION §10 ("Honest engineering") states the O(n) worst case outright. No unqualified claim remained; no change needed beyond confirming it. |

**Measured after Tier 2:** `make bench` green, correctness gates pass. Headline
holds under the stricter method — **100k: quadtree ~32×, R-tree ~35×, spread
±3–10%** — so "~33×" is now a figure with an error bar, not a lucky single run.

## Pass 3 — Framing and claim accuracy  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 9 | **Breadth reads as shallow** | 14 structures + 17 algorithms + simulator + dashboard + CI; an interviewer drills one. | README and docs were restructured to lead with the five graded core structures and treat everything else as explicitly-labelled extensions, so depth is what a reader hits first. |
| 10 | **"No `std::`" reads as NIH** | Framed as pure rigor; a senior engineer reads it as poor production judgment. | README ground rule now states it is a **deliberate learning constraint for the course, not a production recommendation** — in real software you'd use `std::unordered_map` and a mature spatial library. |
| 11 | **Category inflation** | Most of the repo mass is systems/sim/viz, not data structures. | README "What this is" callout names the graded core (`geo/ index/ ds/ graph/`, ≈8k lines) and calls the simulator/dashboard/CI **scaffolding, not the deliverable**. |
| 12 | **Product framing oversells** | "The engine every team imports," "runs offline on a device" — reads as a deployable product. | README callout states plainly: **course project, not a shipped product; no mobile app, no live server; real geography, simulated people.** "on a device" softened to "locally, no server, in simulation." GitHub About/description rewritten to lead with the data-structures framing. |

---

## Pass 4 — Correctness and invariant hardening  ·  status: ✅ done

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

## Pass 5 — Claim–code alignment  ·  status: ✅ done

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

| 9 | **A null-pointer UB in SHA-256, found by the gate the moment it went live** | `sha256(nullptr, 0)` — reached by `MerkleLog::root()` on an empty log, because RFC 6962 *defines* MTH({}) = SHA256() — performed `data + 0` and `memcpy(dst, nullptr, 0)`. Both are undefined on a null pointer even at length zero. It had worked on every machine it had ever run on. | Guarded at the source; the empty-input digest is now pinned to its NIST value rather than merely not crashing. Found by the first CI run after the sanitizer gate reached `main` — which is the argument for the gate, made by the gate. |
| 10 | **`make ubsan` is weaker than CI, and did not say so** | The Makefile presented UBSan as the macOS fallback for a broken ASan runtime, implying equivalent coverage. It is not equivalent: Apple clang's `-fsanitize=undefined` does not flag that memcpy at all; g++'s does. | Verified with a four-line reproducer rather than inferred, and stated in the Makefile: run it before pushing, and expect CI to have the last word. |

**Measured after Tier 5:** **769 assertions across 39 files**, 0 failures.
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
