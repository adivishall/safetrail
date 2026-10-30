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
| Audited access | every location read by someone other than the subject (profile, trail, history, live map) goes into an append-only Merkle-backed audit log; repeated reads within a minute are recorded once | `multi_user_test`, `api_test`, Settings page |
| Retention | movement trails older than the retention period (default 6 h, configurable) are deleted every minute | `Platform::enforce_retention` |
| Erasure | a tourist or administrator can erase a movement history; positions on **closed** alerts are redacted; positions on open alerts are kept until they close, so a responder on the way can still find the person — a deliberate, documented exception | `multi_user_test` |
| Evidence without coordinates | evidence-log entries carry pseudonymous ids and zone ids, never coordinates, so erasure never conflicts with an append-only log | `multi_user_test` ("no coordinates") |
| Honest demonstration data | every scenario person is created with source "scenario", named "SIM · …" and badged SIM; demo responders are named "Demo …" and badged DEMO; production mode creates neither and refuses scenarios | `tests/app/scenario_test.cpp`, `tools/api_e2e.py` |

## 3. Authentication and sessions

| Control | Detail |
|---|---|
| Password storage | PBKDF2-HMAC-SHA256, 16-byte random salt, 120,000 iterations, self-describing record `pbkdf2-sha256$<it>$<salt>$<hash>`; verified against RFC 7914 and RFC 4231 vectors (`tests/app/crypto_test.cpp`). Argon2id would be the production choice; PBKDF2 is what SP 800-132 still permits and is small enough to implement and verify here. |
| Timing | constant-time comparison; a login for an unknown username still runs a key derivation, so response time does not reveal which usernames exist. |
| Session tokens | 256 bits from `/dev/urandom` (fail closed if unavailable); the server stores only SHA-256(token). |
| Cookie | `HttpOnly; SameSite=Strict; Path=/; Max-Age=28800`; `Secure` when `secure_cookies` is set behind TLS. |
| Expiry | 30 minutes idle, 8 hours absolute; deactivating an account ends its sessions on the next request; logout deletes the record. |
| Throttling | five failed logins for a username or a client address lock it for 60 s. |
| CSRF | every POST/PATCH/DELETE must carry `X-SafeTrail-Request: 1`. A cross-site form cannot set it, and a cross-site script cannot send it without a CORS preflight this server never approves. SameSite=Strict is a second layer. |

## 4. Input validation and parsing

- **HTTP:** request line and header grammar enforced; header block ≤ 16 KB, ≤ 64 headers, body ≤ 4 MB checked from Content-Length *before* buffering; chunked encoding refused (501); duplicate Content-Length refused (request-smuggling shape); 15 s idle timeout; ≤ 256 connections (`tests/net/http_parse_test.cpp`).
- **JSON:** the project's parser bounds nesting depth (64), rejects trailing data, raw control characters and invalid escapes ([../../include/safetrail/util/json.hpp](../../include/safetrail/util/json.hpp)).
- **GeoJSON:** every number range-checked before conversion (out-of-range conversion is undefined behaviour in C++); coordinates finite and on the lat/lon domain; polygons validated (self-intersection via the sweep line, holes inside the outer ring); vertex and zone-count caps; **all-or-nothing import** — one bad feature adds nothing.
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

## 7. Deployment posture

The server binds to 127.0.0.1 by default and prints a warning if told to bind
elsewhere: it has **no TLS**. Exposing it beyond the machine needs a
TLS-terminating reverse proxy and `secure_cookies`. The public GitHub Pages and
Vercel deployments serve only the static dashboard (a replay with no accounts
or live data), not this server.

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
5. Rate limiting covers login only; an authenticated client could still issue many heavy lab requests (each is bounded, but there is no per-user quota).
6. The audit log records reads made through the API; anyone with shell access to the server process is outside its scope.
7. This review was made by the developer. No external penetration test has been performed.
