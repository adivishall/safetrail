# Demo failure playbook

What to do when something goes wrong during the review. Every recovery here is
non-destructive: nothing deletes, resets, cleans or edits code. If a recovery
is not on this page, do not improvise one live; move down the fallback ladder.

## The fallback ladder (always in this order)

1. **Retry the non-destructive step** on its own: `tools/review.sh N` for the
   step you were on (1–9). It reuses the engine runs already in `build/review/`
   and never prompts.
2. **Show the already-generated local artefacts:** `dashboard.html` in the
   browser tab you opened before the review; `docs/REVIEW_SLIDES.html` or the
   PDF in `docs/presentation/`.
3. **Show the committed evidence:** `docs/RESULTS.md` (every table with its
   environment), `bench/results/environment.txt`, `docs/TESTING.md`, and the
   green CI run https://github.com/adivishall/safetrail/actions/runs/38035618082
   if there is internet.
4. **Explain from the slides** (slide 4 for the indexes, 5 for geometry and
   state, 7 for correctness, 8 for results, 10 for limitations).
5. **Continue without the failed component.** Say what failed in one sentence
   and move on. A calm "the live run hit an environment problem; here is the
   same output from this morning's run" is worth more than five minutes of
   debugging.

The one-breath backup explanation: *"The demo output is deterministic and I ran
it this morning; here is the dashboard it wrote, and here are the committed
benchmark tables, with the commit and machine they were measured on."*

---

## Failures and recoveries

### Running from the wrong directory

**Symptom:** `make: *** No rule to make target 'review'. Stop.` or
`make: *** No targets specified and no makefile found. Stop.`, or
`tools/review.sh: No such file or directory`.
**Cause:** the shell is not in the repository root (the path has spaces).
**Recovery:**
```bash
cd "/Users/adivishal/Projects/Data Structures CP"
make review PAUSE=1
```
`tools/review.sh` itself changes to the repository root internally, so once you
are anywhere inside the repo, `tools/review.sh N` with the right relative path
also works; the simple fix is the `cd` above.

### Missing compiler or build tools

**Symptom:** `make` prints `c++: command not found`, or `xcrun: error: invalid
active developer path`, or `tools/build/warnings.flags is missing`.
**Cause:** the Xcode command-line tools are not installed on this machine (first
two), or the checkout is incomplete (third).
**Recovery:** on your own Mac this will not happen (Apple clang 17 is present).
On another Mac: `xcode-select --install` *(standard macOS, needs a network and a
few minutes)*. For the flags file: `git checkout tools/build/` restores it.
Otherwise go to fallback 2: the dashboard and PDF do not need a compiler.

### A failed build

**Symptom:** `make review` prints `build failed: run 'make' and read the first
error it prints`, or `make` shows compiler errors.
**Cause:** a source file was edited, or a partial build was interrupted.
**Recovery:** read the **first** error only. If it names a file you edited
today, `git diff --stat` shows what changed; `git stash` would hide uncommitted
edits but is **not** something to do live without understanding what you stash.
Do not run `make clean` (it deletes the build and the next build takes about a
minute, and if the error is in the source it will fail again). Go to fallback
2: the binaries in `build/` from the warm-up may still exist and
`tools/review.sh N` uses them; otherwise use the dashboard and slides.

### Stale generated binaries

