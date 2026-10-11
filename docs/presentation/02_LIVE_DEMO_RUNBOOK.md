# Live demo runbook — `make review PAUSE=1`, step by step

Follow this literally. Every "Expected output" below is copied from a run of
`make review` on this Mac on 2026-10-11 at commit `76b63af`. The scenario is
seeded (`--seed 7`), so every count you see live will be these counts; only the
microsecond timings in step 7 are measured live and will vary.

## What was verified about the commands

| Command | Verified behaviour |
|---|---|
| `make review` | Builds if needed (`make -s all`), then runs all nine steps with no stops. 33 s once built. Exit code 0. |
| `make review PAUSE=1` | Same nine steps, but stops after steps 1–8 with `↵  Enter for the next step`. **Exactly one Enter per prompt, 8 prompts.** Step 9 runs to the end with no prompt. Pauses only when the terminal is interactive; with input redirected it runs straight through, so it cannot hang. Typed text before Enter is ignored. |
| `tools/review.sh N` (N = 1–9) | Runs only step N, never pauses (even with `PAUSE=1`), reuses the three engine runs left in `build/review/` from the last full run and regenerates them if missing. Steps 5 and 8 took 0.2 s and 0.1 s today. Any other argument prints `usage: tools/review.sh [step 1-9]` and exits 1. |
| `make dashboard` | Writes `dashboard.html` (60 tourists, 2 h, 400 synthetic zones, 720 frames). Step 9 runs it; running it again overwrites the same file with identical content. |
| Files created or overwritten | `build/review/brute.txt`, `quadtree.txt`, `rtree.txt`, `naive.txt`, `*.events` (all ignored by git) and `dashboard.html` (ignored by git). **No tracked file is touched**; `git status` stays clean. The committed benchmark CSVs are read, never written. |
| `docs/REVIEW_SLIDES.html` | 10 slides. → / Space / PageDown next, ← / PageUp previous, Home / End, F fullscreen, P print, click right half next / left half previous, deep link `#8`. Loads `docs/images/*.png` and `bench/plots/index_scaling.svg` by relative path. |

**Live versus committed.** Steps 1–7 and 9 print numbers produced in front of
the viewer by the program that just ran. Step 8 prints the **committed** results
of `make bench` from `bench/results/` and says so on screen. Say the same thing
out loud. Do not call step 8 a live measurement.

## Pre-flight (two minutes before)

```bash
cd "/Users/adivishal/Projects/Data Structures CP"
git log -1 --oneline        # the commit you expect
make                        # silent if nothing to build
```

Then start:

```bash
make review PAUSE=1
```

The banner prints first:

```
━━ SafeTrail · project review ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  scenario   38 real OpenStreetMap hazard zones around Shillong + 5,000 synthetic
             40 simulated tourists, 1 simulated hour, one GPS fix per second
             GPS error 4 m open sky / 35 m multipath; seed 7, so every run is identical
  steps      1 problem · 2 brute force · 3 quadtree & R-tree · 4 correctness
             5 transitions · 6 noise · 7 history · 8 performance · 9 engineering
```

There is a pause of a few seconds here while the three engine runs (brute
force, quadtree, R-tree) execute before step 1 prints. Use it to say the
opening line.

---

## Step 1 — The problem

| | |
|---|---|
| **Action** | None: step 1 prints on its own after the banner. |
| **Screen** | Terminal. |
| **Say** | *"SafeTrail is a geofencing engine. Hazard zones are polygons with rules; people report a noisy GPS position every second; the question for every person and every zone near them is inside, outside or uncertain. These zones are real OpenStreetMap geometry around Shillong; the people are simulated, which is what gives me ground truth."* |
| **Expected output** | `zones loaded      5038  (38 authored + 5000 synthetic)` · `tourists          40 in 6 groups` · `GPS error model   4 m open sky / 35 m multipath (25% of fixes)` · `simulated span    1 h at 1000 ms/tick` · six real zones listed: Sonapani Waterfall Cliff (restricted, sev 5), Wards Lake, Deep Water 1–4 (caution) · last line: `144000 evaluations in this hour; checking all 5038 zones each time is the naive cost.` |
| **Point out** | The GPS line (a position is a disc of 4 m or 35 m, not a point) and the real zone names. |
| **Why it matters** | Frames the problem as repeated containment under uncertainty and states the naive cost before any data structure appears. |
| **Recovery** | If the run dies before this with `data/zones/shillong_osm.geojson is missing`: `git checkout data/zones/shillong_osm.geojson`. If with `build failed`: run `make` and read the first error; if it is not a one-line fix, go to the backup plan. |

