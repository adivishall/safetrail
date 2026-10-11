# Links and resources

Every URL below was checked on 2026-10-11. Page URLs returned HTTP 200 to a
plain request; the `/blob/` file URLs returned GitHub's bot-protection status
(503) to a script but the files' presence on `main` was confirmed through the
GitHub API, and they open normally in a browser. The repository is
**public**, so no GitHub account is needed to read anything on github.com.

## The one thing to know about the live site

GitHub Pages deploys from successful workflow runs on `main`: every push to
`main` starts one, and so does "Run workflow" on the Actions tab. Since the
deploy-workflow fix (pull request #4, merged 11 October as `60d8bec`), a
pull-request run never touches a main deploy, and a newer main run waits for a
running one to finish rather than cancelling it (see 07).

That fix also takes the site's benchmark numbers from the committed results. The
workflow's `build` job assembles the site from a clean checkout and fails if any
tracked file differs from the commit; the informational benchmark runs in a
separate `bench` job whose CSVs only become a downloadable artifact. From the
first deploy that includes the fix, slide 8's table and chart and the
dashboard's "main experiment" table show the committed laptop results, as the
demo, the PDF and every document do (239 µs, 33.7× and 224× at 100,000 zones).
Deploys before it, including those of `e794816` and `b7d6ced`, showed a CI
runner's timings in those three places. Verified after run 38153391670 deployed
`60d8bec` (Pages deployment 6998053191): the live `slides.html` is byte-identical
to the committed deck with its chart path made site-relative, the chart and the
three screenshots are byte-identical to the committed files, and the dashboard's
benchmark table equals `bench/results/index_scaling.csv`. Later deploys rebuild
the site, so **before sharing a live link, check that slide 8 of the live deck
shows 239 µs.**

Pull request #3 was merged on 11 October 2026 as merge commit `e794816`, and run
38134207816 deployed it. Checked after that deploy:

- https://adivishall.github.io/safetrail/slides.html serves the review deck
  (title "SafeTrail — project review"); its three screenshots and the chart load.
- https://adivishall.github.io/safetrail/ serves the dashboard built from that
  commit's code (current panels, no internal `[GAP n]` labels).

## Live links (need internet; no GitHub account needed)

| Link | What it is for | Status |
|---|---|---|
| https://github.com/adivishall/safetrail | the repository; `main` is the presented code | live, public; `main` contains the audited work (merge commit `e794816`) |
| https://github.com/adivishall/safetrail/tree/flagship-hardening | the merged branch, at `535aea7` (the kit commit) | live; same code as `main` |
| https://github.com/adivishall/safetrail/pull/3 | pull request #3, `flagship-hardening` → `main`: the audit, 25 commits, CI status | live, **merged** 11 October |
| https://github.com/adivishall/safetrail/actions/runs/38134207816 | the CI run on the merge commit `e794816`: all eight jobs green, the six gates (test, clang, mutation, sanitize, cmake, macos), build and deploy | live; GitHub keeps run logs for 90 days |
| https://github.com/adivishall/safetrail/actions/runs/38035618082 | the earlier CI run on the pull request at `76b63af`: all gates green; deploy skipped (PR) | live |
| https://github.com/adivishall/safetrail/actions | all CI runs | live |
| https://github.com/adivishall/safetrail/blob/main/README.md | the README as presented | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/REVIEW_DEMO.md | the 5-minute demo script the project ships | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/REVIEW_QA.md | the technical Q&A (ten hardest marked ★) | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/REVIEW_SLIDES.html | the review deck's **source**; GitHub shows the HTML as text, it does not render it | live, current; use the PDF below for viewing |
| https://raw.githubusercontent.com/adivishall/safetrail/main/docs/REVIEW_SLIDES.html | the same file raw; also shows source, and its images would not load | live; not a way to view the deck |
| https://github.com/adivishall/safetrail/blob/main/docs/RESULTS.md | every benchmark table, generated from the CSVs, with the protocol and environment | live, current |
| https://github.com/adivishall/safetrail/tree/main/bench/results | the committed CSVs and `environment.txt` behind every number | live, current |
| https://github.com/adivishall/safetrail/blob/main/bench/results/index_scaling.csv | the headline table's data | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/TESTING.md | how correctness is established | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/DATA_STRUCTURES.md | each structure: invariants and bounds | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/ARCHITECTURE.md | the pipeline, decisions, limitations | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/DEFECT_LOG.md | the ten defects and their fixes | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/INTERVIEW.md | the project explained at 30 s / 60 s / 3 min | live, current |
| https://github.com/adivishall/safetrail/blob/main/docs/RESUME.md | résumé bullets with sources, and the "Do not claim" list | live, current; useful to you, less so to the teacher |
| https://github.com/adivishall/safetrail/blob/main/tools/review.sh | the script behind `make review` | live, current |
| https://github.com/adivishall/safetrail/blob/main/.github/workflows/deploy.yml | the CI definition (what gates what) | live, current |
| https://adivishall.github.io/safetrail/ | live dashboard | current code, rebuilt by each successful run on `main`; benchmark table from the committed results once the deploy fix has deployed |
| https://adivishall.github.io/safetrail/slides.html | live slides | the review deck, rebuilt by each successful run on `main`; slide 8 from the committed results once the deploy fix has deployed (check it shows 239 µs) |
| https://github.com/adivishall/safetrail/blob/main/docs/presentation/SafeTrail_review_slides.pdf | the review deck as a PDF, with the committed numbers; GitHub renders PDFs in the browser | live, current |

The kit itself (`docs/presentation/`) is on `main` and readable on GitHub like the
other documents.

## Local files (no internet needed; on this Mac only)

| Path (from the repository root) | What it is for | Teacher access |
|---|---|---|
| `docs/REVIEW_SLIDES.html` | the review deck, interactive (→ ← F P) | only on this Mac, or as a file; needs `docs/images/` and `bench/plots/` next to it |
| `docs/presentation/SafeTrail_review_slides.pdf` | the same ten slides as a PDF, 1.1 MB, 10 pages, landscape 1440 × 900 px; generated 2026-10-11 from the deck at `76b63af`; verified to parse (10 pages, each starting with its slide title) and rendered as screenshots | **shareable**: attach to an email or put on a drive |
| `dashboard.html` | the generated dashboard, one self-contained file, 1.6 MB, no network | **shareable**: attach it; it opens in any browser from a local file |
| `docs/REVIEW_DEMO.md`, `docs/REVIEW_QA.md` | demo script and Q&A | also on GitHub (above) |
| `docs/RESULTS.md`, `bench/results/*.csv`, `bench/results/environment.txt` | the evidence behind every number | also on GitHub |
| `docs/presentation/*.md` | this kit | also on GitHub (`main`) |
| `build/review/*.txt` | the engine output the demo parses (regenerated by each `make review`) | scratch; not for sharing |
| `docs/course/safetrail-slides.pdf` | the **old** course deck PDF (28 Sep) | do not share as current |

## What to send the teacher

A package that works when they have only GitHub and what you attach:

1. **The repository:** https://github.com/adivishall/safetrail
   ("the README is the project summary; the `docs/` folder has the results,
   testing and defect log").
2. **Pull request #3** for the audit and its CI:
   https://github.com/adivishall/safetrail/pull/3
3. **The slides as a PDF:** attach `docs/presentation/SafeTrail_review_slides.pdf`,
   or send https://github.com/adivishall/safetrail/blob/main/docs/presentation/SafeTrail_review_slides.pdf
4. **The dashboard:** attach `dashboard.html` ("open it in any browser; it is
   one file and needs no server").
5. **Where the numbers come from:** https://github.com/adivishall/safetrail/blob/main/docs/RESULTS.md

You can also send the live slides link, https://adivishall.github.io/safetrail/slides.html,
once you have checked that its slide 8 shows 239 µs, as the PDF does. If it
shows anything else, send only the PDF.

## How the PDF was made (and how to remake it)

No new tooling: the deck already has a print stylesheet (one slide per page) and
Google Chrome is installed. The simple way, from the repository root, prints
portrait Letter pages:

```bash
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless --disable-gpu --no-pdf-header-footer --virtual-time-budget=4000 --print-to-pdf="$PWD/docs/presentation/SafeTrail_review_slides.pdf" "file://$PWD/docs/REVIEW_SLIDES.html"
```

The committed PDF was made the same way from a temporary copy of the deck with
one extra CSS rule, `@page{size:1440px 900px;margin:0}`, and the image paths
made absolute, so the pages are landscape and nothing spills. At 1280 × 720 the
last line of some slides spilled onto the next page; 1440 × 900 and larger were
clean. Verified with Python's `pypdf`: 10 pages, each page's text starting with
its slide's tag.

Remake it after any change to `docs/REVIEW_SLIDES.html` or after `make bench`
(the slide-8 table is generated from the CSVs). Do not remake it in the fifteen
minutes before the review.

## Other genuinely useful resources

| | |
|---|---|
| `make help` | lists every make target with a one-line description |
| `tools/review.sh N` | reruns one demo step (1–9) without prompts |
| `./build/safetrail_bench --only 1` | a live timing of the scaling section, about 15 s, if a viewer insists; the ratio will differ from the committed table |
| `docs/DEPLOYMENT.md` | why there is no server, and how Pages is deployed |
| `docs/DATA_PROVENANCE.md` | where every number in the dashboard comes from (zero network requests) |
| `docs/course/README.md` | the archived course material and why its numbers are superseded |
