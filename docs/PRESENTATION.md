# safetrail — Project Presentation

A complete walkthrough for the review, ordered as you would present it. Each
section is roughly one talking point / slide. Speaker notes are in *italics*.
Every number here is measured and reproducible (`make bench`, `make test`).

**Repository:** https://github.com/adivishall/safetrail
**Live dashboard:** https://adivishall.github.io/safetrail/
**Course:** Data Structures · **Origin:** SIH 2025, problem statement `SIH25002`

---

## 1. The problem (1 slide)

Tourist safety in remote Northeast India — Meghalaya, difficult terrain, patchy
mobile coverage, tourists who don't know the local hazards (landslide slopes,
restricted border zones, deep-water lakeshores). The task: know in real time when
someone enters a dangerous area, and respond.

*This is Smart India Hackathon problem statement SIH25002, from the Ministry of
Development of North Eastern Region. We took the problem, not the expected
solution.*

---

## 2. The one idea that defines the project (1 slide)

> **Every existing implementation hands the hard part to a database. We wrote that
> engine ourselves.**

We researched two public implementations before writing any code — *SafeVoyage*
(GitHub) and *STSMIRS* (a published paper). Both converge on the same shape:

| The hard part | What they use | What we did |
|---|---|---|
| "Is this point inside this zone?" | PostGIS `ST_Contains` | **Hand-wrote the geometry** |
| "Which zones are near here?" | PostGIS GiST tree | **Hand-wrote the spatial index** |
| Tamper-proof records | Ethereum smart contract | **Hand-wrote a Merkle log** |

*This single decision is what makes it a data-structures project instead of a web
app — and, as the next slide shows, it also fixes real problems that delegating to
a server-side database makes impossible.*

---

## 3. Why that decision matters — the eleven gaps (1–2 slides)

Delegating containment to a cloud database fails in exactly the terrain this
problem targets. Building the engine ourselves let us close eleven documented
gaps. The eight we built:

| # | The gap | What we do |
|---|---|---|
| 1 | GPS treated as an exact point | Answer **Inside / Outside / Uncertain** — GPS is ±4 m open sky, ±35 m in hills |
| 2 | Alerts fire on entry (too late) | **Predict** the crossing: "4 minutes from the border zone" |
| 3 | Zones are static; risk isn't | Zones turn on/off; we can **rewind** to "the rules at 14:32" |
| 4 | Tourists tracked individually | They travel in **groups**; a straggler 400 m behind is the incident |
| 5 | One landslide → forty alert cards | **Correlate** them into one incident |
| 7 | Continuous GPS = 8–12% battery/hr | **Sample by proximity** to danger |
| 8 | GPS drift makes fences fire constantly | **Hysteresis filter** — removes 91% of false alerts |
| 9 | Ethereum for a tamper-proof log | **Merkle log**, offline-verifiable, no chain |

*Full research with citations is in docs/GAP_ANALYSIS.md. Three more (offline sync,
jurisdiction, faster self-intersection) are designed but not yet built — and we
mark that honestly.*

---

## 4. How it works — the plain version (1 slide)

Every second, for each tourist:

1. **Narrow down** — which zones are even near this person? A tree answers without
   checking all of them (like a book's index vs reading every page).
2. **Check precisely** — are they actually inside? Real polygon geometry.
3. **Report only changes** — "entered" / "exited", never "still inside" repeated
   forever. That's the difference between a usable alert list and a scrolling wall.

*Then: correlate related alerts into incidents, and check whether any group has
split apart.*

---

## 5. How it works — the technical version (1–2 slides)

It all lives in one function, `Evaluator::evaluate()`. Eight steps per tourist:

```
1. usability gate     discard fixes too noisy to mean anything     O(1)
2. spatial prune      100k zones → ~2 candidates             O(log n + k)  ← the point
3. temporal filter    of those, which are in force now?            O(1)
4. exact geometry     ray casting: Inside / Outside / Uncertain    O(k·V)
5. hysteresis         real crossing, or GPS jitter?                O(1)
6. transition diff    did state CHANGE? → emit an Event            O(k)
7. prediction         heading toward a zone? → ETA                 O(k·V)
```

**Step 2 is the whole performance story.** Without the index:
`200 tourists × 100,000 zones × 40 vertices = 800 million operations per tick`.
With it: ~27,000.

*The data flow: real OSM zones → validated ZoneStore → spatial + versioned index →
simulator (movement + GPS noise) → evaluator → correlator + cohesion → dashboard.*

---

## 6. The data structures we built (1–2 slides)

**This is the graded core. All hand-written — no `std::map`, `std::set`,
`std::priority_queue`, no Boost, no PostGIS.**

| Structure | What it does here | Complexity |
|---|---|---|
| **Quadtree** | primary spatial index | query O(log n + k) avg |
| **R-tree** | second index, the comparison | query O(log n + k) avg |
| **Brute force** | correctness oracle + baseline | O(n) |
| **AVL interval tree** | zone validity in time | O(log n + k) **guaranteed** |
| **Circular buffer** | recent GPS history per tourist | O(1) |
| **Persistent quadtree** | time-travel — query any past moment | O(depth) nodes/change |
| **Rollback union-find** | group cohesion (splits + merges) | O(log n) find, O(1) undo |
| **Merkle tree** (RFC 6962) | tamper-evident evidence log | proof O(log n) |

Plus SHA-256 from scratch (checked against NIST vectors), ray casting, winding
number, three-valued containment, and hysteresis.

*Two of these are genuinely advanced: the persistent path-copying quadtree and the
rollback union-find. If asked "which did you understand most deeply", pick one of
those.*

---

## 7. The three showpiece structures (1 slide each, if time)

**Persistent quadtree (time travel).** Nodes are immutable; a change copies only
the O(depth) nodes on one path and *shares* every other subtree by reference count.
So we keep the entire history for a fraction of the cost of full copies — measured
at **13× sharing across 5,000 versions** — and querying the past is as cheap as the
present. *This answers the investigation question: "what were the zone rules at the
moment the accident happened?"*

**Rollback union-find (groups).** Groups split as well as merge, so we need to
*undo* unions — which means we can't use path compression (it makes unbounded,
unrecordable writes). We keep union-by-rank alone: O(log n) finds, O(1) rollback.
*The trade-off is the interesting analysis, not an oversight.*

**Merkle evidence log (tamper-proof).** Every event is committed to a Merkle root.
An inclusion proof lets a responder verify any record offline against just the root
— no network, no blockchain. Consistency proofs show the log was only ever appended
to, never rewritten. *This is the property Ethereum was being used for, delivered in
300 lines that are all ours.*

---

## 8. The data is real (1 slide)

- **Geography: real.** 38 zones fetched from **OpenStreetMap** via the Overpass API
  — actual reservoirs, forests, and landmarks around Shillong, including **Wards
  Lake** and **Sonapani Waterfall Cliff**, at their true coordinates.
- **Tourists: simulated.** No real tracking data exists for this problem, and
  simulation gives us **ground truth** — we know where each tourist truly was, so we
  can *measure* whether the engine got the right answer.
- **How it reaches the dashboard: it doesn't travel.** Zero network requests. The
  engine serialises its output straight into one HTML file (99.3% data, 11.7 KB
  viewer). Opens over `file://` with the network off.

*Full provenance with verification commands: docs/DATA_PROVENANCE.md.*

---

## 9. Results — measured, not claimed (1–2 slides)

All from `make bench` / `make test`:

| Result | Number |
|---|---|
| Spatial index speedup (100k zones) | **32.7× quadtree, 32.5× R-tree** vs brute force |
| Candidate pruning (real run) | 438 zones → 2.42 per query, **181×** |
| Hysteresis false-alert removal | **91.2% under realistic drift**, 92.3% white noise |
| Persistent index sharing (5,000 versions) | **13×** vs full copies |
| Index equivalence (correctness) | 18,000 queries, **0 mismatches** vs brute force |
| Ray casting vs winding number | 100,000 points, **0 disagreements** |
| Merkle tamper detection | forged entries + rewritten history **rejected** |
| Unit tests | **10,915 checks across 25 real files, all pass** |