Press **Enter**.

## Step 2 — Brute force

| | |
|---|---|
| **Action** | Enter (prompt 1). |
| **Screen** | Terminal. |
| **Say** | *"Brute force compares the query box of each fix with every zone box: O(n) per fix. In this hour that is 141,130 queries times 5,038 zones, 711 million box tests. Only about 14 zones per query actually overlap; the exact geometry runs on those. I keep brute force forever: it is the oracle every fast structure is checked against and the denominator of every speedup."* |
| **Expected output** | `index brute-force` · `nodes  5038   max depth  1   memory  196.9 KB` · `pruning: 5038 zones -> 13.91 candidates per query  (362x reduction)` · `index queries                 141130` · `candidates returned          1963571   (13.91 avg per query)` · `exact geometry tests         1963554` · `141130 queries × 5038 zones = 711012940 box tests this hour, O(n) per fix.` |
| **Point out** | `13.91 candidates per query` (remember this number: it reappears unchanged in step 3) and the 711 million. |
| **Why it matters** | The baseline, and the idea that the index is a *filter* that only needs to return a conservative superset. |
| **Recovery** | `tools/review.sh 2` reprints it from the saved run. |

Press **Enter**.

## Step 3 — Quadtree and R-tree

| | |
|---|---|
| **Action** | Enter (prompt 2). |
| **Screen** | Terminal. |
| **Say** | *"Same hour, two indexes. The quadtree partitions space into disjoint cells; the R-tree partitions the items into tight envelopes that may overlap, bulk-loaded with STR. Look at the candidates per query and the exact-geometry count: identical to brute force. The answer is the same by construction; what changes is how it is found: a descent through only the cells or envelopes that overlap the query box, out of 1,229 or 753 nodes, instead of 5,038 box tests."* |
| **Expected output** | `index quadtree` · `nodes  1229   max depth  5   memory  360.9 KB` · `pruning: 5038 zones -> 13.91 candidates per query  (362x reduction)` · `exact geometry tests         1963554` — then `index r-tree` · `nodes  753   max depth  4   memory  267.5 KB` · same `13.91` and same `1963554`. |
| **Point out** | Same 13.91 and 1963554 three times; different node counts and depths (1229/5 vs 753/4). |
| **Why it matters** | Two different partitioning strategies, one contract. |
| **Recovery** | `tools/review.sh 3`. |

Press **Enter**.

## Step 4 — Correctness

| | |
|---|---|
| **Action** | Enter (prompt 3). Takes a few seconds (benchmark section 5 runs live). |
| **Screen** | Terminal. |
| **Say** | *"Faster is only worth something if the answer never changes. The three event streams are compared byte for byte: 27,935 events, identical. Then the benchmark's equivalence section: 18,000 queries, zero mismatches. The test suite goes further: about 55,000 hostile queries per index configuration against brute force, auditing the tree's invariants after every operation."* |
| **Expected output** | `✓ identical event streams: brute force = quadtree = R-tree, 27935 events, byte for byte` · then `5. EQUIVALENCE   every index must return EXACTLY brute force's set (radii 80 m, 400 m, 2 km)` with three rows: `50 zones    6000 queries      1250 hits   quadtree 0   r-tree 0   geohash 0 mismatches`, `500 zones ... 13249 hits ... 0 ... 0 ... 0`, `5000 zones ... 134746 hits ... 0 ... 0 ... 0`. |
| **Point out** | The green ✓ line, then the three zeros per row. |
| **Why it matters** | This is the project's central claim demonstrated, not asserted. |
| **Recovery** | `tools/review.sh 4`. If it ever prints `✗ EVENT STREAMS DIFFER`, the script stops on purpose: that would be a genuine correctness failure. Say so plainly, note that CI and every recorded run have passed this check, and continue with `tools/review.sh 5`. Do not try to debug it live. |

