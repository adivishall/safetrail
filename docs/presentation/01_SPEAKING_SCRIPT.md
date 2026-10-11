# Speaking script

Two versions of what to say. Both follow the order of `make review PAUSE=1`
([02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md)), because the terminal
output is the visual; the slides are optional. Speak at a normal pace (about
130–150 words a minute). You do not have to say these sentences word for word;
you do have to keep the numbers and the caveats exactly as written, because they
come from the committed benchmark CSVs and the test output.

Vocabulary rules for both versions:

- "simulated tourists over real OpenStreetMap zones", never "real users".
- "faster than my own brute force", never "faster than PostGIS" or any library.
- "about 224×, and it has ranged from about 100× to 235× across sessions on this
  laptop", never a single number as a guarantee.
- "O(log n + k) expected, O(n) worst case", never "guaranteed logarithmic".
- "22 of 22 injected bugs caught", never "proven correct".
- "no production-scale load test has been run", said before anyone asks.

---

## Five-minute version (demo-focused)

Sections 1–9 plus the closing line are about 656 spoken words: 262–292 s
of speech at 135–150 words a minute, which leaves room for the keypress waits
inside five minutes. Sections 10 and 11 are extras for when time remains. If you
at step 7, press Enter and skip its words.

### 1. Opening and problem statement — 0:00–0:30

**On screen:** the terminal; `make review PAUSE=1` has just been started and the
banner and step 1 are printing.
**Action:** none yet.

> Good morning. My project is SafeTrail, a geofencing engine in C++17 with hand-built data structures. Hazard zones are polygons with rules; every person's phone reports a GPS position once a second; and for every person and every nearby zone, the engine decides inside, outside or uncertain, and emits an event only when that changes. The zones are real OpenStreetMap geometry around Shillong; the people are simulated, which gives me ground truth. No spatial library, no dependencies beyond the compiler.

**Point out:** `zones loaded 5038`, `40 tourists`, the GPS error line (4 m open
sky, 35 m multipath), the real zone names.
**Transition:** *"First, the naive way."*

### 2. Why brute force gets expensive — 0:30–1:05

**Action:** Enter.

> Brute force compares each fix's query box with every zone's bounding box: O(n) per fix. In this hour that is 141,130 queries times 5,038 zones, 711 million box tests, to find about fourteen candidates per query. I keep brute force forever: it is the oracle every fast structure is checked against, and the denominator of every speedup.

**Point out:** `13.91 candidates per query`, `711012940 box tests`.
**Transition:** *"The indexes find the same fourteen without touching the other five thousand."*

### 3. The quadtree and the R-tree — 1:05–1:45

**Action:** Enter.

> Same hour, two indexes. The quadtree partitions space into disjoint cells; the R-tree partitions the items into tight envelopes that may overlap, bulk-loaded with STR packing. Candidates per query and the exact-geometry count are identical to brute force: 13.91 and 1,963,554. Same answer by construction; what changes is the search, a descent through only the cells or envelopes that overlap the query box, out of 1,229 or 753 nodes, instead of 5,038 box tests.

**Point out:** the same `13.91` and `1963554` three times; `nodes 1229 / depth 5`
versus `nodes 753 / depth 4`.
**Transition:** *"Identical by construction is a claim. The next step checks it."*

### 4. The fast indexes preserve correctness — 1:45–2:20

**Action:** Enter; it runs for a few seconds.

> The three event streams are compared byte for byte: 27,935 events, identical. Then the benchmark's equivalence section: eighteen thousand queries, zero mismatches. The test suite goes further: about 55,000 hostile queries per index configuration against brute force, with the tree's invariants audited after every insert, removal and rebuild.

**Point out:** the green `✓ identical event streams` line, then the zeros.
**Transition:** *"Here is what one person's hour looks like."*

### 5. ENTER, EXIT and uncertain readings — 2:20–3:00

**Action:** Enter.

> One tourist, one forest zone. At seven seconds: UNCERTAIN, the 35-metre accuracy disc straddles the boundary. At fourteen seconds: ENTER, 15 metres inside, three agreeing fixes, five seconds of dwell. At two minutes fifteen: EXIT, 25 metres outside, three fixes. Uncertain is reported but never acted on. And every zone a person has open state with is observed every fix, even when the index no longer returns it, so an exit cannot be missed; that rule was a real bug I found.

