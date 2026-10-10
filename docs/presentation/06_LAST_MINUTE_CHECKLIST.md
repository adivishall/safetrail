# Fifteen minutes before the review

Tick each line. Everything here was checked against what the repository
actually supports on 2026-10-11.

## Hardware and room

- [ ] MacBook charged; charger plugged in now and in the bag.
- [ ] If presenting on a projector or external screen: connect it now and mirror
      the display, so the terminal font check below is done at the size people
      will see.
- [ ] Phone silenced; it is also your copy of the runbook if the laptop is mirrored.

## Repository

- [ ] Terminal open **in the repository root**, path quoted because it has spaces:
      ```bash
      cd "/Users/adivishal/Projects/Data Structures CP"
      ```
- [ ] `git branch --show-current` → `flagship-hardening`.
- [ ] `git log -1 --oneline` → the commit you expect (`76b63af` when this kit was written, or your newer commit).
- [ ] `git status --short` → nothing unexpected. Untracked kit files are fine; a modified `src/`, `include/`, `apps/`, `Makefile` or `tools/` file is not — do **not** fix it now, note it, present from what is built.
- [ ] `make` → prints nothing (nothing to build). If it starts compiling, let it finish (about a minute); do not interrupt it.

## Build and tests confirmed

- [ ] `make test` → ends with `ALL TESTS PASS` (about 8 s once built; measured 8.2 s).
- [ ] `make determinism` → `determinism: identical output across runs`.

## Warm-up

- [ ] `make review` (no `PAUSE`) → runs through in about 35 s, ends with the
      `━━ done ━━` banner, no red line. This also writes `dashboard.html` fresh.
- [ ] Do **not** start `make review PAUSE=1` yet. Start it only when you are
      about to present, so the first prompt is not sitting there for ten minutes.
- [ ] Do **not** run `make bench`, `make validate`, `make mutation`, `make clean`
      or `make rebuild` now. Each takes minutes, and `make clean` / `make rebuild`
      delete the build you are about to use. The committed benchmark results are
      what step 8 shows; rerunning them changes nothing you will present.
- [ ] Do **not** edit any source file now.

## Browser

- [ ] One browser window, exactly two tabs:
      ```bash
      open dashboard.html
      open docs/REVIEW_SLIDES.html
      ```
- [ ] Dashboard tab: the map draws, dots move, the bottom bar shows `pause`, the
      slider, `index: off`, `accuracy discs`, `dispatch`.
- [ ] Slides tab: slide 1 shows; press → once and ← once; press F for fullscreen
      and Esc to leave it. Slide 4's two screenshots and slide 8's chart are
      visible (they load from `docs/images/` and `bench/plots/`).
- [ ] Every other tab and window closed. Browser zoom at 100% or larger.

## Terminal readability

- [ ] Font 16–18 pt (⌘+ enlarges in Terminal.app; *standard macOS*).
- [ ] Window at least 110 columns wide and the full height of the screen. Check
      with the step-8 table from the warm-up: its header row
      `zones  brute us  quad us  rtree us  quadtree x (range)  R-tree x (range)  k/query`
      must fit on one line.
- [ ] Dark theme; high contrast; the green `✓` of step 4 visible.
- [ ] Clear the screen before you start presenting: ⌃L *(standard terminal)*.

## Notifications and distractions

- [ ] Focus / Do Not Disturb on *(standard macOS)*.
- [ ] Messaging apps, mail and calendars quit.
- [ ] Spotlight, screen saver and auto-lock will not fire mid-demo: set the
      display to stay awake for the slot *(standard macOS; or just move the
      mouse between steps)*.

## Materials at hand

- [ ] `docs/presentation/02_LIVE_DEMO_RUNBOOK.md` open where you can glance at it
      (phone, printed, or a second screen that is not mirrored).
- [ ] `docs/presentation/04_TEACHER_QA.md` and `docs/REVIEW_QA.md` one tap away.
- [ ] Offline copies present on the laptop (no Wi-Fi needed):
      `docs/presentation/SafeTrail_review_slides.pdf`, `dashboard.html`,
      `docs/RESULTS.md`, `bench/results/`.
- [ ] Optional: the PDF and `dashboard.html` also on a phone or USB stick.

## In your head

- [ ] The five numbers: **5,038** zones · **13.91** candidates per query ·
      **27,935** identical events · **224×** R-tree and **33.7×** quadtree at
      100,000 zones (ranges 211–224× and 31.7–33.7× in that session; 102–235×
      across sessions) · **22 / 22** mutants caught.
- [ ] The three caveats you say unprompted: expected-case bounds with an O(n)
      worst case; k caps the speedup; no production-scale load test.
- [ ] The backup explanation if the live demo breaks (one breath): *"The demo
      output is deterministic and I ran it this morning; here is the dashboard
      it wrote, and here are the committed benchmark tables, with the commit and
      machine they were measured on."* Then open `dashboard.html` and
      `docs/RESULTS.md` (or slide 8).

## Then

- [ ] ⌃L, and when the teacher is ready:
      ```bash
      make review PAUSE=1
      ```
