# Security and privacy review

SafeTrail handles the location of people, so it is reviewed as a system that
could be misused for surveillance, not only as one that could be attacked.
This document records the threat model, what is implemented, how each control
is tested, and what remains open. It is honest about scale: this is a
single-process system reviewed by its developer, not an audited product.

## 1. Threat model

| Asset | Threat | Actor |
|---|---|---|
| Live location and movement history | read by someone with no need | curious operator, other tourist, outsider |
| Accounts | password guessing, session theft, CSRF | outsider, malicious web page |
| Evidence log | after-the-fact editing to hide or invent an event | insider |
| Zone configuration | malformed or malicious GeoJSON | administrator mistake, uploaded file |
| Server | resource exhaustion (huge bodies, slow clients, many connections, heavy lab requests) | outsider |
| Secrets | credentials committed to the repository | developer mistake |

## 2. Privacy by design

| Principle | Implementation | Tested by |
|---|---|---|
| Opt-in only | a tourist must give consent **and** open a session; nobody else can do either for them — not an operator, not an administrator (`Platform::set_consent`, `start_sharing`) | `tests/app/multi_user_test.cpp`, `tests/app/api_test.cpp` |
| No storage without consent | a fix from a user without consent or a session is refused and stored nowhere | `multi_user_test` ("not stored anywhere") |
| Visible tracking state | "Location sharing is ON/OFF" banner on the tourist's page; "Tracking active" chips for operators | web app |
| Stop at any time | `stop_sharing` ends the session and discards zone state (no invented exit event) | `multi_user_test` |
| Minimal data | username, display name, role; no phone number, email, ID document or real name required | `app::User` |
| Role-based access | admin / operator / responder / tourist checked in the API **and** again in the platform (two gates) | `api_test` role matrix |
| Need-to-know for responders | a responder sees a tourist's location only while assigned to an open incident affecting that tourist; access ends when it resolves | `api_test` |
| Isolation between tourists | a tourist sees only themself on the map and in the user list, only their own alerts and events, and of an incident that concerns them only their own part: not the other people's names, alerts or positions, and not the centroid or route (with two people, the centroid and your own position give away the other's). Responder positions and zone detail (which lists incidents) are for staff and responders only | `api_test` ("two tourists in one incident") |
| Audited access | every location read by someone other than the subject goes into an append-only Merkle-backed audit log: profile, trail and history on every read; the views the console polls (live map, user list, alert feed, incident detail) once per viewer, subject and view every ten minutes; other repeats within a minute are recorded once | `multi_user_test`, `api_test`, Settings page |
| Retention | movement trails older than the retention period (default 6 h, configurable) are deleted every minute; an expired position can no longer be looked up | `multi_user_test` ("Retention") |
| Erasure | a tourist or administrator can erase a movement history; positions on **closed** alerts are redacted; positions on open alerts are kept until they close, so a responder on the way can still find the person — a deliberate, documented exception | `multi_user_test` |
| Evidence without coordinates | evidence-log entries carry pseudonymous ids and zone ids, never coordinates, so erasure never conflicts with an append-only log | `multi_user_test` ("no coordinates") |
| Honest demonstration data | every scenario person is created with source "scenario", named "SIM · …" and badged SIM; demo responders are named "Demo …" and badged DEMO; production mode creates neither and refuses scenarios | `tests/app/scenario_test.cpp`, `tools/api_e2e.py` |

## 3. Authentication and sessions

| Control | Detail |
|---|---|
| Password storage | PBKDF2-HMAC-SHA256, 16-byte random salt, 120,000 iterations, self-describing record `pbkdf2-sha256$<it>$<salt>$<hash>`; verified against RFC 7914 and RFC 4231 vectors (`tests/app/crypto_test.cpp`). Argon2id would be the production choice; PBKDF2 is what SP 800-132 still permits and is small enough to implement and verify here. The public deployment runs with `--pbkdf2-iterations 10000`: on its 0.1-CPU instance one 120,000-iteration hash took 3.7 s with the single-threaded server waiting, and its four passwords are random 256-bit values, which no iteration count makes stronger. |
| Timing | constant-time comparison; a login for an unknown username runs the same key derivation (same iteration count) as a wrong password, so response time does not reveal which usernames exist. Tested by timing both (`multi_user_test`). |
| Session tokens | 256 bits from `/dev/urandom` (fail closed if unavailable); the server stores only SHA-256(token). |
| Cookie | `HttpOnly; SameSite=Strict; Path=/; Max-Age=28800`; `Secure` as well, plus `Strict-Transport-Security: max-age=31536000` on every response, with `--secure-cookies` or `--public` (only behind a TLS-terminating proxy). |
| Expiry | 30 minutes idle, 8 hours absolute; deactivating an account ends its sessions on the next request; logout deletes the record. |
| Throttling | three locks on failed sign-ins: 10 from one client address lock that address for 5 min; 5 for one account from one address lock that pair for 60 s; 25 for one account from anywhere lock the account for 60 s. A wrong password therefore locks only the person typing it, and a shared proxy address locks nobody else. Counts restart after 15 quiet minutes; a success clears the account's counts. With `--public`, sign-ins are also capped at 2/s server-wide (burst 10) and every request is rate limited (§7). |
| CSRF | every POST/PATCH/DELETE must carry `X-SafeTrail-Request: 1`. A cross-site form cannot set it, and a cross-site script cannot send it without a CORS preflight this server never approves. SameSite=Strict is a second layer. |