**Point out:** UNCERTAIN → ENTER → EXIT in order, the `±35m` on each line.
**Transition:** *"This is why the filter matters."*

### 6. Hysteresis and noise — 3:00–3:30

**Action:** Enter; it runs for a few seconds.

> Same trajectories, same noise, filter off: 17,906 entries instead of 602, nearly all flaps at boundaries. The honest check is the second table, against noise-free truth on the same paths: under realistic drift the filter reports 502 of the 521 real transitions, 96 per cent. Under white noise only 66 per cent, which is the price of suppressing twenty thousand false ones.

**Point out:** `602` vs `17906`; `502` vs `521`.
**Transition:** *"Zones also change over time."*

### 7. Persistent indexing and temporal queries — 3:30–3:55

**Action:** Enter; it runs for a few seconds. *Skip the words if behind.*

> Rules change, so the index is persistent: each change copies one root-to-leaf path, about fifteen nodes, and shares the rest. Five thousand and one versions cost 71,314 nodes instead of 930,257, thirteen times sharing. A rule change copies nothing, and a query about the past costs the same as one about now.

**Point out:** `13.0x`, `validity change 0`.
**Transition:** *"Now the numbers."*

### 8. Benchmark results — 3:55–4:35

**Action:** Enter.

> These are the committed results of `make bench`, not a live timing; the screen shows the date, commit and machine. Medians of eleven interleaved rounds, paired ratios against my own brute force, a checksum gate on every row. At 100,000 zones in one district the R-tree is about 224 times faster than the scan, the quadtree about 34 times. Two caveats. The k column: about 98 zones genuinely overlap each query at that size, every index must return them all, and that caps the speedup. And ratios move between sessions on this laptop, the R-tree's by more than two times, so I quote ranges and a floor of about 100.

**Point out:** `commit: a29ef13`, `date:`; the `100000` row; `k/query` rising to 98.45.
**Transition:** *"Finally, the engineering."*

### 9. Tests, determinism and the dashboard — 4:35–5:00

**Action:** Enter; about twenty seconds of test output.

> Forty-five test files, all passing. Same seed, byte-identical output. And the dashboard is one HTML file, no server. Not run here because they take minutes: mutation testing, 22 of 22 injected bugs caught; sanitizers; static analysis. All green in CI on this exact commit.

**Closing line:**

> Five hand-built data structures turn an O(n)-per-fix problem into a fast one. What I am proudest of is not the speedup but the evidence that none of them ever changes the answer, and the ten real bugs that evidence found. I can show the dashboard, or take questions.

### 10. The dashboard, if there is time — 1–2 minutes

**Action:** switch to the browser tab, ⌘R, then the six clicks in the runbook.

> The two-hour, sixty-tourist run on the real zones. The index button draws the actual structure on the map: the quadtree's disjoint cells, the R-tree's overlapping envelopes. Click a person and you see the cells their query touches. The panel has the same committed table and a live check: three thousand random queries, zero mismatches. Scrub the timeline and the rules in force follow it; Wards Lake comes into force at half past. Purple discs are Uncertain.

### 11. Limitations, if asked or if time remains — 20 seconds

> What it does not do: the people are simulated; both spatial trees have an O(n) worst case and O(n) removal; the geometry uses tolerances, not exact predicates; it is single-threaded, in memory, one process; and no production-scale load test has been run, so throughput for thousands of users is reasoned about, not demonstrated.

---

## Ten-minute version (more technical)

About 1,276 spoken words: 8.5–9.8 minutes at 130–150 words a minute,
leaving time inside ten minutes for the keypress waits and the six dashboard
clicks. Same order; the extra time goes into how each structure works, how
correctness is established, how the benchmark is run, and the limitations.

### 1. Opening and problem statement — 0:00–0:50

**On screen:** terminal, banner and step 1.

> Good morning. My project is SafeTrail, a geofencing engine in C++17 built on hand-written spatial and temporal data structures. Hazard zones are polygons with rules and validity windows; each person's phone reports a GPS fix every second with an accuracy radius; for every fix and every zone near it, the engine decides inside, outside or uncertain, and emits an event only when that changes. Containment is repeated millions of times, so finding the few zones near a fix is the cost; and a GPS position is a disc, not a point. The 38 zones are real OpenStreetMap geometry around Shillong, padded with 5,000 synthetic ones; the 40 tourists are simulated, deliberately, because a simulation knows where everyone really was. Everything is hand-written, no dependencies beyond the compiler.

