# SafeTrail — Presentation

Ten core slides, ordered as you would present them, plus an optional appendix.
Speaker notes are in *italics*. Every number is authoritative in
[RESULTS.md](RESULTS.md) and reproducible with `make bench` / `make test`.

**Repository:** https://github.com/adivishall/safetrail
**Course:** Data Structures · **One question:** *how much faster can custom spatial
data structures make repeated geofencing queries without changing the answer?*

---

## Slide 1 — The problem

Tourists move through terrain with hazards — landslide slopes, deep-water
lakeshores, restricted zones. We need to know, **repeatedly and in real time**,
whether each tourist is inside, outside, or uncertain relative to many hazard
**polygons**, and when they **cross a boundary**.

*This is a geometry-and-search problem at its heart. Everything else is built on
answering it fast and correctly.*

---

## Slide 2 — The naive solution, and why it fails

Check **every zone against every tourist, every tick**:

```
200 tourists × 100,000 zones × 40 vertices = 800 million ops / tick
```

That is `O(n)` per query — linear in the zone count, so cost grows without bound as
zones are added. Measured: **~245 µs per query at 100,000 zones**.

*We keep this — `BruteForceIndex` — forever, but not to run it: to test against it.
Its slowness is the motivation for everything that follows.*

---

## Slide 3 — Our approach

Three stages, each a data-structures problem:

1. **Spatial pruning** — a tree returns only the handful of zones near the tourist.
2. **Exact geometry** — point-in-polygon decides the answer for those few.
3. **State-transition detection** — emit an event only when containment *changes*.

> **Spatial index finds the candidates; geometry determines the actual answer.**

*The whole architecture is that one sentence. Step 1 is where the speed comes from;
step 2 is where the correctness comes from.*

---

## Slide 4 — The quadtree (primary spatial index)

Recursively subdivides **space** into four quadrants. A query descends only the
quadrants that can overlap it, skipping the rest.

- Average **O(log n + k)**; worst case **O(n)** — it partitions space, not data, so
  clustered hazards are the bad case (be honest about this).
- Doubling **root expansion** for inserts outside the current extent; **subtree
  collapse** on delete so it doesn't freeze at its high-water shape.
- Measured **~35× faster** than brute force at 100k zones.

*It's also the index we can draw — a fixed spatial subdivision — which is how I found
a real bug (next-but-one slide).*

---

## Slide 5 — The R-tree, and the comparison

A second spatial index with the **opposite** trade-off: it partitions **items** into
tight bounding rectangles, so nothing is forced high by a split it straddles — but
envelopes can overlap, so a query may descend several branches.

The payoff is **STR bulk loading**: sort the whole set, tile it into near-square
minimally-overlapping rectangles. Same data, same query code:

| | quadtree | R-tree (insert) | R-tree (STR) |
|---|---|---|---|
| speedup @100k | ~35× | ~35× | **~240–260×** |
| tree size | — | baseline | **33% smaller** |

*This is the cleanest "the structure, not the machine" result: ~6× purely from how
the tree was assembled. Building both indexes is what makes this visible.*

---

## Slide 6 — Exact geometry (the answer)

The index only narrows candidates; **geometry decides**. Point-in-polygon by:

- **Ray casting** — half-open crossing rule; handles concave polygons and holes.
- **Winding number** — an *independent* second implementation that cross-checks ray
  casting (100,000 points, **0 disagreements**).
- **Three-valued containment** — Inside / Outside / **Uncertain**, because GPS is
  ±4 m open sky, ±35 m in hills: resolve only when the whole uncertainty disc is on
  one side.

*Keeping two independent implementations caught a real bug — they disagreed on holes
until it was fixed. That's exactly the job the second one is kept for.*

---

## Slide 7 — Time and history: interval tree + persistent quadtree

Zones turn on and off, and we sometimes need the past.

- **AVL interval tree** — "which zones are in force at time `t`?" in **guaranteed**
  `O(log n + k)`. The only worst-case guarantee on the query path, because it
  balances on *data*.
