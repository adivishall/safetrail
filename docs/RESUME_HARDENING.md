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

## Tier 2 — Substance / rigor  ·  status: ⬜ pending

| # | Flaw | Before | After |
|---|---|---|---|
| 5 | **Headline results are scenario-engineered** | The 453:1 compression and the Hungarian-vs-greedy gap depend on a hand-placed cohort; a neutral run was degenerate. Not stated. | _pending_ |
| 6 | **No benchmark rigor** | Scaling/persistent numbers are single runs, one machine, no warmup/repeats/variance; "33×" has no error bars and only an internal brute-force baseline. | _pending_ |
| 7 | **All data simulated** | Every "impact" number is on synthetic tourists under our own noise model. Disclosed in places, but impact-style phrasing survives. | _pending_ |
| 8 | **Worst case cuts against the headline** | The benchmarked structure (quadtree/R-tree) is O(n) worst case; only the interval tree and heap are guaranteed. Disclosed in DATA_STRUCTURES, but summaries say "O(log n)" unqualified. | _pending_ |

## Tier 3 — Positioning for a resume  ·  status: ⬜ pending

| # | Flaw | Before | After |
|---|---|---|---|
| 9 | **Breadth reads as shallow** | 14 structures + 17 algorithms + simulator + dashboard + CI; an interviewer drills one. | _pending_ |
| 10 | **"No `std::`" reads as NIH** | Framed as rigor; a senior engineer reads it as poor production judgment. | _pending_ |
| 11 | **Category inflation** | Most of the repo mass is systems/sim/viz, not data structures. | _pending_ |
| 12 | **Product framing oversells** | "The engine every team imports," "eleven gaps" — it is a simulation harness, not a deployable product. | _pending_ |