**Symptom:** output differs from the runbook (different counts), or
`make review` recompiles for a minute first.
**Cause:** sources changed since the last build, so `make` rebuilt; or you are on
a different commit than this kit was written against.
**Recovery:** let the rebuild finish; the counts printed by the new binary are
the truth for that commit. Check `git log -1 --oneline`. If the counts differ
from the runbook, say so plainly ("this is a newer build; the numbers on screen
are the ones that count") rather than reading the runbook's numbers.
Deterministic output means two runs of the *same* binary agree; it does not
mean two commits agree.

### The dashboard file has not been generated

**Symptom:** `open dashboard.html` says the file does not exist, or the browser
shows a blank or old page.
**Cause:** `make review` or `make dashboard` has not run on this checkout
(`dashboard.html` is ignored by git, so a fresh clone has none).
**Recovery:**
```bash
make dashboard        # about 3 s once built; writes dashboard.html (720 frames)
open dashboard.html
```
If the browser tab was already open, reload (⌘R).

### The browser refuses a local asset

**Symptom:** the slides show broken images (slide 4 screenshots, slide 8
chart); or the dashboard opens but nothing draws.
**Cause:** the slides load `images/*.png` and `../bench/plots/index_scaling.svg`
by relative path, so they break if `REVIEW_SLIDES.html` is opened from a copy
outside `docs/`. The dashboard is fully self-contained and makes zero network
requests, so if it does not draw the cause is a very old browser or JavaScript
disabled.
**Recovery:** open the deck from its real location
(`open docs/REVIEW_SLIDES.html` from the repository root), or use the PDF
`docs/presentation/SafeTrail_review_slides.pdf`, which embeds everything. For the
dashboard, try the other browser (Safari ↔ Chrome). Both browsers open `file://`
pages directly; no server is needed. (The only browser known not to open
`file://` is the Claude desktop app's built-in pane, which is irrelevant here.)

### The presentation deck references missing chart assets

**Symptom:** slide 8 has an empty box where the chart should be.
**Cause:** `bench/plots/index_scaling.svg` is missing (it is tracked; a partial
checkout) or the deck was moved.
**Recovery:** `git checkout bench/plots/index_scaling.svg`; or show the same
table in `docs/RESULTS.md` §1 (the numbers are identical, the chart is a plot of
them); or the PDF, which has the chart embedded.

### ANSI-coloured output confusing a script

**Symptom:** a step prints `?` where a number should be, or an `awk` error.
**Cause:** the review script strips colour codes from the engine and benchmark
output before parsing (`plain()`); this would only occur if that stripping were
bypassed, for example by piping through a tool that re-colours.
**Recovery:** run the step directly in the terminal, not through a pager or a
wrapper: `tools/review.sh N`. If a count is genuinely missing, read the raw
output: `grep -m1 'zone entries' build/review/quadtree.txt`.

### The paused demo is waiting for input

**Symptom:** the terminal shows `↵  Enter for the next step` and nothing moves.
**Cause:** that is the pause; it is waiting for you. There are 8 prompts.
**Recovery:** press Enter once. If you pressed it and nothing happened, the
terminal may not have focus: click the terminal window and press Enter again.
Any other text before Enter is ignored. If you want out: ⌃C ends the run; then
`tools/review.sh N` resumes from a chosen step without prompts. It cannot hang
with input redirected: the pause only engages in an interactive terminal.

### A benchmark running longer than expected

**Symptom:** step 4, 6 or 7 takes more than ten seconds, or a live
`./build/safetrail_bench --only 1` runs past 30 s.
**Cause:** the benchmark sections run live in those steps (counts only, but the
timing loops still execute); a loaded machine stretches them. Each is normally
a few seconds; `--only 1` is about 15 s.
**Recovery:** wait; do not ⌃C unless it passes a minute. Fill the time with the
step's narration. Never start `make bench` (2–3 min) or `make bench-variation`
during the review; if asked for a live timing, offer `--only 1` and say the
ratio will differ from the committed table.

### Tests fail live in step 9

**Symptom:** a `[FAIL]` line and `TESTS FAILED`, and the script stops with
`tests failed`.
**Cause:** an edited source, a stale object file, or a genuine regression.
**Recovery:** do not debug. Say: "the suite is green in CI on this commit, run
38035618082; this is a local environment problem I will look at afterwards".
Then `make dashboard` (independent of the tests) and continue in the browser.
After the review: `make rebuild` (clean build plus tests, about 1.5 min) tells
you whether it was stale objects.

### Step 4 reports that the event streams differ

**Symptom:** `✗ EVENT STREAMS DIFFER between indexes` and the script exits.
**Cause:** this has never happened in a recorded run; it would mean an index
changed the engine's answer, or the three runs were made by different binaries
(for example one before and one after a rebuild).
**Recovery:** `make review` (a full rerun, 35 s) regenerates all three runs with
the same binary. If it still differs, say so honestly: it is exactly the kind of
failure the check exists to catch, and the differential test suite and CI are
green on this commit. Continue with `tools/review.sh 5`.

### GitHub Pages is outdated

**Symptom:** someone opens https://adivishall.github.io/safetrail/ or
`/slides.html` and sees an old dashboard or the course deck.
**Cause:** Pages deploys only from `main`; the audited branch is in an unmerged
pull request. The live site is the 17 September deployment.
**Recovery:** do not present from the live site. Show the local `dashboard.html`
and `docs/REVIEW_SLIDES.html`, and say: "the live site refreshes when the pull
request is merged; the current version is what you see here and in the attached
PDF". Merging is a decision for after the review, not a live action.

### Wi-Fi fails

**Symptom:** no network.
**Cause:** irrelevant to the demo. Nothing in `make review`, the dashboard or
the slides uses the network.
**Recovery:** carry on. The only things you lose are opening the GitHub links
during Q&A; `docs/RESULTS.md`, `docs/TESTING.md` and `docs/DEFECT_LOG.md` are on
disk and say the same things. If asked about CI, say the run is green on this
commit and offer to send the link afterwards.

### The projector or mirroring shrinks the terminal

**Symptom:** the step-8 table wraps, or the font is unreadable at the back.
**Recovery:** ⌘+ a couple of times for the font *(standard macOS)*; widen the
window; or run `tools/review.sh 8 | less -S` for a horizontally scrollable view
*(standard Unix pager; q to quit)*. If it still wraps, point at the 100,000 row
in `docs/RESULTS.md` §1 or slide 8 instead.

### You forget the next explanation

**Symptom:** silence.
**Recovery:** look at the step banner on screen (`━━ N. ... ━━`): its title is
the sentence. Read the first "say" line of that step from
[02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md). If the runbook is not at
hand, the script itself prints a short explanation under every step's output;
read that aloud, it is correct. Then press Enter.

### You are asked something you cannot answer

**Recovery:** "I don't know. The answer would be in `docs/RESULTS.md` /
`docs/TESTING.md` / the code at that file, and I can show you after." Then stop
talking. Guessing a number is the one failure this kit cannot recover from.

---

## What not to do live, ever

- `make clean`, `make rebuild`, `make bench`, `make bench-variation`,
  `make validate`, `make mutation`: minutes each, and the first two delete the
  build you are using.
- `git reset`, `git checkout .`, `git clean`, `git stash` without reading what
  they will touch.
- Editing any source file.
- Reading a number from the runbook that is not on the screen in front of you.