Press **Enter**.

## Step 5 — Transitions for one tourist and one zone

| | |
|---|---|
| **Action** | Enter (prompt 4). |
| **Screen** | Terminal. |
| **Say** | *"One tourist and one forest zone. At seven seconds the position is UNCERTAIN: the 35-metre accuracy disc straddles the edge. At fourteen seconds ENTER: 15 metres inside, three agreeing fixes, five seconds of dwell. At two minutes fifteen, EXIT: 25 metres outside, three fixes. Uncertain is reported but never acted on. APPROACHING is a predicted crossing with a time estimate. And every zone a person has open state with is observed every fix, even when the index no longer returns it, so an exit cannot be missed."* |
| **Expected output** | `TID-00020 and Dense Forest - Weak Signal 1:` · `00:00:07  UNCERTAIN    TID-00020  Dense Forest - Weak Signal 1 ±35m accuracy, -14m from edge` · `00:00:14  ENTER        ... 35m deep, ±35m` · `00:02:15  EXIT         ... -67m deep, ±35m` · further `UNCERTAIN` and `APPROACHING ... ETA 199s, 127m out` lines · `... 5 more`. |
| **Point out** | The three kinds in order (UNCERTAIN → ENTER → EXIT), and the `±35m` on each line. |
| **Why it matters** | Three-valued containment and the hysteresis state machine, on real geometry. |
| **Recovery** | `tools/review.sh 5`. |

Press **Enter**.

## Step 6 — Noise: the hour with and without the hysteresis filter

| | |
|---|---|
| **Action** | Enter (prompt 5). Takes a few seconds (one more engine run with `--no-hysteresis`, then benchmark section 10). |
| **Screen** | Terminal. |
| **Say** | *"Same trajectories, same GPS noise, filter off: 17,906 entries instead of 602, all flaps from drift at a boundary. The honest check is the second table, against noise-free truth on the same paths: under realistic correlated drift the filter reports 502 of the 521 real transitions, about 96 per cent. Under pure white noise it keeps 66 per cent, which is the price of suppressing twenty thousand false ones."* |
| **Expected output** | Table: `zone entries   602   17906` · `zone exits   554   17862` · `uncertain   7865   24016` · `flaps suppressed   3175   0` — then `10. HYSTERESIS A/B` with rows `white noise (rho=0)   1934   521   20848   343   100.9%` and `realistic drift (rho=0.9)   1934   521   16168   502   100.1%`. |
| **Point out** | 602 vs 17,906; then 502 vs 521. If asked what `excess cut 100.9%` means: the filter removed slightly *more* than the noise-induced excess, i.e. it also suppressed a few real transitions, which is exactly what the filtered-vs-target column exposes. |
| **Why it matters** | The filter is measured against ground truth, not just against itself; this replaced a flawed metric (DEFECT_LOG, pass 6). |
| **Recovery** | `tools/review.sh 6` (re-runs the no-hysteresis hour and benchmark section 10, about 6 s). |

Press **Enter**.

## Step 7 — History: the persistent quadtree

| | |
|---|---|
| **Action** | Enter (prompt 6). Takes a few seconds (benchmark section 9 live). **Skip the narration, not the keypress, if short on time.** |
| **Screen** | Terminal. |
| **Say** | *"Rules change over time, so the index is persistent. Each change copies one root-to-leaf path, about 15 nodes, and shares everything else. 5,001 versions cost 71,314 nodes instead of 930,257: 13 times sharing. A rule change copies nothing, because validity is an append-only log per zone. And a query against the past is the same traversal from an older root."* |
| **Expected output** | `9. PERSISTENT QUADTREE` table: `51   523   1903   3.6x`, `201   2582   14633   5.7x`, `1001   14025   158257   11.3x`, `5001   71314   930257   13.0x`, each row followed by two **live** microsecond columns and a past/now ratio · mutation table: `add (new zone)   avg 14.7   max 15` · `replace (moved zone)   avg 29.0   max 30` · `validity change   avg 0.0   max 0` · `remove   avg 14.3   max 15`. |
| **Point out** | The node counts and the `13.0x`; the `validity change 0`. **The µs columns here are live timings** and will differ slightly from RESULTS.md (today: 0.529 / 0.887 µs at 5,001 versions vs 0.548 / 0.911 committed). The counts are deterministic. |
| **Why it matters** | The advanced structure: path copying with structural sharing, measured. |
| **Recovery** | `tools/review.sh 7`. |

