# Design Defense — Answers to the Hard Questions

Prep for the review. These are the questions a data-structures examiner is most
likely to ask, and the honest, precise answer to each. If you can explain the
five "load-bearing" ones from memory, the sophistication of the project becomes an
asset instead of a liability.

The rule for the room: **never oversell.** Where something is unbuilt, say so.
The project's credibility is built on honesty (see the ◻/✅ status marks in
[DATA_STRUCTURES.md](DATA_STRUCTURES.md)) — protect it.

---

## The five load-bearing explanations

If you internalise nothing else, internalise these. They are the "smart" parts,
and they are what will be probed.

### 1. Why the speedup ceiling is ~33×, not thousands

> The index turns an O(n) scan into O(log n + **k**), where k is the number of
> zones the query actually overlaps. At 100,000 dense zones, ~99 of them genuinely
> intersect each query box — those are correct answers, and no index can return
> fewer results than exist. So the speedup is bounded by output size k, not by the
> tree. That's why all three indexes report the *same* candidate count: it's a
> property of the data, not the structure. The naive early estimate assumed k≈3,
> which needs sparse zones.

The point to land: *stating this caveat is more rigorous than quoting a big
number.*

### 2. Why the quadtree's worst case is O(n)

> The quadtree partitions **space** on a fixed grid, not **data**. If every zone
> falls in one region — which is realistic; hazards cluster in a valley — they all
> pile into one branch and a query walks a linear chain. Average case is
> O(log n + k); worst case is O(n). We root the tree to the data extent and cap
> its depth to mitigate, but there's no asymptotic guarantee. The one structure on
> the query path that *does* guarantee O(log n) is the AVL interval tree.

Do not claim the quadtree is O(log n) worst case. It isn't, and admitting it is
the stronger answer.

### 3. Why the rollback union-find can't use path compression

> Path compression rewrites an unbounded number of parent pointers on every
> `find`. To support rollback we keep an undo stack of every write a union made —
> but we can't undo an unbounded, unrecorded set of compression writes. So we drop
> compression and keep union-by-rank alone, which still gives O(log n) finds and
> O(1) rollback per union. That trade — giving up near-constant find to gain
> undo — is the actual design decision, not an oversight. Groups split as well as
> merge, and plain compressed DSU cannot un-merge.

### 4. How the persistent (time-travel) index works

> It's a **path-copying** quadtree. Nodes are immutable and children are
> `shared_ptr`. A mutation copies only the O(depth) nodes on the root-to-leaf path
> and *shares* every untouched subtree with the previous version by reference
> count. So we keep the whole history for O(log n) extra nodes per change instead
> of an O(n) full copy — measured at 13× sharing across 5,000 versions. Querying
> the past is just a descent from an older root pointer; it costs the same as the
> present, no replay.

### 5. Why "transitions, not states" is the core design decision

> The evaluator emits an event only when a tourist's confirmed containment
> *changes* — Enter, Exit — never "still inside" every tick. Reporting state every
> tick is what floods existing dashboards into an unreadable wall. Diffing against
> the previous tick is also where the hard bugs live: GPS jitter makes a tourist
> flicker across a boundary, which is exactly what the hysteresis filter absorbs
> before the diff runs.

---

## Questions about scope and depth

**"Is this a data structures project or a systems project?"**
> Both surfaces exist, but the graded core is the data structures. The one I'll
> defend in depth is the **persistent path-copying quadtree**: it has a proven
> sharing bound, a measured 13× result, an equivalence test against brute force at
> 120 historical versions, and no equivalent in any existing implementation. The
> simulator, dashboard, and CI are the harness that makes those structures
> measurable and visible — they're how we *prove* the structures work, not the
> deliverable itself.

**"You have 49 modules but half are stubs."**
> Correct, and marked as such — 21 implemented, the rest are designed interfaces
> on the roadmap. We deliberately built the analytically interesting structures
> fully (the two trees, the persistent index, the rollback DSU, the interval tree)
> and left the routing/dispatch stack as designed stubs, because Dijkstra and
> Hungarian matching are well-understood and add breadth, not depth. The status
> marks in the docs never claim a stub as done.