**Point out:** `5038 (38 authored + 5000 synthetic)`, `GPS error model 4 m / 35 m (25% of fixes)`.
**Transition:** *"Let me start with what the naive solution costs."*

### 2. Why brute force becomes expensive — 0:50–1:35

**Action:** Enter.

> My brute-force index tests the query box, the bounding box of the accuracy disc, against every zone's box, O(n) per fix, and runs exact geometry only on the overlaps. This hour: 141,130 queries times 5,038 zones, 711 million box tests, to find 13.91 candidates per query, a 362-fold reduction before any geometry. Filter then refine is the shape of every spatial query system; the one requirement is that the filter is conservative, because a dropped zone is a missed alert. Brute force stays forever: too simple to be wrong, the oracle for every index and the baseline for every speedup.

**Point out:** `13.91`, `362x reduction`, the 711 million line.
**Transition:** *"The two indexes reach the same fourteen a different way."*

### 3. The quadtree and the R-tree — 1:35–2:45

**Action:** Enter.

> The quadtree partitions space: the root is fitted to the data, each node splits into four, and a zone's box descends only into a child that fully contains it, otherwise it stays put. Every zone lives in exactly one node, so removal is unambiguous and a query cannot return duplicates. The R-tree partitions the items: nodes hold the tight envelope of their contents, all leaves at one depth, envelopes may overlap, so a query may descend several branches. I bulk-load it with sort-tile-recursive packing: sort by longitude, slice, sort each slice by latitude, pack runs of eight; that alone is worth about six times in query speed over building by insertion. On screen, both return 13.91 candidates, like brute force. Both are O(log n plus k) expected and O(n) worst case: give every zone the same box and nothing separates them.

**Point out:** identical `13.91` / `1963554`; the node counts.
**Transition:** *"Same answer by construction is a claim. Here is the check."*

### 4. Correctness — 2:45–3:40

**Action:** Enter; runs a few seconds.

> The three event streams are compared byte for byte: 27,935 events, identical. The benchmark's equivalence section: 18,000 queries, zero mismatches. The real evidence is the test suite. A differential test compares five index configurations with brute force over seven workload profiles chosen for where spatial indexes break: identical boxes, zero-area boxes, the poles, past longitude 180; about 55,000 queries per configuration, with the tree's invariant audit after every operation. The whole engine then runs under all four indexes and must emit bit-identical events. And the tests are themselves tested: mutation testing injects 22 realistic bugs one at a time and requires a test to fail for each. All 22 are caught.

**Point out:** the ✓ line; the three rows of zeros.
**Transition:** *"Now one person, one zone."*

### 5. ENTER, EXIT and uncertain readings — 3:40–4:30

**Action:** Enter.

> One tourist against one forest zone. Containment is three-valued: ray casting decides inside or outside for the point, then the signed distance to the boundary is compared with the accuracy radius; only when the whole disc is on one side do I say Inside or Outside. At seven seconds: UNCERTAIN. At fourteen: ENTER, more than 15 metres inside, three consecutive fixes, five seconds of dwell. At two fifteen: EXIT, more than 25 metres outside, three fixes; a Schmitt trigger. Two rules came from bugs. Uncertain is reported but never acted on: it cannot end Inside and it cancels a pending exit. And every zone a person has open state with is observed every fix, whether or not the index returned it; before that fix, a walk across 60 zones produced 60 entries and zero exits.

**Point out:** the three event kinds in order; `±35m`.
**Transition:** *"How much does the filter actually buy?"*

### 6. Hysteresis and noise — 4:30–5:10

**Action:** Enter; runs a few seconds.

> Filter off, same trajectories and noise: 17,906 entries instead of 602. But off-versus-on only shows that the filter removes transitions, not that it removes the right ones; my first experiment made exactly that mistake. So the second table compares four runs on identical trajectories: noise-free without and with the filter, noisy without and with it. The noise-free filtered run is the target, 521. Under realistic drift the filter reports 502, 96 per cent; under white noise 343, 66 per cent. I show both because the second is the real cost.