- **Persistent quadtree** — "what was the map at 14:32?" A path-copying tree copies
  only the `O(depth)` nodes on one path per change and **shares** the rest by
  refcount: **13× cheaper than full copies** at 5,001 versions, and past queries cost
  the same as present ones.

*The persistent quadtree is the most advanced structure here — a proven sharing
bound, a measured result, and no counterpart in existing implementations.*

---

## Slide 8 — Experimental results

All from `make bench` / `make test`; ratios are to **our own brute force**; data is
**simulated** (real geography). Full table: [RESULTS.md](RESULTS.md).

| Result | Number |
|---|---|
| Quadtree speedup @100k | **~35×** |
| R-tree speedup @100k (STR) | **~240–260×** |
| Candidates/query @100k | 98.78 — identical across indexes |
| Correctness | **18,000 queries, 0 mismatches** vs brute force |
| STR bulk-load gain | ~6× from tree shape alone |
| Persistent-index sharing | 13× @ 5,001 versions |
| Ray casting vs winding | 100,000 points, 0 disagreements |
| Tests | 39 files, ≈770 assertions, 0 failed |

**The headline insight:** we predicted ~29,000×, measured ~35×, and *explaining why*
is the result — the speedup ceiling is output size `k`, not the tree. At 100k dense
zones ~99 zones genuinely overlap each query, and no index can return fewer than
exist.

---

## Slide 9 — Live demo

```bash
make test        # correctness first: every index vs a brute-force oracle
make bench       # the three-index comparison + the k ceiling
make dashboard   # open dashboard.html — the quadtree drawn over real geography
```

On the dashboard: cycle the **index switch** (brute force → quadtree → R-tree, each
drawn as real cells over the same data), **scrub the timeline** to watch zones
activate (interval tree), and read the **persistent-index panel** — consecutive
versions with the copied path highlighted and shared subtrees dimmed (path copying
made visible). Step-by-step: [DEMO_SCRIPT.md](DEMO_SCRIPT.md).

---

## Slide 10 — Conclusion

Five hand-built structures — **brute force** (oracle), **quadtree** and **R-tree**
(measured against each other), **interval tree** (time), **persistent quadtree**
(history) — plus the point-in-polygon geometry that decides the answer.

> Every optimisation is proven against brute force with zero mismatches, analysed
> honestly including where it degrades, and measured on real geography. The data
> structures are the deliverable; everything else exists to exercise and prove them.

**Be honest about limits:** quadtree/R-tree are O(n) worst case; tourists are
simulated; speedups are ratios to our own brute force. Stating that is the point.

---

## Appendix (optional — only if asked)

### A1 — Extensions (Level 4)

Substantial, tested work that supports the core but is not required to understand it:
groups (rollback union-find), prediction, alert correlation (DSU), routing
(Dijkstra/A*), dispatch (Hungarian), offline sync (Lamport + geohash serialisation),
evidence (RFC 6962 Merkle log, SHA-256 from scratch), adaptive sampling, hysteresis,
jurisdiction. See [COURSE_MAPPING.md](COURSE_MAPPING.md) and
[GAP_ANALYSIS.md](GAP_ANALYSIS.md).

### A2 — Testing & determinism

Every fast structure is checked against a brute-force oracle; the two containment
algorithms cross-check each other; the whole suite runs under AddressSanitizer +
UBSan in CI; and `make determinism` asserts byte-identical output for a fixed seed.

### A3 — Architecture

`Input → Zone Store → Spatial Index → Temporal Filter → Geometry → State Machine →
Event`, with extensions hanging off the event stream. See
[ARCHITECTURE.md](ARCHITECTURE.md).

### A4 — Where to find everything

| Question | Document |
|---|---|
| Every measured number | [RESULTS.md](RESULTS.md) |
| The five structures in depth | [DATA_STRUCTURES.md](DATA_STRUCTURES.md) |
| Viva questions | [VIVA.md](VIVA.md) · [DESIGN_DEFENSE.md](DESIGN_DEFENSE.md) |
| How to run the demo | [DEMO_SCRIPT.md](DEMO_SCRIPT.md) |
| Is the data real? | [DATA_PROVENANCE.md](DATA_PROVENANCE.md) |
