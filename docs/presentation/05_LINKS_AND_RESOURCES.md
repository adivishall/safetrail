# Links and resources

Every URL below was checked on 2026-10-11. Page URLs returned HTTP 200 to a
plain request; the `/blob/` file URLs returned GitHub's bot-protection status
(503) to a script but the files' presence on the branch was confirmed through
the GitHub API, and they open normally in a browser. The repository is
**public**, so no GitHub account is needed to read anything on github.com.

## The one thing to know about the live site

GitHub Pages deploys only on a push to `main`. Pull request #3 has not been
merged, so the live site still serves what `main` deployed on **17 September
2026**, before the 28 September audit:

- https://adivishall.github.io/safetrail/ is an **old dashboard** with pre-audit
  numbers and behaviour.
- https://adivishall.github.io/safetrail/slides.html is the **old course deck**
  (its title is "safetrail — presentation"), not the review deck.

Do not send the teacher either of those as the current work. The green CI run
on the presented commit did build the new site as an artifact (it contains the
review deck as `slides.html` and the new dashboard as `index.html`), but it is
only deployed when `main` changes. Merging #3 is a decision for you, not part
of this kit.

## Live links (need internet; no GitHub account needed)

| Link | What it is for | Status |
|---|---|---|
| https://github.com/adivishall/safetrail | the repository | live, public; the default branch shown is `main`, which is **behind** the presented branch |
| https://github.com/adivishall/safetrail/tree/flagship-hardening | the branch being presented, at `76b63af` | live, current |
| https://github.com/adivishall/safetrail/pull/3 | pull request #3, `flagship-hardening` → `main`: the audit, 24 commits, CI status | live, open, mergeable |
| https://github.com/adivishall/safetrail/actions/runs/38035618082 | the CI run on `76b63af`: test, clang, mutation, sanitize, cmake, macos, build all green; deploy skipped (PR) | live; GitHub keeps run logs for 90 days |
| https://github.com/adivishall/safetrail/actions | all CI runs | live |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/README.md | the README as presented | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/REVIEW_DEMO.md | the 5-minute demo script the project ships | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/REVIEW_QA.md | the technical Q&A (ten hardest marked ★) | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/REVIEW_SLIDES.html | the review deck's **source**; GitHub shows the HTML as text, it does not render it | live, current; use the PDF below for viewing |
| https://raw.githubusercontent.com/adivishall/safetrail/flagship-hardening/docs/REVIEW_SLIDES.html | the same file raw; also shows source, and its images would not load | live; not a way to view the deck |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/RESULTS.md | every benchmark table, generated from the CSVs, with the protocol and environment | live, current |
| https://github.com/adivishall/safetrail/tree/flagship-hardening/bench/results | the committed CSVs and `environment.txt` behind every number | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/bench/results/index_scaling.csv | the headline table's data | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/TESTING.md | how correctness is established | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/DATA_STRUCTURES.md | each structure: invariants and bounds | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/ARCHITECTURE.md | the pipeline, decisions, limitations | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/DEFECT_LOG.md | the ten defects and their fixes | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/INTERVIEW.md | the project explained at 30 s / 60 s / 3 min | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/RESUME.md | résumé bullets with sources, and the "Do not claim" list | live, current; useful to you, less so to the teacher |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/tools/review.sh | the script behind `make review` | live, current |
| https://github.com/adivishall/safetrail/blob/flagship-hardening/.github/workflows/deploy.yml | the CI definition (what gates what) | live, current |
| https://adivishall.github.io/safetrail/ | live dashboard | **stale** (17 Sep deploy of `main`); do not present from it |
| https://adivishall.github.io/safetrail/slides.html | live slides | **stale**: old course deck |

Note on the `docs/presentation/` kit itself: until you commit and push it, these
files exist only on this Mac and have no GitHub URL.

## Local files (no internet needed; on this Mac only)

| Path (from the repository root) | What it is for | Teacher access |
|---|---|---|
| `docs/REVIEW_SLIDES.html` | the review deck, interactive (→ ← F P) | only on this Mac, or as a file; needs `docs/images/` and `bench/plots/` next to it |
| `docs/presentation/SafeTrail_review_slides.pdf` | the same ten slides as a PDF, 1.1 MB, 10 pages, landscape 1440 × 900 px; generated 2026-10-11 from the deck at `76b63af`; verified to parse (10 pages, each starting with its slide title) and rendered as screenshots | **shareable**: attach to an email or put on a drive |
| `dashboard.html` | the generated dashboard, one self-contained file, 1.6 MB, no network | **shareable**: attach it; it opens in any browser from a local file |
| `docs/REVIEW_DEMO.md`, `docs/REVIEW_QA.md` | demo script and Q&A | also on GitHub (above) |
| `docs/RESULTS.md`, `bench/results/*.csv`, `bench/results/environment.txt` | the evidence behind every number | also on GitHub |
| `docs/presentation/*.md` | this kit | local until committed |
| `build/review/*.txt` | the engine output the demo parses (regenerated by each `make review`) | scratch; not for sharing |
| `docs/course/safetrail-slides.pdf` | the **old** course deck PDF (28 Sep) | do not share as current |

## What to send the teacher

A package that works when they have only GitHub and what you attach:

1. **The branch:** https://github.com/adivishall/safetrail/tree/flagship-hardening
   ("the README on this branch is the project summary; the `docs/` folder has
   the results, testing and defect log").
2. **Pull request #3** for the audit and the green CI:
   https://github.com/adivishall/safetrail/pull/3
3. **The slides as a PDF:** attach `docs/presentation/SafeTrail_review_slides.pdf`.
4. **The dashboard:** attach `dashboard.html` ("open it in any browser; it is
   one file and needs no server").
5. **Where the numbers come from:** https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/RESULTS.md

Say explicitly: "The live GitHub Pages site is an older deployment and will be
refreshed when the pull request is merged; please use the attached PDF and
dashboard."

If you commit and push this kit (your decision; nothing in it is pushed yet),
the PDF gets a URL of the form
`https://github.com/adivishall/safetrail/blob/flagship-hardening/docs/presentation/SafeTrail_review_slides.pdf`,
and GitHub renders PDFs in the browser. Until then, attach the file.

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
