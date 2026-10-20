# Deployment

## The one thing to understand first

**This project has no server to deploy.** That is not an omission — it is the
entire point of the design.

Every existing implementation of `SIH25002` runs a backend: a Node/FastAPI server,
a PostGIS database, an Ethereum node. Ours doesn't. The engine is a self-contained
C++ program that answers every geofencing question in-process, and the dashboard
is a single static HTML file it emits. There is no API, no database, no socket
(see [DATA_PROVENANCE.md](DATA_PROVENANCE.md) §3).

So "deployment" here means three separate, simple things, not one complicated one:

| What | Is really | Deployed as |
|---|---|---|
| **The dashboard** (what people see) | one static HTML file | GitHub Pages, Vercel, or any static host |
| **The engine** (the actual project) | a C++ library + CLI | source + `make`, or prebuilt binaries |
| **On-device** (the real-world target) | the same engine, cross-compiled | an embeddable library — no cloud |

---

## 1. Deploying the dashboard — GitHub Pages (automated)

This is set up and runs on every push to `main`.

`.github/workflows/deploy.yml` does, on GitHub's Ubuntu runners:

```
checkout → gates (test, clang, mutation, sanitize, cmake, macos)
             ├─→ bench   make bench on the runner → benchmark-results artifact (informational)
             └─→ build   make dashboard from the committed results → _site → deploy (main only)
```

The site never contains timings measured on the runner. Its benchmark table,
slide 8 and the chart come from the committed results: `build` assembles the
site from a clean checkout, fails if any tracked file differs from the commit,
and checks that the dashboard embeds the committed scaling CSV. `make bench`,
which rewrites the CSVs, the chart and the generated tables, runs only in the
separate `bench` job. (The dashboard's simulation and its live equivalence check
do run on the runner; they are deterministic.)

Concurrency is one group per pull request, where a newer push cancels the older
run, and one per branch for pushes and manual runs, without cancelling a running
run: a running main deploy always finishes. A newer main run waits for the whole
running run, its benchmark included (that step is limited to 15 minutes).
GitHub keeps at most one waiting run per group and replaces it with any newer
one, so the newest waiting main run deploys next and intermediate commits may
be skipped.

To redeploy, use "Run workflow" on `main`. Do not re-run an older main run: a
re-run uses its original commit and that commit's workflow file, and runs from
before this change would publish CI-runner timings again.

Every job below gates the deploy except the benchmark, which deliberately does not:

| Gate | What it runs |
|---|---|
| test (Ubuntu 24.04, g++ 13) | docs links resolve, results tables match the committed CSVs, every header compiles standalone, the whole suite, and `make determinism` — all with `WERROR=1` |
| clang (Linux) | the suite with clang's stricter conversion warnings as errors, `make stress` (randomised tests at 10×), and the Clang Static Analyzer |
| mutation | `tools/mutation_check.py`: 22 injected bugs, each must fail a test |
| sanitize | `make asan` — the WHOLE suite under ASan + UBSan with `-fno-sanitize-recover`, so UB aborts instead of printing and continuing |
| cmake | configure with `-DSAFETRAIL_WERROR=ON`, build, `ctest`, and check Make and CMake see the same source files |
| macos | the suite with Apple clang and `-Werror`, then the whole suite under UBSan |

Runner images are pinned (`ubuntu-24.04`, `macos-15`), so the compilers — and
the warnings `-Werror` turns into failures — only change when the workflow file
does.
| *benchmarks* | *informational only.* Timings on shared CI runners are noisy; gating on them produces flaky failures that teach people to ignore red builds. They run in their own `bench` job; the CSVs are uploaded as an artifact when the run completes (a failed run raises a warning and uploads nothing) and never reach the published site. |

If any gate fails, nothing publishes. The generated `dashboard.html`
becomes the live site.

**Result:** the dashboard is live at
`https://adivishall.github.io/safetrail/` after the first successful run.

Nothing to configure per-push. To trigger it manually, use the "Run workflow"
button on the Actions tab (`workflow_dispatch` is enabled).

### One-time setup (already done, documented for completeness)

```bash
# point Pages at GitHub Actions as its source
gh api -X POST repos/adivishall/safetrail/pages -f build_type=workflow
```

---

## 2. Deploying the dashboard — Vercel (Hobby plan, no cost)

The same dashboard, built by Vercel from the repository on every deploy.
`vercel.json` carries the whole configuration, so the project needs no settings
in the Vercel dashboard:

| Setting | Value | Why |
|---|---|---|
| Framework preset | Other (`"framework": null`) | a static page; nothing to detect |
| Install command | `""` (skipped) | no `package.json`; the C++ build needs nothing installed |
| Build command | `sh tools/vercel_build.sh` | `make dashboard CXX=g++`, then `dashboard.html` → `public/index.html` |
| Output directory | `public` | that one file is the whole site; `build/` and the sources are never served |

