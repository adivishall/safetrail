# SafeTrail — presentation preparation: master index

A complete study pack for presenting SafeTrail as a Data Structures course
project: every website feature, every data structure, the execution pipeline,
the Lab, benchmarks, testing, architecture, limitations, 115 viva questions and
ready-to-speak scripts. Everything was written from the repository's code and
committed results; numbers were checked against `bench/results/*.csv` and a test
run on 2026-11-02.

## Documents

| # | Document | What it gives you |
|---|---|---|
| 01 | [01_PROJECT_OVERVIEW.md](01_PROJECT_OVERVIEW.md) | the project in one paragraph; real vs simulated vs model; repository map; stack; headline numbers; **glossary** |
| 02 | [02_COMPLETE_WEBSITE_FEATURES.md](02_COMPLETE_WEBSITE_FEATURES.md) | every page, button, filter and workflow traced to its handler, endpoint, C++ function and data structure |
| 03 | [03_DATA_STRUCTURES_DEEP_DIVE.md](03_DATA_STRUCTURES_DEEP_DIVE.md) | each structure: definition, node layout, insert/delete/search, invariants, bounds (expected vs guaranteed), diagrams, worked examples |
| 04 | [04_ALGORITHMS_AND_EXECUTION_PIPELINE.md](04_ALGORITHMS_AND_EXECUTION_PIPELINE.md) | one GPS fix traced end to end; enter/exit/inaccurate/no-zone cases; every advanced algorithm |
| 05 | [05_WEBSITE_DEMONSTRATION_SCRIPT.md](05_WEBSITE_DEMONSTRATION_SCRIPT.md) | the live demo: where to click, what appears, what to say, fallbacks |
| 06 | [06_DATA_STRUCTURES_LAB.md](06_DATA_STRUCTURES_LAB.md) | all 15 Lab experiments; real traces vs replay; what data it uses |
| 07 | [07_BENCHMARKS_AND_PERFORMANCE.md](07_BENCHMARKS_AND_PERFORMANCE.md) | the measurement protocol, verified numbers, why speedups plateau, variation, fairness |
| 08 | [08_TESTING_AND_CORRECTNESS.md](08_TESTING_AND_CORRECTNESS.md) | oracles, differential tests, invariants, the 28 mutants, CI, the defects found |
| 09 | [09_SYSTEM_ARCHITECTURE.md](09_SYSTEM_ARCHITECTURE.md) | four diagrams; every layer; security; deployment; stack and reasons |
| 10 | [10_LIMITATIONS_AND_FUTURE_SCOPE.md](10_LIMITATIONS_AND_FUTURE_SCOPE.md) | genuine limitations, why they exist, ready answers, future work |
| 11 | [11_VIVA_QUESTION_BANK.md](11_VIVA_QUESTION_BANK.md) | 115 questions in 15 categories with short and deeper answers |
| 12 | [12_FINAL_PRESENTATION_SCRIPT.md](12_FINAL_PRESENTATION_SCRIPT.md) | 30 s, 1 min, 3 min, 5 min and 10 min scripts; why this project; conclusion |

## Recommended study order

1. **Day 1 — the story.** 01 (with the glossary), then 12 §1–§3. Say the 30-second and
   1-minute introductions aloud until they are natural.
2. **Day 2 — the core DS.** 03 §1–§3 (brute force, quadtree, R-tree), 04 §2 (the
   pipeline), 07 §4–§5 (what the speedups mean). Draw the quadtree and STR diagrams
   from memory.
3. **Day 3 — the rest of the DS.** 03 §6–§11 (interval tree, persistence,
   union-find, heap, hash map, timing wheel), 04 §4.
4. **Day 4 — the website.** 02 with the site open, page by page; then 06 (Lab).
5. **Day 5 — evidence.** 08 (testing), 09 (architecture), 10 (limitations).
6. **Day 6 — rehearsal.** 05 twice on the live site, once on `make app` locally.
   Then 11: cover the answers, read each question, answer aloud; mark the ones you
   miss.
7. **Day of the viva.** Re-read the ★ questions in 11, 07 §7 (variation) and 10 §2.

## The ten facts to know cold

| Fact | Value | Source |
|---|---|---|
| R-tree vs linear scan, 100k synthetic zones, one district | 223.67× (≈ 224×), floor ≈ 100× across sessions | `index_scaling.csv`, `docs/RESULTS.md` |
| Quadtree, same | 32.71× | same |
| Results per query there (k) | 98.45 | same |
| Constant density, 100k | quadtree 555.74×, R-tree 1026.86× | `index_density.csv` |
| STR vs insertion-built R-tree query | 6.26× faster | `index_build.csv` |
| Live index | R-tree (STR-loaded) | `src/app/platform.cpp` |
| Hysteresis defaults | enter 15 m, exit 25 m, 3 fixes, 5 s | `include/safetrail/fence/hysteresis.hpp` |
| Incident correlation | union-find, 200 m and 10 min | `PlatformConfig::correlation` |
| Demo routing | A* 772 vs Dijkstra 5,557 junctions | live run, incident #1 |
| Tests | 51 files, 12,166 checks; e2e 38; mutation 28/28 | `make test`, `make e2e`, `make mutation` |

## Quick links

- Live application: https://safetrail-v0bh.onrender.com (password: Render →
  safetrail → Environment)
- Static dashboard: https://adivishall.github.io/safetrail/
- Repository: https://github.com/adivishall/safetrail
- Official deliverables: [docs/final/README.md](../docs/final/README.md) (report,
  technical reference, viva guide, deck)
