#pragma once
//
// The HTTP API: authentication, sessions, role checks, and the JSON endpoints
// the operations web app (web/app/) is built on.
//
// Security model, in one place (docs/final/SECURITY_AND_PRIVACY.md has the why):
//   * passwords: PBKDF2-HMAC-SHA256 (app/crypto.hpp);
//   * sessions: 256-bit random tokens in an HttpOnly, SameSite=Strict cookie;
//     the server stores only SHA-256(token), so a memory dump of the session
//     table holds nothing a browser could present; 30-minute idle and 8-hour
//     absolute expiry; logout deletes the record;
//   * CSRF: every state-changing request must carry the header
//     X-SafeTrail-Request: 1, which a cross-site form cannot send and a
//     cross-site script cannot send without a CORS preflight this server never
//     approves -- on top of SameSite=Strict;
//   * login throttling, per client address (10 failures: 5 min), per username
//     and address (5 failures: 60 s), and per username across all addresses
//     (25 failures: 60 s) -- so neither a shared proxy address nor one attacker
//     can lock everyone out of an account;
//   * behind a reverse proxy, the client address comes from one configured
//     header (CF-Connecting-IP on Render), and only when the TCP peer is a
//     trusted proxy; request rate limits per session and per address;
//   * roles: every endpoint checks the caller's role, and the platform checks
//     again (an Outcome of 403) -- two independent gates;
//   * location reads by anyone other than the subject are written to the
//     access audit log -- profile, trail, history, and the views that are
//     polled (live map, user list, alert feed, incident detail) once per
//     viewer, subject and view every ten minutes;
//   * a tourist sees only their own part of an incident that concerns them,
//     and no responder positions;
//   * responses carry a strict Content-Security-Policy, nosniff, frame denial
//     and no-referrer; API responses are never cached.
//
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "safetrail/app/platform.hpp"
#include "safetrail/app/scenario.hpp"
#include "safetrail/ds/hash_table.hpp"
#include "safetrail/net/http.hpp"

namespace safetrail::app {

// Token buckets: `per_s` requests a second on average, `burst` at once.
struct RateLimits {
  bool enabled = false;
  double anon_per_s = 2, anon_burst = 20;            // /api/* with no session, per client address
  double session_per_s = 20, session_burst = 60;     // per signed-in session
  double address_per_s = 100, address_burst = 300;   // everything from one client address
  double lab_cost = 5;                               // a Lab experiment counts as this many requests
  // Sign-in attempts from everyone together. Each one is a full key derivation
  // on a single thread; on a 0.1-CPU host an unbounded stream of them would
  // starve every other request, the host's health check included.
  double login_per_s = 2, login_burst = 10;
};

struct ApiOptions {
  std::string web_root = "web/app";
  std::string bench_dir = "bench/results";
  bool demo = true;                     // scenarios and fixtures allowed
  bool secure_cookies = false;          // set when served over HTTPS; adds HSTS too
  bool public_demo = false;             // a public deployment: every page says simulated, resets
  // Behind a reverse proxy every TCP peer is the proxy. With this set (a
  // lower-case header name, e.g. "cf-connecting-ip"), the client address is
  // that header's value -- but only when the peer is in `trusted_proxies`, so
  // a request that did not come through the proxy cannot choose its address.
  std::string client_ip_header;
  std::vector<std::string> trusted_proxies = {"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16",
                                              "100.64.0.0/10", "127.0.0.0/8"};
  RateLimits limits;
  int64_t idle_timeout_ms = 30 * 60'000;
  int64_t absolute_timeout_ms = 8 * 3600'000;
  // Wall clock for session expiry and throttling; injectable for tests.
  std::function<int64_t()> wall_ms;
};

// IPv4 dotted quad, and "a.b.c.d/n" (no "/n" means /32). For the proxy list.
bool parse_ipv4(const std::string& s, uint32_t* out);
bool parse_cidr(const std::string& s, uint32_t* network, uint32_t* mask);

class Api {
 public:
  Api(ApiOptions opt, std::unique_ptr<Platform>* world, ScenarioRunner* runner);
  net::Response handle(const net::Request& req);

  // For tests: log in without HTTP and get the cookie value.
  std::string login_token(const std::string& username, const std::string& password);

 private:
  struct Session {
    UserId user = kNoId;
    int64_t created = 0, last_seen = 0;
  };
  struct Throttle {
    int failures = 0;
    int64_t locked_until = 0, last = 0;
  };
  struct Bucket {
    double tokens = 0;
    int64_t at = 0;
  };

  ApiOptions opt_;
  std::unique_ptr<Platform>* world_;
  ScenarioRunner* runner_;
  ds::HashMap<std::string, Session> sessions_;   // key: hex SHA-256 of the token
  ds::HashMap<std::string, Throttle> throttle_;
  ds::HashMap<std::string, int64_t> live_audit_;  // "actor:subject:view" -> last audited poll
  ds::HashMap<std::string, Bucket> buckets_;       // rate limits: "a:" address, "s:" session, "n:" anonymous
  std::vector<std::pair<uint32_t, uint32_t>> proxies_;   // trusted_proxies parsed: (network, mask)

  Platform& pf() { return **world_; }
  int64_t wall() const;
  bool session_for(const net::Request& req, Principal* out, std::string* key);
  // The client's address: the TCP peer, or the proxy's header (see ApiOptions).
  std::string client_of(const net::Request& req) const;
  // Take `cost` tokens from bucket `key`; false = over the limit.
  bool take(const std::string& key, double per_s, double burst, double cost, int64_t now);
  // Request rate limits; a 429 response when over, else status 0.
  net::Response limit(const net::Request& req, const std::string& client);
  // Audit a polled view of `subject`'s location at most once per ten minutes.
  void audit_view(const Principal& me, UserId subject, const char* view);
  net::Response route(const net::Request& req, const std::string& client);
  net::Response serve_static(const net::Request& req);
};

}  // namespace safetrail::app
