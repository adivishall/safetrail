#include "safetrail/app/api.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

#include "safetrail/app/crypto.hpp"
#include "safetrail/app/json_out.hpp"
#include "safetrail/app/lab.hpp"
#include "safetrail/util/json.hpp"

namespace safetrail::app {

namespace {

const char* kCookie = "st_session";
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

net::Response json(int status, std::string body) {
  net::Response r;
  r.status = status;
  r.body = std::move(body);
  return r;
}

net::Response error(int status, const std::string& msg) {
  return json(status, "{\"error\":" + util::Json::escape(msg) + "}");
}

net::Response from(const Outcome& o) {
  if (!o.ok()) return error(o.code, o.error);
  JsonOut w;
  w.obj().kb("ok", true).kid("id", o.id).end_obj();
  return json(200, w.take());
}

std::vector<std::string> segments(const std::string& path) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < path.size()) {
    while (i < path.size() && path[i] == '/') ++i;
    size_t j = i;
    while (j < path.size() && path[j] != '/') ++j;
    if (j > i) out.push_back(path.substr(i, j - i));
    i = j;
  }
  return out;
}

bool parse_id(const std::string& s, uint32_t* out) {
  if (s.empty() || s.size() > 9) return false;
  uint32_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + uint32_t(c - '0');
  }
  *out = v;
  return true;
}

bool body_json(const net::Request& req, util::Json* out, std::string* err) {
  if (req.body.empty()) { *out = util::Json{}; out->type = util::Json::Type::Object; return true; }
  if (!util::Json::parse(req.body, *out, err)) return false;
  if (out->type != util::Json::Type::Object) { *err = "body must be a JSON object"; return false; }
  return true;
}

double jnum(const util::Json& j, const char* k, double d) {
  const util::Json* v = j.find(k);
  return v ? v->number_or(d) : d;
}
std::string jstr(const util::Json& j, const char* k) {
  const util::Json* v = j.find(k);
  return v ? v->string_or("") : std::string();
}
bool jbool(const util::Json& j, const char* k, bool d) {
  const util::Json* v = j.find(k);
  return v ? v->bool_or(d) : d;
}
bool jhas(const util::Json& j, const char* k) { return j.find(k) != nullptr; }

// ── Numbers from the client, as integers ────────────────────────────────────
// A double that is NaN, infinite or outside an integer type's range has no
// defined conversion to that type in C++ (UBSan: float-cast-overflow), and
// JSON's 1e999 parses to infinity. So client numbers become integers only
// through these, which refuse what they cannot convert instead of casting it.
constexpr double kMaxExactInt = 9007199254740992.0;   // 2^53

bool to_int64(double x, int64_t* out) {
  if (!std::isfinite(x) || std::fabs(x) > kMaxExactInt) return false;
  *out = int64_t(x);
  return true;
}
// Field `k` as an integer: `d` when absent, false when present but not a
// number in range (a string "5" is refused, not silently defaulted).
bool jint(const util::Json& j, const char* k, int64_t d, int64_t* out) {
  const util::Json* v = j.find(k);
  if (!v) { *out = d; return true; }
  return v->type == util::Json::Type::Number && to_int64(v->num, out);
}
// Field `k` as an id: present, whole, and below kNoId.
bool jid(const util::Json& j, const char* k, uint32_t* out) {
  int64_t v = 0;
  if (!jhas(j, k) || !jint(j, k, 0, &v) || v < 0 || v >= int64_t(kNoId)) return false;
  *out = uint32_t(v);
  return true;
}
// Query parameter `k` as a number: `d` when absent or empty, false when it is
// not entirely a finite number (atoll read "junk" as 0 and 1e999 as 1).
bool qnum(const net::Request& req, const char* k, double d, double* out) {
  const std::string s = req.query_param(k);
  if (s.empty()) { *out = d; return true; }
  char* end = nullptr;
  const double x = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + s.size() || !std::isfinite(x)) return false;
  *out = x;
  return true;
}
bool qint(const net::Request& req, const char* k, int64_t d, int64_t* out) {
  double x = 0;
  return qnum(req, k, double(d), &x) && to_int64(x, out);
}

bool is_staff(const Principal& p) { return p.role == Role::Admin || p.role == Role::Operator; }

std::string content_type_for(const std::string& path) {
  auto ends = [&](const char* e) {
    const size_t n = std::char_traits<char>::length(e);
    return path.size() >= n && path.compare(path.size() - n, n, e) == 0;
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".js")) return "text/javascript; charset=utf-8";
  if (ends(".css")) return "text/css; charset=utf-8";
  if (ends(".svg")) return "image/svg+xml";
  if (ends(".png")) return "image/png";
  if (ends(".json")) return "application/json";
  if (ends(".csv")) return "text/csv; charset=utf-8";
  if (ends(".txt")) return "text/plain; charset=utf-8";
  if (ends(".ico")) return "image/x-icon";
  return "application/octet-stream";
}

