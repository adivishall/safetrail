# Resume Hardening — Flaws and Before/After

A judged critique of this project (as if by a demanding data-structures examiner,
for a resume context) and the tier-by-tier record of what was fixed. Each tier is
committed and pushed separately; this file is the running before/after ledger.

The flaws are grouped by how badly they would hurt under interview questioning:
**Tier 1** = credibility killers (a reviewer catches these first), **Tier 2** =
substance/rigor, **Tier 3** = positioning for a resume.

---

## Tier 1 — Credibility killers  ·  status: ✅ done

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

## Tier 2 — Substance / rigor  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 5 | **Headline results are scenario-engineered** | 453:1 compression and the Hungarian-vs-greedy gap depend on a hand-placed cohort; a neutral run was degenerate. Not stated. | Every results table now labels correlation/dispatch **scenario-dependent** and reports **both** numbers: ~450:1 on a clustered incident, **~9:1 scattered**. The distinction is guarded by `golden/incident_formation_test`. |
| 6 | **No benchmark rigor** | Scaling numbers were a single timed pass, no warmup, no repeats, no variance; "33×" had no error bars. | `time_queries` now does a **warmup pass + median of 7 timed passes**, and reports **best run + spread** (printed and in the CSV). At 100k the spread is ±~5%; small-n rows show ±30–230% and are flagged as noise. Docs state "single machine, ratio to our own brute force — no external baseline." |
| 7 | **All data simulated** | Every "impact" number is synthetic under our own noise model; disclosed in places, but impact phrasing survived. | README results block and PRESENTATION now say "simulated GPS / simulated tourists" inline next to each figure; the ratio-to-own-brute-force caveat makes the internal baseline explicit. |
| 8 | **Worst case cuts against the headline** | Benchmarked structure is O(n) worst case; only interval tree/heap guaranteed. Disclosed in DATA_STRUCTURES but summaries risked unqualified "O(log n)." | Verified: the structure tables already say **"O(log n + k) avg"**, DATA_STRUCTURES keeps a dedicated worst-case table, and PRESENTATION §10 ("Honest engineering") states the O(n) worst case outright. No unqualified claim remained; no change needed beyond confirming it. |

**Measured after Tier 2:** `make bench` green, correctness gates pass. Headline
holds under the stricter method — **100k: quadtree ~32×, R-tree ~35×, spread
±3–10%** — so "~33×" is now a figure with an error bar, not a lucky single run.

## Tier 3 — Positioning for a resume  ·  status: ✅ done

| # | Flaw | Before | After |
|---|---|---|---|
| 9 | **Breadth reads as shallow** | 14 structures + 17 algorithms + simulator + dashboard + CI; an interviewer drills one. | Added the **Resume framing** section below: lead with depth (the persistent quadtree + the honest ceiling analysis), one disciplined bullet, and what *not* to quote. The README callout points evaluators at the graded core first. |
| 10 | **"No `std::`" reads as NIH** | Framed as pure rigor; a senior engineer reads it as poor production judgment. | README ground rule now states it is a **deliberate learning constraint for the course, not a production recommendation** — in real software you'd use `std::unordered_map` and a mature spatial library. |
| 11 | **Category inflation** | Most of the repo mass is systems/sim/viz, not data structures. | README "What this is" callout names the graded core (`geo/ index/ ds/ graph/`, ≈8k lines) and calls the simulator/dashboard/CI **scaffolding, not the deliverable**. |
| 12 | **Product framing oversells** | "The engine every team imports," "runs offline on a device" — reads as a deployable product. | README callout states plainly: **course project, not a shipped product; no mobile app, no live server; real geography, simulated people.** "on a device" softened to "locally, no server, in simulation." GitHub About/description rewritten to lead with the data-structures framing. |

---

## Resume framing — how to present this without getting exposed

The project's breadth is a liability in an interview: an interviewer picks **one**
thing and drills to bedrock. Lead with depth, not the catalogue.

**Do:**

- **Lead with one advanced structure you can defend to the floor** — the
  persistent path-copying quadtree. It is genuinely uncommon, it is real
  (`shared_ptr<const Node>`, immutable nodes, measured 13× structural sharing),
  and the "why the speedup ceilings at ~33×, not 29,000×" analysis shows judgment,
  not just coding.
- **Quote numbers with their caveats built in:** "≈33× over a brute-force oracle
  (median of 7 runs, single machine), bounded by output size — analysed, not just
  measured."
- **Call it what it is:** a data-structures course project / simulation study.

**Suggested one-liner** (defensible end to end):

> Built a persistent (path-copying) quadtree in C++17 with reference-counted
> immutable nodes for O(log n)-per-update historical queries; measured 13×
> structural sharing over 5,000 versions and validated every index against a
> brute-force oracle. Analysed why the spatial-index speedup is output-bound
> (~33×), not the ~29,000× first predicted.

**Don't:**

- Don't quote a big test-count or "eleven gaps" as if breadth proves depth.
- Don't imply a deployed product or real-world safety impact — the data is simulated.
- Don't claim "O(log n) spatial index" unqualified — say "average-case; O(n) worst
  case, which is why the AVL interval tree is the one with a real guarantee."
- Don't list all 14 structures; name two or three you can whiteboard on demand.