The build runs on Vercel's build image (Amazon Linux 2023), whose pre-installed
packages include `gcc-c++`, `make` and `git`, so the engine compiles there from
source: the executable is a build-time tool, not a hosted service, and no binary
from a laptop is involved. Before anything is published the script makes the
same checks as CI's `build` job: no tracked file changed, the dashboard's
benchmark table equals the committed `bench/results/index_scaling.csv` field for
field, and the page loads nothing from anywhere. A build that would publish
something else fails instead.

Rehearsed on 20 October 2026 in a container built from `amazonlinux:2023` plus
Vercel's documented package list: GCC 11.5, 7–8 s with four jobs, and the output
byte-identical (sha256 `77a6f012…`) to the file GitHub Pages was serving from
the same commit — both are Linux g++ builds. (A macOS clang build of the same
commit differs in 163 bytes of the dispatch summary; the frames and the event
stream are identical.)

To deploy from a laptop with Vercel CLI — the Hobby plan is free, needs no
payment method, and pauses rather than charges when a limit is reached:

```bash
npx vercel@latest login                          # once; opens a browser
npx vercel@latest link --yes --project safetrail # creates the project if needed
npx vercel@latest deploy --prod --skip-domain    # builds on Vercel, prints the URL,
                                                 # does not point the production domain at it
#   open that URL and check the dashboard
npx vercel@latest promote <that URL>             # now it is the production site
```

The first deployment of a new project is always a production deployment, which
is why the staged (`--skip-domain`) form is used for it; afterwards a plain
`npx vercel@latest deploy` makes a preview and `--prod` a production deploy. To
have every push to `main` deploy by itself, connect the repository under the
project's Settings → Git (free; it installs Vercel's GitHub app on the
repository): pull requests then get preview deployments and `main` the
production one, each built by the same script.

What Vercel does not do: measure anything. As on Pages, the site's benchmark
numbers are the committed results.

---

## 3. Deploying the dashboard — any static host (manual)

Because it is one file with zero dependencies, it hosts anywhere:

```bash
make dashboard
# then drop dashboard.html on any of:
#   - Netlify:  drag it onto app.netlify.com/drop
#   - Vercel:   vercel deploy dashboard.html   (one file, by hand; §2 is the build)
#   - S3:       aws s3 cp dashboard.html s3://bucket/ --acl public-read
#   - a USB stick handed to an examiner — it opens over file://
```

No build step, no `npm install`, no environment variables. The 1.6 MB file is the
whole site.

---

## 4. Distributing the engine

For a course project the distribution is the source plus the Makefile:

```bash
git clone https://github.com/adivishall/safetrail
cd safetrail && make        # builds the library + CLI, no dependencies
```

If you want prebuilt binaries (e.g. to attach to a GitHub Release):

```bash
make                                    # produces build/safetrail_headless, build/safetrail_bench
gh release create v0.1 build/safetrail_* dashboard.html \
   --title "safetrail v0.1" --notes "engine + dashboard"
```

The binaries are self-contained — they link only the C++ standard library.

---

## 5. The real-world deployment story (what we'd argue in the report)

The problem statement targets remote terrain with no connectivity. A cloud
deployment is therefore the *wrong* architecture for the actual field use — which
is the whole reason we built an in-process engine instead of a PostGIS client.

The genuine deployment target is **on-device**: the same `libsafetrail` compiled
for an Android handset or a rugged GPS unit, evaluating zones locally with the
serialised spatial index shipped to it (GAP 6), and reconciling events when it
next reaches a network (Lamport sync, GAP 6). No server is in the loop at the
moment safety actually depends on it.

The reconciliation half of that path is built: `sync/lamport.hpp` is the
happened-before clock and the offline event queue, tested end to end in
`tests/sync/offline_scenario_test.cpp`, and the serialised index the device would
carry is `index/geohash.hpp` (44 bytes per zone, explicitly little-endian, with a
deserialiser that refuses malformed input rather than loading a prefix). What is
NOT built is the packaging: an Android NDK build, a device GPS source, a transport.
Those are integration work, not algorithms, and the architecture is deliberately
shaped for them — no module in the evaluation path allocates, blocks on I/O, or
assumes a network. The engine that runs in the demo is the engine that would run
on the device.

---

## 6. What is NOT deployed, and why

| Not deployed | Because |
|---|---|
| A database | There isn't one. Zones live in a GeoJSON file and the in-memory index. |
| A backend API | The engine is in-process. Nothing to serve. |
| A tile server | The map is drawn on a canvas from polygon coordinates we already hold. |
| The simulator | It is a development/demo tool. The field engine consumes real GPS, not mobility models. |
| Secrets / keys | There are none. No accounts, no auth, no third-party services. |

---

## Summary

```
dashboard   →  GitHub Pages (CI) and Vercel (built from the repo), or any static host — it's one file
engine      →  git clone && make, or release binaries
field use   →  on-device library, no cloud (by design; engine + sync built,
               device packaging is not)
```

The absence of a server to deploy is the feature, not a gap.