**"Which structure did you understand most deeply?"**
> Pick one and go deep — recommend the persistent quadtree (explanation #4) or the
> rollback DSU (#3). Have the complexity analysis and the one key trade-off ready.

---

## Questions about the data and results

**"Where's the real human tracking data?"**
> There is none, by design. The *geography* is real (OpenStreetMap, fetched via
> Overpass — Wards Lake, Sonapani Waterfall Cliff, real reservoirs). The
> *tourists* are simulated with a random-waypoint mobility model, because no real
> tourist-tracking dataset exists and simulation gives us **ground truth**: we know
> where each tourist truly was, so we can measure whether the engine got the right
> answer. A recording of real GPS couldn't tell us that. See
> [DATA_PROVENANCE.md](DATA_PROVENANCE.md).

**"Your GPS noise is unrealistic — real error drifts, it doesn't jump."**
> We model both. The error is an **AR(1) process** (`correlation` = 0.9 by
> default), so consecutive fixes are temporally correlated — smooth drift, like a
> real receiver — not independent white noise. The hysteresis benchmark reports
> against *both* regimes: 92.3% of false transitions removed under white noise,
> **91.2% under realistic correlated drift**. The filter is robust to the honest,
> harder case, not just the flattering one.

**"The dashboard — is it live?"**
> No, it's a deterministic **replay** of a recorded run: 720 frames captured every
> 10 simulated seconds, serialised into one static HTML file. That's deliberate —
> it needs no server and works offline, which mirrors the whole project's thesis
> (no cloud dependency). The engine itself runs live; the dashboard is a recording
> of one run for inspection.

**"Prove the numbers aren't hardcoded."**
> Every dashboard field traces to an engine getter (no literals in the exporter);
> two different seeds give different results; the same seed gives a byte-identical
> file; and the accuracy values are exactly {4, 35, 999} — the three regimes the
> GPS model emits. Commands to verify all of this are in
> [DATA_PROVENANCE.md](DATA_PROVENANCE.md) §4.

---

## Questions about the "hand-written" rule

**"You use `std::sort` and `std::shared_ptr` — isn't that cheating?"**
> The rule is about the *data structures being graded* — no `std::map`, `std::set`,
> `std::unordered_map`, `std::priority_queue`, no Boost.Geometry, no PostGIS. Those
> are ours. `std::sort` is an algorithm, not a structure; `std::shared_ptr` is
> memory management that the persistent index's node-sharing needs. We hand-wrote
> the quadtree, R-tree, interval tree, union-find, circular buffer, and the
> persistent structure — the things the course is about.

**"Why hand-write a heap but not use one from the STL?"**
> The binary heap is currently a designed stub (◻) — the alert triage path that
> would use it isn't built yet. When it is, it'll be hand-written for the same
> reason as the rest. We didn't substitute `std::priority_queue` for it.

---

## Weaknesses to volunteer before they're found

Stating these first reads as rigor, not weakness:

- **Quadtree/R-tree have no worst-case guarantee** (O(n) on clustered data). The
  interval tree does (AVL).
- **~15 modules are designed stubs**, not built — routing, dispatch, offline sync,
  Merkle log. Marked ◻ throughout.
- **The predictive path uses straight-line extrapolation**, which is fiction
  beyond a few minutes in hill terrain — capped at a 5-minute horizon for that
  reason.
- **The tourists are simulated.** Real geography, simulated people.
- **The self-intersection check is O(V²)**, not the O((n+k) log n) Bentley–Ottmann
  sweep line (which is a ◻ upgrade). Fine at authoring time, once per zone.

---

## The one-sentence framing

> We built the geofencing engine that every competing implementation imports from
> PostGIS — the quadtree, the R-tree, the persistent time-travel index, the
> containment geometry — by hand, analysed it honestly including where it degrades,
> measured it against real geography and a realistic noise model, and marked
> exactly what is built versus planned.
