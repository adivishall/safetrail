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
//   * login throttling: five failures for a username or a client address lock
//     it for 60 s;
//   * roles: every endpoint checks the caller's role, and the platform checks
//     again (an Outcome of 403) -- two independent gates;
//   * location reads by anyone other than the subject are written to the
//     access audit log;
//   * responses carry a strict Content-Security-Policy, nosniff, frame denial
//     and no-referrer; API responses are never cached.
//
#include <functional>
#include <memory>
#include <string>

#include "safetrail/app/platform.hpp"
#include "safetrail/app/scenario.hpp"
#include "safetrail/ds/hash_table.hpp"
#include "safetrail/net/http.hpp"

namespace safetrail::app {

struct ApiOptions {
  std::string web_root = "web/app";
  std::string bench_dir = "bench/results";
  bool demo = true;                     // scenarios and fixtures allowed
  bool secure_cookies = false;          // set when served over HTTPS
  int64_t idle_timeout_ms = 30 * 60'000;
  int64_t absolute_timeout_ms = 8 * 3600'000;
  // Wall clock for session expiry and throttling; injectable for tests.
  std::function<int64_t()> wall_ms;
};

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
    int64_t locked_until = 0;
  };

  ApiOptions opt_;
  std::unique_ptr<Platform>* world_;
  ScenarioRunner* runner_;
  ds::HashMap<std::string, Session> sessions_;   // key: hex SHA-256 of the token
  ds::HashMap<std::string, Throttle> throttle_;
  ds::HashMap<std::string, int64_t> live_audit_;  // "actor:subject" -> last audited live view

  Platform& pf() { return **world_; }
  int64_t wall() const;
  bool session_for(const net::Request& req, Principal* out, std::string* key);
  net::Response route(const net::Request& req);
  net::Response serve_static(const net::Request& req);
};

}  // namespace safetrail::app
