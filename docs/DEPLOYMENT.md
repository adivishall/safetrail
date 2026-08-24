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
| **The dashboard** (what people see) | one static HTML file | GitHub Pages / any static host |
| **The engine** (the actual project) | a C++ library + CLI | source + `make`, or prebuilt binaries |
| **On-device** (the real-world target) | the same engine, cross-compiled | an embeddable library — no cloud |

---

## 1. Deploying the dashboard — GitHub Pages (automated)

This is set up and runs on every push to `main`.

`.github/workflows/deploy.yml` does, on GitHub's Ubuntu runners:

```
checkout → make test  →  make bench  →  make dashboard  →  publish to Pages
             (gate)      (artifact)     (real OSM data)     (_site/index.html)
```

The deploy is **gated on the test suite** — 10,941 checks must pass or nothing
publishes. The benchmark CSVs are uploaded as a downloadable artifact on every
run. The generated `dashboard.html` becomes the live site.

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

## 2. Deploying the dashboard — any static host (manual)

Because it is one file with zero dependencies, it hosts anywhere:

```bash
make dashboard
# then drop dashboard.html on any of:
#   - Netlify:  drag it onto app.netlify.com/drop
#   - Vercel:   vercel deploy dashboard.html
#   - S3:       aws s3 cp dashboard.html s3://bucket/ --acl public-read
#   - a USB stick handed to an examiner — it opens over file://
```

No build step, no `npm install`, no environment variables. The 1.6 MB file is the
whole site.

---

## 3. Distributing the engine

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

## 4. The real-world deployment story (what we'd argue in the report)

The problem statement targets remote terrain with no connectivity. A cloud
deployment is therefore the *wrong* architecture for the actual field use — which
is the whole reason we built an in-process engine instead of a PostGIS client.

The genuine deployment target is **on-device**: the same `libsafetrail` compiled
for an Android handset or a rugged GPS unit, evaluating zones locally with the
serialised spatial index shipped to it (GAP 6), and reconciling events when it
next reaches a network (Lamport sync, GAP 6). No server is in the loop at the
moment safety actually depends on it.

That path is not built yet — `sync/` is still a stub — but the architecture is
deliberately shaped for it: no module in the evaluation path allocates, blocks on
I/O, or assumes a network. The engine that runs in the demo is the engine that
would run on the device.

---

## 5. What is NOT deployed, and why

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
dashboard   →  GitHub Pages (CI, automatic) or any static host — it's one file
engine      →  git clone && make, or release binaries
field use   →  on-device library, no cloud (by design; sync layer still a stub)
```

The absence of a server to deploy is the feature, not a gap.