Press **Enter**.

## Step 8 — Performance (committed results, not a live timing)

| | |
|---|---|
| **Action** | Enter (prompt 7). Instant. |
| **Screen** | Terminal. |
| **Say** | *"These are the committed results of `make bench`, not a live timing: the screen shows when and on which commit and machine they were taken. Medians of 11 interleaved rounds, paired ratios against my own brute force, a checksum gate on every row. At 100,000 zones in one district the R-tree answers the index query about 224 times faster than the scan, the quadtree about 34 times. Two caveats I want to state myself: the k column, about 98 zones genuinely overlap each query at that size and every index must return all of them, which caps the speedup; and the ratios move between sessions on this laptop, the R-tree's by more than two times, so I quote ranges and a floor of about 100×."* |
| **Expected output** | `date:     2026-10-08T21:59Z` · `commit:   a29ef13` · `cpu:      Apple M4` · `power:    Battery Power` · `load:     2.61 3.34 3.04` · seven-row table ending `100000    238.581    7.070    1.065   33.7 (31.7-33.7)   223.9 (210.7-223.9)   98.45` · `ranges: min-max over the 4 runs of that session (make bench, make bench-variation)` · the paragraph explaining k and the session drift · `To measure now: ./build/safetrail_bench --only 1 (about 15 s).` |
| **Point out** | The `commit:` and `date:` lines first (so nobody thinks it is live), then the 100,000 row, then the k/query column (0.10 → 98.45). |
| **Why it matters** | The measured result with its protocol and its limits in the same breath. |
| **Recovery** | `tools/review.sh 8` (reads the CSVs only). If asked to measure live and you have 20 s: `./build/safetrail_bench --only 1`; say beforehand that the ratio will differ from the committed table and that this is why the docs quote ranges. |

Press **Enter**.

## Step 9 — Engineering: tests, determinism, dashboard

| | |
|---|---|
| **Action** | Enter (prompt 8). About 20 s. No further prompt. |
| **Screen** | Terminal; then switch to the browser. |
| **Say** | *"Forty-five test files, each structure against its oracle, all passing; same seed, byte-identical output; and the dashboard is one HTML file with no server. Not run here because they take minutes: mutation testing, 22 of 22 injected bugs caught; the sanitizers; static analysis. All of them run in CI on this commit and are green."* |
| **Expected output** | 45 lines `[ ok ] <name>   N checks, 0 failed` (plus a few indented diagnostic lines) · `ALL TESTS PASS` · `determinism: identical output across runs` · the four "not run here" lines (`make mutation`, `make ubsan`, `make analyze`, `make validate`) · `dashboard  dashboard.html  (720 frames, self-contained -- just open it)` · `━━ done ━━` · `Open dashboard.html in a browser (macOS: open dashboard.html). One file, no server.` |
| **Point out** | `ALL TESTS PASS`, the determinism line, the `720 frames` line. If the test list scrolls too fast, say *"forty-five files, I'll show the summary line"* and point at `ALL TESTS PASS`. |
| **Why it matters** | Evidence that the engineering is checked, not described. |
| **Recovery** | `tools/review.sh 9` reruns tests, determinism and the dashboard. If `make test` fails live, do not debug; say the suite is green in CI on this commit (run 38035618082) and move to the dashboard from the warm-up, which is already on disk. |

Switch to the browser tab with `dashboard.html` and press **⌘R**.

---

## The dashboard (one to two minutes)

`dashboard.html` is the two-hour, 60-tourist run over the real zones (720
frames), generated by `make dashboard`, which step 9 just ran. All controls were
exercised in a browser on 2026-10-10 against this commit's output.