**Point out:** `602 / 17906`; `502` vs `521`; `343` vs `521`.
**Transition:** *"Zones also change in time, and that needs its own structure."*

### 7. Persistent indexing and temporal queries — 5:10–6:10

**Action:** Enter; runs a few seconds.

> Rules change, so the zone index is persistent: a path-copying quadtree. Nodes are immutable; a change copies only the root-to-leaf path it touches, about fifteen nodes, and shares the other three children at every level. 5,001 versions cost 71,314 nodes instead of 930,257: thirteen times sharing. A validity change copies nothing, because validity is an append-only log per zone. The counts are deterministic; the microsecond columns are live. Two time axes stay separate: transaction time, when the operator changed the rules, selects the version; valid time says when a zone is in force. The AVL interval tree sits beside this for "every zone in force at time t"; it is not on the per-fix path, where validity is an O(1) check per candidate.

**Point out:** `13.0x`; `validity change avg 0.0 max 0`.
**Transition:** *"Now the performance numbers, with their caveats."*

### 8. Benchmark results — 6:10–7:20

**Action:** Enter.

> These are the committed results of `make bench`, not a live timing; the date, commit and machine are on screen. I replaced my first protocol after its headline could not be reproduced. Now each sample runs at least 20 milliseconds; eleven rounds with the contenders rotated so drift lands on all of them; medians with interquartile ranges; paired speedups; and a checksum of every contender's results, so the run fails if answers differ. At 100,000 zones in one district: brute force about 239 microseconds per query, the quadtree about 7, the R-tree about 1; speedups of about 34 and 224 times, ranges over four runs in brackets. The k column is the ceiling: about 98 zones genuinely overlap each query and every index must return them all, which is why the quadtree's ratio stops growing. At constant density the same code is 543 and 1,042 times faster. And ratios move between sessions on this laptop, the R-tree's from 102 to 235 times, so I quote ranges and a floor of about 100.

**Point out:** `commit: a29ef13`; the `100000` row; `k/query` from 0.10 to 98.45.
**Transition:** *"And the engineering that holds it together."*

### 9. Tests, determinism and the dashboard — 7:20–8:00

**Action:** Enter; about twenty seconds.

> Forty-five test files, 11,844 checks, every structure against an independent oracle. Determinism is gated: the same seed must give byte-identical output, and the Make and CMake builds must agree byte for byte, which is why floating-point contraction is switched off. Not run here because they take minutes: the mutation check, 22 of 22; the sanitizers; the static analyser, zero findings. All seven CI jobs are green on this commit.

**Point out:** `ALL TESTS PASS`, the determinism line, `720 frames`.
**Transition:** *"The dashboard puts the structures on the real map."*

### 10. The dashboard — 8:00–9:00

**Action:** switch to the browser tab, ⌘R, then the six clicks from the runbook.

> The two-hour, sixty-tourist run on the real zones, one self-contained file. The index button draws the actual structure: the quadtree's disjoint cells, then the R-tree's overlapping envelopes. Click a person and the cells their query touches light up. The panel shows the committed table and a live check: three thousand random queries, zero mismatches. Scrub the timeline and the rules in force follow it, Wards Lake at half past; the persistent-index panel shows the copied path bright and the shared subtrees dim. Purple discs are Uncertain.

### 11. Limitations and conclusion — 9:00–10:00

> What it does not do, before anyone asks. The people are simulated. Both spatial trees have an O(n) worst case, and removal is O(n) in every index because there is no id-to-node map. The interval tree reports k results in O(min(n, (k+1) log n)), not O(log n plus k). Geometry uses tolerances, not exact predicates. Single-threaded, in memory, one process. Benchmarks from one laptop, quoted as ranges. And no production-scale load test has been run, so throughput for thousands of users is reasoned about, not demonstrated. To close: five hand-built data structures turn an O(n)-per-fix problem into a fast one, and the evidence that none of them changes the answer found ten real bugs in code that passed its tests. Thank you.

---

## If you lose your place

Look at the terminal. The step banner (`━━ N. ... ━━`) tells you where you are;
the runbook's "Say" cell for that step is a complete sentence you can read out.
