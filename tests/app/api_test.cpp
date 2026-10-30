// The HTTP API in process (no sockets): authentication, sessions, CSRF, login
// throttling, the role matrix, static-file confinement, and that a responder
// sees a tourist's location only while assigned to that tourist's incident.
#include "safetrail/app/api.hpp"
#include "safetrail/app/world.hpp"
#include "safetrail/util/json.hpp"
#include "test_harness.hpp"

using namespace safetrail;

namespace {

struct Rig {
  std::unique_ptr<app::Platform> world;
  int64_t wall = 1'000'000;
  std::unique_ptr<app::Api> api;
  Rig() {
    app::WorldOptions o;
    o.roads_path = "";                       // synthetic grid: fast
    o.config.pbkdf2_iterations = 1000;
    o.staff = {{"admin", "Admin", "admin-password-x", app::Role::Admin},
               {"operator", "Op", "operator-password", app::Role::Operator},
               {"responder", "Medic", "responder-password", app::Role::Responder},
               {"tourist", "Tess", "tourist-password", app::Role::Tourist}};
    std::string err;
    world = app::build_world(o, &err);
    app::ApiOptions ao;
    ao.web_root = "web/app";
    ao.wall_ms = [this] { return wall; };
    api = std::make_unique<app::Api>(ao, &world, nullptr);
  }
  net::Response call(const std::string& method, const std::string& target, const std::string& body = "",
                     const std::string& token = "", bool csrf = true, const std::string& ip = "10.0.0.1") {
    net::Request r;
    r.method = method;
    r.target = target;
    const size_t q = target.find('?');
    r.path = target.substr(0, q);
    r.query = q == std::string::npos ? "" : target.substr(q + 1);
    r.body = body;
    r.remote = ip;
    if (!token.empty()) r.headers.emplace_back("cookie", "st_session=" + token);
    if (csrf && method != "GET") r.headers.emplace_back("x-safetrail-request", "1");
    return api->handle(r);
  }
  std::string login(const std::string& u, const std::string& p) {
    const auto r = call("POST", "/api/login", "{\"username\":\"" + u + "\",\"password\":\"" + p + "\"}");
    for (const auto& h : r.headers)
      if (h.first == "Set-Cookie") {
        const size_t a = h.second.find('=') + 1;
        return h.second.substr(a, h.second.find(';') - a);
      }
    return {};
  }
  app::UserId id(const std::string& u) const { return world->user_by_name(u)->id; }
};

bool has_header(const net::Response& r, const std::string& name, const std::string& contains) {
  for (const auto& h : r.headers)
    if (h.first == name && h.second.find(contains) != std::string::npos) return true;
  return false;
}

}  // namespace