bool read_file(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

const char* zone_kind_name(fence::ZoneKind k) { return fence::to_string(k); }

bool zone_kind_from(const std::string& s, fence::ZoneKind* k) {
  if (s == "restricted") { *k = fence::ZoneKind::Restricted; return true; }
  if (s == "caution") { *k = fence::ZoneKind::Caution; return true; }
  if (s == "advisory") { *k = fence::ZoneKind::Advisory; return true; }
  if (s == "safe") { *k = fence::ZoneKind::Safe; return true; }
  return false;
}

// ── Serialisers ─────────────────────────────────────────────────────────────

void ring_json(JsonOut& w, const geo::Ring& r) {
  w.arr();
  for (const auto& p : r) w.arr().num(p.lat).num(p.lon).end_arr();
  w.end_arr();
}

void time_or_null(JsonOut& w, const char* k, Timestamp t) {
  if (t == kForever || t == 0) w.key(k).null();
  else w.ki(k, t);
}

void alert_json(JsonOut& w, const Platform& pf, const AlertRecord& r, double priority) {
  const alert::Alert& a = r.a;
  w.obj().ku("id", a.id).kv("kind", alert::to_string(a.kind)).kv("state", alert::to_string(a.state));
  w.ku("severity", a.severity).ku("original_severity", r.original_severity).kb("escalated", r.escalated);
  w.kid("user", a.tourist).kid("zone", a.zone).kid("incident", a.incident).kid("group", r.group);
  const User* u = pf.user(a.tourist);
  w.kv("user_name", u ? u->display_name : std::string());
  const fence::Zone* z = pf.zones().get(a.zone);
  w.kv("zone_name", z ? z->name : std::string());
  if (r.redacted) w.key("position").null();
  else w.key("position").arr().num(a.position.lat).num(a.position.lon).end_arr();
  w.kn("accuracy_m", a.accuracy_m).ki("raised_ms", a.raised_ms).ki("recorded_ms", r.history.front().first);
  w.kn("eta_s", a.eta_s).kv("note", a.note).kb("late", r.late).kv("source", to_string(r.source));
  w.kn("priority", priority);
  w.key("history").arr();
  for (const auto& [t, s] : r.history) w.arr().integer(t).str(alert::to_string(s)).end_arr();
  w.end_arr().end_obj();
}

void route_json(JsonOut& w, const Route& r) {
  w.obj().kb("found", r.found).kn("distance_m", r.distance_m).ku("astar_expanded", r.astar_expanded)
      .ku("dijkstra_expanded", r.dijkstra_expanded);
  w.key("points").arr();
  for (const auto& p : r.points) w.arr().num(p.lat).num(p.lon).end_arr();
  w.end_arr().end_obj();
}

// `self`: the viewer is one of the people affected, not staff or the assigned
// responder, and sees only their own part of the incident -- not the others'
// names, alerts or positions. Nor the centroid, radius or route, because with
// two people the centroid and your own position give away the other's.
void incident_json(JsonOut& w, const Platform& pf, const IncidentCase& c, bool full, UserId self = kNoId) {
  const alert::Incident* inc = pf.correlator().get(c.id);
  if (!inc) return;
  const bool own = self != kNoId;
  auto shown = [&](AlertId a) { return a < pf.alerts().size() && (!own || pf.alerts()[a].a.tourist == self); };
  w.obj().ku("id", c.id).kv("state", to_string(c.state)).ku("people", own ? 1 : inc->people())
      .ku("severity", inc->max_severity);
  if (own) w.key("centroid").null().key("radius_m").null();
  else w.key("centroid").arr().num(inc->centroid.lat).num(inc->centroid.lon).end_arr().kn("radius_m", inc->radius_m);
  w.ki("opened_ms", inc->opened_ms).ki("updated_ms", inc->last_update_ms);
  w.kid("recommended", c.recommended).kn("recommended_m", c.recommended_m).kid("responder", c.responder);
  const auto* rec = c.recommended < pf.responders().size() ? &pf.responders()[c.recommended] : nullptr;
  w.kv("recommended_name", rec ? rec->name : std::string());
  const auto* asg = c.responder < pf.responders().size() ? &pf.responders()[c.responder] : nullptr;
  w.kv("responder_name", asg ? asg->name : std::string());
  time_or_null(w, "acknowledged_ms", c.acknowledged_ms);
  time_or_null(w, "dispatched_ms", c.dispatched_ms);
  time_or_null(w, "resolved_ms", c.resolved_ms);
  w.kv("resolution", c.resolution);
  w.key("affected").arr();
  for (auto u : inc->affected) if (!own || u == self) w.uint(u);
  w.end_arr();
  w.key("affected_names").arr();
  for (auto u : inc->affected)
    if (!own || u == self) { const User* usr = pf.user(u); w.str(usr ? usr->display_name : std::string()); }
  w.end_arr();
  w.key("alerts").arr();
  for (auto a : inc->alerts) if (shown(a)) w.uint(a);
  w.end_arr();
  bool late = false;
  for (auto a : inc->alerts) if (shown(a) && pf.alerts()[a].late) late = true;
  w.kb("late", late);
  w.key("route");
  if (c.responder != kNoId && !own) route_json(w, c.route); else w.null();
  if (full) {
    w.key("history").arr();
    for (const auto& [t, s] : c.history) w.arr().integer(t).str(to_string(s)).end_arr();
    w.end_arr();
  }
  w.end_obj();
}

// Who may see an incident, and how much of it: staff and the assigned
// responder all of it, a person it concerns only their own part.
enum class IncidentView { None, Own, Full };

IncidentView incident_view(const Platform& pf, const Principal& me, const IncidentCase& c) {
  if (is_staff(me)) return IncidentView::Full;
  const User* mu = pf.user(me.id);
  if (me.role == Role::Responder && mu && mu->unit != kNoId && c.responder == mu->unit) return IncidentView::Full;
  const auto* inc = pf.correlator().get(c.id);
  if (inc && std::find(inc->affected.begin(), inc->affected.end(), me.id) != inc->affected.end())
    return IncidentView::Own;
  return IncidentView::None;
}

void responder_json(JsonOut& w, const Platform& pf, const dispatch::Responder& r) {
  const ResponderInfo* info = pf.responder_info(r.id);
  w.obj().ku("id", r.id).kv("name", r.name).kv("type", info ? info->unit_type : std::string());
  w.key("position").arr().num(r.pos.lat).num(r.pos.lon).end_arr();
  w.kb("available", r.available).kid("assigned", info ? info->assigned : kNoId);
  w.kv("source", info ? to_string(info->source) : "api").kid("account", info ? info->account : kNoId);
  w.ki("node", r.node).ki("position_ms", info ? info->position_ms : 0);
  w.end_obj();
}

void zone_json(JsonOut& w, const Platform& pf, const fence::Zone& z, bool geometry) {
  w.obj().ku("id", z.id).kv("name", z.name).kv("kind", zone_kind_name(z.kind)).ku("severity", z.severity);
  w.kv("source", to_string(pf.zone_source(z.id))).kb("synthetic", z.synthetic);
  w.ki("max_dwell_s", z.max_dwell_ms / 1000);
  w.ki("active_from_ms", z.validity.from);
  if (z.validity.to == kForever) w.key("active_to_ms").null(); else w.ki("active_to_ms", z.validity.to);
  w.kb("active_now", z.validity.active_at(pf.now()));
  w.ku("users_inside", pf.users_inside(z.id)).ku("vertices", z.shape.vertex_count());
  w.kn("area_m2", std::fabs(z.shape.signed_area()));
  const geo::Bbox b = z.shape.bbox();
  w.key("bbox").arr().num(b.min_lat).num(b.min_lon).num(b.max_lat).num(b.max_lon).end_arr();
  if (geometry) {
    w.key("ring");
    ring_json(w, z.shape.outer());
    w.key("holes").arr();
    for (const auto& h : z.shape.holes()) ring_json(w, h);
    w.end_arr();
  }
  w.end_obj();
}


// The battery model the UI shows. Linear in fix rate, anchored on the 8-12 %
// per hour that continuous GPS is documented to cost (docs/GAP_ANALYSIS.md,
// gap 7): a 1 Hz device spends 10 %/hour, a device sending one fix a minute
// spends 1/60 of that. It ignores the radio, screen and CPU, and the UI says so.
constexpr double kPctPerHourAt1Hz = 10.0;

void write_user(JsonOut& w, const Platform& pf, const User& u, bool with_location, bool detail) {
  w.obj().ku("id", u.id).kv("username", u.username).kv("display_name", u.display_name);
  w.kv("role", to_string(u.role)).kb("active", u.active).kv("source", to_string(u.source));
  w.kb("simulated", u.source == Source::Scenario).ki("created_ms", u.created_ms);
  if (u.role == Role::Responder) w.kid("unit", u.unit);
  if (u.role != Role::Tourist) { w.end_obj(); return; }

  w.kb("consent", u.consent).kb("sharing", u.sharing).ku("sessions", u.sessions);
  time_or_null(w, "session_started_ms", u.session_started_ms);
  time_or_null(w, "session_ended_ms", u.session_ended_ms);
  w.kid("group", u.group);
  const GroupRecord* g = pf.group_rec(u.group);
  w.kv("group_name", g ? g->name : std::string());
  w.kb("battery_aware", u.battery_aware).kv("status", pf.status_label(u.id));
  size_t open_alerts = 0;
  uint8_t worst = 0;
  std::vector<IncidentId> incs;
  for (AlertId aid : pf.alerts_of(u.id)) {
    const AlertRecord& r = pf.alerts()[aid];
    const auto s = r.a.state;
    if (s != alert::AlertState::Open && s != alert::AlertState::Acknowledged && s != alert::AlertState::Dispatched)
      continue;
    ++open_alerts;
    worst = std::max(worst, r.a.severity);
    if (r.a.incident != kNoId && std::find(incs.begin(), incs.end(), r.a.incident) == incs.end())
      incs.push_back(r.a.incident);
  }
  w.ku("open_alerts", open_alerts).ku("worst_severity", worst);
  w.key("incidents").arr();
  for (auto i : incs) w.uint(i);
  w.end_arr();

  const Tracked* tr = pf.tracked(u.id);
  if (!with_location || !tr) { w.key("location").null().kb("location_visible", false); w.end_obj(); return; }
  w.kb("location_visible", true);
  if (tr->has_fix && u.sharing) {
    const auto& f = tr->tourist.last_fix;
    w.key("location").obj().kn("lat", f.pos.lat).kn("lon", f.pos.lon).kn("accuracy_m", f.accuracy_m)
        .ki("t_ms", tr->last_fix_ms).ki("age_ms", pf.now() - tr->last_fix_ms)
        .kn("speed_mps", tr->tourist.speed_mps()).kn("heading_deg", tr->tourist.heading_deg()).end_obj();
  } else {
    w.key("location").null();
  }
  if (!detail) { w.end_obj(); return; }

  w.key("ingest").obj().ku("accepted", tr->accepted).ku("duplicates", tr->duplicates)
      .ku("out_of_order", tr->out_of_order).ku("invalid", tr->invalid).ku("jumps", tr->jumps).end_obj();
  w.kv("anomaly", track::to_string(tr->anomaly));
  w.key("zones").arr();
  for (const auto& zs : pf.zone_status(u.id, 4)) {
    const fence::Zone* z = pf.zones().get(zs.zone);
    if (!z) continue;
    w.obj().ku("zone", zs.zone).kv("name", z->name).kv("kind", zone_kind_name(z->kind)).ku("severity", z->severity)
        .kv("state", geo::to_string(zs.state)).kn("distance_m", zs.distance_m).end_obj();
  }
  w.end_arr();
  // Battery: what this session actually sent, against a fixed 1 Hz policy.
  const Timestamp session_ms = tr->session_time_ms + (u.sharing ? pf.now() - u.session_started_ms : 0);
  const double hours = double(session_ms) / 3600'000.0;
  const double fixed = double(session_ms) / 1000.0;
  const double rate = session_ms > 0 ? double(tr->fixes_sent) / (double(session_ms) / 1000.0) : 0.0;
  double nearest = -1;
  for (const auto& zs : pf.zone_status(u.id, 1)) nearest = nearest < 0 ? zs.distance_m : std::min(nearest, zs.distance_m);
  w.key("power").obj().kb("battery_aware", u.battery_aware)
      .ki("interval_ms", tr->tourist.sampler.current_interval_ms())
      .kn("nearest_zone_m", nearest).ku("fixes_sent", tr->fixes_sent).kn("fixes_at_1hz", fixed)
      .kn("session_hours", hours).kn("pct_per_hour", std::min(rate, 1.0) * kPctPerHourAt1Hz)
      .kn("pct_per_hour_fixed", kPctPerHourAt1Hz)
      .kv("model", "linear in fix rate; 1 fix/s = 10 %/h (midpoint of the 8-12 %/h cited for continuous GPS); radio, screen and CPU not modelled")
      .end_obj();
  w.end_obj();
}

std::string hex_sha(const std::string& s) {
  const auto h = evidence::sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  return hex(h.data(), h.size());
}

}  // namespace

