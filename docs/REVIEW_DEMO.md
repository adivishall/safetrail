# Project review — the 5-minute live demo

One command carries the whole story: `make review` runs the engine under each
index, proves the fast indexes give the brute-force answer, shows one tourist's
enter/exit history, the hour with and without the noise filter, the persistent
index, a live benchmark, the test suite, and finally writes the dashboard. With
`PAUSE=1` it waits for Enter between steps so you narrate at your own pace.
Every number on screen is read back from the program that just produced it, and
the scenario is seeded, so a rehearsal and the real thing print the same output.

## Before the review (10 minutes, once)

```bash
git status                      # clean tree, branch you mean to show
make review                     # warms the build (~1.5 min first time; ~50 s after)
open dashboard.html             # check it opens; leave the tab open
```

Then: terminal font at 16–18 pt, dark theme, window wide enough for 100
columns; close other heavy apps (the benchmark in step 8 is timed live);
plug in the charger. Open [REVIEW_SLIDES.html](REVIEW_SLIDES.html) in a second
browser tab if you want slides (→ to advance, F for fullscreen); the dashboard
does most of the visual work, so slides are optional.

**If anything breaks live:** the dashboard from the rehearsal is already on
disk, the committed tables are in [RESULTS.md](RESULTS.md), and every step can
be run on its own with `tools/review.sh N`. Nothing in the demo depends on the
network.

## The run

Start with `make review PAUSE=1`. The table gives you, for each step, what is on
screen, what to say, and the question it tends to raise.

