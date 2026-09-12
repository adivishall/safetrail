# Demo Script — a literal 5-minute walkthrough

For presenting to a professor or examiner. Each step gives the **command** to run,
the **screen** to show, **what to say**, **what to point at**, and the **question it
answers**. Total time ≈ 5 minutes. Rehearse once so the builds are warm (run
`make test bench dashboard` beforehand so nothing compiles live).

All numbers below are what the tools actually print; the authoritative table is
[RESULTS.md](RESULTS.md).

---

## 0. One sentence before you touch the keyboard (15 s)

> *"SafeTrail decides whether tourists are entering dangerous zones. Checking every
> zone against every tourist is too slow, so I built spatial data structures that
> prune the search, and I proved they never change the answer."*

Then start the demo.

---

## 1. Correctness first (45 s)

**Command:**
```bash
make test
```
**Screen:** the list of test files, each ending `[ ok ] … N checks, 0 failed`, then
`ALL TESTS PASS`.

**Say:** *"39 test files, and every fast structure is checked against a brute-force
oracle — the slow-but-obviously-correct version. If any index ever disagreed with
brute force, this goes red. It doesn't."*

**Point at:** the `index/equivalence` and `geo/*` lines.

**Answers:** *"How do you know the optimised code is correct?"* — before you show any
speed, you show correctness. This ordering is the whole credibility of the project.

---

## 2. The main experiment — brute force vs quadtree vs R-tree (90 s)

**Command:**
```bash
make bench
```
**Screen:** section **1. INDEX SCALING** — the table from 10 to 100,000 zones.

**Say:** *"Same data, same queries, three indexes. Watch the brute-force column: it's
linear — 245 microseconds at 100,000 zones. The quadtree is ~35× faster, the R-tree
~240×. But look at the candidates column —"*

**Point at:** the `cands` column (98.78 at 100k).

**Say:** *"— it's identical across all three. Those are true positives; ~99 zones
really do overlap each query. No index can return fewer results than exist, so the
speedup is capped by output size k, exactly as O(log n + k) predicts. Our first
estimate was 29,000×; measurement disciplined it to 35×, and explaining why is worth
more than the big number."*

**Then scroll to** section **2. EQUIVALENCE**: *"18,000 randomized queries, zero
mismatches — that's the gate behind the speed."* and section **9. BULK LOADING**:
*"the R-tree got ~6× faster purely from how the tree was assembled — STR packing,
same query code. That's the structure, not the machine."*

**Answers:** *"How much faster, and how do you know it's still correct?"* and
*"Why build two spatial indexes?"*

**Optional visual:** open [`bench/plots/index_scaling.svg`](../bench/plots/index_scaling.svg)
— the brute-force line going vertical while the trees stay flat.

---

## 3. The dashboard — the quadtree made visible (90 s)

**Command:**
```bash
make dashboard          # then open dashboard.html in a browser
```
**Screen:** the animated map over real Shillong geography, dots (tourists) moving,
zones lighting up.

**Say:** *"This is one self-contained HTML file — no server, no network. Real
OpenStreetMap geography: those are actual reservoirs and Wards Lake. The tourists are
simulated, on purpose, because the simulator knows their true position — that's the
ground truth we test against."*

**Do:** click the **`index:`** button to cycle through the three modes —
**brute force → quadtree → R-tree.**

**Say:** *"This is the whole project in one control. Brute force: no structure —
every one of 438 zones tested per query, that's the O(n) baseline. Quadtree: a
disjoint grid that partitions space — where zones cluster it subdivides finely.
R-tree: tight envelopes that partition items, and you can see them overlap — that's
the structural difference. Same data, same queries, three ways to organise the
search. This quadtree overlay once showed me the tree was rooted at the whole
planet, wasting 11 levels of depth — fixing it more than doubled performance."*

**Do:** drag the **timeline scrubber** past ~00:45, then look at the
**persistent index [GAP 3]** panel on the right.

**Say:** *"Zones activate and lapse over time — that's the interval tree answering
'which zones are in force right now?'. And this panel is the persistent quadtree:
each little map is one version. The dim cells are shared with the previous version;
the highlighted ones are the single path this version had to copy — +14 new nodes,
115 shared. That's path copying: keep the whole history for the cost of one path,
not a full copy. Querying any past version is just a different root pointer."*

**Point at:** the "+14 new / 115 shared" captions, and the stats panel —
`candidates/query`, `index versions`, `node sharing`.

**Answers:** *"Show me the data structure actually doing something,"* and *"How do you
handle zones that change over time / historical queries?"*

---

## 4. Honesty slide — say it before they ask (30 s)

**Say:** *"Three things I'll state plainly: the quadtree and R-tree are O(n) in the
worst case — they partition space, not data, so clustered hazards are the bad case;
the one guaranteed structure is the AVL interval tree. The tourists are simulated.
And the speedup is a ratio to our own brute force, not an external library. Stating
that is the point — it's a course project defended on evidence, not a sales pitch."*

**Answers:** every "gotcha" question, pre-empted. Full prep: [VIVA.md](VIVA.md).

---

## 5. Close (20 s)

**Say:** *"Five hand-built structures — brute force as the oracle, a quadtree and an
R-tree I measured against each other, an interval tree for time, and a persistent
quadtree for history — plus the point-in-polygon geometry that decides the actual
answer. Every optimisation proven against brute force with zero mismatches. That's
the project."*

---

## If a command fails live

- `make` needs only a C++17 compiler; if a binary is stale, `make clean && make`.
- If the browser blocks `dashboard.html` over `file://`, open it via
  `File → Open`, or serve the folder with `python3 -m http.server` (the file itself
  makes **zero** network requests either way).
- Fallback numbers to quote from memory: **~35× quadtree, ~240× R-tree at 100k zones,
  0 mismatches over 18,000 queries, 13× persistent-index sharing.**

## Cheat-sheet of quotable numbers

| Claim | Number | Source |
|---|---|---|
| Quadtree speedup @100k | ~35× vs brute force | `make bench` §1 |
| R-tree speedup @100k | ~240–260× vs brute force | `make bench` §1 |
| Candidates/query @100k | 98.78 (identical across indexes) | `make bench` §1 |
| Correctness | 18,000 queries, 0 mismatches | `make bench` §2 |
| STR bulk-load gain | ~6× from tree shape alone | `make bench` §9 |
| Persistent sharing | 13× @ 5,001 versions | `make bench` §5 |
| Tests | 39 files, ≈770 assertions, 0 failed | `make test` |