## 4. Input validation and parsing

- **HTTP:** request line and header grammar enforced; header block ≤ 16 KB, ≤ 64 headers, body ≤ 4 MB checked from Content-Length *before* buffering; chunked encoding refused (501); duplicate Content-Length refused (request-smuggling shape); 15 s idle timeout, and a request must arrive complete within 20 s of the connection opening however steadily it trickles in; ≤ 256 connections; a handler that throws answers 500 for that request instead of ending the process that holds every user's state (`tests/net/http_parse_test.cpp`, slow-client check in `tools/api_e2e.py`).
- **JSON:** the project's parser bounds nesting depth (64), rejects trailing data, raw control characters and invalid escapes ([../../include/safetrail/util/json.hpp](../../include/safetrail/util/json.hpp)).
- **GeoJSON:** every number range-checked before conversion (out-of-range conversion is undefined behaviour in C++); coordinates finite and on the lat/lon domain; polygons validated (self-intersection via the sweep line, holes inside the outer ring); vertex and zone-count caps; **all-or-nothing import** — one bad feature adds nothing.
- **Numbers from clients:** a JSON or query-string number becomes an integer only through checked helpers (`jint`, `jid`, `qint` in `src/app/api.cpp`) that refuse NaN, infinities (JSON `1e999` parses to infinity) and anything beyond 2^53 with a 400, instead of casting — an out-of-range double-to-integer conversion is undefined behaviour. Query parameters must be entirely numeric (`from=junk` is refused, not read as 0). The sanitizer builds include `float-cast-overflow`, which g++'s `-fsanitize=undefined` leaves out.
- **Fixes:** finite coordinates in range, accuracy in (0, 5000] m, timestamp not more than 60 s in the future nor before the session began.
- **Static files:** an allow-list of characters and no `..`; nothing outside `web/app/` (or the read-only `bench/results/` whitelist) can be named, including percent-encoded traversal (`api_test`, `api_e2e`).
- **Lab:** every experiment bounds its inputs (points, grid size, operation count).

## 5. Browser hardening

`Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'`,
plus `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`,
`Referrer-Policy: no-referrer`, `Permissions-Policy` denying geolocation, camera
and microphone; `Cache-Control: no-store` on every API response. The web app
builds every text node with `textContent`, never `innerHTML`, so user-supplied
names and notes cannot inject markup. No third-party scripts, fonts or tiles are
loaded.

## 6. Secrets

No credential is in the repository. Passwords come from
`SAFETRAIL_<ROLE>_PASSWORD` environment variables; otherwise they are
generated at random on first start and stored in `var/demo-credentials.txt`
(mode 0600, directory 0700, git-ignored). `make app-reset` deletes it. The
existing `.gitignore` already excludes `.env*` (the Vercel CLI token).

With `--public` nothing is generated or written: the server refuses to start
unless all four variables are set, each at least 16 characters, and its error
names only the missing variables. On Render the four values were generated by
Render (`generateValue` in [render.yaml](../../render.yaml), 256 random bits
each) and live only in the service's environment; the deployed log lists the
accounts as `(from environment)`. `.dockerignore` keeps `.env*`, `var/` and
`.vercel/` out of the image's build context.

## 7. Deployment posture

The server binds to 127.0.0.1 by default and prints a warning if told to bind
elsewhere: it has **no TLS** of its own. The public instance at
https://safetrail-v0bh.onrender.com runs in a container behind Render's
TLS-terminating proxy with `--public` ([DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) §1):

- **Client address.** Behind a proxy every connection comes from the proxy, so
  per-address limits would treat all visitors as one. The server reads a
  client-address header only when told which (`--client-ip-header
  CF-Connecting-IP` on Render) and only when the connection itself comes from a
  trusted proxy range (private and loopback ranges by default,
  `--trusted-proxy` to change them), the header appears exactly once and holds
  an IP address; otherwise it uses the socket address. Cloudflare, in front of
  Render, overwrites that header on every request and refuses a request that
  already carries one (checked on the live URL: 403, error code 1000).
  `X-Forwarded-For`, whose leftmost entry a caller controls, is not used
  (the server has no special handling for it, and the deployment does not name it).
- **Rate limits** (token buckets, per process): 100 requests/s per address
  (burst 300) on everything but `/api/health`; 2/s (burst 20) for API requests
  without a session; 20/s (burst 60) per session, with a Lab run costing 5;
  2 sign-ins/s server-wide (burst 10). Over a limit: 429 with `Retry-After`.