| # | Action | Where | What you see | Say |
|---|---|---|---|---|
| 1 | Click **`index: off`** once, twice, three times | bottom bar, third button | the button reads `index: brute force`, then `index: quadtree` (a grid of disjoint cells over the map), then `index: r-tree` (overlapping envelopes) | *"The actual structure drawn on the real map: the quadtree's disjoint cells, the R-tree's overlapping envelopes."* |
| 2 | Click a moving dot | the map | the **tracked tourist** panel fills in for that person, and the cells or envelopes their query touches are highlighted and counted | *"This person's query touches these cells; everything else is never visited."* |
| 3 | Read the **main experiment** panel | top of the right panel | the committed scaling table (the `100,000` row: `238.6 µs`, `7.07`, `1.07`, `34×`, `224×`, `98.45`) and `0 mismatches` from a live check of 3,000 random queries over this run's zones | *"The same committed table, and a live correctness check: 3,000 random queries, zero mismatches."* |
| 4 | Drag the **timeline slider** | bottom bar | **rules in force** updates its *as of* time; before 00:30 Wards Lake is greyed out, it comes into force at 00:30; the **zone change log** and **persistent index** panels follow (consecutive versions, copied path highlighted, shared subtrees dim) | *"Rules change at 00:30 and 00:45; the persistent index keeps every version and the dashboard shows the copied path versus the shared subtrees."* |
| 5 | Click **`accuracy discs`** | bottom bar | each dot gets its uncertainty disc in the state colour; purple is Uncertain | *"The GPS uncertainty drawn to scale with the state colour; purple means the disc straddles a boundary."* |
| 6 | Scroll the right panel to **event stream** | bottom of the panel | ENTER / EXIT / UNCERTAIN lines with distance and accuracy | *"The engine's only output: transitions, not states."* |

`play` / `pause` is the first button; the replay loops. Deep links work:
`dashboard.html#index=rtree&frame=400&pause` opens on that view (this is how the
README screenshots were taken).

The panel also has **open incidents**, **counters**, **course concepts
demonstrated** and **evidence log** (Merkle inclusion proofs with a `verify
offline` button). These are extensions; show them only if asked.

## The slides (optional, or as the fallback)

```bash
open docs/REVIEW_SLIDES.html
```

| Slide | Title | When to use it |
|---|---|---|
| 1 | SafeTrail, efficient tourist geofencing | opening, if you start with slides |
| 2 | Geofencing is repeated point-in-polygon, under uncertainty | the problem |
| 3 | Check every zone against every fix: O(n) per fix | brute force |
| 4 | Quadtree partitions space; R-tree partitions items | the two indexes (dashboard screenshots) |
| 5 | The index finds candidates; geometry decides; a state machine reports | filter-refine, three-valued containment, hysteresis |
| 6 | A persistent quadtree: "what were the rules at 00:45?" | history |
| 7 | Faster is only worth something if the answer never changes | correctness and the ten defects |
| 8 | Measured against our own brute force, as paired ratios | the headline table (generated from the CSVs) and the chart |
| 9 | One path from fix to event; everything else hangs off the event stream | architecture |
| 10 | What it does not do, said before being asked | limitations and conclusion |

Keys: → or Space next, ← previous, F fullscreen, Home / End, `#8` in the URL
jumps to slide 8. The offline copy is
[SafeTrail_review_slides.pdf](SafeTrail_review_slides.pdf) in this folder.

## Timing for a five-minute slot

| Step | Budget | Cumulative |
|---|---|---|
| banner + 1 | 0:30 | 0:30 |
| 2 | 0:40 | 1:10 |
| 3 | 0:40 | 1:50 |
| 4 | 0:40 | 2:30 |
| 5 | 0:40 | 3:10 |
| 6 | 0:30 | 3:40 |
| 7 | 0:25 (press Enter and skip the narration if behind) | 4:05 |
| 8 | 0:30 | 4:35 |
| 9 + closing line | 0:25 | 5:00 |
| dashboard | 1:00–2:00 | if the slot allows |

## If a step misbehaves

See [07_DEMO_FAILURE_PLAYBOOK.md](07_DEMO_FAILURE_PLAYBOOK.md). The one-line
version: ⌃C, then `tools/review.sh N` for the step you were on; if that fails
too, open `dashboard.html` from the warm-up and continue from the slides.
