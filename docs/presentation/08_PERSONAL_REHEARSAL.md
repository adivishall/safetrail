# Personal rehearsal and self-assessment

Use this after each practice run. It does not record that a rehearsal has
happened; you do, by filling in the dates and scores at the bottom.

## Ten key facts you must know cold

Every one is from the committed CSVs, the test output or the code at `76b63af`.

1. **What it is:** a C++17 geofencing engine; for each GPS fix (a disc, not a point) it decides inside / outside / uncertain for every nearby zone and emits an event only on a confirmed change.
2. **The five structures:** brute-force scan (the oracle), quadtree (partitions space), STR-packed R-tree (partitions items), AVL interval tree (which validity windows contain t), persistent path-copying quadtree (what were the rules at t). All hand-written.
3. **The bounds:** quadtree and R-tree query O(log n + k) *expected*, O(n) *worst case*; removal O(n) in all indexes; interval tree stab O(min(n, (k + 1) log n)) *guaranteed*, insert/remove O(log n); persistent change O(depth) new nodes, validity change 0.
4. **The headline measurement:** 100,000 zones in one district, brute force 239 µs per query, quadtree 33.7× (31.7–33.7× over 4 runs), R-tree 224× (211–224×), identical results; across sessions the R-tree has ranged 102–235× and the quadtree 26–35×, so quote ranges and a floor of ~100×.
5. **Why the speedup caps:** k. About 98 zones genuinely overlap each query at 100,000 zones in one district and every index must return them all. At constant density (k ≈ 1) the same code is 543× / 1042× faster at 100,000 zones.
6. **The demo scenario:** 38 real OpenStreetMap zones + 5,000 synthetic = 5,038; 40 simulated tourists; one hour; 141,130 queries; 13.91 candidates per query; 711 million box tests under brute force; 27,935 events, byte-identical under all three indexes.
7. **Hysteresis:** ENTER needs 15 m inside, 3 agreeing fixes, 5 s dwell; EXIT needs 25 m outside, 3 fixes; Uncertain is reported, never acted on. Filter off: 17,906 entries vs 602. Against noise-free truth the filter reports 502 of 521 transitions (96%) under realistic drift, 343 (66%) under white noise.
8. **Correctness evidence:** differential tests, ~55,000 hostile queries per index configuration over 7 profiles, invariants audited after every operation; bit-identical event streams under all four indexes; mutation testing 22/22; UBSan locally, ASan+UBSan and g++ -Werror in CI; 45 test files, 11,844 checks; determinism gated.
9. **Ten real defects**, eight in the engine and two in measurement, in code that passed its tests. The three to tell: zones that left the candidate window were never observed again (60 entries, 0 exits); hysteresis let Uncertain through (about 60% of the demo's enter/exit events; 2,440 → 987 entries); the query box was 0.11% too narrow (1,728 of 160,000 sampled points outside it).
10. **Limitations:** simulated people over real geography; O(n) worst cases and O(n) removal; tolerance-based geometry; no antimeridian; single-threaded, in-memory, one process; one laptop's benchmarks; no production-scale load test.

## Ten things you must physically demonstrate

Tick when you have done each one yourself, without reading, in a timed run.

- [ ] 1. `cd` into the quoted repository path and start `make review PAUSE=1`.
- [ ] 2. Advance with exactly one Enter per prompt (8 prompts) without pressing early.
- [ ] 3. Point at `13.91 candidates per query` in step 2 and again, unchanged, in step 3.
- [ ] 4. Point at the `✓ identical event streams ... 27935 events` line and the zeros in step 4.
- [ ] 5. Read the UNCERTAIN → ENTER → EXIT lines of step 5 aloud with the times.
- [ ] 6. Point at `602` vs `17906` and then `502` vs `521` in step 6.
- [ ] 7. In step 8, point at `commit: a29ef13` and say "committed results, not live" before reading the 100,000 row.
- [ ] 8. After step 9, switch to the browser, ⌘R, cycle `index: off` → brute force → quadtree → R-tree.
- [ ] 9. Click a tourist dot; drag the slider past 00:30 and show Wards Lake coming into force; toggle `accuracy discs`.
- [ ] 10. Open `docs/REVIEW_SLIDES.html`, jump to slide 8 with → or `#8`, go fullscreen with F, leave with Esc.

## Ten questions to answer without reading

Say each answer aloud in under 20 seconds, then check it against
[04_TEACHER_QA.md](04_TEACHER_QA.md).

1. Why not just loop over every zone?
2. Is O(log n + k) guaranteed for your quadtree and R-tree?
3. Why does the R-tree's speedup keep growing while the quadtree's plateaus?
4. How do you know the indexes return the same answers as brute force?
5. What does 22/22 in mutation testing prove, and what does it not prove?
6. Why did GPS gaps cause missed exits, and what fixed it?
7. What does hysteresis do, and what does it cost?
8. What is the interval tree used for, and what is its bound?
9. Why do your speedup numbers change between sessions, and how do you report them?
10. Has this been tested at production scale?

## Ways you might accidentally exaggerate (and the honest phrasing)

| Tempting | Say instead |
|---|---|
| "The R-tree is 224× faster." | "About 224× in that session on this laptop, against my own brute force; it has ranged from about 100× to 235× across sessions." |
| "O(log n + k) queries." | "O(log n + k) expected; O(n) worst case on adversarial data." |
| "The interval tree is O(log n + k)." | "Finding one is O(log n); reporting k is O(min(n, (k + 1) log n)), a guarantee from the AVL height but not O(log n + k)." |
| "Proven correct." | "Differentially tested against an oracle on hostile workloads, with invariants audited and mutation testing at 22/22." |
| "Tested with real users / real GPS." | "Real OpenStreetMap geography; simulated people and a GPS error model, because simulation gives ground truth." |
| "Faster than PostGIS." | "Faster than my own brute-force scan. No library comparison was made." |
| "Production-ready / scales to millions." | "Single-threaded, in memory, one process; no load test has been run; the scaling plan is reasoning, not evidence." |
| "The filter removes 94% of false alerts." (old number) | "The filter reports 96% of the noise-free target under realistic drift and 66% under white noise; it removes essentially all of the naive run's excess." |
| "I measured this live." (step 8) | "These are the committed results of make bench from 8 October; I can measure live if you like, and the ratio will differ." |
| "Real-time system." | "One simulated fix per second per person; per-fix cost about 5 µs at 5,000 zones on this laptop, single-threaded." |

## Timed 5-minute rehearsal checklist

Start a timer when you press Enter on `make review PAUSE=1`.

- [ ] Opening said while the banner and step 1 print (no dead air).
- [ ] Step 2 finished by 1:10.
- [ ] Step 4 finished by 2:30.
- [ ] Step 6 finished by 3:40.
- [ ] Step 7: Enter pressed; narration skipped if behind.
- [ ] Step 8: "committed, not live" said; finished by 4:35.
- [ ] Closing line said over the test output; timer stopped at or under 5:00.
- [ ] No number said that is not in [01_SPEAKING_SCRIPT.md](01_SPEAKING_SCRIPT.md).
- [ ] Dashboard shown only if time remained.

## Timed 10-minute rehearsal checklist

- [ ] Section 3 (both indexes, STR, worst case) finished by 2:50.
- [ ] Section 4 (differential tests, invariants, mutation) finished by 3:50.
- [ ] Section 7 (path copying, two time axes, where the interval tree is and is not used) finished by 6:30.
- [ ] Section 8 (protocol, k, constant density, session drift) finished by 7:40.
- [ ] Dashboard: all six clicks done between 8:20 and 9:20.
- [ ] Limitations said unprompted, including "no production-scale load test".
- [ ] Finished at or under 10:00 including the keypress waits.

## Self-scoring rubric (1–5 each; be harsh)

| Criterion | 1 | 3 | 5 |
|---|---|---|---|
| **Clarity** | read from the page, lost the thread | followed the order, some hesitation | spoke to the output on screen, one idea per sentence |
| **Technical understanding** | could not explain a structure beyond its name | explained what each structure does | explained why each exists, its trade-off and its worst case |
| **Demo quality** | wrong directory, early Enter, dead air | ran through with small stumbles | every pointer landed on the right number, dashboard clicks smooth |
| **Evidence** | quoted numbers without source | quoted the right numbers | quoted the number, its range, and where it comes from |
| **Honesty** | overclaimed at least once | caveats given when asked | caveats given unprompted; said "I don't know" cleanly once if needed |

| Date | Version (5 / 10 min) | Time taken | Clarity | Technical | Demo | Evidence | Honesty | One thing to fix next time |
|---|---|---|---|---|---|---|---|---|
| | | | | | | | | |
| | | | | | | | | |
| | | | | | | | | |

A run is ready when every column is 4 or 5 twice in a row.