// ─── Api ────────────────────────────────────────────────────────────────────

Api::Api(ApiOptions opt, std::unique_ptr<Platform>* world, ScenarioRunner* runner)
    : opt_(std::move(opt)), world_(world), runner_(runner) {}

int64_t Api::wall() const {
  if (opt_.wall_ms) return opt_.wall_ms();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool Api::session_for(const net::Request& req, Principal* out, std::string* key) {
  const std::string tok = req.cookie(kCookie);
  if (tok.size() != 64) return false;
  const std::string k = hex_sha(tok);
  Session* s = sessions_.get(k);
  if (!s) return false;
  const int64_t now = wall();
  const User* u = pf().user(s->user);
  if (!u || !u->active || now - s->last_seen > opt_.idle_timeout_ms || now - s->created > opt_.absolute_timeout_ms) {
    sessions_.erase(k);
    return false;
  }
  s->last_seen = now;
  out->id = u->id;
  out->role = u->role;
  out->system = false;
  if (key) *key = k;
  return true;
}

void Api::audit_view(const Principal& me, UserId subject, const char* view) {
  if (subject == me.id) return;
  Platform& P = pf();
  const std::string k = std::to_string(me.id) + ":" + std::to_string(subject) + ":" + view;
  int64_t* last = live_audit_.get(k);
  if (last && P.now() - *last <= 600'000 && P.now() >= *last) return;
  P.audit_access(me, subject, view, true);
  live_audit_.put(k, P.now());
}

std::string Api::login_token(const std::string& username, const std::string& password) {
  const User* u = pf().authenticate(username, password);
  if (!u) return {};
  uint8_t raw[32];
  if (!random_bytes(raw, sizeof raw)) return {};
  const std::string tok = hex(raw, sizeof raw);
  const int64_t now = wall();
  sessions_.put(hex_sha(tok), Session{u->id, now, now});
  return tok;
}

net::Response Api::handle(const net::Request& req) {
  net::Response r;
  if (req.path.compare(0, 5, "/api/") == 0 || req.path == "/api") {
    r = route(req);
    r.headers.emplace_back("Cache-Control", "no-store");
  } else {
    r = serve_static(req);
  }
  r.headers.emplace_back("Content-Security-Policy",
                         "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                         "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; "
                         "base-uri 'none'; form-action 'self'");
  r.headers.emplace_back("X-Content-Type-Options", "nosniff");
  r.headers.emplace_back("X-Frame-Options", "DENY");
  r.headers.emplace_back("Referrer-Policy", "no-referrer");
  r.headers.emplace_back("Permissions-Policy", "geolocation=(), camera=(), microphone=()");
  return r;
}

net::Response Api::serve_static(const net::Request& req) {
  if (req.method != "GET" && req.method != "HEAD") return error(405, "method not allowed");
  std::string p = req.path == "/" ? std::string("/index.html") : req.path;
  // A strict allow-list of characters, and no "..": nothing outside web_root
  // can be named, whatever encoding tricks the request tries.
  if (p.find("..") != std::string::npos || p.find("//") != std::string::npos) return error(404, "not found");
  for (char c : p)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
          c == '.' || c == '-' || c == '_'))
      return error(404, "not found");
  std::string body;
  if (!read_file(opt_.web_root + p, &body)) return error(404, "not found");
  net::Response r;
  r.content_type = content_type_for(p);
  r.body = std::move(body);
  r.headers.emplace_back("Cache-Control", "no-cache");
  return r;
}

