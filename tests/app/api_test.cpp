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

  // ── two tourists in one incident: each sees only their own part ───────────
  {
    Rig h;
    const std::string ad = h.login("admin", "admin-password-x");
    t::ok(h.call("POST", "/api/users", "{\"username\":\"tourist2\",\"display_name\":\"Uma\",\"role\":\"tourist\",\"password\":\"uma-password-1\"}", ad).status == 200,
          "admin registers a second tourist");
    t::ok(h.call("POST", "/api/zones", zone, ad).status == 200, "a zone both will walk into");
    const std::string ta = h.login("tourist", "tourist-password"), tb = h.login("tourist2", "uma-password-1");
    const std::string opr = h.login("operator", "operator-password"), med = h.login("responder", "responder-password");
    const app::UserId ua = h.id("tourist"), ub = h.id("tourist2");
    const std::string pa = "/api/users/" + std::to_string(ua), pb = "/api/users/" + std::to_string(ub);
    for (const auto& [p, tok] : {std::pair{pa, ta}, std::pair{pb, tb}}) {
      h.call("POST", p + "/consent", "{\"consent\":true}", tok);
      h.call("POST", p + "/sharing", "{\"active\":true}", tok);
    }
    for (int s = 1; s <= 15; ++s) {
      h.world->tick(s * 1000);
      const std::string ts = std::to_string(s * 1000);
      h.call("POST", pa + "/fix", "{\"lat\":25.5810,\"lon\":91.8810,\"accuracy_m\":5,\"t_ms\":" + ts + "}", ta);
      h.call("POST", pb + "/fix", "{\"lat\":25.5813,\"lon\":91.8813,\"accuracy_m\":5,\"t_ms\":" + ts + "}", tb);
    }
    IncidentId shared = kNoId;
    for (const auto& c : h.world->cases()) {
      const auto* ic = c.id == kNoId ? nullptr : h.world->correlator().get(c.id);
      if (ic && ic->affected.size() == 2) shared = c.id;
    }
    t::ok(shared != kNoId, "both breaches fall into one incident");
    const std::string ip = "/api/incidents/" + std::to_string(shared);

    auto parse = [](const net::Response& r) { util::Json j; std::string e; util::Json::parse(r.body, j, &e); return j; };
    // Everything about the incident a tourist can read must be about them alone.
    auto only_mine = [&](const util::Json& ij, app::UserId me, const std::string& other) {
      const util::Json* aff = ij.find("affected");
      const util::Json* names = ij.find("affected_names");
      const util::Json* centroid = ij.find("centroid");
      const util::Json* route = ij.find("route");
      bool ok = aff && aff->arr.size() == 1 && aff->arr[0].number_or(-1) == double(me) && names && names->arr.size() == 1 &&
                centroid && centroid->type == util::Json::Type::Null && route && route->type == util::Json::Type::Null &&
                ij.find("people")->number_or(0) == 1;
      for (const auto& n : names ? names->arr : std::vector<util::Json>{}) if (n.string_or("") == other) ok = false;
      return ok;
    };
    {
      const auto r = h.call("GET", ip, "", ta);
      t::ok(r.status == 200, "a tourist may open an incident that concerns them");
      const util::Json j = parse(r);
      bool alerts_mine = true, timeline_mine = true;
      for (const auto& a : j.find("alerts")->arr) if (a.find("user")->number_or(-1) != double(ua)) alerts_mine = false;
      for (const auto& e : j.find("timeline")->arr) {
        const util::Json* u = e.find("user");
        if (!u || u->number_or(-1) == double(ub)) timeline_mine = false;
      }
      t::ok(only_mine(*j.find("incident"), ua, "Uma"), "...and sees only themself in it: no other name, no centroid, no route");
      t::ok(alerts_mine, "...only their own alerts, so no other person's position");
      t::ok(timeline_mine, "...and no timeline entry about the other person");
    }
    {
      const util::Json lv = parse(h.call("GET", "/api/live", "", tb));
      bool found = false, ok = true;
      for (const auto& i : lv.find("incidents")->arr)
        if (i.find("id")->number_or(-1) == double(shared)) { found = true; ok = ok && only_mine(i, ub, "Tess"); }
      t::ok(found && ok, "the live map shows the other tourist the same incident, also only their part");
      const util::Json lst = parse(h.call("GET", "/api/incidents", "", tb));
      ok = !lst.find("incidents")->arr.empty();
      for (const auto& i : lst.find("incidents")->arr) ok = ok && only_mine(i, ub, "Tess");
      t::ok(ok, "...and so does the incident list");
    }
    {
      const util::Json j = parse(h.call("GET", ip, "", opr));
      t::ok(j.find("incident")->find("affected")->arr.size() == 2 && j.find("alerts")->arr.size() >= 2,
            "an operator sees the whole incident");
    }
    // Reads of someone else's data by another tourist.
    t::ok(h.call("GET", pb + "/trail", "", ta).status == 403, "a tourist cannot read another tourist's trail");
    t::ok(h.call("GET", pb, "", ta).status == 403, "...or their profile");
    t::ok(h.call("DELETE", pb + "/history", "", ta).status == 403, "...or erase their history");
    t::ok(h.call("POST", pb + "/fix", "{\"lat\":25.58,\"lon\":91.88,\"accuracy_m\":5}", ta).status == 403,
          "...or post a fix as them");
    t::ok(h.call("POST", pb + "/consent", "{\"consent\":false}", ta).status == 403, "...or change their consent");
    {
      const util::Json us = parse(h.call("GET", "/api/users", "", ta));
      t::ok(us.find("users")->arr.size() == 1, "the user list shows a tourist only themself");
      const util::Json al = parse(h.call("GET", "/api/alerts", "", ta));
      bool mine = !al.find("alerts")->arr.empty();
      for (const auto& a : al.find("alerts")->arr) if (a.find("user")->number_or(-1) != double(ua)) mine = false;
      t::ok(mine, "the alert list shows a tourist only their own alerts");
      const util::Json ev = parse(h.call("GET", "/api/events?user=" + std::to_string(ub), "", ta));
      bool own = true;
      for (const auto& e : ev.find("events")->arr) if (e.find("user")->number_or(-1) == double(ub)) own = false;
      t::ok(own, "asking the event log for another user's events returns only your own");
    }
    // Staff-only and responder-only data.
    t::ok(h.call("GET", "/api/zones/0", "", ta).status == 403, "zone detail (with its incidents) is for staff");
    t::ok(h.call("GET", "/api/responders", "", ta).status == 403, "responder positions are not shown to tourists");
    t::ok(parse(h.call("GET", "/api/live", "", ta)).find("responders")->arr.empty(), "...not on the live map either");
    t::ok(h.call("GET", "/api/responders", "", med).status == 200, "a responder sees the other units");
    t::ok(h.call("GET", "/api/history?t=5000", "", ta).status == 403 && h.call("GET", "/api/history?t=5000", "", med).status == 403,
          "the investigation view is for staff only");
    t::ok(h.call("GET", "/api/evidence", "", med).status == 403 && h.call("GET", "/api/stats", "", ta).status == 403,
          "so are evidence and statistics");
    t::ok(h.call("POST", ip + "/ack", "{}", med).status == 403, "a responder cannot acknowledge an incident");
    t::ok(h.call("POST", ip + "/assign", "{}", med).status == 403, "...or assign one");
    t::ok(h.call("POST", ip + "/resolve", "{\"note\":\"x\"}", med).status == 403, "...or resolve one not assigned to them");
    t::ok(h.call("POST", "/api/zones", zone, med).status == 403, "...or create zones");
    t::ok(h.call("POST", "/api/users", "{\"username\":\"x-user\",\"display_name\":\"X\",\"role\":\"admin\",\"password\":\"long-enough-pw\"}", opr).status == 403,
          "an operator cannot create an administrator");
    t::ok(h.call("PATCH", "/api/settings", "{\"auto_dispatch\":true}", opr).status == 403, "...or change settings");
    t::ok(h.call("PATCH", pa, "{\"active\":false}", opr).status == 403, "...or deactivate a user");

    // Every look at someone else's location is audited: the list, the alert
    // feed and the incident detail as well as the profile, trail and map.
    auto was_audited = [&](app::UserId actor, app::UserId subject, const std::string& what) {
      for (const auto& a : h.world->access_log())
        if (a.actor == actor && a.subject == subject && a.what == what && a.allowed) return true;
      return false;
    };
    const app::UserId uo = h.id("operator");
    h.call("GET", "/api/users", "", opr);
    h.call("GET", "/api/alerts", "", opr);
    t::ok(was_audited(uo, ua, "list") && was_audited(uo, ub, "list"), "the user list is audited per person shown");
    t::ok(was_audited(uo, ua, "alerts") && was_audited(uo, ub, "alerts"), "the alert feed (which carries positions) is audited");
    t::ok(was_audited(uo, ua, "incident") && was_audited(uo, ub, "incident"), "the incident detail is audited");
    const size_t before = h.world->access_log().size();
    h.call("GET", "/api/alerts", "", opr);
    h.call("GET", "/api/users", "", opr);
    t::ok(h.world->access_log().size() == before, "polling again within ten minutes is not re-recorded");

    // ── numbers from the client never reach an undefined conversion ─────────
    // Each of these once cast a client double straight to an integer type. JSON
    // 1e999 parses to infinity, and NaN, infinity or an out-of-range value has
    // no defined conversion -- UBSan's float-cast-overflow aborts on it.
    const std::string ring = "\"ring\":[[25.590,91.890],[25.590,91.892],[25.592,91.892],[25.592,91.890]]";
    auto zone_with = [&](const std::string& field) {
      return "{\"name\":\"Big\",\"kind\":\"caution\",\"severity\":3," + field + "," + ring + "}";
    };
    t::ok(h.call("POST", "/api/zones", "{\"name\":\"Big\",\"kind\":\"caution\",\"severity\":1e999," + ring + "}", ad).status == 400,
          "severity 1e999 is refused");
    t::ok(h.call("POST", "/api/zones", "{\"name\":\"Big\",\"kind\":\"caution\",\"severity\":\"5\"," + ring + "}", ad).status == 400,
          "a severity that is not a number is refused, not defaulted");
    t::ok(h.call("POST", "/api/zones", zone_with("\"max_dwell_s\":-1e300"), ad).status == 400, "dwell -1e300 is refused");
    t::ok(h.call("POST", "/api/zones", zone_with("\"active_from_ms\":1e300"), ad).status == 400, "validity start 1e300 is refused");
    t::ok(h.call("POST", "/api/zones", zone_with("\"active_to_ms\":-1e999"), ad).status == 400, "validity end -1e999 is refused");
    t::ok(h.call("POST", "/api/zones/0/validity", "{\"from_ms\":1e300,\"to_ms\":null}", ad).status == 400,
          "a validity window starting at 1e300 is refused");
    t::ok(h.call("POST", pa + "/fix", "{\"lat\":25.58,\"lon\":91.88,\"accuracy_m\":5,\"t_ms\":1e300}", ta).status == 400,
          "a fix timestamped 1e300 is refused");
    t::ok(h.call("POST", ip + "/assign", "{\"responder\":-1}", opr).status == 400, "responder -1 is refused, not wrapped");
    t::ok(h.call("POST", ip + "/assign", "{\"responder\":1e999}", opr).status == 400, "responder 1e999 is refused");
    const auto grp = h.call("POST", "/api/groups", "{\"name\":\"Trek\"}", opr);
    t::ok(grp.status == 200, "a group");
    const std::string gp = "/api/groups/" + std::to_string(int64_t(parse(grp).find("id")->number_or(0)));
    t::ok(h.call("POST", gp + "/members", "{\"user\":-1}", opr).status == 400, "group member -1 is refused");
    t::ok(h.call("POST", gp + "/members", "{}", opr).status == 400, "a member request without a user is refused");
    t::ok(h.call("PATCH", gp, "{\"confirm_s\":1e300}", opr).status == 400, "confirmation time 1e300 is refused");
    t::ok(h.call("GET", pa + "/trail?from=1e999", "", ta).status == 400, "trail from=1e999 is refused");
    t::ok(h.call("GET", pa + "/trail?to=99999999999999999999999", "", ta).status == 400, "trail to=10^23 is refused");
    t::ok(h.call("GET", pa + "/trail?from=junk", "", ta).status == 400, "trail from=junk is refused, not read as 0");
    t::ok(h.call("GET", pa + "/trail?from=0&to=20000.5", "", ta).status == 200, "an ordinary window is answered");
    t::ok(h.call("GET", "/api/history?t=1e999", "", opr).status == 400, "history t=1e999 is refused");
    t::ok(h.call("GET", "/api/history?t=12300.000000000002", "", opr).status == 200, "a fractional time from a slider is accepted");
    t::ok(h.call("GET", "/api/events?limit=99999999999999999999999", "", opr).status == 400, "events limit=10^23 is refused");
    t::ok(h.call("GET", "/api/events?limit=5", "", opr).status == 200, "an ordinary limit is answered");
    t::ok(h.call("GET", "/api/roads?min_lat=1e999&min_lon=0&max_lat=1&max_lon=1", "", opr).status == 400, "roads min_lat=1e999 is refused");
    t::ok(h.call("GET", "/api/roads?min_lat=25.5&min_lon=91.8&max_lat=25.6&max_lon=91.9", "", opr).status == 200, "an ordinary road tile is answered");

    // A profile update applies every field it carries, not just the first.
    t::ok(h.call("PATCH", pa, "{\"display_name\":\"Tess B\",\"password\":\"tess-new-password\"}", ta).status == 200,
          "a tourist updates their name and password together");
    t::ok(h.world->user(ua)->display_name == "Tess B" && !h.login("tourist", "tess-new-password").empty(),
          "...and both changes took effect");
  }
  return t::report("app/api");
}
