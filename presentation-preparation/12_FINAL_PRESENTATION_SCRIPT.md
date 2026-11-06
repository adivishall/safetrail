# 12 — Final presentation scripts

> Spoken English, technically correct. Intuition first, then the formal term.
> Numbers are verified (see 07). Adjust "I" and "we" to how your team presents.

## 1. The 30-second introduction

"Our project is SafeTrail, a geofencing engine for tourist safety, written in C++
with data structures we built ourselves. The problem: given thousands of hazard
zones and a stream of noisy GPS positions, decide quickly whether each person is
inside, outside, or — honestly — uncertain. Checking every zone for every position
is linear time, so we built spatial indexes — a quadtree and an R-tree — that skip
whole regions, and we proved they give exactly the same answers as the slow way.
At 100,000 zones the R-tree answered about 224 times faster than a linear scan."

## 2. The 1-minute introduction

"SafeTrail started from a Smart India Hackathon problem about tourist safety in
the North-East. Imagine a tourist near a cliff closure: the phone reports a
position, but GPS is only accurate to, say, twenty metres. So two questions matter.

First, speed. Testing the position against every hazard zone costs O(n) per
update. A spatial index works like a library catalogue: you don't walk every
shelf, you go straight to the right section. We implemented a quadtree, which
divides the map into quadrants, and an R-tree, which groups nearby zones into
boxes.

Second, honesty. A position is really a circle, not a point. If the circle
crosses a boundary we say 'uncertain' instead of guessing, and a state machine
waits for a few agreeing readings before announcing that someone entered.

On top of the engine we built an operations console — alerts, incidents, nearest
responder by road with Dijkstra and A* — and a Data Structures Lab that runs our
C++ structures live and draws what they did."

## 3. The 3-minute technical explanation

"Let me follow one GPS fix.

It arrives at our server and first goes through an ingestion boundary: is this
user consented and sharing? Is the timestamp sane? Is it a duplicate, or older
than the last fix? Late fixes are stored in history using binary search but not
evaluated, because running the state machine backwards would create false
events.

Then the engine builds a query box around the fix. Its radius is the GPS accuracy
plus how far the person could walk in five minutes, so the box is conservative —
it cannot miss a zone that matters.

That box goes to the spatial index. Our live index is an R-tree: every node stores
the bounding rectangle of everything below it, so if the query box misses a node's
rectangle, the whole subtree is skipped. We bulk-load it with STR — sort by
longitude, slice, sort each slice by latitude — which gives tiles with little
overlap. That was about six times faster to query than building it by insertion.

The index returns a handful of candidates. For each we check whether the zone is
in force right now, then run exact point-in-polygon with ray casting and compute
the signed distance to the boundary. If the whole accuracy circle is inside, it's
inside; fully outside, outside; otherwise uncertain.

Then hysteresis: entry is confirmed only after three agreeing readings at least
15 metres inside. Without it, in our white-noise test, the naive rule produced
about forty times more transitions than the true number — 20,848 against 521.

A confirmed entry becomes an event, an alert, and through union-find an incident.
One Dijkstra from the incident gives road distances to every responder; assigning
one runs A*, which settled 772 junctions instead of 5,557 in our demo."

## 4. The 5-minute full explanation

**(0:00) Problem.** "Tourists walk into hazards; help arrives late. Any geofencing
system has to answer 'which zones contain this position' for every position. The
naive answer scans every zone."

**(0:40) Core idea — spatial indexing.** "We store each zone's bounding box in a
tree. A quadtree divides space into four quadrants recursively. An R-tree instead
groups the zones themselves into rectangles that can overlap but hug the data.
Both answer a range query in expected O(log n + k) time, where k is the number of
answers. At 100,000 synthetic zones in one district, the R-tree measured about 224
times faster than a linear scan and the quadtree about 33 times."

**(1:30) Why the speedup has a ceiling.** "In a crowded district about 98 zones
genuinely overlap each query, and no index can avoid returning real matches. When
we keep density constant so each query overlaps about one zone, the R-tree is about
a thousand times faster. That's the k in O(log n + k), visible in data."

**(2:10) GPS honesty.** "A fix is a disc. Three-valued containment — inside,
outside, uncertain — plus a hysteresis state machine stop noise from creating
false alerts."

**(2:40) Time.** "Zones can be switched on and off by time, and every change is a
new version of a persistent quadtree using path copying, so we can ask 'what were
the rules at 14:32'. An AVL interval tree, augmented with the maximum end time,
finds which windows contain a given time."

**(3:20) Operations.** "Alerts are ordered by a binary max-heap; unacknowledged
ones escalate through a timing wheel; nearby alerts are merged into incidents with
union-find; the nearest responder comes from one Dijkstra on the real OpenStreetMap
road graph, and the route from A*."

**(4:00) Correctness.** "Every fast structure has a slow, obviously correct
oracle. We ran about 55,000 hostile random queries per index against brute force,
audited invariants after every operation, required identical output from every
index, and injected 28 realistic bugs — every one was caught. That process found
ten real defects in code that was already passing its tests."