net::Response Api::route(const net::Request& req) {
  const auto seg = segments(req.path);   // seg[0] == "api"
  const std::string& m = req.method;
  auto at = [&](size_t i) -> std::string { return i < seg.size() ? seg[i] : std::string(); };
  const std::string res = at(1);
  Platform& P = pf();

  // ── public ────────────────────────────────────────────────────────────────
  if (res == "health" && m == "GET") {
    JsonOut w;
    w.obj().kb("ok", true).kv("mode", opt_.demo ? "demo" : "production").ki("now_ms", P.now()).end_obj();
    return json(200, w.take());
  }
  const bool mutating = m == "POST" || m == "PATCH" || m == "DELETE" || m == "PUT";
  if (mutating) {
    const std::string* x = req.header("x-safetrail-request");
    if (!x || *x != "1") return error(403, "missing X-SafeTrail-Request header (CSRF protection)");
  }
  if (res == "login" && m == "POST") {
    util::Json b;
    std::string perr;
    if (!body_json(req, &b, &perr)) return error(400, perr);
    const std::string user = jstr(b, "username"), pass = jstr(b, "password");
    if (user.empty() || user.size() > 64 || pass.size() > 256) return error(400, "username and password required");
    const int64_t now = wall();
    for (const std::string& tk : {"u:" + user, "ip:" + req.remote}) {
      const Throttle* t = throttle_.get(tk);
      if (t && t->locked_until > now) return error(429, "too many failed attempts; try again in a minute");
    }
    const std::string tok = login_token(user, pass);
    if (tok.empty()) {
      for (const std::string& tk : {"u:" + user, "ip:" + req.remote}) {
        Throttle t = throttle_.get(tk) ? *throttle_.get(tk) : Throttle{};
        if (++t.failures >= 5) { t.locked_until = now + 60'000; t.failures = 0; }
        throttle_.put(tk, t);
      }
      return error(401, "invalid username or password");
    }
    throttle_.erase("u:" + user);
    const User* u = P.user_by_name(user);
    net::Response r;
    JsonOut w;
    w.obj().key("user");
    write_user(w, P, *u, true, false);
    w.end_obj();
    r.body = w.take();
    r.headers.emplace_back("Set-Cookie", std::string(kCookie) + "=" + tok +
                                             "; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800" +
                                             (opt_.secure_cookies ? "; Secure" : ""));
    return r;
  }

  Principal me;
  std::string skey;
  const bool authed = session_for(req, &me, &skey);
  if (res == "session" && m == "GET") {
    JsonOut w;
    w.obj().key("user");
    if (authed) write_user(w, P, *P.user(me.id), true, true); else w.null();
    w.kv("mode", opt_.demo ? "demo" : "production").end_obj();
    return json(200, w.take());
  }
  if (!authed) return error(401, "sign in required");

  if (res == "logout" && m == "POST") {
    sessions_.erase(skey);
    net::Response r = json(200, "{\"ok\":true}");
    r.headers.emplace_back("Set-Cookie", std::string(kCookie) + "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return r;
  }

  const bool staff = is_staff(me);
  util::Json b;
  if (mutating) {
    std::string perr;
    if (res != "zones" || at(2) != "import")
      if (!body_json(req, &b, &perr)) return error(400, "invalid JSON: " + perr);
  }

  // ── clock / live map ──────────────────────────────────────────────────────
  if (res == "live" && m == "GET") {
    JsonOut w;
    w.obj().ki("now_ms", P.now());
    w.key("runner").obj();
    if (runner_) {
      w.kv("scenario", runner_->scenario_id()).kv("state", to_string(runner_->state()))
          .kn("speed", runner_->speed()).ki("duration_ms", runner_->duration_ms());
    }
    w.end_obj();
    // Users: everyone the caller may see. Live views by staff are audited once
    // per subject per ten minutes, not once per poll.
    w.key("users").arr();
    for (const auto& u : P.users()) {
      if (u.role != Role::Tourist) continue;
      const bool vis = P.may_view_location(me, u.id);
      if (!vis && u.id != me.id && !staff) continue;
      write_user(w, P, u, vis, false);
      if (vis && u.sharing) audit_view(me, u.id, "live");
    }
    w.end_arr();
    // Unit positions are for the people who coordinate or are the units.
    w.key("responders").arr();
    if (staff || me.role == Role::Responder)
      for (const auto& r : P.responders().all()) responder_json(w, P, r);
    w.end_arr();
    w.key("incidents").arr();
    for (const auto& c : P.cases()) {
      if (c.id == kNoId) continue;
      const IncidentView v = incident_view(P, me, c);
      if (v != IncidentView::None) incident_json(w, P, c, false, v == IncidentView::Own ? me.id : kNoId);
    }
    w.end_arr();
    w.key("groups").arr();
    if (staff)
      for (const auto& g : P.groups()) {
        w.obj().ku("id", g.id).kv("name", g.name).ku("members", g.members.size())
            .ku("tracked", g.tracked_members).ku("components", g.components).kb("fragmented", g.fragmented);
        if (g.has_centre) w.key("centre").arr().num(g.centre.lat).num(g.centre.lon).end_arr();
        else w.key("centre").null();
        w.key("stragglers").arr();
        for (const auto& s : g.stragglers)
          w.obj().ku("user", s.user).kn("separation_m", s.separation_m).ki("since_ms", s.since_ms)
              .kb("alerted", s.alerted).end_obj();
        w.end_arr().end_obj();
      }
    w.end_arr();
    size_t open_alerts = 0;
    for (const auto& r : P.alerts())
      if (r.a.state == alert::AlertState::Open) ++open_alerts;
    w.ku("open_alerts", staff ? open_alerts : 0).ku("log_size", P.log().size());
    w.end_obj();
    return json(200, w.take());
  }

  // ── users ─────────────────────────────────────────────────────────────────
  if (res == "users") {
    if (seg.size() == 2 && m == "GET") {
      JsonOut w;
      w.obj().key("users").arr();
      for (const auto& u : P.users()) {
        if (!staff && u.id != me.id && !(me.role == Role::Responder && P.may_view_location(me, u.id))) continue;
        const bool vis = P.may_view_location(me, u.id);
        write_user(w, P, u, vis, false);
        if (vis && u.role == Role::Tourist && u.sharing) audit_view(me, u.id, "list");
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (seg.size() == 2 && m == "POST") {
      Role role;
      if (!role_from(jstr(b, "role"), &role)) return error(400, "role must be admin, operator, responder or tourist");
      return from(P.create_user(me, jstr(b, "username"), jstr(b, "display_name"), role, jstr(b, "password")));
    }
    uint32_t uid = 0;
    if (!parse_id(at(2), &uid) || !P.user(uid)) return error(404, "no such user");
    const std::string sub = at(3);
    if (sub.empty() && m == "GET") {
      if (!staff && uid != me.id && !P.may_view_location(me, uid)) return error(403, "not allowed");
      const bool vis = P.may_view_location(me, uid);
      P.audit_access(me, uid, "profile", vis);
      JsonOut w;
      w.obj().key("user");
      write_user(w, P, *P.user(uid), vis, true);
      w.end_obj();
      return json(200, w.take());
    }
    if (sub.empty() && m == "PATCH") {
      // Every field the request carries, in order; the first refusal stops it.
      if (jhas(b, "active")) { Outcome o = P.set_active(me, uid, jbool(b, "active", true)); if (!o.ok()) return from(o); }
      if (jhas(b, "display_name")) { Outcome o = P.set_display_name(me, uid, jstr(b, "display_name")); if (!o.ok()) return from(o); }
      if (jhas(b, "password")) { Outcome o = P.set_password(me, uid, jstr(b, "password")); if (!o.ok()) return from(o); }
      return from(Outcome::good(uid));
    }
    if (sub == "trail" && m == "GET") {
      const bool ok = P.may_view_location(me, uid);
      P.audit_access(me, uid, "trail", ok);
      if (!ok) return error(403, "not allowed to view this user's movement history");
      const Tracked* tr = P.tracked(uid);
      Timestamp from_t = 0, to_t = 0;
      if (!qint(req, "from", 0, &from_t) || !qint(req, "to", P.now(), &to_t))
        return error(400, "from and to: times in milliseconds");
      JsonOut w;
      w.obj().ku("user", uid).ki("retention_ms", P.config().trail_retention_ms).key("points").arr();
      if (tr) {
        size_t step = 1;
        size_t count = 0;
        for (const auto& p : tr->trail) if (p.t_ms >= from_t && p.t_ms <= to_t) ++count;
        if (count > 4000) step = count / 4000 + 1;   // thin very long trails, keep every flagged point
        size_t i = 0;
        for (const auto& p : tr->trail) {
          if (p.t_ms < from_t || p.t_ms > to_t) continue;
          if (step > 1 && (i++ % step) != 0 && p.flags == 0) continue;
          w.arr().integer(p.t_ms).num(p.lat).num(p.lon).num(p.accuracy_m).uint(p.flags).integer(p.recorded_ms).end_arr();
        }
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (sub == "consent" && m == "POST") return from(P.set_consent(me, uid, jbool(b, "consent", false)));
    if (sub == "sharing" && m == "POST")
      return from(jbool(b, "active", false) ? P.start_sharing(me, uid) : P.stop_sharing(me, uid));
    if (sub == "battery" && m == "POST") return from(P.set_battery_aware(me, uid, jbool(b, "aware", false)));
    if (sub == "sos" && m == "POST") return from(P.raise_sos(me, uid));
    if (sub == "history" && m == "DELETE") return from(P.erase_history(me, uid));
    if (sub == "fix" && m == "POST") {
      FixInput f;
      f.lat = jnum(b, "lat", kNaN);
      f.lon = jnum(b, "lon", kNaN);
      f.accuracy_m = jnum(b, "accuracy_m", 10.0);
      if (!jint(b, "t_ms", -1, &f.t_ms)) return error(400, "t_ms: a time in milliseconds");
      const double seq = jnum(b, "seq", 0);
      f.seq = seq > 0 && seq < 9e15 ? uint64_t(seq) : 0;
      const FixResult fr = P.ingest_fix(me, uid, f);
      JsonOut w;
      w.obj().kv("status", to_string(fr.status)).kv("reason", fr.reason).ku("events", fr.events)
          .kb("jump", fr.jump).end_obj();
      const int code = fr.status == FixStatus::Accepted || fr.status == FixStatus::Duplicate ||
                               fr.status == FixStatus::OutOfOrder ? 200
                     : fr.status == FixStatus::NotTracking ? 403
                     : fr.status == FixStatus::Offline ? 409 : 400;
      return json(code, w.take());
    }
    return error(404, "unknown user endpoint");
  }

  // ── groups ────────────────────────────────────────────────────────────────
  if (res == "groups") {
    if (!staff) return error(403, "operators and administrators only");
    if (seg.size() == 2 && m == "GET") {
      JsonOut w;
      w.obj().key("groups").arr();
      for (const auto& g : P.groups()) {
        w.obj().ku("id", g.id).kv("name", g.name).kv("source", to_string(g.source)).ki("created_ms", g.created_ms);
        w.kn("proximity_m", g.cfg.proximity_m).kn("straggler_m", g.cfg.straggler_m).ki("confirm_ms", g.cfg.confirm_ms);
        w.key("members").arr();
        for (auto u : g.members) w.uint(u);
        w.end_arr();
        w.ku("tracked", g.tracked_members).ku("components", g.components).kb("fragmented", g.fragmented);
        if (g.has_centre) w.key("centre").arr().num(g.centre.lat).num(g.centre.lon).end_arr();
        else w.key("centre").null();
        w.key("stragglers").arr();
        for (const auto& s : g.stragglers)
          w.obj().ku("user", s.user).kn("separation_m", s.separation_m).ki("since_ms", s.since_ms)
              .kb("alerted", s.alerted).end_obj();
        w.end_arr().end_obj();
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (seg.size() == 2 && m == "POST") return from(P.create_group(me, jstr(b, "name")));
    uint32_t gid = 0;
    if (!parse_id(at(2), &gid) || !P.group_rec(gid)) return error(404, "no such group");
    if (at(3) == "members" && m == "POST") {
      uint32_t u = kNoId;
      if (!jid(b, "user", &u)) return error(400, "user: a user id");
      return from(jstr(b, "action") == "remove" ? P.remove_member(me, gid, u) : P.add_member(me, gid, u));
    }
    if (seg.size() == 3 && m == "PATCH") {
      const GroupRecord* g = P.group_rec(gid);
      int64_t confirm_ms = 0;
      if (!to_int64(jnum(b, "confirm_s", double(g->cfg.confirm_ms) / 1000.0) * 1000.0, &confirm_ms))
        return error(400, "confirm_s out of range");
      return from(P.set_group_thresholds(me, gid, jnum(b, "proximity_m", g->cfg.proximity_m),
                                         jnum(b, "straggler_m", g->cfg.straggler_m), confirm_ms));
    }
    return error(404, "unknown group endpoint");
  }

  // ── zones ─────────────────────────────────────────────────────────────────
  if (res == "zones") {
    if (seg.size() == 2 && m == "GET") {
      const bool with_synthetic = req.query_param("synthetic") == "1";
      JsonOut w;
      w.obj().ki("now_ms", P.now()).key("zones").arr();
      for (ZoneId id : P.zones().all_ids()) {
        const fence::Zone* z = P.zones().get(id);
        if (z->synthetic && !with_synthetic) continue;
        zone_json(w, P, *z, true);
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (at(2) == "import" && m == "POST") {
      size_t added = 0;
      Outcome o = P.import_geojson(me, req.body, &added);
      if (!o.ok()) return from(o);
      JsonOut w;
      w.obj().kb("ok", true).ku("added", added).end_obj();
      return json(200, w.take());
    }
    auto zone_input = [&](Platform::ZoneInput* zi, std::string* err) -> bool {
      zi->name = jstr(b, "name");
      if (!zone_kind_from(jstr(b, "kind"), &zi->kind)) { *err = "kind must be restricted, caution, advisory or safe"; return false; }
      int64_t sev = 0;
      const util::Json* to = b.find("active_to_ms");
      if (!jint(b, "severity", 3, &sev) || !jint(b, "max_dwell_s", 0, &zi->max_dwell_s) ||
          !jint(b, "active_from_ms", 0, &zi->active_from) ||
          !(!to || to->type == util::Json::Type::Null ? (zi->active_to = kForever, true)
                                                      : jint(b, "active_to_ms", 0, &zi->active_to))) {
        *err = "severity, max_dwell_s, active_from_ms and active_to_ms must be numbers in range";
        return false;
      }
      zi->severity = int(std::clamp<int64_t>(sev, -1, 99));   // outside 1..5 is refused by the platform
      const util::Json* ring = b.find("ring");
      if (!ring || ring->type != util::Json::Type::Array) { *err = "ring: array of [lat, lon]"; return false; }
      for (const auto& p : ring->arr) {
        if (p.type != util::Json::Type::Array || p.arr.size() != 2) { *err = "ring: array of [lat, lon]"; return false; }
        zi->ring.push_back({p.arr[0].number_or(kNaN), p.arr[1].number_or(kNaN)});
      }
      return true;
    };
    if (seg.size() == 2 && m == "POST") {
      Platform::ZoneInput zi;
      std::string err;
      if (!zone_input(&zi, &err)) return error(400, err);
      return from(P.create_zone(me, zi));
    }
    uint32_t zid = 0;
    if (!parse_id(at(2), &zid) || !P.zones().get(zid)) return error(404, "no such zone");
    if (seg.size() == 3 && m == "GET") {
      if (!staff) return error(403, "operators and administrators only");   // it lists incidents and who caused changes
      JsonOut w;
      w.obj().key("zone");
      zone_json(w, P, *P.zones().get(zid), true);
      w.key("history").arr();
      for (const auto& ch : P.versioned().history_for(zid)) {
        const char* kind = ch.kind == index::VersionedIndex::Change::Kind::Added ? "added"
                         : ch.kind == index::VersionedIndex::Change::Kind::Removed ? "removed"
                         : ch.kind == index::VersionedIndex::Change::Kind::Replaced ? "geometry replaced"
                                                                                    : "validity changed";
        w.obj().ku("version", ch.version).ki("at_ms", ch.at).kv("change", kind).end_obj();
      }
      w.end_arr();
      w.key("log").arr();
      for (const auto& e : P.log())
        if (e.zone == zid && (e.kind == LogKind::ZoneCreated || e.kind == LogKind::ZoneUpdated ||
                              e.kind == LogKind::ZoneValidity || e.kind == LogKind::ZoneDeleted))
          w.obj().ki("t_ms", e.t_ms).kv("kind", to_string(e.kind)).kv("detail", e.detail).kid("actor", e.actor).end_obj();
      w.end_arr();
      w.key("incidents").arr();
      for (const auto& c : P.cases()) {
        if (c.id == kNoId) continue;
        const alert::Incident* inc = P.correlator().get(c.id);
        bool touches = false;
        if (inc) for (AlertId a : inc->alerts) if (a < P.alerts().size() && P.alerts()[a].a.zone == zid) touches = true;
        if (touches) incident_json(w, P, c, false);
      }
      w.end_arr();
      w.end_obj();
      return json(200, w.take());
    }
    if (seg.size() == 3 && m == "PATCH") {
      Platform::ZoneInput zi;
      std::string err;
      if (!zone_input(&zi, &err)) return error(400, err);
      return from(P.update_zone(me, zid, zi));
    }
    if (seg.size() == 3 && m == "DELETE") return from(P.delete_zone(me, zid));
    if (at(3) == "validity" && m == "POST") {
      if (jhas(b, "active")) {
        const fence::Zone* z = P.zones().get(zid);
        if (jbool(b, "active", true)) return from(P.set_zone_validity(me, zid, std::min(z->validity.from, P.now()), kForever));
        return from(P.set_zone_validity(me, zid, std::min(z->validity.from, P.now()), std::max<Timestamp>(P.now(), 1)));
      }
      const util::Json* to = b.find("to_ms");
      Timestamp from_t = 0, to_t = kForever;
      if (!jint(b, "from_ms", 0, &from_t) || (to && to->type != util::Json::Type::Null && !jint(b, "to_ms", 0, &to_t)))
        return error(400, "from_ms and to_ms: times in milliseconds");
      return from(P.set_zone_validity(me, zid, from_t, to_t));
    }
    return error(404, "unknown zone endpoint");
  }

  // ── alerts ────────────────────────────────────────────────────────────────
  if (res == "alerts") {
    if (seg.size() == 2 && m == "GET") {
      const std::string state = req.query_param("state");
      std::vector<std::pair<AlertId, double>> order;
      for (const auto& t : P.triage_order()) order.emplace_back(t.id, t.priority);
      if (state != "open") {
        for (size_t i = P.alerts().size(); i-- > 0;) {
          const auto s = P.alerts()[i].a.state;
          if (s == alert::AlertState::Resolved || s == alert::AlertState::Dismissed) order.emplace_back(AlertId(i), 0.0);
          if (order.size() > 1500) break;
        }
      }
      JsonOut w;
      w.obj().kv("order", "triage: open alerts by severity, age and isolation (binary max-heap), then closed alerts newest first");
      w.key("alerts").arr();
      for (const auto& [id, pr] : order) {
        const AlertRecord& r = P.alerts()[id];
        if (!staff && r.a.tourist != me.id) continue;
        alert_json(w, P, r, pr);
        if (!r.redacted && r.a.tourist != kNoId) audit_view(me, r.a.tourist, "alerts");
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    uint32_t aid = 0;
    if (!parse_id(at(2), &aid)) return error(404, "no such alert");
    if (at(3) == "ack" && m == "POST") return from(P.acknowledge_alert(me, aid));
    if (at(3) == "resolve" && m == "POST") return from(P.resolve_alert(me, aid, false));
    if (at(3) == "dismiss" && m == "POST") return from(P.resolve_alert(me, aid, true));
    return error(404, "unknown alert endpoint");
  }

  // ── incidents ─────────────────────────────────────────────────────────────
  if (res == "incidents") {
    if (seg.size() == 2 && m == "GET") {
      JsonOut w;
      w.obj().key("incidents").arr();
      for (size_t i = P.cases().size(); i-- > 0;) {
        const auto& c = P.cases()[i];
        if (c.id == kNoId) continue;
        const IncidentView v = incident_view(P, me, c);
        if (v != IncidentView::None) incident_json(w, P, c, false, v == IncidentView::Own ? me.id : kNoId);
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    uint32_t iid = 0;
    if (!parse_id(at(2), &iid) || !P.incident_case(iid)) return error(404, "no such incident");
    const IncidentCase& c = *P.incident_case(iid);
    if (seg.size() == 3 && m == "GET") {
      const IncidentView v = incident_view(P, me, c);
      if (v == IncidentView::None) return error(403, "not allowed");
      const UserId self = v == IncidentView::Own ? me.id : kNoId;
      JsonOut w;
      w.obj().key("incident");
      incident_json(w, P, c, true, self);
      w.key("alerts").arr();
      for (AlertId a : P.correlator().get(iid)->alerts) {
        const AlertRecord& r = P.alerts()[a];
        if (self != kNoId && r.a.tourist != self) continue;
        alert_json(w, P, r, 0);
        if (!r.redacted && r.a.tourist != kNoId) audit_view(me, r.a.tourist, "incident");
      }
      w.end_arr();
      w.key("timeline").arr();
      for (const auto& e : P.log()) {
        bool rel = e.incident == iid;
        if (!rel && e.alert != kNoId) {
          const auto& al = P.correlator().get(iid)->alerts;
          rel = std::find(al.begin(), al.end(), e.alert) != al.end();
        }
        if (!rel || (self != kNoId && e.user != kNoId && e.user != self)) continue;
        w.obj().ku("seq", e.seq).ki("t_ms", e.t_ms).ki("recorded_ms", e.recorded_ms).kv("kind", to_string(e.kind))
            .kid("user", e.user).kv("detail", e.detail).kb("late", e.late).ku("evidence_index", e.evidence_index).end_obj();
      }
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (at(3) == "ack" && m == "POST") return from(P.acknowledge_incident(me, iid));
    if (at(3) == "assign" && m == "POST") {
      uint32_t rid = kNoId;   // absent: the recommended unit
      if (jhas(b, "responder") && !jid(b, "responder", &rid)) return error(400, "responder: a responder id");
      return from(P.assign_incident(me, iid, rid));
    }
    if (at(3) == "resolve" && m == "POST") return from(P.resolve_incident(me, iid, jstr(b, "note")));
    return error(404, "unknown incident endpoint");
  }

  // ── dispatch ──────────────────────────────────────────────────────────────
  if (res == "dispatch" && at(2) == "plan" && m == "POST") {
    if (!staff) return error(403, "operators and administrators only");
    const auto s = P.plan_dispatch(me, jbool(b, "apply", false));
    JsonOut w;
    w.obj().key("incidents").arr();
    for (auto i : s.incidents) w.uint(i);
    w.end_arr().key("responders").arr();
    for (auto r : s.responders) w.uint(r);
    w.end_arr();
    auto plan = [&](const char* k, const dispatch::Plan& p) {
      w.key(k).obj().kn("total_m", p.total_m).kn("makespan_m", p.makespan_m).ku("unassigned", p.unassigned);
      w.key("pairs").arr();
      for (const auto& d : p.dispatches) w.arr().uint(d.responder).uint(d.incident).num(d.travel_m).end_arr();
      w.end_arr().end_obj();
    };
    plan("greedy", s.greedy);
    plan("optimal", s.optimal);
    w.kb("applied", jbool(b, "apply", false)).end_obj();
    return json(200, w.take());
  }

  // ── responders ────────────────────────────────────────────────────────────
  if (res == "responders") {
    if (seg.size() == 2 && m == "GET") {
      if (!staff && me.role != Role::Responder) return error(403, "operators, administrators and responders only");
      JsonOut w;
      w.obj().key("responders").arr();
      for (const auto& r : P.responders().all()) responder_json(w, P, r);
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (seg.size() == 2 && m == "POST")
      return from(P.create_responder(me, jstr(b, "name"), jstr(b, "type"), {jnum(b, "lat", kNaN), jnum(b, "lon", kNaN)}));
    uint32_t rid = 0;
    if (!parse_id(at(2), &rid) || rid >= P.responders().size()) return error(404, "no such responder");
    if (seg.size() == 3 && m == "PATCH") {
      if (jhas(b, "lat") || jhas(b, "lon")) {
        Outcome o = P.set_responder_position(me, rid, {jnum(b, "lat", kNaN), jnum(b, "lon", kNaN)});
        if (!o.ok()) return from(o);
      }
      if (jhas(b, "available")) return from(P.set_responder_available(me, rid, jbool(b, "available", true)));
      return from(Outcome::good(rid));
    }
    return error(404, "unknown responder endpoint");
  }

  // ── roads (for drawing the network under the map) ─────────────────────────
  if (res == "roads" && m == "GET") {
    const graph::RoadGraph& g = P.roads();
    geo::Bbox bb{};
    if (!qnum(req, "min_lat", 0, &bb.min_lat) || !qnum(req, "min_lon", 0, &bb.min_lon) ||
        !qnum(req, "max_lat", 0, &bb.max_lat) || !qnum(req, "max_lon", 0, &bb.max_lon))
      return error(400, "min_lat, min_lon, max_lat, max_lon: numbers");
    JsonOut w;
    w.obj().ku("nodes", g.node_count()).ku("edges", g.edge_count()).key("segments").arr();
    size_t n = 0;
    bool truncated = false;
    for (graph::NodeId u = 0; u < graph::NodeId(g.node_count()) && !truncated; ++u) {
      const geo::LatLon& a = g.pos(u);
      for (const auto& e : g.neighbors(u)) {
        if (e.to < u) continue;
        const geo::LatLon& c = g.pos(e.to);
        const bool in = (a.lat >= bb.min_lat && a.lat <= bb.max_lat && a.lon >= bb.min_lon && a.lon <= bb.max_lon) ||
                        (c.lat >= bb.min_lat && c.lat <= bb.max_lat && c.lon >= bb.min_lon && c.lon <= bb.max_lon);
        if (!in) continue;
        if (++n > 60'000) { truncated = true; break; }
        w.arr().num(std::round(a.lat * 1e6) / 1e6).num(std::round(a.lon * 1e6) / 1e6)
            .num(std::round(c.lat * 1e6) / 1e6).num(std::round(c.lon * 1e6) / 1e6).end_arr();
      }
    }
    w.end_arr().kb("truncated", truncated).end_obj();
    return json(200, w.take());
  }

  // ── history and evidence ──────────────────────────────────────────────────
  if (res == "history" && m == "GET") {
    if (!staff) return error(403, "operators and administrators only");
    Timestamp t = 0;
    if (!qint(req, "t", 0, &t)) return error(400, "t: a time in milliseconds");
    JsonOut w;
    w.obj().ki("t_ms", t).ki("now_ms", P.now());
    w.kv("semantics", "what the system knew at t: points and records received by t (transaction time)");
    w.key("users").arr();
    for (const auto& u : P.users()) {
      if (u.role != Role::Tourist) continue;
      const TrailPoint* p = P.position_at(u.id, t);
      if (!p) continue;
      P.audit_access(me, u.id, "history", true);
      w.obj().ku("id", u.id).kv("name", u.display_name).kb("simulated", u.source == Source::Scenario)
          .ki("t_ms", p->t_ms).kn("lat", p->lat).kn("lon", p->lon).kn("accuracy_m", p->accuracy_m)
          .ku("flags", p->flags).end_obj();
    }
    w.end_arr();
    std::vector<ZoneId> active;
    P.versioned().active_at(t, active);
    w.key("zones_in_force").arr();
    for (auto z : active) w.uint(z);
    w.end_arr();
    w.ku("version_at_t", P.versioned().version_at(t)).ku("zones_at_t", P.versioned().zone_count_at(t));
    w.key("alerts").arr();
    for (const auto& r : P.alerts()) {
      bool existed = false;
      const auto s = P.alert_state_at(r.a.id, t, &existed);
      if (!existed) continue;
      w.obj().ku("id", r.a.id).kv("kind", alert::to_string(r.a.kind)).kv("state_at_t", alert::to_string(s))
          .kv("state_now", alert::to_string(r.a.state)).ku("severity", r.a.severity).kid("user", r.a.tourist)
          .kid("incident", r.a.incident).kb("late", r.late).end_obj();
    }
    w.end_arr();
    w.key("incidents").arr();
    for (const auto& c : P.cases()) {
      if (c.id == kNoId) continue;
      bool existed = false;
      const auto s = P.case_state_at(c.id, t, &existed);
      if (!existed) continue;
      w.obj().ku("id", c.id).kv("state_at_t", to_string(s)).kv("state_now", to_string(c.state)).end_obj();
    }
    w.end_arr();
    w.key("recent").arr();
    for (const auto& e : P.log()) {
      if (e.recorded_ms > t || e.t_ms < t - 300'000) continue;
      w.obj().ku("seq", e.seq).ki("t_ms", e.t_ms).kv("kind", to_string(e.kind)).kid("user", e.user)
          .kid("zone", e.zone).kv("detail", e.detail).kb("late", e.late).end_obj();
    }
    w.end_arr().end_obj();
    return json(200, w.take());
  }

  if (res == "events" && m == "GET") {
    const std::string uq = req.query_param("user");
    const std::string kind = req.query_param("kind");
    const bool lamport = req.query_param("order") == "lamport";
    int64_t lim = 400;
    if (!qint(req, "limit", 400, &lim)) return error(400, "limit: a number");
    const size_t limit = size_t(std::clamp<int64_t>(lim, 1, 2000));
    uint32_t only = kNoId;
    if (!uq.empty() && !parse_id(uq, &only)) return error(400, "bad user");
    if (!staff) only = me.id;
    std::vector<const LogEntry*> v;
    for (const auto& e : P.log()) {
      if (only != kNoId && e.user != only) continue;
      if (!kind.empty() && kind != to_string(e.kind)) continue;
      v.push_back(&e);
    }
    if (lamport)
      std::stable_sort(v.begin(), v.end(), [](const LogEntry* a, const LogEntry* c) { return a->stamp.before(c->stamp); });
    const size_t start = v.size() > limit ? v.size() - limit : 0;
    JsonOut w;
    w.obj().ku("total", v.size()).kv("order", lamport ? "lamport" : "arrival").key("events").arr();
    for (size_t i = v.size(); i-- > start;) {
      const LogEntry& e = *v[i];
      const User* u = P.user(e.user);
      const fence::Zone* z = P.zones().get(e.zone);
      w.obj().ku("seq", e.seq).ku("lamport", e.stamp.counter).ku("device", e.stamp.device).ki("t_ms", e.t_ms)
          .ki("recorded_ms", e.recorded_ms).kv("kind", to_string(e.kind)).kid("user", e.user)
          .kv("user_name", u ? u->display_name : std::string()).kid("zone", e.zone)
          .kv("zone_name", z ? z->name : std::string()).kid("alert", e.alert).kid("incident", e.incident)
          .kid("group", e.group).kid("responder", e.responder).kid("actor", e.actor).kb("late", e.late)
          .kv("detail", e.detail).ku("evidence_index", e.evidence_index).end_obj();
    }
    w.end_arr().end_obj();
    return json(200, w.take());
  }

  if (res == "evidence" && m == "GET") {
    if (!staff) return error(403, "operators and administrators only");
    const auto& log = P.evidence();
    if (seg.size() == 2) {
      JsonOut w;
      w.obj().ku("size", log.size()).kv("root", log.size() ? evidence::to_hex(log.root()) : std::string())
          .ku("access_log_size", P.access_evidence().size()).end_obj();
      return json(200, w.take());
    }
    uint32_t idx = 0;
    if (!parse_id(at(2), &idx) || idx >= log.size() || idx >= P.log().size()) return error(404, "no such entry");
    const std::string entry = canonical(P.log()[idx]);
    const auto leaf = evidence::leaf_hash(reinterpret_cast<const uint8_t*>(entry.data()), entry.size());
    const auto proof = log.prove(idx);
    const auto root = log.root();
    JsonOut w;
    w.obj().ku("index", idx).ku("size", proof.size).kv("entry", entry).kv("leaf_hash", evidence::to_hex(leaf))
        .kv("root", evidence::to_hex(root));
    w.key("path").arr();
    for (const auto& h : proof.path) w.str(evidence::to_hex(h));
    w.end_arr().kb("verified_by_server", proof.verify(leaf, root)).end_obj();
    return json(200, w.take());
  }

  if (res == "audit" && m == "GET") {
    if (me.role != Role::Admin) return error(403, "administrators only");
    JsonOut w;
    const auto& al = P.access_log();
    w.obj().ku("size", al.size()).kv("root", P.access_evidence().size() ? evidence::to_hex(P.access_evidence().root()) : "")
        .key("records").arr();
    const size_t start = al.size() > 1000 ? al.size() - 1000 : 0;
    for (size_t i = al.size(); i-- > start;) {
      const auto& r = al[i];
      const User* a = P.user(r.actor);
      const User* s = P.user(r.subject);
      w.obj().ki("t_ms", r.t_ms).kv("actor", a ? a->username : "?").kv("subject", s ? s->display_name : "?")
          .kv("what", r.what).kb("allowed", r.allowed).end_obj();
    }
    w.end_arr().end_obj();
    return json(200, w.take());
  }

  if (res == "stats" && m == "GET") {
    if (!staff) return error(403, "operators and administrators only");
    const auto& s = P.stats();
    const auto ec = P.eval_counters();
    const auto is = P.spatial_index().stats();
    const auto cs = P.correlator().stats();
    const auto rs = P.reconciler().stats();
    const auto ss = P.versioned().share_stats();
    size_t tracked = 0, sharing = 0;
    for (const auto& u : P.users()) if (u.role == Role::Tourist) { ++tracked; if (u.sharing) ++sharing; }
    JsonOut w;
    w.obj().ki("now_ms", P.now()).ku("tourists", tracked).ku("sharing", sharing);
    w.key("ingest").obj().ku("accepted", s.fixes_accepted).ku("duplicates", s.fixes_duplicate)
        .ku("out_of_order", s.fixes_out_of_order).ku("invalid", s.fixes_invalid).ku("refused", s.fixes_refused)
        .ku("jumps", s.jumps).end_obj();
    w.key("evaluator").obj().ku("evaluations", ec.evaluations).ku("candidates_examined", ec.candidates_examined)
        .ku("exact_tests", ec.exact_tests_run).ku("noisy_rejected", ec.fixes_rejected_noise)
        .ku("flaps_suppressed", ec.flaps_suppressed).ku("out_of_window", ec.out_of_window_observations)
        .ku("states_closed", ec.states_closed).kn("mean_us", s.eval_calls ? s.eval_us_total / double(s.eval_calls) : 0.0)
        .ku("calls", s.eval_calls).end_obj();
    w.key("index").obj().kv("kind", P.spatial_index().name()).ku("zones", P.spatial_index().size())
        .ku("nodes", is.node_count).ku("depth", is.max_depth).ku("bytes", is.bytes).ku("queries", is.queries)
        .kn("avg_candidates", is.avg_candidates()).end_obj();
    w.key("alerts").obj().ku("raised", s.alerts).ku("escalations", s.escalations).ku("transitions", s.transitions).end_obj();
    w.key("correlation").obj().ku("alerts_ingested", cs.alerts_ingested).ku("incidents_opened", cs.incidents_opened)
        .ku("incidents_closed", cs.incidents_closed).ku("alerts_absorbed", cs.alerts_absorbed)
        .kn("compression", cs.compression_ratio()).end_obj();
    w.key("cohesion").obj().ku("passes", s.cohesion_passes)
        .kn("mean_us", s.cohesion_passes ? s.cohesion_us_total / double(s.cohesion_passes) : 0.0).end_obj();
    w.key("sync").obj().ku("merged", rs.merged).ku("duplicates_rejected", rs.duplicates_rejected)
        .ku("reordered", rs.reordered).ki("max_skew_ms", rs.max_skew_ms).end_obj();
    w.key("history").obj().ku("versions", P.versioned().version_count()).ku("nodes_allocated", ss.total_nodes_allocated)
        .ku("nodes_if_full_copies", ss.nodes_if_full_copies).kn("sharing_ratio", ss.sharing_ratio())
        .ku("log_entries", P.log().size()).ku("evidence_leaves", P.evidence().size()).end_obj();
    w.key("roads").obj().ku("nodes", P.roads().node_count()).ku("edges", P.roads().edge_count()).end_obj();
    // Per-kind event counts, from the log itself.
    w.key("events_by_kind").obj();
    std::vector<std::pair<std::string, size_t>> counts;
    for (const auto& e : P.log()) {
      const std::string k = to_string(e.kind);
      bool f = false;
      for (auto& c : counts) if (c.first == k) { ++c.second; f = true; break; }
      if (!f) counts.emplace_back(k, 1);
    }
    for (const auto& c : counts) w.ku(c.first.c_str(), c.second);
    w.end_obj().end_obj();
    return json(200, w.take());
  }

  if (res == "settings") {
    if (m == "GET") {
      if (!staff) return error(403, "operators and administrators only");
      const auto& c = P.config();
      JsonOut w;
      w.obj().kv("mode", opt_.demo ? "demo" : "production");
      w.key("editable").obj().ki("trail_retention_ms", c.trail_retention_ms).kb("auto_dispatch", c.auto_dispatch)
          .ku("incident_min_breach_severity", c.incident_min_breach_severity)
          .ku("warning_min_severity", c.warning_min_severity).kn("approach_alert_eta_s", c.approach_alert_eta_s)
          .kn("jump_speed_mps", c.jump_speed_mps).end_obj();
      w.key("fixed").obj().ki("escalation_timeout_ms", c.escalation_timeout_ms)
          .ki("cohesion_interval_ms", c.cohesion_interval_ms).ki("stationary_confirm_ms", c.stationary_confirm_ms)
          .kn("hysteresis_enter_margin_m", c.eval.hysteresis.enter_margin_m)
          .kn("hysteresis_exit_margin_m", c.eval.hysteresis.exit_margin_m)
          .ku("hysteresis_confirm_samples", c.eval.hysteresis.confirm_samples)
          .ki("hysteresis_min_dwell_ms", c.eval.hysteresis.min_dwell_ms)
          .kn("prediction_horizon_s", c.eval.prediction_horizon_s)
          .kn("correlation_radius_m", c.correlation.space_radius_m)
          .ki("correlation_window_ms", c.correlation.time_window_ms)
          .ku("pbkdf2_iterations", c.pbkdf2_iterations).kv("spatial_index", P.spatial_index().name()).end_obj();
      w.end_obj();
      return json(200, w.take());
    }
    if (m == "PATCH") {
      if (me.role != Role::Admin) return error(403, "administrators only");
      PlatformConfig& c = P.mutable_config();
      if (jhas(b, "trail_retention_ms")) {
        const double v = jnum(b, "trail_retention_ms", 0);
        if (!(v >= 60'000 && v <= 30.0 * 86400'000)) return error(400, "retention: 1 minute to 30 days");
        c.trail_retention_ms = int64_t(v);
      }
      if (jhas(b, "auto_dispatch")) c.auto_dispatch = jbool(b, "auto_dispatch", false);
      auto sev = [&](const char* k, uint8_t* dst) -> bool {
        if (!jhas(b, k)) return true;
        const double v = jnum(b, k, 0);
        if (!(v >= 1 && v <= 5)) return false;
        *dst = uint8_t(v);
        return true;
      };
      if (!sev("incident_min_breach_severity", &c.incident_min_breach_severity) ||
          !sev("warning_min_severity", &c.warning_min_severity))
        return error(400, "severities are 1..5");
      if (jhas(b, "approach_alert_eta_s")) {
        const double v = jnum(b, "approach_alert_eta_s", 0);
        if (!(v >= 10 && v <= 3600)) return error(400, "eta threshold 10..3600 s");
        c.approach_alert_eta_s = v;
      }
      if (jhas(b, "jump_speed_mps")) {
        const double v = jnum(b, "jump_speed_mps", 0);
        if (!(v >= 5 && v <= 1000)) return error(400, "jump speed 5..1000 m/s");
        c.jump_speed_mps = v;
      }
      return from(Outcome::good());
    }
  }

  // ── scenario runner ───────────────────────────────────────────────────────
  if (res == "scenario") {
    if (!staff) return error(403, "operators and administrators only");
    if (!opt_.demo || !runner_) return error(409, "scenarios are disabled in production mode");
    ScenarioRunner& R = *runner_;
    const std::string act = at(2);
    if (m == "POST") {
      if (act == "load") {
        std::string err;
        if (!R.load(jstr(b, "id"), &err)) return error(400, err);
        sessions_ = ds::HashMap<std::string, Session>{};   // fresh world: user ids are rebuilt
        live_audit_ = ds::HashMap<std::string, int64_t>{};
        // The caller's own session survives: staff accounts are recreated with
        // the same ids, so re-insert it.
        sessions_.put(skey, Session{me.id, wall(), wall()});
      } else if (act == "start") R.start();
      else if (act == "pause") R.pause();
      else if (act == "resume") R.resume();
      else if (act == "reset") {
        R.reset();
        sessions_ = ds::HashMap<std::string, Session>{};
        live_audit_ = ds::HashMap<std::string, int64_t>{};
        sessions_.put(skey, Session{me.id, wall(), wall()});
      }
      else if (act == "step") R.step(int(std::max(1.0, std::min(600.0, jnum(b, "ticks", 1)))));
      else if (act == "speed") R.set_speed(jnum(b, "speed", 1.0));
      else return error(404, "unknown scenario action");
    }
    JsonOut w;
    w.obj().kv("id", R.scenario_id()).kv("state", to_string(R.state())).kn("speed", R.speed())
        .ki("now_ms", pf().now()).ki("duration_ms", R.duration_ms());
    w.key("catalogue").arr();
    for (const auto& c : ScenarioRunner::catalogue())
      w.obj().kv("id", c.id).kv("title", c.title).kv("summary", c.summary).ki("duration_ms", c.duration_ms).end_obj();
    w.end_arr().key("narration").arr();
    for (const auto& n : R.narration()) w.obj().ki("at_ms", n.at_ms).kv("text", n.text).kb("done", n.done).end_obj();
    w.end_arr().key("devices").arr();
    for (const auto& d : R.devices())
      w.obj().ku("user", d.user).kv("name", d.name).kb("offline", d.offline).ku("queued", d.queued)
          .ki("clock_skew_ms", d.clock_skew_ms).kb("battery_aware", d.battery_aware)
          .ku("fixes_sent", d.fixes_sent).ku("fixes_skipped", d.fixes_skipped).end_obj();
    w.end_arr().key("sync_reports").arr();
    for (const auto& s : R.sync_reports()) w.str(s);
    w.end_arr().end_obj();
    return json(200, w.take());
  }

  // ── data structures lab ───────────────────────────────────────────────────
  if (res == "lab") {
    if (m == "GET") {
      JsonOut w;
      w.obj().key("experiments").arr();
      for (const auto& e : lab_experiments()) w.str(e);
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    if (m == "POST") {
      int status = 200;
      std::string out = run_lab(at(2), b, &status);
      return json(status, std::move(out));
    }
  }

  // ── committed benchmark results (read-only) ───────────────────────────────
  if (res == "bench" && m == "GET") {
    const std::string f = at(2);
    if (f.empty()) {
      JsonOut w;
      w.obj().key("files").arr();
      for (const char* n : {"index_scaling.csv", "index_density.csv", "index_build.csv", "interval_tree.csv",
                            "versioned_index.csv", "routing.csv", "dispatch.csv", "power.csv",
                            "evaluator_cost.csv", "hysteresis_ab.csv", "multi_user.csv", "environment.txt"})
        w.str(n);
      w.end_arr().end_obj();
      return json(200, w.take());
    }
    for (char c : f)
      if (!((c >= 'a' && c <= 'z') || c == '_' || c == '.')) return error(404, "not found");
    if (f.find("..") != std::string::npos) return error(404, "not found");
    std::string body;
    if (!read_file(opt_.bench_dir + "/" + f, &body)) return error(404, "not found");
    net::Response r;
    r.content_type = content_type_for(f);
    r.body = std::move(body);
    return r;
  }

  return error(404, "no such endpoint");
}

}  // namespace safetrail::app