- **Public-demo labelling.** The sign-in page and the top bar say that every
  person is simulated and that all state resets; there is no self-registration.
- **Same origin.** The console and the API are served by the same process from
  one origin; no cross-origin request is ever approved.

The GitHub Pages and Vercel deployments serve only the static dashboard (a
replay with no accounts or live data), not this server.

## 8. Dependencies

None at run time beyond the C++ standard library and POSIX sockets. The
build needs a C++17 compiler and make; the tools need Python 3's standard
library. The supply-chain surface is the compiler toolchain and the GitHub
Actions used by CI (pinned to major versions).

## 9. Open items (known, not hidden)

1. No TLS in process (by design; see §7).
2. All state is in memory: accounts, zones and evidence are lost on restart. Persisting them (and signing published Merkle roots) is future work.
3. PBKDF2 rather than a memory-hard hash.
4. Throttling and sessions are per process; a multi-instance deployment would need a shared store.
5. Rate limits are per process and in memory, and apply only with `--public`; the local demo has the sign-in locks but no request limits.
6. The audit log records reads made through the API; anyone with shell access to the server process is outside its scope.
7. The public instance is one shared world: any signed-in operator or administrator can run, reset or replace the scenario for everyone. That is acceptable only because every person in it is simulated and nothing is kept.
8. Sessions that expire are removed when next presented, not swept; the table grows with logins until then (each one needs a valid password).
9. This review was made by the developer. No external penetration test has been performed.
10. The client-address header is trusted from any private-range peer. On Render only the platform's proxy can connect to the container, so this holds there; on a host where other machines share the private network, `--trusted-proxy` must name the proxy exactly.

## 10. Defects found by the final verification (2026-11-01)

Each was confirmed by a test that failed on the code before the fix
(22 failing checks in `api_test` and `multi_user_test`, the slow-client
check in `api_e2e`, and a UBSan abort on the old code).

| Defect | Effect | Fix |
|---|---|---|
| Incident detail and the live map showed a tourist the whole incident they were part of | another tourist's name, alert positions, timeline entries, and a centroid from which their position follows | own-part-only view for people the incident concerns |
| Responder positions and zone detail were readable by any signed-in user | tourists could see where units were, and which incidents touched a zone | staff and responders only |
| User list, alert feed and incident detail showed locations without an audit record | the "every read is audited" claim did not hold for those views | audited once per viewer, subject and view per ten minutes |
| The dummy hash for unknown usernames had 1 PBKDF2 iteration against 120,000 | an unknown name answered ~100,000× faster: usernames could be enumerated by timing | dummy hash at the configured cost |
| Client numbers cast straight to integers (`int(1e999)`, `uint32_t(-1.0)`), and `atoll`/`atof` on query strings | undefined behaviour (UBSan aborted); `from=junk` read as 0; `responder: -1` wrapped to an id | checked conversions, 400 on bad input |
| No request deadline | a client trickling a byte every 14 s held a connection for hours; 256 of them locked everyone out | 20 s from accept to a complete request |
| A handler exception would end the process | every user's in-memory state lost | caught per request (500) |
| `PATCH /api/users/{id}` stopped after the first field | a name and password change together kept the old password | every field applied |
| `secure_cookies` existed but no command-line switch set it | the documented Secure-cookie deployment was not possible | `--secure-cookies` |

## 11. Public deployment checks (2026-11-02)

Against the live URL, without an account (a script; 44 of 44 passed): HTTPS
redirect and certificate; HSTS, CSP, frame, type and referrer headers; the
client address is the visitor's, and spoofed `X-Forwarded-For`, `X-Real-IP`,
`True-Client-IP` and `Forwarded` headers do not change it, while a spoofed
`CF-Connecting-IP` is refused at Cloudflare's edge; 16 data endpoints answer
401 without a session; mutating requests without the CSRF header answer 403; a
wrong sign-in answers 401 without a cookie; four path-traversal forms are
refused; a burst of 45 anonymous requests drew 22 answers of 429 while
`/api/health` stayed available. Signed in (the owner typing the password): the
session cookie is invisible to page scripts, and the whole walkthrough ran with
no failed request. The server log shows no password. The `Secure` attribute
itself was observed on the same image run locally (`tools/api_e2e.py` public-mode
checks); the browser tools used on the live site do not expose response headers.

Locally, `tests/app/api_test.cpp` covers the same ground in process: an access
sweep over every route for anonymous, CSRF-less and tourist callers; the
client address only from a trusted peer, a single header copy and a plausible
value; a shared proxy address that does not lock out other visitors; request
and sign-in limits. `tools/api_e2e.py` starts the real binary in public mode:
it refuses to start without the four secrets and writes nothing, reads
`$PORT`, sets `Secure` and HSTS, honours the client-address header only from
the proxy, rate limits, prints no secret and exits 0 on SIGTERM.