| Time | Step · what runs | Show | Say | Likely question → answer |
|---|---|---|---|---|
| 0:00–0:30 | **1. Problem** — the scenario header prints: 38 real OpenStreetMap zones + 5,000 synthetic, 40 tourists, one fix per second, GPS error 4 m / 35 m | the real zone names (Sonapani Waterfall Cliff, Wards Lake, Deep Water…) | *"Hazard zones are polygons with rules. Every second, for every person, the question is inside, outside or uncertain — and GPS is a disc, not a point. Checking every zone against every fix is the naive cost."* | *Where is the data from?* → Zones: OpenStreetMap via the Overpass API, converted by `tools/osm_to_zones.py`. People: simulated, because simulation gives ground truth — we know where everyone really was. |
| 0:30–1:15 | **2. Brute force** — the same hour with `--index brute` | `pruning: 5038 zones -> 13.91 candidates`, and the line *141,130 queries × 5,038 zones = 711 million box tests* | *"Brute force compares the query box with every zone box — O(n) per fix. About 14 zones per query actually overlap; the exact geometry runs on those. We keep brute force forever: it is the oracle and the denominator."* | *Why keep slow code?* → Because it is too simple to be wrong. Every fast structure is checked against it (step 4), and every speedup is a ratio to it. |
| 1:15–2:00 | **3. Quadtree and R-tree** — the same hour under each | identical `13.91 candidates per query` and identical `exact geometry tests`; different node counts and depths | *"Same candidates, same geometry count — the answer is identical by construction. What changes is how it is found: a descent through the few cells or envelopes that overlap the query box instead of 5,038 box tests. Quadtree partitions space into disjoint cells; R-tree partitions the items into tight envelopes that may overlap, packed with STR bulk loading."* | *Why two indexes?* → Opposite trade-offs: the quadtree is simpler and path-copies cleanly (it is what the persistent index is built on); the STR R-tree reaches the same answers through fewer nodes and is faster past a few thousand zones. Building both is what makes that visible. |
| 2:00–2:45 | **4. Correctness** — the three event streams are compared, then `safetrail_bench --only 5` | `✓ identical event streams: brute force = quadtree = R-tree, 27,935 events, byte for byte`; `18,000 queries … 0 mismatches` | *"Faster is only worth something if the answer never changes. Three indexes, one engine, 27,935 events, byte for byte the same. The test suite goes much further: ~55,000 hostile queries per index against brute force, auditing the tree's invariants after every single operation."* | *How do you know the tests themselves work?* → Mutation testing: `make mutation` injects 22 realistic bugs one at a time (a skipped child, a missing refit, a skipped rebalance…) and requires a test to fail for each: 22/22. |
| 2:45–3:30 | **5. Transitions** — one real tourist and one real zone | an UNCERTAIN → ENTER → EXIT sequence with signed distances and ±accuracy | *"Here is one person and one forest zone: uncertain while the accuracy disc straddles the edge, ENTER after 15 m inside and three agreeing fixes, EXIT after 25 m outside and three fixes. Uncertain is reported but never acted on — that single rule used to be wrong, and it produced 60% of the demo's events."* | *What if the index stops returning a zone the person is inside?* → Every open (person, zone) state is observed every fix whether or not the index returned it; a zone not returned by a provably conservative filter is a certain Outside, still fed through the same filter. Before that fix, a walk across 60 zones produced 60 entries and zero exits. |
| 3:30–4:00 | **6. Noise** — the same hour with `--no-hysteresis`, then `bench --only 10` | 602 entries with the filter vs 17,906 without; the A/B table: filtered 502 vs noise-free target 521 under realistic drift | *"Same trajectories, same noise, filter off: thirty times the entries, all flaps. The honest check is the last table: against noise-free truth on the same paths, the filter reports 96% of the real transitions."* | *Doesn't the filter hide real crossings?* → Under white noise it keeps 66% of the target — the documented price of suppressing ~20,000 false transitions; under realistic drift 96%. The old benchmark could not tell the two apart; this one can. |
| 4:00–4:30 | **7. History** — `bench --only 9` | sharing 13.0× at 5,001 versions; ~15 nodes per add or remove; 0 for a rule change; past query ≈ present query | *"What were the rules at 00:45? A persistent quadtree: each change copies one root-to-leaf path and shares the rest, so 5,001 versions cost 71,000 nodes instead of 930,000. A query against the past is the same traversal from an older root."* | *Why not copy the tree per version?* → O(zones) per change. Path copying is O(depth), and a validity change allocates nothing because validity is an append-only log per zone. The dashboard's persistent-index panel draws it: copied path highlighted, shared subtrees dim. |
| 4:30–5:00 | **8. Performance** — `bench --only 1`, measured live (~15 s) | the scaling table: brute force linear in n; quadtree ~30×; R-tree in the hundreds at 100,000 zones; `k/query` 98.45; `equal ok` on every row | *"Measured now, medians of 11 interleaved rounds, paired ratios. Look at k: about 98 zones genuinely overlap each query at 100,000, and every index must return all of them — that caps the speedup. The index removes the scan, not the answer. At constant density the same code is hundreds of times faster."* | *Why does the number differ from the README?* → Ratios move between sessions on this laptop (the R-tree's by more than 2×), which is why the docs quote ranges and the floor. The committed tables come from `make bench` on this machine; this is today's sample. |
| (after) | **9. Engineering** — `make test`, `make determinism`, `make dashboard` | 45 test files, `ALL TESTS PASS`; `determinism: identical output`; then open `dashboard.html` | *"Forty-five test files, each structure against its oracle; same seed, byte-identical output; and the dashboard is one HTML file with no server."* | *What else is checked?* → `make mutation` (22/22), UBSan here and ASan + UBSan in Linux CI, Clang Static Analyzer (0 findings), `-Werror` on clang and g++ in CI, Make and CMake builds required to agree byte for byte. |

## On the dashboard (1–2 minutes, if there is time)

`open dashboard.html`. It is the two-hour, 60-tourist run over the real zones.

1. **index: off → brute force → quadtree → R-tree** (the button bottom right): the
   same map, with each structure's actual cells or envelopes drawn over it. Click a
   dot: the cells that tourist's query touches are highlighted and counted.
2. **main experiment** (top of the panel): the committed benchmark table, and a
   live correctness check — 3,000 random queries over this run's zones, 0 mismatches.
3. **Scrub the timeline**: *rules in force* and the *zone change log* follow it
   (Wards Lake comes into force at 00:30, Love Jungle at 00:45); the *persistent
   index* panel shows consecutive versions with the copied path highlighted.
4. **accuracy discs**: the GPS uncertainty drawn to scale with the state colour —
   purple is Uncertain.
5. **event stream** (bottom): ENTER / EXIT / UNCERTAIN with distance and accuracy.

## Closing line

*"Five hand-built data structures turn an O(n)-per-fix problem into a fast one.
The part I am proudest of is not the speedup but the evidence that none of them
ever changes the answer — and the ten real bugs that evidence found."*

Questions you may be asked, with answers from the code: [REVIEW_QA.md](REVIEW_QA.md).