**The most interesting result:** our design doc predicted a ~29,000× speedup.
Measurement brought it down to 33×, and *explaining why* is worth more than the big
number: at 100k dense zones, ~99 genuinely overlap each query — those are correct
answers, and no index can return fewer results than exist. The ceiling is output
size, exactly as `O(log n + k)` predicts.

*Stating that caveat honestly is the point. We also fixed three real bugs that only
the measurements caught — including one found by looking at the visualisation.*

---

## 10. Honest engineering (1 slide) — say this before they ask

- **Worst case:** the quadtree/R-tree are O(n) worst case (they partition space,
  not data; clustered hazards are the bad case). The **AVL interval tree** is the
  one with a guaranteed O(log n).
- **Scope:** 23 modules built, ~13 designed-but-stubbed (routing, dispatch, offline
  sync). Marked ◻ / ✅ throughout — never a stub claimed as done.
- **The tourists are simulated.** Real geography, simulated people, on purpose.
- **The dashboard is a deterministic replay**, not a live server.

*This slide is a strength, not a confession. It's what separates a graded project
from a sales pitch. Full prep for hard questions: docs/DESIGN_DEFENSE.md.*

---

## 11. Live demo (do this) 

```bash
make test        # 10,915 checks pass — proves it works
make demo        # watch events stream over real geography
make bench       # the speedup + correctness numbers
make dashboard   # open dashboard.html — animated map, scrub the timeline
```

On the dashboard, three things to show:
1. Dots moving over **real Shillong reservoirs**; zones light up on entry.
2. Toggle **index overlay** — the actual quadtree cells (this is what made a bug
   visible and doubled our performance).
3. **Scrub the timeline** past 00:45 — Wards Lake and Love Jungle activate and
   lapse. That's the persistent index answering "what were the rules then?".

Or just open the **live URL** — it's the same file, deployed via CI to GitHub Pages.

---

## 12. Engineering practices (1 slide, optional)

- **Every structure is unit-tested**, most cross-checked against a brute-force
  oracle — that's how we caught our bugs.
- **Determinism:** same seed → byte-identical output, which makes the replay and
  every A/B comparison valid.
- **CI on every push:** builds on g++, runs 10,915 checks (gates the deploy), runs
  AddressSanitizer/UBSan (advisory), regenerates the dashboard, publishes to Pages.
- **No dependencies:** `git clone && make`, nothing else. No cmake, no libraries.

---

## 13. The one-sentence close

> We built the geofencing engine that every competing team imports from PostGIS —
> the quadtree, the R-tree, the persistent time-travel index, the containment
> geometry, a tamper-evident Merkle log — by hand, analysed it honestly including
> where it degrades, measured it against real OpenStreetMap geography, and shipped
> it as a single self-contained dashboard anyone can open offline.

---

## Appendix — where to find everything

| Question | Document |
|---|---|
| How does one run work, start to finish? | [WALKTHROUGH.md](WALKTHROUGH.md) |
| Why these features? (research + citations) | [GAP_ANALYSIS.md](GAP_ANALYSIS.md) |
| What structures, what complexity, what's built? | [DATA_STRUCTURES.md](DATA_STRUCTURES.md) |
| Is the data real? How does it reach the page? | [DATA_PROVENANCE.md](DATA_PROVENANCE.md) |
| Hard-question prep for the viva | [DESIGN_DEFENSE.md](DESIGN_DEFENSE.md) |
| How is it deployed? | [DEPLOYMENT.md](DEPLOYMENT.md) |
| Onboarding for a teammate | [../TEAM_BRIEF.md](../TEAM_BRIEF.md) |
