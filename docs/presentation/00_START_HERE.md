# SafeTrail presentation kit — start here

This folder is the complete kit for presenting SafeTrail to a teacher: what to
run, what to say, what to click, what to answer, and what to do when something
breaks. Follow it top to bottom even if you are nervous; nothing in it requires
improvising.

**State this kit was written against (checked 2026-10-11):** branch
`flagship-hardening` at commit `76b63af`, identical to `origin/flagship-hardening`,
working tree clean, GitHub Actions run
[38035618082](https://github.com/adivishall/safetrail/actions/runs/38035618082)
green on that commit (all seven jobs), pull request
[#3](https://github.com/adivishall/safetrail/pull/3) open and not merged. If
`git log -1 --oneline` shows a different commit, re-check the numbers in this kit
against [RESULTS.md](../RESULTS.md) before presenting.

| File | Use it for |
|---|---|
| **00_START_HERE.md** (this file) | master checklist: before, during, after |
| [01_SPEAKING_SCRIPT.md](01_SPEAKING_SCRIPT.md) | what to say: a 5-minute and a 10-minute version |
| [02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md) | the nine demo steps, the dashboard and the slides, click by click, with recovery |
| [03_PROJECT_EXPLAINED.md](03_PROJECT_EXPLAINED.md) | the whole system explained at two levels, tied to the source files |
| [04_TEACHER_QA.md](04_TEACHER_QA.md) | the question bank: short answer, deeper answer, source, what not to overclaim |
| [05_LINKS_AND_RESOURCES.md](05_LINKS_AND_RESOURCES.md) | every URL and path, what is live and what is local, what to send the teacher |
| [06_LAST_MINUTE_CHECKLIST.md](06_LAST_MINUTE_CHECKLIST.md) | the 15-minutes-before list |
| [07_DEMO_FAILURE_PLAYBOOK.md](07_DEMO_FAILURE_PLAYBOOK.md) | what to do when a step fails |
| [08_PERSONAL_REHEARSAL.md](08_PERSONAL_REHEARSAL.md) | self-assessment after practising |
| [SafeTrail_review_slides.pdf](SafeTrail_review_slides.pdf) | offline PDF of the ten review slides (backup; the HTML deck is the primary) |

The maintained project documents the kit draws on: [REVIEW_DEMO.md](../REVIEW_DEMO.md)
(the original 5-minute script), [REVIEW_QA.md](../REVIEW_QA.md),
[REVIEW_SLIDES.html](../REVIEW_SLIDES.html), [RESULTS.md](../RESULTS.md),
[TESTING.md](../TESTING.md), [DATA_STRUCTURES.md](../DATA_STRUCTURES.md),
[ARCHITECTURE.md](../ARCHITECTURE.md), [DEFECT_LOG.md](../DEFECT_LOG.md). When
this kit and one of those disagree, the project document wins; tell me.

Every command below was run on this Mac on 2026-10-11 unless it is marked
*(standard macOS, not project-specific)* or *(unverified)*.

---

## Part 1 — Preparation before class (do this the day before, once)

### 1.1 What must be installed or present

| Need | Check | Status on this Mac |
|---|---|---|
| A C++17 compiler (Apple clang) | `c++ --version` | Apple clang 17.0.0 |
| GNU make | `make --version` | GNU Make 3.81 (the one macOS ships) |
| Python 3 (only for `make mutation`, `make bench` table rendering and docs checks; **not** needed for the demo) | `python3 --version` | Python 3.13.7 |
| A browser (Safari or Chrome) for `dashboard.html` and the slides | — | Safari and Google Chrome present |
| The repository with its data file | `ls data/zones/shillong_osm.geojson` | present |

If the compiler is missing on another Mac, `xcode-select --install` installs the
command-line tools *(standard macOS, not project-specific)*. Nothing else needs
installing: the project has no dependencies beyond the compiler.

### 1.2 Open a terminal in the repository root

The path contains spaces, so quote it:

```bash
cd "/Users/adivishal/Projects/Data Structures CP"
```

Confirm you are where you think you are:

```bash
git status --short | wc -l        # 0 means a clean tree (new kit files count until committed)
git branch --show-current         # flagship-hardening
git log -1 --oneline              # 76b63af ... (or newer, if you committed since)
```

### 1.3 Build

```bash
make
```

Builds `build/safetrail_headless` and `build/safetrail_bench`. Already built:
prints nothing and returns in under a second. From scratch: about a minute.

### 1.4 Quick pre-review test (about 10 s once built)

```bash
make test
make determinism
```

Expected endings: `ALL TESTS PASS` (45 test files, 11,844 checks) and
`determinism: identical output across runs`.

### 1.5 Warm-up run of the nine-step demo

```bash
make review
```

Runs all nine steps without stopping. Measured today: 33 s once built. It prints
exactly what the live run will print (the scenario is seeded), so read it once
and compare with [02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md). It also
writes `dashboard.html`, which you will open in Part 3.

What it writes: `build/review/*.txt` and `*.events` (scratch, ignored by git) and
`dashboard.html` (ignored by git). It does not touch any tracked file, so
`git status` stays clean.

### 1.6 Open the dashboard and the slides once, to check they open

```bash
open dashboard.html                 # (standard macOS 'open'; opens in your default browser)
open docs/REVIEW_SLIDES.html        # the ten slides; → to advance, F for fullscreen
```

Both are local files and work with Wi-Fi off. The slides load three screenshots
from `docs/images/` and the chart from `bench/plots/index_scaling.svg`; if you
copy the HTML file elsewhere on its own, those break. Use the PDF for sharing.

### 1.7 Read, in this order

1. [02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md) alongside the warm-up output.
2. [01_SPEAKING_SCRIPT.md](01_SPEAKING_SCRIPT.md), the 5-minute version, aloud, twice.
3. [04_TEACHER_QA.md](04_TEACHER_QA.md), at least the critical and complexity sections.
4. [08_PERSONAL_REHEARSAL.md](08_PERSONAL_REHEARSAL.md), score yourself.

### 1.8 Terminal and screen setup

- Terminal: dark theme, font 16–18 pt (Terminal.app: ⌘+ to enlarge; *standard
  macOS*), window at least 110 columns wide and as tall as the screen. The
  widest line the demo prints is the step-8 table (about 100 columns).
- Browser: one window, two tabs: `dashboard.html` and `REVIEW_SLIDES.html`.
  Close every other tab.
- Notifications off (Focus / Do Not Disturb; *standard macOS*).
- Charger plugged in. The benchmark numbers you show are committed, so battery
  state will not change them, but a dying laptop ends the review.

---

## Part 2 — The five-minute live presentation

Open the runbook on a phone or a second screen if you can; otherwise print it.

1. In the terminal, from the repository root:
   ```bash
   make review PAUSE=1
   ```
2. The header prints, then step 1 runs and stops at
   `↵  Enter for the next step`. **One Enter advances one step.** There are 8
   prompts for 9 steps; step 9 runs through to the end (tests, determinism,
   dashboard) with no prompt after it. Enter does nothing else: typing other
   text before Enter is ignored.
3. Narrate each step from [01_SPEAKING_SCRIPT.md](01_SPEAKING_SCRIPT.md) while its
   output is on screen; the runbook says what to point at.
4. If you must stop, ⌃C ends the run. To redo a single step on its own, without
   prompts, use `tools/review.sh N` (N = 1–9); it reuses the files the earlier
   run left in `build/review/`.
5. After the final `━━ done ━━` banner, switch to the browser tab with
   `dashboard.html` and reload it (⌘R) so you are showing the file the demo just
   wrote. (The content is identical to the warm-up's; reloading is only so you
   can truthfully say "this is the one it just wrote".)

**Pausing only happens in an interactive terminal.** If you run the command with
input redirected (for example inside a script), `PAUSE=1` is ignored and the run
goes straight through. That is by design and cannot hang.

Short on time? The script marks what to skip: step 7 (history) and the scrolling
test list in step 9.

---

## Part 3 — The dashboard demonstration (one to two minutes)

In the browser tab with `dashboard.html` (the two-hour, 60-tourist run over the
real zones, 720 frames):

1. Bottom bar, **`index: off`** button: click it to cycle brute force → quadtree
   → R-tree. The real structure is drawn over the map: a grid of disjoint cells
   for the quadtree, overlapping envelopes for the R-tree.
2. Click any moving dot: the **tracked tourist** panel shows that person and the
   cells or envelopes their query touches.
3. Top of the right panel, **main experiment**: the committed benchmark table and
   the live correctness check (3,000 random queries, `0 mismatches`).
4. Drag the **timeline slider**: *rules in force as of* and the *zone change
   log* follow it (Wards Lake comes into force at 00:30), and the **persistent
   index** panel shows consecutive versions with the copied path highlighted.
5. **`accuracy discs`** button: the GPS uncertainty drawn with the state colour;
   purple is Uncertain. **event stream** at the bottom of the panel.

Exact clicks and what each shows: [02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md),
section "The dashboard".

---

## Part 4 — Questions and answers

- [04_TEACHER_QA.md](04_TEACHER_QA.md) is grouped by difficulty; each answer has a
  10–20 second version and a deeper one.
- The project's own [REVIEW_QA.md](../REVIEW_QA.md) has the ten hardest marked ★.
- When you do not know: say so, say where the answer would be found
  (`docs/RESULTS.md` for any number, `docs/TESTING.md` for any test,
  `docs/DEFECT_LOG.md` for any bug), and offer to show it.
- Numbers: always quote ranges and say "on this laptop". The one-liner that is
  always safe: *"R-tree over 100× faster than our brute force at 100,000 zones,
  with identical results; the quadtree about 30×."*

---

## Part 5 — Cleanup after the presentation

There is nothing that must be cleaned. The demo writes only `build/review/` and
`dashboard.html`, both ignored by git. Check and leave it:

```bash
git status --short | wc -l        # 0 (or just the kit files, until committed)
```

Do **not** run `make clean`, `git reset --hard` or any `git clean` afterwards
"to tidy up": the first deletes the build (next `make review` takes 1.5 minutes
instead of 35 s), the other two can destroy uncommitted work.

---

## Which URLs to share with the teacher

Full table with what needs an account or a connection: [05_LINKS_AND_RESOURCES.md](05_LINKS_AND_RESOURCES.md).
The short list:

- Repository (public): https://github.com/adivishall/safetrail
- The branch being presented: https://github.com/adivishall/safetrail/tree/flagship-hardening
- Pull request #3 (the audited branch, CI green): https://github.com/adivishall/safetrail/pull/3
- The green CI run on the presented commit: https://github.com/adivishall/safetrail/actions/runs/38035618082
- Live dashboard (GitHub Pages) — **stale, from the 17 September deploy of
  `main`, before the audit**: https://adivishall.github.io/safetrail/
- Live slides URL — **currently serves the old course deck**, not the review
  deck: https://adivishall.github.io/safetrail/slides.html

Because the live site is stale until pull request #3 is merged, share the review
deck as the PDF in this folder and the dashboard as the `dashboard.html` file
(one self-contained file, about 1.6 MB, opens offline). Do not point the teacher
at the live `slides.html`.

---

## If the demo fails

[07_DEMO_FAILURE_PLAYBOOK.md](07_DEMO_FAILURE_PLAYBOOK.md), in this order:
retry the step on its own (`tools/review.sh N`), show the dashboard from the
rehearsal (already on disk), show the committed evidence (`docs/RESULTS.md`,
step 8's table, `docs/TESTING.md`), explain from the slides, continue without
the failed piece. Nothing in the demo needs the network.

## Getting back to a working state after a failed command

```bash
cd "/Users/adivishal/Projects/Data Structures CP"   # 1. be in the root
make                                                # 2. rebuild anything stale (seconds if nothing changed)
tools/review.sh 4                                   # 3. re-run just the step that failed (any of 1-9)
```

If `make` itself fails, read the **first** error it prints; it names the file
and line. Do not edit code during the review; switch to the backup plan.

## Backup plan (no Wi-Fi, no GitHub Pages, no network at all)

Everything needed is on the laptop and nothing in the demo makes a network
request:

| Need | Local source |
|---|---|
| The demo | `make review PAUSE=1` (binaries in `build/`, data in `data/zones/`) |
| The dashboard | `dashboard.html` from the warm-up run, in the repository root |
| The slides | `docs/REVIEW_SLIDES.html`, or `docs/presentation/SafeTrail_review_slides.pdf` |
| The benchmark evidence | `bench/results/*.csv`, `bench/results/environment.txt`, `docs/RESULTS.md` |
| The test evidence | `make test` output; `docs/TESTING.md` |
| The code | the repository itself |

If even the laptop fails: the PDF and `dashboard.html` on a USB stick or a phone
open on any computer with a browser.

---

## Final pre-review checklist

- [ ] Laptop charged, charger in the bag.
- [ ] `cd "/Users/adivishal/Projects/Data Structures CP"` in a terminal, 16–18 pt font, ≥110 columns.
- [ ] `git log -1 --oneline` shows the commit you expect.
- [ ] `make` is silent (nothing to build).
- [ ] `make review` ran today without a red line and ended with the `done` banner.
- [ ] `dashboard.html` open in a browser tab; `docs/REVIEW_SLIDES.html` in a second tab.
- [ ] `docs/presentation/02_LIVE_DEMO_RUNBOOK.md` and `04_TEACHER_QA.md` open where you can read them.
- [ ] Notifications off; every unrelated window closed.
- [ ] You have said the 5-minute script aloud at least twice, timed.
- [ ] You know the five numbers that matter (see [08_PERSONAL_REHEARSAL.md](08_PERSONAL_REHEARSAL.md), "Ten key facts").