**(4:40) Close.** "So the contribution is the structures, their measured
behaviour, and the evidence that they're correct — inside a system you can open
and use."

## 5. The 10-minute presentation (with the live site)

| Time | Show | Say |
|---|---|---|
| 0:00 | Title slide | The 30-second introduction. |
| 0:40 | Problem slide | "Linear scan per fix; at 100,000 zones that is the whole cost." |
| 1:20 | Live site: Overview (signed in) | "Real OSM zones and roads; simulated people marked SIM." |
| 2:00 | Top bar: 10×, Run → Live Monitoring; click a tourist | "Each fix: ingestion, R-tree query, exact geometry, hysteresis." |
| 3:00 | Ravi enters the closure; Pause | "Approaching, uncertain, then inside — three-valued and confirmed by hysteresis." |
| 3:45 | Incidents → #1 → Acknowledge → Assign | "Union-find merged the alerts; one Dijkstra recommended the unit; A* routed it — 772 vs 5,557 junctions." |
| 4:45 | Lab → spatial experiment; drag a box; Play | "Teal descended, red pruned; every index 0 mismatches vs brute force." |
| 5:45 | Lab → AVL (zig-zag) → step | "Rotations keep height logarithmic; max_high lets the stab skip subtrees." |
| 6:30 | Lab → graph → toggle Dijkstra/A* | "Same cost, far fewer settled nodes; the heap array is shown." |
| 7:15 | Analytics → index scaling chart | "224× and 33× at 100k, flattening because k ≈ 98; at constant density about 1,000×." |
| 8:00 | Investigation → slider → Verify | "Persistent index and binary search answer 'what did we know then'; the browser verifies the Merkle proof." |
| 8:45 | Testing slide | "Oracles, differential tests, invariants, 28/28 mutants, ten real defects found." |
| 9:20 | Limitations slide | "Simulated people, in-memory state on a free host, one laptop's benchmarks." |
| 9:45 | Conclusion | §10 below. |

## 6. Why we chose this project (simple)

"Because it is a real problem where data structures decide whether the system
works at all. Geofencing looks simple — 'is a point inside a polygon?' — but done
for thousands of zones and many people per second, the naive way doesn't scale,
and real GPS noise makes the naive answer wrong. It let us use trees, heaps,
hashing, graphs and union-find for real reasons, and measure the difference."

## 7. How SafeTrail differs from a conventional geofencing app

| Conventional | SafeTrail |
|---|---|
| Point-in-polygon against every fence, or an off-the-shelf spatial database | Hand-written quadtree, STR R-tree, geohash, each compared with brute force |
| Inside/outside only | Inside / outside / **uncertain** from the GPS accuracy disc |
| Raw transitions, flapping at boundaries | Hysteresis: margins + agreeing fixes + minimum dwell |
| Current rules only | Persistent index: rules at any past time |
| Trust the server's logs | Merkle proofs verified in the browser |
| Assumes connectivity | Offline phone with a shipped index; Lamport-ordered reconciliation |
| "Tests pass" | Oracles, invariant audits, mutation testing, byte-identical builds |

## 8. The most important DS concepts demonstrated

1. Spatial range queries: quadtree (space partitioning) vs R-tree (item partitioning), STR bulk loading.
2. Expected vs worst-case bounds, and the role of output size k.
3. Balanced BSTs with augmentation: AVL interval tree.
4. Persistence by path copying vs rollback by undo stack.
5. Union-find (union by rank; why no path compression with rollback).
6. Heaps: Dijkstra/A* frontier, triage.
7. Hashing: open addressing, tombstones, load factor.
8. Graphs: adjacency lists, Dijkstra, A* admissibility, Hungarian assignment.
9. Timing wheels, circular buffers, Merkle trees, Lamport clocks.

## 9. The most impressive technical achievements (say two or three)

- Measured, reproducible speedups with an honest protocol (paired ratios,
  checksums, ranges) — and the explanation of why they plateau.
- Correctness evidence: ~55,000 hostile queries per index vs brute force, 28/28
  mutants killed, byte-identical output across four indexes, two build systems and
  two operating systems.
- Ten real defects found by that process in code that was already passing tests.
- A complete working system on top, publicly deployed for free, where the Lab
  shows the real structures operating.

## 10. A convincing conclusion

"To conclude: SafeTrail turns a textbook question — is this point in this polygon —
into a data-structures problem at scale, and answers it with structures we built
and measured. The R-tree and quadtree make the per-fix cost logarithmic plus the
size of the answer; the interval tree and persistent quadtree make time and history
queryable; heaps, union-find, Dijkstra and A* turn detections into a response. Most
importantly, every fast answer is checked against a slow, obviously correct one. The
honest limits are simulated people, in-memory state, and benchmarks from one
laptop. Thank you — I'm happy to show any part of it live."
