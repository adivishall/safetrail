# Deployment guide

SafeTrail has two very different deliverables that are easy to confuse: a
**static dashboard** (one HTML file, hosted publicly) and the **integrated
application** (the C++ server with accounts, live multi-user tracking, alerts,
incidents and the Data Structures Lab). Only the first is on the internet.
This guide says what each one is, how to run it, and what a public deployment
of the server would take. Checked on 2026-11-01.

| | What it is | Where it runs | Accounts, live data, API |
|---|---|---|---|
| 1. Static dashboard | a replay produced by `safetrail_headless`, embedded in one HTML file | GitHub Pages and Vercel (public) | none |
| 2. Integrated application | `safetrail_server` + the web console | your own machine (127.0.0.1) | yes |
| 3. Public backend | the same server behind a TLS proxy on a hosting service | **not deployed** | — |

## 1. Static dashboard (public)

`make dashboard` runs the engine headless over the OpenStreetMap zones with
simulated walkers and writes `dashboard.html`: a self-contained page with the
map, the replayed events and the committed benchmark table. It has no server
behind it, no accounts and no live data; nothing a visitor does reaches any
backend (`/api/health` on these hosts answers 404).

| URL | Built from | Who can open it |
|---|---|---|
| https://adivishall.github.io/safetrail/ | `main`, by `.github/workflows/deploy.yml` (job `deploy`, after every gate) | anyone |
| https://safetrail-adivishall.vercel.app | `main`, by Vercel's Git integration running `tools/vercel_build.sh` | anyone |
| https://safetrail-git-final-product-completion-adivishall.vercel.app | this pull request's branch | **only signed-in members of the Vercel project**; anyone else gets Vercel's login page (deployment protection on previews) |

The PR preview proves the branch builds on Vercel's image; it is not a
public demo, and it is not the integrated application. Details of both
static pipelines: [../DEPLOYMENT.md](../DEPLOYMENT.md).

## 2. Integrated application (local — the guaranteed demonstration)

Everything in the live demo runs here, on one laptop, with no network, no
account anywhere and nothing to pay for.

On a MacBook Air M4 (macOS, Apple clang):

```bash
xcode-select --install
```

```bash
git clone https://github.com/adivishall/safetrail.git
```

```bash
cd safetrail && make app
```

The first build takes about a minute. The server prints the four accounts
(`admin`, `operator`, `responder`, `tourist`) with generated passwords, saves
them in `var/demo-credentials.txt` (mode 0600, git-ignored), loads the
walkthrough scenario paused at T+00:00:00, and serves the console at
**http://127.0.0.1:8080**. To choose the passwords instead:

```bash
SAFETRAIL_ADMIN_PASSWORD='choose-10+-chars' SAFETRAIL_OPERATOR_PASSWORD='choose-10+-chars' make app
```

Before presenting, prove the build end to end (starts its own server on a
free port; about 22 s):

```bash
make e2e
```

Other modes, ports and the reset command are in
[COMPLETE_TECHNICAL_DOCUMENTATION.md](COMPLETE_TECHNICAL_DOCUMENTATION.md) §2;
fallbacks if anything fails on the day are in
[LIVE_DEMO_SCRIPT.md](LIVE_DEMO_SCRIPT.md).

Properties of this deployment, stated plainly: all state is in memory (a
restart starts a fresh world); the server listens on loopback only; there is
no TLS, which is acceptable only because nothing leaves the machine; every
person in the scenarios is simulated and labelled SIM.

## 3. A public backend: is there a safe free path?

**Short answer: technically yes, for a demonstration with throw-away state;
it has not been done, and it should not be done before the changes in §3.3.**

### 3.1 Free hosting that fits (checked 2026-11-01)

The server is one process with no database, so any free container host with
a TLS front end can run it. Two candidates:

| Host | Free terms (as published) | Fit |
|---|---|---|
| Render, free web service | Docker supported; spins down after 15 minutes without traffic and takes up to a minute to start again; 750 free instance hours a month | works, but every spin-down erases all state (accounts created, scenario progress, evidence log) |
| Hugging Face Spaces, Docker SDK | free CPU Basic (2 vCPU, 16 GB); sleeps when unused; one public port; container runs as UID 1000 | works; same loss of state on sleep |

Not counted as free: Koyeb (sources disagree on whether a card is now
required), Fly.io and Railway (trial credit or paid). Prices and terms change;
check the provider's page before relying on any of this. Sources: Render
"Deploy for Free" (docs.render.com/free) and changelog, Koyeb pricing
(koyeb.com/pricing), Hugging Face Hub Spaces overview
(huggingface.co/docs/hub/en/spaces-overview).

### 3.2 A container that runs it (tested locally, not deployed)

This Dockerfile was built and run with Docker (Colima) on 2026-11-01: a
148 MB image; `/api/health` answers; sign-in sets an `HttpOnly; SameSite=Strict;
Secure` cookie; the process runs as the unprivileged user `ubuntu` (UID 1000).
It is not in the repository because nothing deploys it.

```dockerfile
FROM ubuntu:24.04 AS build
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends g++ make findutils >/dev/null && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make -j"$(nproc)" build/safetrail_server

FROM ubuntu:24.04
WORKDIR /app
COPY --from=build /src/build/safetrail_server /app/
COPY web/app /app/web/app
COPY data/zones /app/data/zones
COPY data/osm/roads.txt /app/data/osm/roads.txt
COPY bench/results /app/bench/results
USER ubuntu
EXPOSE 8080
CMD ["/app/safetrail_server", "--host", "0.0.0.0", "--port", "8080", "--secure-cookies", "--var", "/tmp/safetrail"]
```

`--host 0.0.0.0` is needed inside a container; the server prints a warning
for it, rightly, because the TLS must come from the host's proxy.

### 3.3 What must change before anyone points the internet at it

1. **Client addresses behind the proxy.** Every request would come from the
   proxy, so the per-address login lock (five failures, 60 s) would lock
   everyone out at once. The server must read `X-Forwarded-For`, and trust it
   only from the proxy.
2. **Secrets.** Set all four `SAFETRAIL_<ROLE>_PASSWORD` values as the host's
   secrets. A password that is not set is generated and printed to the
   container log.
3. **Demo or production mode.** Demo mode lets any operator load and run
   scenarios; on a public host that is a shared, resettable world. Decide
   which accounts exist and who holds them.
4. **Persistence.** On a free tier the world vanishes on every sleep. A real
   deployment needs the accounts, zones and evidence log stored and the Merkle
   roots signed (open item 2 in [SECURITY_AND_PRIVACY.md](SECURITY_AND_PRIVACY.md)).
5. **Per-user rate limits** beyond login (open item 5 there).
6. **No real people.** The platform is built for consented tracking, but this
   project has no consent process, privacy notice or data-protection review
   for real users. A public instance must carry simulated people only.

Until then the integrated application is demonstrated locally (§2), and the
public links show the static dashboard (§1).