int main() {
  Rig g;
  // ── authentication ────────────────────────────────────────────────────────
  t::ok(g.call("GET", "/api/live").status == 401, "the API requires a session");
  t::ok(g.call("POST", "/api/login", "{\"username\":\"operator\",\"password\":\"x\"}", "", false).status == 403,
        "a POST without the CSRF header is refused, even login");
  t::ok(g.login("operator", "wrong-password").empty(), "wrong password: no session");
  const std::string op = g.login("operator", "operator-password");
  t::ok(op.size() == 64, "a 256-bit token");
  t::ok(g.call("GET", "/api/live", "", op).status == 200, "the session works");
  {
    const auto r = g.call("POST", "/api/login", "{\"username\":\"admin\",\"password\":\"admin-password-x\"}");
    t::ok(has_header(r, "Set-Cookie", "HttpOnly") && has_header(r, "Set-Cookie", "SameSite=Strict"),
          "cookie is HttpOnly and SameSite=Strict");
    t::ok(has_header(r, "Content-Security-Policy", "script-src 'self'") && has_header(r, "X-Frame-Options", "DENY"),
          "security headers present");
  }
  // Throttling: five failures lock the account name for a minute.
  for (int i = 0; i < 5; ++i) g.call("POST", "/api/login", "{\"username\":\"tourist\",\"password\":\"nope\"}", "", true, "10.9.9.9");
  t::ok(g.call("POST", "/api/login", "{\"username\":\"tourist\",\"password\":\"tourist-password\"}", "", true, "10.1.1.1").status == 429,
        "after five failures even the right password waits");
  g.wall += 61'000;
  const std::string tou = g.login("tourist", "tourist-password");
  t::ok(!tou.empty(), "the lock expires after a minute");
  // Idle expiry.
  g.wall += 31 * 60'000;
  t::ok(g.call("GET", "/api/live", "", op).status == 401, "a session idle for 31 minutes has expired");
  const std::string op2 = g.login("operator", "operator-password");
  const std::string tou2 = g.login("tourist", "tourist-password");
  const std::string adm = g.login("admin", "admin-password-x");
  const std::string rsp = g.login("responder", "responder-password");
  t::ok(g.call("POST", "/api/logout", "", op2).status == 200 && g.call("GET", "/api/live", "", op2).status == 401,
        "logout ends the session");
  const std::string op3 = g.login("operator", "operator-password");

  // ── role matrix ───────────────────────────────────────────────────────────
  const std::string zone = "{\"name\":\"Test\",\"kind\":\"restricted\",\"severity\":5,\"ring\":[[25.580,91.880],[25.580,91.882],[25.582,91.882],[25.582,91.880]]}";
  t::ok(g.call("POST", "/api/zones", zone, op3).status == 403, "an operator cannot create zones");
  t::ok(g.call("POST", "/api/zones", zone, tou2).status == 403, "nor can a tourist");
  t::ok(g.call("POST", "/api/zones", zone, adm).status == 200, "an administrator can");
  t::ok(g.call("POST", "/api/zones", "{\"name\":\"Bow\",\"kind\":\"caution\",\"severity\":3,\"ring\":[[0,0],[1,1],[0,1],[1,0]]}", adm).status == 400,
        "a self-intersecting polygon is rejected");
  t::ok(g.call("POST", "/api/zones/import", "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{\"name\":\"ok\"},"
                "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[91.9,25.5],[91.91,25.5],[91.91,25.51],[91.9,25.51],[91.9,25.5]]]}},"
                "{\"type\":\"Feature\",\"properties\":{\"name\":\"bad\",\"severity\":9},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[91.9,25.5],[91.91,25.5],[91.91,25.51]]]}}]}",
                adm).status == 400, "an import with one bad feature adds nothing");
  t::ok(g.call("GET", "/api/users", "", tou2).status == 200, "a tourist may list users...");
  {
    util::Json j; std::string e;
    util::Json::parse(g.call("GET", "/api/users", "", tou2).body, j, &e);
    t::ok(j.find("users")->arr.size() == 1, "...but sees only themself");
  }
  t::ok(g.call("GET", "/api/audit", "", op3).status == 403, "only administrators read the audit log");
  t::ok(g.call("GET", "/api/audit", "", adm).status == 200, "administrators can");
  t::ok(g.call("POST", "/api/users", "{\"username\":\"newbie\",\"display_name\":\"N\",\"role\":\"tourist\",\"password\":\"long-enough-pw\"}", op3).status == 403,
        "operators cannot register users");
  t::ok(g.call("POST", "/api/users", "{\"username\":\"newbie\",\"display_name\":\"N\",\"role\":\"tourist\",\"password\":\"short\"}", adm).status == 400,
        "a short password is refused");
  t::ok(g.call("PATCH", "/api/settings", "{\"trail_retention_ms\":1}", adm).status == 400, "retention below a minute is refused");

  // ── consent is the tourist's alone ────────────────────────────────────────
  const auto tid = g.id("tourist");
  const std::string tp = "/api/users/" + std::to_string(tid);
  t::ok(g.call("POST", tp + "/consent", "{\"consent\":true}", op3).status == 403, "an operator cannot consent for a tourist");
  t::ok(g.call("POST", tp + "/consent", "{\"consent\":true}", tou2).status == 200, "the tourist can");
  t::ok(g.call("POST", tp + "/sharing", "{\"active\":true}", tou2).status == 200, "and start sharing");
  t::ok(g.call("POST", tp + "/fix", "{\"lat\":25.581,\"lon\":91.881,\"accuracy_m\":5}", op3).status == 403,
        "an operator cannot forge a tourist's fix");
  for (int s = 1; s <= 15; ++s) {
    g.world->tick(s * 1000);
    g.call("POST", tp + "/fix", "{\"lat\":25.581,\"lon\":91.881,\"accuracy_m\":5,\"t_ms\":" + std::to_string(s * 1000) + "}", tou2);
  }
  t::ok(g.world->users_inside(0) + g.world->users_inside(ZoneId(g.world->zones().size() - 2)) >= 1 ||
            !g.world->alerts().empty(), "the tourist's fixes reached the engine");

  // ── responder sees only their assignment ──────────────────────────────────
  t::ok(g.call("GET", tp + "/trail", "", rsp).status == 403, "a responder cannot read an unassigned tourist's trail");
  IncidentId inc = kNoId;
  for (const auto& c : g.world->cases()) if (c.id != kNoId) inc = c.id;
  t::ok(inc != kNoId, "the breach opened an incident");
  const auto unit = g.world->user(g.id("responder"))->unit;
  t::ok(g.call("POST", "/api/incidents/" + std::to_string(inc) + "/assign", "{\"responder\":" + std::to_string(unit) + "}", op3).status == 200,
        "operator assigns the responder's unit");
  t::ok(g.call("GET", tp + "/trail", "", rsp).status == 200, "now the responder may read it");
  t::ok(g.call("POST", "/api/incidents/" + std::to_string(inc) + "/resolve", "{\"note\":\"ok\"}", rsp).status == 200,
        "the assigned responder can resolve it");
  t::ok(g.call("GET", tp + "/trail", "", rsp).status == 403, "and once resolved, access ends");
  bool audited = false;
  for (const auto& a : g.world->access_log())
    if (a.actor == g.id("responder") && a.what == "trail" && a.allowed) audited = true;
  t::ok(audited, "the responder's read is in the audit log");

  // ── scenarios are refused without a runner; static files are confined ────
  t::ok(g.call("POST", "/api/scenario/start", "{}", op3).status == 409, "no scenario runner: refused");
  t::ok(g.call("GET", "/../var/demo-credentials.txt").status == 404, "path traversal refused");
  t::ok(g.call("GET", "/js/..%2f..%2fMakefile").status == 404, "encoded traversal refused");
  t::ok(g.call("GET", "/").status == 200, "the app itself is served");
  t::ok(g.call("GET", "/api/bench/..%2fMakefile", "", op3).status == 404, "bench files are confined too");
  t::ok(g.call("POST", "/api/lab/spatial", "{\"n\":999999}", tou2).status == 400, "lab inputs are bounded");
  t::ok(g.call("POST", "/api/lab/graph", "{\"rows\":5,\"cols\":5}", tou2).status == 200, "lab runs for any signed-in user");
  return t::report("app/api");
}
