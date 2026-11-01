#include "safetrail/app/platform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "safetrail/alert/triage.hpp"
#include "safetrail/app/crypto.hpp"
#include "safetrail/graph/astar.hpp"
#include "safetrail/graph/dijkstra.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/util/bytes.hpp"

namespace safetrail::app {

// ─── Names ──────────────────────────────────────────────────────────────────

const char* to_string(Role r) {
  switch (r) {
    case Role::Admin: return "admin";
    case Role::Operator: return "operator";
    case Role::Responder: return "responder";
    case Role::Tourist: return "tourist";
  }
  return "?";
}

bool role_from(const std::string& s, Role* out) {
  if (s == "admin") { *out = Role::Admin; return true; }
  if (s == "operator") { *out = Role::Operator; return true; }
  if (s == "responder") { *out = Role::Responder; return true; }
  if (s == "tourist") { *out = Role::Tourist; return true; }
  return false;
}

const char* to_string(Source s) {
  switch (s) {
    case Source::Api: return "api";
    case Source::Fixture: return "fixture";
    case Source::Scenario: return "scenario";
  }
  return "?";
}

const char* to_string(FixStatus s) {
  switch (s) {
    case FixStatus::Accepted: return "accepted";
    case FixStatus::Duplicate: return "duplicate";
    case FixStatus::OutOfOrder: return "out_of_order";
    case FixStatus::Invalid: return "invalid";
    case FixStatus::NotTracking: return "not_tracking";
    case FixStatus::Offline: return "offline";
    case FixStatus::UnknownUser: return "unknown_user";
  }
  return "?";
}

const char* to_string(CaseState s) {
  switch (s) {
    case CaseState::Open: return "open";
    case CaseState::Acknowledged: return "acknowledged";
    case CaseState::Dispatched: return "dispatched";
    case CaseState::Resolved: return "resolved";
  }
  return "?";
}

const char* to_string(LogKind k) {
  switch (k) {
    case LogKind::ZoneEnter: return "zone_enter";
    case LogKind::ZoneExit: return "zone_exit";
    case LogKind::ZoneUncertain: return "zone_uncertain";
    case LogKind::ZoneApproaching: return "zone_approaching";
    case LogKind::DwellExceeded: return "dwell_exceeded";
    case LogKind::AlertRaised: return "alert_raised";
    case LogKind::AlertAcknowledged: return "alert_acknowledged";
    case LogKind::AlertEscalated: return "alert_escalated";
    case LogKind::AlertResolved: return "alert_resolved";
    case LogKind::AlertDismissed: return "alert_dismissed";
    case LogKind::IncidentOpened: return "incident_opened";
    case LogKind::IncidentUpdated: return "incident_updated";
    case LogKind::IncidentAcknowledged: return "incident_acknowledged";
    case LogKind::IncidentAssigned: return "incident_assigned";
    case LogKind::IncidentResolved: return "incident_resolved";
    case LogKind::SessionStarted: return "session_started";
    case LogKind::SessionStopped: return "session_stopped";
    case LogKind::ConsentGranted: return "consent_granted";
    case LogKind::ConsentWithdrawn: return "consent_withdrawn";
    case LogKind::HistoryErased: return "history_erased";
    case LogKind::UserCreated: return "user_created";
    case LogKind::UserUpdated: return "user_updated";
    case LogKind::GroupCreated: return "group_created";
    case LogKind::GroupChanged: return "group_changed";
    case LogKind::GroupSeparated: return "group_separated";
    case LogKind::GroupRejoined: return "group_rejoined";
    case LogKind::ZoneCreated: return "zone_created";
    case LogKind::ZoneUpdated: return "zone_updated";
    case LogKind::ZoneDeleted: return "zone_deleted";
    case LogKind::ZoneValidity: return "zone_validity";
    case LogKind::ResponderCreated: return "responder_created";
    case LogKind::ResponderUpdated: return "responder_updated";
    case LogKind::DeviceOffline: return "device_offline";
    case LogKind::DeviceReconnected: return "device_reconnected";
    case LogKind::SyncMerged: return "sync_merged";
  }
  return "?";
}

std::string canonical(const LogEntry& e) {
  std::string s = "safetrail-log-v1|" + std::to_string(e.seq) + "|" +
                  std::to_string(e.stamp.counter) + "|" + std::to_string(e.stamp.device) + "|" +
                  std::to_string(e.t_ms) + "|" + std::to_string(e.recorded_ms) + "|" +
                  to_string(e.kind) + "|" + std::to_string(e.user) + "|" +
                  std::to_string(e.actor) + "|" + std::to_string(e.zone) + "|" +
                  std::to_string(e.alert) + "|" + std::to_string(e.incident) + "|" +
                  std::to_string(e.group) + "|" + std::to_string(e.responder) + "|" +
                  (e.late ? "late" : "live") + "|" + e.detail;
  return s;
}

// ─── Offline device ─────────────────────────────────────────────────────────

struct DeviceAgent {
  UserId user = kNoId;
  sync::OfflineQueue queue{0};
  fence::ZoneStore zones;
  index::Geohash idx;
  std::unique_ptr<fence::Evaluator> eval;
  track::Tourist tourist;
  int64_t skew_ms = 0;
  Timestamp offline_since = 0;
  uint64_t fixes = 0;
  Timestamp last_fix_true_ms = 0;
};

namespace {

constexpr uint8_t kPayloadFix = 1;
constexpr uint8_t kPayloadEvent = 2;

std::vector<uint8_t> encode_fix(const geo::UncertainPoint& p) {
  std::vector<uint8_t> b;
  util::put_u8(b, kPayloadFix);
  util::put_i64(b, p.t_ms);
  util::put_f64(b, p.pos.lat);
  util::put_f64(b, p.pos.lon);
  util::put_f64(b, p.accuracy_m);
  return b;
}

std::vector<uint8_t> encode_event(const fence::Event& e) {
  std::vector<uint8_t> b;
  util::put_u8(b, kPayloadEvent);
  util::put_u8(b, uint8_t(e.kind));
  util::put_u32(b, e.zone);
  util::put_i64(b, e.t_ms);
  util::put_f64(b, e.depth_m);
  util::put_f64(b, e.accuracy_m);
  util::put_f64(b, e.eta_s);
  util::put_u8(b, uint8_t(e.containment));
  return b;
}

LogKind log_kind(fence::EventKind k) {
  switch (k) {
    case fence::EventKind::ZoneEnter: return LogKind::ZoneEnter;
    case fence::EventKind::ZoneExit: return LogKind::ZoneExit;
    case fence::EventKind::ZoneUncertain: return LogKind::ZoneUncertain;
    case fence::EventKind::ZoneApproaching: return LogKind::ZoneApproaching;
    case fence::EventKind::DwellExceeded: return LogKind::DwellExceeded;
  }
  return LogKind::ZoneEnter;
}

bool valid_username(const std::string& s) {
  if (s.size() < 3 || s.size() > 32) return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
      return false;
  return true;
}

bool valid_label(const std::string& s, size_t max_len) {
  if (s.empty() || s.size() > max_len) return false;
  for (char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

double elapsed_us(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - since).count();
}

bool incident_worthy(const AlertRecord& r, uint8_t min_breach) {
  switch (r.a.kind) {
    case alert::AlertKind::ZoneBreach: return r.a.severity >= min_breach;
    case alert::AlertKind::DwellExceeded:
    case alert::AlertKind::Straggler:
    case alert::AlertKind::Stationary:
    case alert::AlertKind::SignalLost:
    case alert::AlertKind::SosTriggered: return true;
    default: return false;   // approaching, uncertain, fragmented, deviation: warnings
  }
}

bool alert_is_open(alert::AlertState s) {
  return s == alert::AlertState::Open || s == alert::AlertState::Acknowledged ||
         s == alert::AlertState::Dispatched;
}

}  // namespace

// ─── Construction ───────────────────────────────────────────────────────────

Platform::Platform(PlatformConfig cfg) : cfg_(std::move(cfg)) {
  index_ = std::make_unique<index::RTree>();
  vindex_ = std::make_unique<index::VersionedIndex>();
  eval_ = std::make_unique<fence::Evaluator>(*index_, zones_, cfg_.eval);
  corr_ = std::make_unique<alert::Correlator>(cfg_.correlation);
  escalation_ = std::make_unique<alert::EscalationTracker>(cfg_.escalation_timeout_ms, 1000, 512, 0);
  clock_.device = 0;   // the server is device 0; devices are 1 + user id
}

Platform::~Platform() = default;

// ─── Clock ──────────────────────────────────────────────────────────────────

void Platform::tick(Timestamp now_ms) {
  if (now_ms < now_ms_) return;
  now_ms_ = now_ms;
  detect_anomalies(now_ms_);
  if (now_ms_ >= next_cohesion_ms_) {
    run_cohesion();
    next_cohesion_ms_ = now_ms_ + cfg_.cohesion_interval_ms;
  }
  escalate(now_ms_);
  if (now_ms_ >= next_retention_ms_) {
    enforce_retention(now_ms_);
    expire_warnings(now_ms_);
    next_retention_ms_ = now_ms_ + 60'000;
  }
}

// ─── World ──────────────────────────────────────────────────────────────────

bool Platform::load_zones_file(const std::string& path, std::string* err) {
  const size_t before = zones_.size();
  if (!zones_.load_geojson(path, err)) return false;
  zone_source_.resize(zones_.size(), Source::Fixture);
  std::vector<std::pair<ZoneId, geo::Bbox>> items;
  for (ZoneId id : zones_.all_ids()) items.emplace_back(id, zones_.get(id)->shape.bbox());
  index_->build(items);   // STR bulk load for the initial set; CRUD inserts after
  for (size_t i = before; i < zones_.size(); ++i) {
    const fence::Zone* z = zones_.get(ZoneId(i));
    if (z) vindex_->add_zone(z->id, z->shape.bbox(), z->validity, now_ms_);
  }
  return true;
}

bool Platform::load_roads_file(const std::string& path, std::string* err) {
  graph::RoadGraph g;
  if (!g.load_file(path, err)) return false;
  roads_ = std::move(g);
  pool_.snap_all(roads_);
  return true;
}

void Platform::use_grid_roads(int rows, int cols) {
  geo::Bbox area = geo::Bbox::empty();
  for (ZoneId id : zones_.all_ids()) area.expand(zones_.get(id)->shape.bbox());
  if (!(area.max_lat > area.min_lat)) area = {25.50, 91.80, 25.62, 91.95};
  roads_ = graph::RoadGraph::grid(area, rows, cols, 7);
  pool_.snap_all(roads_);
}

// ─── Log ────────────────────────────────────────────────────────────────────

LogEntry& Platform::append_log(LogEntry e) {
  e.seq = log_.size();
  if (e.recorded_ms == 0) e.recorded_ms = now_ms_;
  if (e.stamp.counter == 0) {                  // a server-originated entry
    clock_.tick();
    e.stamp = clock_;
  }
  e.evidence_index = merkle_.append(canonical(e));
  log_.push_back(std::move(e));
  return log_.back();
}

void Platform::audit_access(const Principal& by, UserId subject, const std::string& what,
                            bool allowed) {
  if (by.system) return;
  if (by.id == subject && allowed) return;      // a user reading their own data
  // The same person re-reading the same thing within a minute (a page that
  // refreshes while open) is one access, not thirty.
  for (size_t i = access_.size(), seen = 0; i-- > 0 && seen < 64; ++seen) {
    const AccessRecord& p = access_[i];
    if (now_ms_ - p.t_ms > 60'000) break;
    if (p.actor == by.id && p.subject == subject && p.what == what && p.allowed == allowed) return;
  }
  AccessRecord r{now_ms_, by.id, subject, what, allowed};
  access_merkle_.append("access-v1|" + std::to_string(r.t_ms) + "|" + std::to_string(r.actor) +
                        "|" + std::to_string(r.subject) + "|" + r.what + "|" +
                        (allowed ? "allowed" : "denied"));
  access_.push_back(std::move(r));
}

// ─── Users ──────────────────────────────────────────────────────────────────

bool Platform::is_staff(const Principal& by) const {
  return by.system || by.role == Role::Admin || by.role == Role::Operator;
}

User* Platform::user_mut(UserId u) { return u < users_.size() ? &users_[u] : nullptr; }
const User* Platform::user(UserId u) const { return u < users_.size() ? &users_[u] : nullptr; }

const User* Platform::user_by_name(const std::string& username) const {
  const UserId* id = by_name_.get(username);
  return id ? user(*id) : nullptr;
}

Tracked& Platform::tracked_mut(UserId u) {
  if (tracked_.size() <= u) tracked_.resize(size_t(u) + 1);
  if (!tracked_[u]) {
    tracked_[u] = std::make_unique<Tracked>();
    tracked_[u]->tourist.id = u;
    tracked_[u]->tourist.digital_id = "ST-" + std::to_string(u);
  }
  return *tracked_[u];
}

const Tracked* Platform::tracked(UserId u) const {
  return u < tracked_.size() ? tracked_[u].get() : nullptr;
}

Outcome Platform::create_user(const Principal& by, const std::string& username,
                              const std::string& display_name, Role role,
                              const std::string& password, Source src) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator can create users");
  if (users_.size() >= cfg_.max_users) return Outcome::fail(409, "user limit reached");
  if (!valid_username(username))
    return Outcome::fail(400, "username: 3-32 characters, lower-case letters, digits, . _ -");
  if (!valid_label(display_name, 64)) return Outcome::fail(400, "display name: 1-64 printable characters");
  if (by_name_.contains(username)) return Outcome::fail(409, "username already taken");
  if (src == Source::Api && password.size() < 10)
    return Outcome::fail(400, "password must be at least 10 characters");
  if (password.size() > 256) return Outcome::fail(400, "password too long");

  User u;
  u.id = UserId(users_.size());
  u.username = username;
  u.display_name = display_name;
  u.role = role;
  u.source = src;
  u.created_ms = now_ms_;
  // Simulated users never sign in; they get no password at all, so there is no
  // demo credential to leak for them.
  if (!password.empty()) u.password_hash = hash_password(password, cfg_.pbkdf2_iterations);
  by_name_.put(username, u.id);
  users_.push_back(u);
  if (role == Role::Tourist) tracked_mut(u.id);

  LogEntry e;
  e.kind = LogKind::UserCreated; e.t_ms = now_ms_; e.user = u.id; e.actor = by.id;
  e.detail = std::string(to_string(role)) + " " + to_string(src);
  append_log(std::move(e));
  return Outcome::good(u.id);
}

const User* Platform::authenticate(const std::string& username, const std::string& password) const {
  const User* u = user_by_name(username);
  // Run the full derivation even for an unknown user, so response time does not
  // say whether the username exists. The dummy must cost what a real hash
  // costs: it once had 1 iteration against 120,000, which made unknown names
  // answer ~100,000 times faster.
  if (dummy_hash_.empty()) dummy_hash_ = hash_password("not anyone's password", cfg_.pbkdf2_iterations);
  const bool ok = verify_password(password, u && !u->password_hash.empty() ? u->password_hash : dummy_hash_);
  if (!u || !ok || !u->active || u->password_hash.empty()) return nullptr;
  return u;
}

Outcome Platform::set_password(const Principal& by, UserId u, const std::string& password) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.role == Role::Admin || by.id == u)) return Outcome::fail(403, "not allowed");
  if (password.size() < 10 || password.size() > 256) return Outcome::fail(400, "password must be 10-256 characters");
  usr->password_hash = hash_password(password, cfg_.pbkdf2_iterations);
  return Outcome::good(u);
}

Outcome Platform::set_active(const Principal& by, UserId u, bool active) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator can change activation");
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!active && usr->sharing) {
    Principal sys = Principal::sys();
    stop_sharing(sys, u);
  }
  usr->active = active;
  LogEntry e;
  e.kind = LogKind::UserUpdated; e.t_ms = now_ms_; e.user = u; e.actor = by.id;
  e.detail = active ? "activated" : "deactivated";
  append_log(std::move(e));
  return Outcome::good(u);
}

Outcome Platform::set_display_name(const Principal& by, UserId u, const std::string& name) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.role == Role::Admin || by.id == u)) return Outcome::fail(403, "not allowed");
  if (!valid_label(name, 64)) return Outcome::fail(400, "display name: 1-64 printable characters");
  usr->display_name = name;
  return Outcome::good(u);
}

Outcome Platform::set_consent(const Principal& by, UserId u, bool consent) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  // Consent is the user's alone. Not even an administrator grants it for them.
  if (!(by.system || by.id == u)) return Outcome::fail(403, "only the user can give or withdraw consent");
  if (usr->role != Role::Tourist) return Outcome::fail(400, "only tourists are tracked");
  if (usr->consent == consent) return Outcome::good(u);
  if (!consent && usr->sharing) stop_sharing(by, u);
  usr->consent = consent;
  usr->consent_ms = now_ms_;
  LogEntry e;
  e.kind = consent ? LogKind::ConsentGranted : LogKind::ConsentWithdrawn;
  e.t_ms = now_ms_; e.user = u; e.actor = by.id;
  append_log(std::move(e));
  return Outcome::good(u);
}

Outcome Platform::start_sharing(const Principal& by, UserId u) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.id == u)) return Outcome::fail(403, "only the user can start sharing their location");
  if (usr->role != Role::Tourist) return Outcome::fail(400, "only tourists are tracked");
  if (!usr->active) return Outcome::fail(409, "account is deactivated");
  if (!usr->consent) return Outcome::fail(409, "consent has not been given");
  if (usr->sharing) return Outcome::good(u);
  usr->sharing = true;
  ++usr->sessions;
  usr->session_started_ms = now_ms_;
  Tracked& tr = tracked_mut(u);
  // A new session starts with no engine state: whatever the user was inside of
  // last time is not known to still hold.
  tr.tourist.zone_states.clear();
  tr.tourist.pings.clear();
  tr.tourist.sampler = power::AdaptiveSampler{};
  tr.has_fix = false;
  tr.last_seq = 0;
  tr.anomaly = tr.anomaly_raw = track::AnomalyKind::None;
  tr.anomaly_raw_since = now_ms_;
  LogEntry e;
  e.kind = LogKind::SessionStarted; e.t_ms = now_ms_; e.user = u; e.actor = by.id;
  e.detail = "session " + std::to_string(usr->sessions);
  append_log(std::move(e));
  return Outcome::good(u);
}

Outcome Platform::stop_sharing(const Principal& by, UserId u) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.id == u)) return Outcome::fail(403, "only the user can stop sharing");
  if (!usr->sharing) return Outcome::good(u);
  usr->sharing = false;
  usr->session_ended_ms = now_ms_;
  Tracked& tr = tracked_mut(u);
  tr.session_time_ms += now_ms_ - usr->session_started_ms;
  // Zone state is discarded, not exited: the system no longer knows where the
  // user is, and inventing a ZoneExit would put a false event in the evidence.
  tr.tourist.zone_states.clear();
  if (u < devices_.size()) devices_[u].reset();
  usr->connected = true;
  LogEntry e;
  e.kind = LogKind::SessionStopped; e.t_ms = now_ms_; e.user = u; e.actor = by.id;
  append_log(std::move(e));
  for (auto& g : groups_)
    if (std::find(g.members.begin(), g.members.end(), u) != g.members.end()) rebuild_monitor(g);
  return Outcome::good(u);
}

Outcome Platform::set_battery_aware(const Principal& by, UserId u, bool on) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.id == u || by.role == Role::Admin)) return Outcome::fail(403, "not allowed");
  usr->battery_aware = on;
  return Outcome::good(u);
}

Outcome Platform::erase_history(const Principal& by, UserId u) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.id == u || by.role == Role::Admin)) return Outcome::fail(403, "not allowed");
  Tracked& tr = tracked_mut(u);
  const size_t n = tr.trail.size();
  tr.trail.clear();
  tr.trail.shrink_to_fit();
  // Positions on CLOSED alerts are redacted too. Open alerts keep theirs: a
  // responder may be on the way to that point, and erasing it would endanger
  // the person the data exists to protect.
  size_t redacted = 0;
  if (u < alerts_by_user_.size())
    for (AlertId id : alerts_by_user_[u]) {
      AlertRecord& r = alerts_[id];
      if (alert_is_open(r.a.state) || r.redacted) continue;
      r.a.position = {};
      r.redacted = true;
      ++redacted;
    }
  LogEntry e;
  e.kind = LogKind::HistoryErased; e.t_ms = now_ms_; e.user = u; e.actor = by.id;
  e.detail = std::to_string(n) + " trail points, " + std::to_string(redacted) + " alert positions";
  append_log(std::move(e));
  return Outcome::good(u);
}

// ─── Ingestion ──────────────────────────────────────────────────────────────

FixResult Platform::ingest_fix(const Principal& by, UserId u, const FixInput& in) {
  FixResult res;
  const User* usr = user(u);
  if (!usr) { res.status = FixStatus::UnknownUser; res.reason = "no such user"; return res; }
  if (!(by.system || by.id == u)) {
    res.status = FixStatus::NotTracking; res.reason = "a fix can only come from the user's own device";
    ++stats_.fixes_refused; return res;
  }
  if (usr->role != Role::Tourist || !usr->active || !usr->consent || !usr->sharing) {
    // Not stored anywhere: without consent and an open session there is no
    // basis to keep the position at all.
    res.status = FixStatus::NotTracking;
    res.reason = !usr->consent ? "no consent" : !usr->sharing ? "no active tracking session" : "not trackable";
    ++stats_.fixes_refused; return res;
  }
  if (is_offline(u)) {
    res.status = FixStatus::Offline;
    res.reason = "device is offline; its fixes are queued on the device until it reconnects";
    return res;
  }
  Tracked& tr = tracked_mut(u);
  const Timestamp t = in.t_ms < 0 ? now_ms_ : in.t_ms;
  geo::UncertainPoint p;
  p.pos = {in.lat, in.lon};
  p.accuracy_m = in.accuracy_m;
  p.t_ms = t;
  if (!std::isfinite(in.lat) || !std::isfinite(in.lon) || !p.pos.valid() ||
      !std::isfinite(in.accuracy_m) || in.accuracy_m <= 0 || in.accuracy_m > 5000) {
    res.status = FixStatus::Invalid; res.reason = "coordinates or accuracy out of range";
    ++tr.invalid; ++stats_.fixes_invalid; return res;
  }
  if (t > now_ms_ + cfg_.max_future_skew_ms) {
    res.status = FixStatus::Invalid; res.reason = "timestamp is in the future";
    ++tr.invalid; ++stats_.fixes_invalid; return res;
  }
  if (t < usr->session_started_ms) {
    res.status = FixStatus::Invalid; res.reason = "timestamp is before the tracking session began";
    ++tr.invalid; ++stats_.fixes_invalid; return res;
  }

  if (tr.has_fix) {
    const geo::LatLon& lp = tr.tourist.last_fix.pos;
    const bool same_pos = std::fabs(lp.lat - in.lat) < 1e-12 && std::fabs(lp.lon - in.lon) < 1e-12;
    if ((in.seq != 0 && in.seq == tr.last_seq) || (t == tr.last_fix_ms && same_pos)) {
      res.status = FixStatus::Duplicate; res.reason = "already received";
      ++tr.duplicates; ++stats_.fixes_duplicate; return res;
    }
    if (t < tr.last_fix_ms || (in.seq != 0 && tr.last_seq != 0 && in.seq < tr.last_seq)) {
      // Recorded, flagged, NOT evaluated -- see the header note on ordering.
      TrailPoint tp{t, in.lat, in.lon, in.accuracy_m, kTrailLate, now_ms_};
      auto it = std::upper_bound(tr.trail.begin(), tr.trail.end(), t,
                                 [](Timestamp v, const TrailPoint& q) { return v < q.t_ms; });
      tr.trail.insert(it, tp);
      res.status = FixStatus::OutOfOrder;
      res.reason = "older than the last accepted fix; recorded in history, not evaluated";
      ++tr.out_of_order; ++stats_.fixes_out_of_order; return res;
    }
    const double dt_s = double(t - tr.last_fix_ms) / 1000.0;
    const double d = geo::distance_m(lp, p.pos);
    if ((dt_s > 0 && d / dt_s > cfg_.jump_speed_mps) || (dt_s <= 0 && d > 50.0)) {
      res.jump = true; ++tr.jumps; ++stats_.jumps;
    }
  }

  uint8_t flags = 0;
  if (res.jump) flags |= kTrailJump;
  if (!p.usable()) flags |= kTrailNoisy;
  tr.trail.push_back({t, in.lat, in.lon, in.accuracy_m, flags, now_ms_});
  if (tr.trail.size() > cfg_.trail_max_points)
    tr.trail.erase(tr.trail.begin(), tr.trail.begin() + long(tr.trail.size() - cfg_.trail_max_points));

  std::vector<fence::Event> evs;
  evaluate_fix(u, tr, p, evs);
  tr.has_fix = true;
  tr.last_fix_ms = t;
  if (in.seq) tr.last_seq = in.seq;
  ++tr.accepted;
  ++tr.fixes_sent;
  ++stats_.fixes_accepted;

  // Contact restored: an open "signal lost" alert has answered itself.
  if (tr.signal_alert != kNoId && tr.signal_alert < alerts_.size()) {
    AlertRecord& r = alerts_[tr.signal_alert];
    if (alert_is_open(r.a.state)) {
      r.a.note = "contact restored";
      set_alert_state(r, alert::AlertState::Resolved, kNoId);
    }
    tr.signal_alert = kNoId;
  }
  // Only a LOST signal is answered by a fix. A stationary or deviating user
  // who keeps sending fixes is still stationary or deviating; detect_anomalies
  // clears those when the readings say so.
  if (tr.anomaly == track::AnomalyKind::SignalLost) tr.anomaly = track::AnomalyKind::None;

  apply_events(u, evs, false, now_ms_);
  res.status = FixStatus::Accepted;
  res.events = evs.size();
  return res;
}

void Platform::evaluate_fix(UserId u, Tracked& tr, const geo::UncertainPoint& fix,
                            std::vector<fence::Event>& out) {
  (void)u;
  track::Tourist& t = tr.tourist;
  t.last_fix = fix;
  track::Ping ping{};
  ping.fix = fix;
  t.pings.push(ping);
  const auto t0 = std::chrono::steady_clock::now();
  eval_->evaluate(t, fix.t_ms, out);
  stats_.eval_us_total += elapsed_us(t0);
  ++stats_.eval_calls;
}

// Where the user was at time t, from the trail (ignoring when it was received).
// For a live fix this is the fix just appended; for an event synced from an
// offline device it is the device's own fix that produced the event, which was
// inserted into the trail just before it.
geo::LatLon Platform::position_near(const Tracked& tr, Timestamp t) const {
  if (tr.trail.empty()) return tr.tourist.last_fix.pos;
  auto it = std::upper_bound(tr.trail.begin(), tr.trail.end(), t,
                             [](Timestamp v, const TrailPoint& q) { return v < q.t_ms; });
  if (it == tr.trail.begin()) return {it->lat, it->lon};
  --it;
  return {it->lat, it->lon};
}

void Platform::apply_events(UserId u, const std::vector<fence::Event>& evs, bool late,
                            Timestamp recorded, const sync::LamportClock* device_stamp) {
  std::vector<AlertId> fresh;
  const User* usr = user(u);
  for (const auto& ev : evs) {
    ++stats_.transitions;
    LogEntry e;
    e.kind = log_kind(ev.kind);
    e.t_ms = ev.t_ms; e.recorded_ms = recorded; e.user = u; e.zone = ev.zone; e.late = late;
    // A device's event keeps the device's Lamport stamp: its causal position is
    // where the device put it, not where the server happened to read it.
    if (device_stamp) e.stamp = *device_stamp;
    e.detail = std::string(geo::to_string(ev.containment)) + " depth " +
               std::to_string(int(std::lround(ev.depth_m))) + " m, accuracy " +
               std::to_string(int(std::lround(ev.accuracy_m))) + " m";
    if (ev.kind == fence::EventKind::ZoneApproaching)
      e.detail += ", eta " + std::to_string(int(std::lround(ev.eta_s))) + " s";
    append_log(std::move(e));

    if (ev.kind == fence::EventKind::ZoneExit) {           // exits are history, not alerts
      settle_warnings(u, ev.zone, "left the zone");
      continue;
    }
    if (ev.kind == fence::EventKind::ZoneEnter) settle_warnings(u, ev.zone, "superseded by entry");
    const fence::Zone* z = zones_.get(ev.zone);
    if (!z || z->kind == fence::ZoneKind::Safe || z->synthetic) continue;
    // Warnings are only worth an operator's attention for serious hazards and
    // imminent crossings. A standing tourist's GPS drift reads as a slow walk,
    // and the predictor duly forecasts crossings into every advisory polygon in
    // range; those stay in the event log and on the user's own panel, but they
    // do not become alerts.
    if (ev.kind == fence::EventKind::ZoneApproaching &&
        (z->severity < cfg_.warning_min_severity || ev.eta_s > cfg_.approach_alert_eta_s))
      continue;
    if (ev.kind == fence::EventKind::ZoneUncertain && z->severity < cfg_.warning_min_severity) continue;
    alert::Alert a{};
    a.kind = ev.kind == fence::EventKind::ZoneApproaching ? alert::AlertKind::ZoneApproaching
           : ev.kind == fence::EventKind::ZoneUncertain   ? alert::AlertKind::ZoneUncertain
           : ev.kind == fence::EventKind::DwellExceeded   ? alert::AlertKind::DwellExceeded
                                                          : alert::AlertKind::ZoneBreach;
    a.severity = z->severity;
    a.tourist = u;
    a.zone = ev.zone;
    a.position = tracked(u) ? position_near(*tracked(u), ev.t_ms) : geo::LatLon{};
    a.accuracy_m = ev.accuracy_m;
    a.raised_ms = ev.t_ms;
    a.eta_s = ev.eta_s;
    a.note = z->name;
    fresh.push_back(raise_alert(a, usr ? usr->group : kNoId, late,
                                usr ? usr->source : Source::Api));
  }
  correlate(fresh);
}

// ─── Alerts ─────────────────────────────────────────────────────────────────

AlertId Platform::raise_alert(alert::Alert a, GroupId g, bool late, Source src) {
  a.id = AlertId(alerts_.size());
  a.state = alert::AlertState::Open;
  AlertRecord r;
  r.a = a;
  r.group = g;
  r.original_severity = a.severity;
  r.late = late;
  r.source = src;
  r.history.emplace_back(now_ms_, alert::AlertState::Open);
  alerts_.push_back(std::move(r));
  if (a.tourist != kNoId) {
    if (alerts_by_user_.size() <= a.tourist) alerts_by_user_.resize(size_t(a.tourist) + 1);
    alerts_by_user_[a.tourist].push_back(a.id);
  }
  // Only alerts an operator must act on are put on the escalation clock;
  // approaching/uncertain warnings settle themselves (settle_warnings).
  if (incident_worthy(alerts_.back(), cfg_.incident_min_breach_severity))
    escalation_->arm(a.id, std::max(a.raised_ms, now_ms_));
  ++stats_.alerts;
  if (a.tourist != kNoId && a.tourist < tracked_.size() && tracked_[a.tourist] && a.severity >= 3)
    tracked_[a.tourist]->tourist.alert_active = true;   // the sampler goes to full rate

  LogEntry e;
  e.kind = LogKind::AlertRaised; e.t_ms = a.raised_ms; e.user = a.tourist; e.zone = a.zone;
  e.alert = a.id; e.group = g; e.late = late;
  e.detail = std::string(alert::to_string(a.kind)) + " severity " + std::to_string(int(a.severity));
  append_log(std::move(e));
  return a.id;
}

void Platform::set_alert_state(AlertRecord& r, alert::AlertState s, UserId by) {
  if (r.a.state == s) return;
  r.a.state = s;
  r.acted_by = by;
  r.history.emplace_back(now_ms_, s);
  if (s != alert::AlertState::Open) escalation_->acknowledge(r.a.id);
  if (s == alert::AlertState::Acknowledged) r.a.acknowledged_ms = now_ms_;
  LogKind k = LogKind::AlertAcknowledged;
  if (s == alert::AlertState::Resolved) k = LogKind::AlertResolved;
  else if (s == alert::AlertState::Dismissed) k = LogKind::AlertDismissed;
  else if (s == alert::AlertState::Dispatched) k = LogKind::AlertAcknowledged;
  LogEntry e;
  e.kind = k; e.t_ms = now_ms_; e.user = r.a.tourist; e.alert = r.a.id; e.actor = by;
  e.incident = r.a.incident; e.detail = alert::to_string(s);
  if (!r.a.note.empty() && (s == alert::AlertState::Resolved)) e.detail += ": " + r.a.note;
  append_log(std::move(e));

  // The sampler leaves full rate once no serious alert is open for the user.
  if (r.a.tourist != kNoId && r.a.tourist < tracked_.size() && tracked_[r.a.tourist]) {
    bool any = false;
    if (r.a.tourist < alerts_by_user_.size())
      for (AlertId id : alerts_by_user_[r.a.tourist]) {
        const auto& o = alerts_[id].a;
        if (alert_is_open(o.state) && o.severity >= 3) { any = true; break; }
      }
    tracked_[r.a.tourist]->tourist.alert_active = any;
  }
}

Outcome Platform::acknowledge_alert(const Principal& by, AlertId a) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators acknowledge alerts");
  if (a >= alerts_.size()) return Outcome::fail(404, "no such alert");
  AlertRecord& r = alerts_[a];
  if (r.a.state != alert::AlertState::Open) return Outcome::fail(409, "alert is not open");
  set_alert_state(r, alert::AlertState::Acknowledged, by.id);
  return Outcome::good(a);
}

Outcome Platform::resolve_alert(const Principal& by, AlertId a, bool dismiss) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators resolve alerts");
  if (a >= alerts_.size()) return Outcome::fail(404, "no such alert");
  AlertRecord& r = alerts_[a];
  if (!alert_is_open(r.a.state)) return Outcome::fail(409, "alert is already closed");
  set_alert_state(r, dismiss ? alert::AlertState::Dismissed : alert::AlertState::Resolved, by.id);
  return Outcome::good(a);
}

Outcome Platform::raise_sos(const Principal& by, UserId u) {
  if (!(by.system || by.id == u)) return Outcome::fail(403, "only the user can raise their own SOS");
  const User* usr = user(u);
  const Tracked* tr = tracked(u);
  if (!usr || !tr) return Outcome::fail(404, "no such tourist");
  if (!usr->sharing || !tr->has_fix) return Outcome::fail(409, "SOS needs an active session with a position");
  alert::Alert a{};
  a.kind = alert::AlertKind::SosTriggered;
  a.severity = 5;
  a.tourist = u;
  a.position = tr->tourist.last_fix.pos;
  a.accuracy_m = tr->tourist.last_fix.accuracy_m;
  a.raised_ms = now_ms_;
  a.note = "SOS";
  const AlertId id = raise_alert(a, usr->group, false, usr->source);
  correlate({id});
  return Outcome::good(id);
}

double Platform::nearest_responder_m(const geo::LatLon& p) const {
  double best = 1e9;
  for (const auto& r : pool_.all())
    if (r.available) best = std::min(best, geo::distance_m(p, r.pos));
  return best >= 1e9 ? 50'000.0 : best;
}

std::vector<Platform::Triaged> Platform::triage_order() const {
  alert::TriageQueue q;
  for (const auto& r : alerts_) {
    if (!alert_is_open(r.a.state)) continue;
    alert::Alert a = r.a;
    a.priority = alert::triage_score(a, now_ms_, nearest_responder_m(a.position));
    q.push(a);
  }
  std::vector<Triaged> out;
  out.reserve(q.size());
  while (!q.empty()) {
    const alert::Alert a = q.pop();
    out.push_back({a.id, a.priority});
  }
  return out;
}

// A warning (approaching, uncertain) is a forecast. Once the forecast comes
// true -- the user entered -- or stops applying -- they left, or nothing came
// of it for warning_ttl_ms -- it is resolved by the system, so the open-alert
// board holds only what still needs a decision.
void Platform::settle_warnings(UserId u, ZoneId z, const char* why) {
  if (u >= alerts_by_user_.size()) return;
  for (AlertId id : alerts_by_user_[u]) {
    AlertRecord& r = alerts_[id];
    if (r.a.zone == z && alert_is_open(r.a.state) &&
        (r.a.kind == alert::AlertKind::ZoneApproaching || r.a.kind == alert::AlertKind::ZoneUncertain)) {
      r.a.note = why;
      set_alert_state(r, alert::AlertState::Resolved, kNoId);
    }
  }
}

void Platform::expire_warnings(Timestamp now) {
  for (auto& r : alerts_)
    if (r.a.state == alert::AlertState::Open && now - r.a.raised_ms > cfg_.warning_ttl_ms &&
        (r.a.kind == alert::AlertKind::ZoneApproaching || r.a.kind == alert::AlertKind::ZoneUncertain)) {
      r.a.note = "expired: no entry followed";
      set_alert_state(r, alert::AlertState::Resolved, kNoId);
    }
}

void Platform::escalate(Timestamp now) {
  for (AlertId id : escalation_->advance(now)) {
    if (id >= alerts_.size()) continue;
    AlertRecord& r = alerts_[id];
    if (r.a.state != alert::AlertState::Open) continue;
    r.escalated = true;
    if (r.a.severity < 5) r.a.severity = uint8_t(r.a.severity + 1);
    ++stats_.escalations;
    LogEntry e;
    e.kind = LogKind::AlertEscalated; e.t_ms = now; e.user = r.a.tourist; e.alert = id;
    e.incident = r.a.incident;
    e.detail = "unacknowledged for " + std::to_string(cfg_.escalation_timeout_ms / 1000) +
               " s; severity now " + std::to_string(int(r.a.severity));
    append_log(std::move(e));
  }
}

// ─── Incidents ──────────────────────────────────────────────────────────────


void Platform::correlate(const std::vector<AlertId>& fresh) {
  std::vector<alert::Alert> batch;
  for (AlertId id : fresh)
    if (incident_worthy(alerts_[id], cfg_.incident_min_breach_severity)) batch.push_back(alerts_[id].a);
  if (batch.empty()) return;
  for (IncidentId id : corr_->ingest(batch)) after_incident_touched(id);
}

void Platform::after_incident_touched(IncidentId id) {
  const alert::Incident* inc = corr_->get(id);
  if (!inc) return;
  for (AlertId a : inc->alerts)
    if (a < alerts_.size() && alerts_[a].a.incident == kNoId) alerts_[a].a.incident = id;

  if (cases_.size() <= id) {
    cases_.resize(size_t(id) + 1);
    IncidentCase& c = cases_[id];
    c.id = id;
    c.state = CaseState::Open;
    c.history.emplace_back(now_ms_, CaseState::Open);
    LogEntry e;
    e.kind = LogKind::IncidentOpened; e.t_ms = inc->opened_ms; e.incident = id;
    e.user = inc->affected.empty() ? kNoId : inc->affected.front();
    e.alert = inc->alerts.empty() ? kNoId : inc->alerts.front();
    e.detail = std::to_string(inc->people()) + " people, severity " + std::to_string(int(inc->max_severity));
    append_log(std::move(e));
    recommend(c);
    if (cfg_.auto_dispatch && c.recommended != kNoId) {
      Principal sys = Principal::sys();
      assign_incident(sys, id, c.recommended);
    }
  } else {
    LogEntry e;
    e.kind = LogKind::IncidentUpdated; e.t_ms = inc->last_update_ms; e.incident = id;
    e.detail = std::to_string(inc->alerts.size()) + " alerts, " + std::to_string(inc->people()) + " people";
    append_log(std::move(e));
    if (cases_[id].responder == kNoId && cases_[id].state != CaseState::Resolved) recommend(cases_[id]);
  }
}

// One Dijkstra from the incident's junction gives the road distance to EVERY
// responder at once. That is valid because the loaded road graph is undirected
// (every OSM road in data/osm/roads.txt is stored both ways), so distance from
// the incident to a responder equals distance from the responder to it. The
// route itself is then computed in the true direction, responder -> incident.
void Platform::recommend(IncidentCase& c) {
  c.recommended = kNoId;
  c.recommended_m = 0;
  const alert::Incident* inc = corr_->get(c.id);
  if (!inc || roads_.node_count() == 0) return;
  const graph::NodeId at = roads_.nearest_node(inc->centroid);
  if (at == graph::kNoNode) return;
  const graph::ShortestPaths sp = graph::dijkstra(roads_, at);
  double best = graph::kUnreachable;
  for (size_t i = 0; i < pool_.size(); ++i) {
    const auto& r = pool_[i];
    if (!r.available || r.node == graph::kNoNode) continue;
    const double d = sp.dist[size_t(r.node)];
    if (d < best) { best = d; c.recommended = r.id; }
  }
  if (c.recommended != kNoId) c.recommended_m = best;
}

const IncidentCase* Platform::incident_case(IncidentId i) const {
  return i < cases_.size() && cases_[i].id != kNoId ? &cases_[i] : nullptr;
}

Outcome Platform::acknowledge_incident(const Principal& by, IncidentId i) {
  if (i >= cases_.size() || cases_[i].id == kNoId) return Outcome::fail(404, "no such incident");
  IncidentCase& c = cases_[i];
  const bool own = by.role == Role::Responder && user(by.id) && c.responder != kNoId &&
                   user(by.id)->unit == c.responder;
  if (!is_staff(by) && !own) return Outcome::fail(403, "not allowed");
  if (c.state == CaseState::Resolved) return Outcome::fail(409, "incident is resolved");
  if (c.acknowledged_ms) return Outcome::fail(409, "already acknowledged");
  c.acknowledged_ms = now_ms_;
  c.acknowledged_by = by.id;
  if (c.state == CaseState::Open) {
    c.state = CaseState::Acknowledged;
    c.history.emplace_back(now_ms_, CaseState::Acknowledged);
  }
  if (const alert::Incident* inc = corr_->get(i))
    for (AlertId a : inc->alerts)
      if (a < alerts_.size() && alerts_[a].a.state == alert::AlertState::Open)
        set_alert_state(alerts_[a], alert::AlertState::Acknowledged, by.id);
  LogEntry e;
  e.kind = LogKind::IncidentAcknowledged; e.t_ms = now_ms_; e.incident = i; e.actor = by.id;
  append_log(std::move(e));
  return Outcome::good(i);
}

Route Platform::route_between(geo::LatLon from, geo::LatLon to) const {
  Route r;
  if (roads_.node_count() == 0) return r;
  r.from = roads_.nearest_node(from);
  r.to = roads_.nearest_node(to);
  if (r.from == graph::kNoNode || r.to == graph::kNoNode) return r;
  const graph::AStarResult a = graph::astar(roads_, r.from, r.to);
  r.astar_expanded = a.nodes_expanded;
  r.dijkstra_expanded = graph::dijkstra(roads_, r.from, r.to).nodes_expanded;
  if (!a.found) return r;
  r.found = true;
  r.distance_m = a.cost;
  r.points.reserve(a.path.size());
  for (graph::NodeId n : a.path) r.points.push_back(roads_.pos(n));
  return r;
}

void Platform::free_responder(ResponderId r) {
  if (r >= pool_.size()) return;
  pool_[r].available = true;
  rinfo_[r].assigned = kNoId;
}

Outcome Platform::assign_incident(const Principal& by, IncidentId i, ResponderId r) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators assign responders");
  if (i >= cases_.size() || cases_[i].id == kNoId) return Outcome::fail(404, "no such incident");
  IncidentCase& c = cases_[i];
  if (c.state == CaseState::Resolved) return Outcome::fail(409, "incident is resolved");
  if (r == kNoId) r = c.recommended;
  if (r == kNoId || r >= pool_.size()) return Outcome::fail(409, "no available responder");
  if (r == c.responder) return Outcome::good(i);
  if (!pool_[r].available) return Outcome::fail(409, "that responder is not available");
  const alert::Incident* inc = corr_->get(i);
  if (!inc) return Outcome::fail(404, "no such incident");

  const ResponderId previous = c.responder;
  if (previous != kNoId) free_responder(previous);
  c.responder = r;
  c.route = route_between(pool_[r].pos, inc->centroid);
  pool_[r].available = false;
  rinfo_[r].assigned = i;
  if (c.state != CaseState::Dispatched) {
    c.state = CaseState::Dispatched;
    c.history.emplace_back(now_ms_, CaseState::Dispatched);
  }
  c.dispatched_ms = now_ms_;
  for (AlertId a : inc->alerts)
    if (a < alerts_.size() && alert_is_open(alerts_[a].a.state))
      set_alert_state(alerts_[a], alert::AlertState::Dispatched, by.id);
  LogEntry e;
  e.kind = LogKind::IncidentAssigned; e.t_ms = now_ms_; e.incident = i; e.actor = by.id;
  e.responder = r;
  e.detail = (previous != kNoId ? "reassigned from responder " + std::to_string(previous) + ", " : std::string()) +
             (c.route.found ? "route " + std::to_string(int(std::lround(c.route.distance_m))) + " m"
                            : std::string("no road route"));
  append_log(std::move(e));
  // Other open incidents may have recommended this responder; recompute.
  for (auto& o : cases_)
    if (o.id != kNoId && o.id != i && o.state != CaseState::Resolved && o.responder == kNoId &&
        o.recommended == r)
      recommend(o);
  return Outcome::good(i);
}

Outcome Platform::resolve_incident(const Principal& by, IncidentId i, const std::string& note) {
  if (i >= cases_.size() || cases_[i].id == kNoId) return Outcome::fail(404, "no such incident");
  IncidentCase& c = cases_[i];
  const bool own = by.role == Role::Responder && user(by.id) && c.responder != kNoId &&
                   user(by.id)->unit == c.responder;
  if (!is_staff(by) && !own) return Outcome::fail(403, "not allowed");
  if (c.state == CaseState::Resolved) return Outcome::fail(409, "already resolved");
  if (note.size() > 500) return Outcome::fail(400, "note too long");
  c.state = CaseState::Resolved;
  c.resolved_ms = now_ms_;
  c.resolved_by = by.id;
  c.resolution = note;
  c.history.emplace_back(now_ms_, CaseState::Resolved);
  corr_->close(i, now_ms_);
  if (c.responder != kNoId) free_responder(c.responder);
  if (const alert::Incident* inc = corr_->get(i))
    for (AlertId a : inc->alerts)
      if (a < alerts_.size() && alert_is_open(alerts_[a].a.state)) {
        alerts_[a].a.note = "incident resolved";
        set_alert_state(alerts_[a], alert::AlertState::Resolved, by.id);
      }
  LogEntry e;
  e.kind = LogKind::IncidentResolved; e.t_ms = now_ms_; e.incident = i; e.actor = by.id;
  e.responder = c.responder; e.detail = note;
  append_log(std::move(e));
  for (auto& o : cases_)
    if (o.id != kNoId && o.state != CaseState::Resolved && o.responder == kNoId) recommend(o);
  return Outcome::good(i);
}

Platform::PlanSummary Platform::plan_dispatch(const Principal& by, bool apply) {
  PlanSummary s;
  if (!is_staff(by) || roads_.node_count() == 0) return s;
  std::vector<dispatch::Incident> incs;
  for (const auto& c : cases_) {
    if (c.id == kNoId || c.state == CaseState::Resolved || c.responder != kNoId) continue;
    const alert::Incident* inc = corr_->get(c.id);
    if (!inc) continue;
    incs.push_back({c.id, inc->centroid, graph::kNoNode});
    s.incidents.push_back(c.id);
  }
  for (size_t i : pool_.available_indices()) s.responders.push_back(pool_[i].id);
  if (incs.empty() || s.responders.empty()) return s;
  dispatch::snap_incidents(incs, roads_);
  s.greedy = dispatch::assign_greedy(pool_, incs, roads_);
  s.optimal = dispatch::assign_optimal(pool_, incs, roads_);
  if (apply) {
    const auto plan = s.optimal.dispatches;
    for (const auto& d : plan)
      if (d.travel_m < dispatch::kUnreachableCost) assign_incident(by, d.incident, d.responder);
  }
  return s;
}

// ─── Responders ─────────────────────────────────────────────────────────────

Outcome Platform::create_responder(const Principal& by, const std::string& name,
                                   const std::string& type, geo::LatLon pos, Source src,
                                   UserId account) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator can register responders");
  if (!valid_label(name, 64) || !valid_label(type, 32)) return Outcome::fail(400, "name and type required");
  if (!pos.valid()) return Outcome::fail(400, "position out of range");
  if (account != kNoId) {
    User* a = user_mut(account);
    if (!a || a->role != Role::Responder) return Outcome::fail(400, "account must be a responder account");
  }
  dispatch::Responder r;
  r.pos = pos;
  r.name = name;
  r.available = true;
  r.node = roads_.node_count() ? roads_.nearest_node(pos) : graph::kNoNode;
  const ResponderId id = pool_.add(r);
  ResponderInfo info;
  info.unit_type = type;
  info.source = src;
  info.account = account;
  info.position_ms = now_ms_;
  rinfo_.push_back(info);
  if (account != kNoId) user_mut(account)->unit = id;
  LogEntry e;
  e.kind = LogKind::ResponderCreated; e.t_ms = now_ms_; e.responder = id; e.actor = by.id;
  e.detail = name + " (" + type + ", " + to_string(src) + ")";
  append_log(std::move(e));
  for (auto& c : cases_)
    if (c.id != kNoId && c.state != CaseState::Resolved && c.responder == kNoId) recommend(c);
  return Outcome::good(id);
}

Outcome Platform::set_responder_available(const Principal& by, ResponderId r, bool available) {
  if (r >= pool_.size()) return Outcome::fail(404, "no such responder");
  const bool own = by.role == Role::Responder && user(by.id) && user(by.id)->unit == r;
  if (!is_staff(by) && !own) return Outcome::fail(403, "not allowed");
  if (rinfo_[r].assigned != kNoId && available)
    return Outcome::fail(409, "responder is assigned to an incident; resolve or reassign it first");
  pool_[r].available = available;
  LogEntry e;
  e.kind = LogKind::ResponderUpdated; e.t_ms = now_ms_; e.responder = r; e.actor = by.id;
  e.detail = available ? "available" : "unavailable";
  append_log(std::move(e));
  for (auto& c : cases_)
    if (c.id != kNoId && c.state != CaseState::Resolved && c.responder == kNoId) recommend(c);
  return Outcome::good(r);
}

Outcome Platform::set_responder_position(const Principal& by, ResponderId r, geo::LatLon pos) {
  if (r >= pool_.size()) return Outcome::fail(404, "no such responder");
  const bool own = by.role == Role::Responder && user(by.id) && user(by.id)->unit == r;
  if (!(by.system || by.role == Role::Admin) && !own) return Outcome::fail(403, "not allowed");
  if (!pos.valid()) return Outcome::fail(400, "position out of range");
  pool_[r].pos = pos;
  pool_[r].node = roads_.node_count() ? roads_.nearest_node(pos) : graph::kNoNode;
  rinfo_[r].position_ms = now_ms_;
  return Outcome::good(r);
}

const ResponderInfo* Platform::responder_info(ResponderId r) const {
  return r < rinfo_.size() ? &rinfo_[r] : nullptr;
}

// ─── Access ─────────────────────────────────────────────────────────────────

bool Platform::may_view_location(const Principal& by, UserId subject) const {
  if (by.system || by.id == subject) return true;
  if (by.role == Role::Admin || by.role == Role::Operator) return true;
  if (by.role == Role::Responder) {
    const User* me = user(by.id);
    if (!me || me->unit == kNoId) return false;
    for (const auto& c : cases_) {
      if (c.id == kNoId || c.state == CaseState::Resolved || c.responder != me->unit) continue;
      const alert::Incident* inc = corr_->get(c.id);
      if (inc && std::find(inc->affected.begin(), inc->affected.end(), subject) != inc->affected.end())
        return true;
    }
  }
  return false;
}

// ─── Zone status ────────────────────────────────────────────────────────────

std::vector<Platform::ZoneStatus> Platform::zone_status(UserId u, size_t nearest) const {
  std::vector<ZoneStatus> out;
  const Tracked* tr = tracked(u);
  if (!tr || !tr->has_fix) return out;
  const geo::LatLon p = tr->tourist.last_fix.pos;
  for (const auto& st : tr->tourist.zone_states) {
    const fence::Zone* z = zones_.get(st.zone);
    if (!z || st.confirmed == geo::Containment::Outside) continue;
    out.push_back({st.zone, st.confirmed, geo::signed_distance_m(z->shape, p)});
  }
  std::vector<ZoneId> cand;
  index_->query(geo::Bbox::around(p, 3000.0), cand);
  std::vector<ZoneStatus> near;
  for (ZoneId id : cand) {
    const fence::Zone* z = zones_.get(id);
    if (!z || z->synthetic) continue;
    bool listed = false;
    for (const auto& o : out) if (o.zone == id) { listed = true; break; }
    if (listed) continue;
    near.push_back({id, geo::Containment::Outside, geo::signed_distance_m(z->shape, p)});
  }
  std::sort(near.begin(), near.end(), [](const ZoneStatus& a, const ZoneStatus& b) {
    return a.distance_m != b.distance_m ? a.distance_m < b.distance_m : a.zone < b.zone;
  });
  for (size_t i = 0; i < near.size() && i < nearest; ++i) out.push_back(near[i]);
  return out;
}

std::string Platform::status_label(UserId u) const {
  const User* usr = user(u);
  const Tracked* tr = tracked(u);
  if (!usr || usr->role != Role::Tourist) return "staff";
  if (!usr->sharing) return usr->consent ? "not sharing" : "no consent";
  if (!tr || !tr->has_fix) return "awaiting fix";
  if (tr->anomaly == track::AnomalyKind::SignalLost) return "no contact";
  std::string worst = "clear";
  int rank = 0;
  for (const auto& st : tr->tourist.zone_states) {
    const fence::Zone* z = zones_.get(st.zone);
    if (!z) continue;
    if (st.confirmed == geo::Containment::Inside) {
      const int r = z->kind == fence::ZoneKind::Restricted ? 5 : z->kind == fence::ZoneKind::Caution ? 4 : 2;
      if (r > rank) { rank = r; worst = std::string("inside ") + fence::to_string(z->kind); }
    } else if (st.confirmed == geo::Containment::Uncertain && rank < 3) {
      rank = 3; worst = "uncertain";
    } else if (st.approach_reported && rank < 1) {
      rank = 1; worst = "approaching";
    }
  }
  for (const auto& g : groups_)
    for (const auto& s : g.stragglers)
      if (s.user == u && rank < 3) { rank = 3; worst = "separated"; }
  if (tr->anomaly == track::AnomalyKind::Stationary && rank < 3) worst = "stationary";
  return worst;
}

size_t Platform::users_inside(ZoneId z) const {
  size_t n = 0;
  for (size_t i = 0; i < tracked_.size(); ++i) {
    const Tracked* tr = tracked_[i].get();
    if (!tr || !users_[i].sharing) continue;
    if (const track::ZoneState* st = tr->tourist.peek_state(z))
      if (st->confirmed != geo::Containment::Outside) ++n;
  }
  return n;
}

// ─── Anomalies, retention ───────────────────────────────────────────────────

void Platform::detect_anomalies(Timestamp now) {
  track::AnomalyConfig acfg;
  acfg.stationary_window_ms = 50'000;
  acfg.stationary_radius_m = 30.0;
  std::vector<AlertId> fresh;
  for (size_t i = 0; i < tracked_.size(); ++i) {
    Tracked* tr = tracked_[i].get();
    if (!tr || !users_[i].sharing || !tr->has_fix) continue;
    // A battery-aware device is EXPECTED to be quiet for its sampling interval;
    // without this a five-minute interval reads as a lost signal every time.
    acfg.signal_gap_ms = 120'000;
    if (users_[i].battery_aware)
      acfg.signal_gap_ms = std::max<int64_t>(acfg.signal_gap_ms,
                                             tr->tourist.sampler.current_interval_ms() * 5 / 2);
    track::AnomalyResult ar = track::detect(tr->tourist, now, acfg);
    // Standing still next to your own companions is resting, not an injury: a
    // stationary reading counts only when no other member of the user's group
    // is within the group's proximity distance.
    if (ar.kind == track::AnomalyKind::Stationary && users_[i].group != kNoId &&
        users_[i].group < groups_.size()) {
      const GroupRecord& g = groups_[users_[i].group];
      for (UserId m : g.members) {
        const Tracked* o = tracked(m);
        if (m == i || !o || !o->has_fix || !users_[m].sharing) continue;
        if (geo::distance_m(o->tourist.last_fix.pos, tr->tourist.last_fix.pos) <= g.cfg.proximity_m) {
          ar.kind = track::AnomalyKind::None;
          break;
        }
      }
    }
    if (ar.kind != tr->anomaly_raw) { tr->anomaly_raw = ar.kind; tr->anomaly_raw_since = now; }
    const int64_t need = ar.kind == track::AnomalyKind::SignalLost ? 0
                       : ar.kind == track::AnomalyKind::Stationary ? cfg_.stationary_confirm_ms
                                                                   : cfg_.anomaly_confirm_ms;
    if (now - tr->anomaly_raw_since < need || ar.kind == tr->anomaly) continue;
    tr->anomaly = ar.kind;
    if (ar.kind == track::AnomalyKind::None) continue;
    alert::Alert a{};
    a.kind = ar.kind == track::AnomalyKind::SignalLost ? alert::AlertKind::SignalLost
           : ar.kind == track::AnomalyKind::Stationary ? alert::AlertKind::Stationary
                                                       : alert::AlertKind::RouteDeviation;
    a.severity = 3;
    a.tourist = UserId(i);
    a.position = tr->tourist.last_fix.pos;
    a.accuracy_m = tr->tourist.last_fix.accuracy_m;
    a.raised_ms = now;
    a.note = ar.kind == track::AnomalyKind::SignalLost
                 ? "no fix for " + std::to_string(int(ar.value / 1000.0)) + " s"
                 : "moved under " + std::to_string(int(acfg.stationary_radius_m)) + " m in " +
                       std::to_string(acfg.stationary_window_ms / 1000) + " s";
    const AlertId id = raise_alert(a, users_[i].group, false, users_[i].source);
    if (ar.kind == track::AnomalyKind::SignalLost) tr->signal_alert = id;
    fresh.push_back(id);
  }
  correlate(fresh);
}

void Platform::enforce_retention(Timestamp now) {
  const Timestamp cutoff = now - cfg_.trail_retention_ms;
  for (auto& p : tracked_) {
    if (!p || p->trail.empty()) continue;
    auto it = std::lower_bound(p->trail.begin(), p->trail.end(), cutoff,
                               [](const TrailPoint& q, Timestamp v) { return q.t_ms < v; });
    p->trail.erase(p->trail.begin(), it);
  }
}

// ─── Groups ─────────────────────────────────────────────────────────────────

void Platform::rebuild_monitor(GroupRecord& g) {
  g.monitor = std::make_unique<group::CohesionMonitor>(g.cfg);
  group::DeclaredGroup dg;
  dg.id = g.id;
  dg.label = g.name;
  for (UserId m : g.members) dg.members.push_back(m);
  g.monitor->declare_group(std::move(dg));
  g.fragmented = false;
  g.stragglers.clear();
}

Outcome Platform::create_group(const Principal& by, const std::string& name, Source src) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators create groups");
  if (!valid_label(name, 64)) return Outcome::fail(400, "group name: 1-64 printable characters");
  GroupRecord g;
  g.id = GroupId(groups_.size());
  g.name = name;
  g.source = src;
  g.created_ms = now_ms_;
  rebuild_monitor(g);
  groups_.push_back(std::move(g));
  LogEntry e;
  e.kind = LogKind::GroupCreated; e.t_ms = now_ms_; e.group = groups_.back().id; e.actor = by.id;
  e.detail = name;
  append_log(std::move(e));
  return Outcome::good(groups_.back().id);
}

Outcome Platform::add_member(const Principal& by, GroupId gid, UserId u) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators manage groups");
  if (gid >= groups_.size()) return Outcome::fail(404, "no such group");
  User* usr = user_mut(u);
  if (!usr || usr->role != Role::Tourist) return Outcome::fail(400, "only tourists join groups");
  if (usr->group == gid) return Outcome::good(gid);
  if (usr->group != kNoId && usr->group < groups_.size()) remove_member(by, usr->group, u);
  GroupRecord& g = groups_[gid];
  g.members.push_back(u);
  usr->group = gid;
  rebuild_monitor(g);
  LogEntry e;
  e.kind = LogKind::GroupChanged; e.t_ms = now_ms_; e.group = gid; e.user = u; e.actor = by.id;
  e.detail = "joined";
  append_log(std::move(e));
  return Outcome::good(gid);
}

Outcome Platform::remove_member(const Principal& by, GroupId gid, UserId u) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators manage groups");
  if (gid >= groups_.size()) return Outcome::fail(404, "no such group");
  GroupRecord& g = groups_[gid];
  auto it = std::find(g.members.begin(), g.members.end(), u);
  if (it == g.members.end()) return Outcome::fail(404, "not a member");
  g.members.erase(it);
  if (User* usr = user_mut(u)) usr->group = kNoId;
  rebuild_monitor(g);
  LogEntry e;
  e.kind = LogKind::GroupChanged; e.t_ms = now_ms_; e.group = gid; e.user = u; e.actor = by.id;
  e.detail = "left";
  append_log(std::move(e));
  return Outcome::good(gid);
}

Outcome Platform::set_group_thresholds(const Principal& by, GroupId gid, double proximity_m,
                                       double straggler_m, int64_t confirm_ms) {
  if (!is_staff(by)) return Outcome::fail(403, "operators and administrators manage groups");
  if (gid >= groups_.size()) return Outcome::fail(404, "no such group");
  if (!(proximity_m >= 10 && proximity_m <= 5000 && straggler_m >= proximity_m &&
        straggler_m <= 20000 && confirm_ms >= 0 && confirm_ms <= 3600'000))
    return Outcome::fail(400, "need 10 <= proximity <= straggler <= 20000 m and confirm 0..3600 s");
  GroupRecord& g = groups_[gid];
  g.cfg.proximity_m = proximity_m;
  g.cfg.straggler_m = straggler_m;
  g.cfg.confirm_ms = confirm_ms;
  rebuild_monitor(g);
  LogEntry e;
  e.kind = LogKind::GroupChanged; e.t_ms = now_ms_; e.group = gid; e.actor = by.id;
  e.detail = "thresholds " + std::to_string(int(proximity_m)) + "/" + std::to_string(int(straggler_m)) +
             " m, confirm " + std::to_string(confirm_ms / 1000) + " s";
  append_log(std::move(e));
  return Outcome::good(gid);
}

const GroupRecord* Platform::group_rec(GroupId g) const {
  return g < groups_.size() ? &groups_[g] : nullptr;
}

// Cohesion runs per group over that group's own tracked members, so one
// group's state cannot leak into another's, and each group has its own
// thresholds. The monitor reports when a separation BEGINS; this pass measures
// how long it has lasted and raises the alert only once it has outlasted
// confirm_ms -- the rule CohesionConfig::confirm_ms describes, which the
// monitor itself never applied (docs/final/AUDIT.md, A1).
void Platform::run_cohesion() {
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<AlertId> fresh;
  for (auto& g : groups_) {
    std::vector<track::Tourist> ts;
    for (UserId m : g.members) {
      const Tracked* tr = tracked(m);
      const User* usr = user(m);
      if (tr && usr && usr->sharing && tr->has_fix && !is_offline(m)) ts.push_back(tr->tourist);
    }
    g.tracked_members = ts.size();
    if (ts.size() < 2) {
      g.components = ts.size();
      g.has_centre = !ts.empty();
      if (!ts.empty()) g.centre = ts.front().last_fix.pos;
      g.stragglers.clear();
      continue;
    }
    std::vector<group::CohesionEvent> evs;
    g.monitor->update(ts, now_ms_, evs);
    for (const auto& ce : evs) {
      LogEntry e;
      e.t_ms = now_ms_; e.group = g.id; e.user = ce.subject;
      if (ce.kind == group::CohesionEventKind::MemberRejoined) {
        e.kind = LogKind::GroupRejoined;
        e.detail = "group whole again";
        append_log(std::move(e));
        for (auto& r : alerts_)
          if (r.group == g.id && alert_is_open(r.a.state) &&
              (r.a.kind == alert::AlertKind::Straggler || r.a.kind == alert::AlertKind::GroupFragmented)) {
            r.a.note = "member rejoined";
            set_alert_state(r, alert::AlertState::Resolved, kNoId);
          }
      } else if (ce.kind != group::CohesionEventKind::StragglerDetected) {
        e.kind = LogKind::GroupSeparated;
        e.detail = std::string(ce.kind == group::CohesionEventKind::GroupDispersed ? "dispersed" : "fragmented") +
                   " into " + std::to_string(ce.component_count) + " components, worst " +
                   std::to_string(int(ce.separation_m)) + " m";
        append_log(std::move(e));
      }
    }

    // Centre: centroid of the largest observed component.
    const auto comps = g.monitor->observed_components();
    g.components = comps.size();
    size_t best = 0;
    for (size_t i = 1; i < comps.size(); ++i) if (comps[i].size() > comps[best].size()) best = i;
    double slat = 0, slon = 0;
    for (track::TouristId id : comps[best]) {
      const auto& fx = tracked(id)->tourist.last_fix.pos;
      slat += fx.lat; slon += fx.lon;
    }
    g.centre = {slat / double(comps[best].size()), slon / double(comps[best].size())};
    g.has_centre = true;

    const bool frag = g.monitor->fragmented(g.id);
    if (frag && !g.fragmented) g.fragmented_since = now_ms_;
    g.fragmented = frag;

    const auto now_str = g.monitor->stragglers(g.id);
    std::vector<Straggler> next;
    for (track::TouristId sid : now_str) {
      Straggler s;
      s.user = sid;
      s.since_ms = now_ms_;
      for (const auto& old : g.stragglers) if (old.user == sid) s = old;
      s.separation_m = geo::distance_m(g.centre, tracked(sid)->tourist.last_fix.pos);
      if (!s.alerted && now_ms_ - s.since_ms >= g.cfg.confirm_ms) {
        s.alerted = true;
        alert::Alert a{};
        a.kind = alert::AlertKind::Straggler;
        a.severity = 4;
        a.tourist = sid;
        a.position = tracked(sid)->tourist.last_fix.pos;
        a.accuracy_m = tracked(sid)->tourist.last_fix.accuracy_m;
        a.raised_ms = now_ms_;
        a.note = std::to_string(int(s.separation_m)) + " m from " + g.name + " for " +
                 std::to_string((now_ms_ - s.since_ms) / 1000) + " s";
        fresh.push_back(raise_alert(a, g.id, false, user(sid)->source));
      }
      next.push_back(s);
    }
    g.stragglers = std::move(next);
    if (g.fragmented && now_ms_ - g.fragmented_since >= g.cfg.confirm_ms && g.stragglers.empty()) {
      bool open = false;
      for (const auto& r : alerts_)
        if (r.group == g.id && r.a.kind == alert::AlertKind::GroupFragmented && alert_is_open(r.a.state))
          open = true;
      if (!open) {
        alert::Alert a{};
        a.kind = alert::AlertKind::GroupFragmented;
        a.severity = 3;
        a.position = g.centre;
        a.raised_ms = now_ms_;
        a.note = g.name + " split into " + std::to_string(g.components) + " parts";
        fresh.push_back(raise_alert(a, g.id, false, g.source));
      }
    }
  }
  correlate(fresh);
  stats_.cohesion_us_total += elapsed_us(t0);
  ++stats_.cohesion_passes;
}

// ─── Zones ──────────────────────────────────────────────────────────────────

Source Platform::zone_source(ZoneId z) const {
  return z < zone_source_.size() ? zone_source_[z] : Source::Api;
}

Outcome Platform::validate_zone(const ZoneInput& in, fence::Zone* out) const {
  if (!valid_label(in.name, 80)) return Outcome::fail(400, "zone name: 1-80 printable characters");
  if (in.severity < 1 || in.severity > 5) return Outcome::fail(400, "severity must be 1..5");
  if (in.ring.size() < 3) return Outcome::fail(400, "a polygon needs at least 3 vertices");
  if (in.ring.size() > cfg_.max_zone_vertices) return Outcome::fail(400, "too many vertices");
  if (in.max_dwell_s < 0 || in.max_dwell_s > 7 * 86400) return Outcome::fail(400, "dwell limit out of range");
  if (in.active_from >= in.active_to) return Outcome::fail(400, "validity must end after it starts");
  for (const auto& p : in.ring)
    if (!std::isfinite(p.lat) || !std::isfinite(p.lon) || !p.valid())
      return Outcome::fail(400, "coordinate out of range");
  fence::Zone z;
  z.name = in.name;
  z.kind = in.kind;
  z.severity = uint8_t(in.severity);
  z.shape = geo::Polygon(in.ring);
  z.max_dwell_ms = in.max_dwell_s * 1000;
  z.validity = {in.active_from, in.active_to};
  const auto v = z.shape.validate();
  if (v != geo::Polygon::Validity::Ok)
    return Outcome::fail(400, std::string("invalid polygon: ") + geo::Polygon::to_string(v));
  *out = std::move(z);
  return Outcome::good();
}

void Platform::index_zone(ZoneId id, Timestamp at) {
  const fence::Zone* z = zones_.get(id);
  if (!z) return;
  index_->remove(id);
  index_->insert(id, z->shape.bbox());
  vindex_->add_zone(id, z->shape.bbox(), z->validity, at);
}

Outcome Platform::create_zone(const Principal& by, const ZoneInput& in, Source src) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator manages zones");
  if (zones_.all_ids().size() >= cfg_.max_zones) return Outcome::fail(409, "zone limit reached");
  fence::Zone z;
  Outcome o = validate_zone(in, &z);
  if (!o.ok()) return o;
  const ZoneId id = zones_.add(std::move(z));
  zone_source_.resize(zones_.size(), Source::Api);
  zone_source_[id] = src;
  index_zone(id, now_ms_);
  LogEntry e;
  e.kind = LogKind::ZoneCreated; e.t_ms = now_ms_; e.zone = id; e.actor = by.id;
  e.detail = in.name + " (" + fence::to_string(in.kind) + ", severity " + std::to_string(in.severity) + ")";
  append_log(std::move(e));
  return Outcome::good(id);
}

Outcome Platform::update_zone(const Principal& by, ZoneId id, const ZoneInput& in) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator manages zones");
  fence::Zone* cur = zones_.get_mut(id);
  if (!cur) return Outcome::fail(404, "no such zone");
  fence::Zone z;
  Outcome o = validate_zone(in, &z);
  if (!o.ok()) return o;
  z.id = id;
  z.synthetic = cur->synthetic;
  z.jurisdiction = cur->jurisdiction;
  z.enter_margin_m = cur->enter_margin_m;
  z.exit_margin_m = cur->exit_margin_m;
  *cur = std::move(z);
  index_zone(id, now_ms_);
  LogEntry e;
  e.kind = LogKind::ZoneUpdated; e.t_ms = now_ms_; e.zone = id; e.actor = by.id;
  e.detail = in.name + ", " + std::to_string(in.ring.size()) + " vertices";
  append_log(std::move(e));
  return Outcome::good(id);
}

Outcome Platform::delete_zone(const Principal& by, ZoneId id) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator manages zones");
  const fence::Zone* z = zones_.get(id);
  if (!z) return Outcome::fail(404, "no such zone");
  const std::string name = z->name;
  index_->remove(id);
  vindex_->remove_zone(id, now_ms_);
  zones_.remove(id);
  LogEntry e;
  e.kind = LogKind::ZoneDeleted; e.t_ms = now_ms_; e.zone = id; e.actor = by.id; e.detail = name;
  append_log(std::move(e));
  return Outcome::good(id);
}

Outcome Platform::set_zone_validity(const Principal& by, ZoneId id, Timestamp from, Timestamp to) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator manages zones");
  fence::Zone* z = zones_.get_mut(id);
  if (!z) return Outcome::fail(404, "no such zone");
  if (from >= to) return Outcome::fail(400, "validity must end after it starts");
  z->validity = {from, to};
  vindex_->update_validity(id, z->validity, now_ms_);
  LogEntry e;
  e.kind = LogKind::ZoneValidity; e.t_ms = now_ms_; e.zone = id; e.actor = by.id;
  e.detail = "in force " + std::to_string(from / 1000) + " s to " +
             (to == kForever ? std::string("open-ended") : std::to_string(to / 1000) + " s");
  append_log(std::move(e));
  return Outcome::good(id);
}

Outcome Platform::set_zone_dwell(const Principal& by, ZoneId id, int64_t max_dwell_s) {
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator manages zones");
  fence::Zone* z = zones_.get_mut(id);
  if (!z) return Outcome::fail(404, "no such zone");
  if (max_dwell_s < 0 || max_dwell_s > 7 * 86400) return Outcome::fail(400, "dwell limit out of range");
  z->max_dwell_ms = max_dwell_s * 1000;
  LogEntry e;
  e.kind = LogKind::ZoneUpdated; e.t_ms = now_ms_; e.zone = id; e.actor = by.id;
  e.detail = "dwell limit " + std::to_string(max_dwell_s) + " s";
  append_log(std::move(e));
  return Outcome::good(id);
}

Outcome Platform::import_geojson(const Principal& by, const std::string& text, size_t* added) {
  if (added) *added = 0;
  if (!(by.system || by.role == Role::Admin)) return Outcome::fail(403, "only an administrator manages zones");
  std::vector<fence::Zone> parsed;
  std::string err;
  if (!fence::ZoneStore::parse_geojson(text, parsed, &err)) return Outcome::fail(400, "GeoJSON rejected: " + err);
  if (parsed.empty()) return Outcome::fail(400, "no polygon features found");
  if (zones_.all_ids().size() + parsed.size() > cfg_.max_zones) return Outcome::fail(409, "zone limit reached");
  for (const auto& z : parsed)
    if (z.shape.vertex_count() > cfg_.max_zone_vertices)
      return Outcome::fail(400, "zone '" + z.name + "' has too many vertices");
  for (auto& z : parsed) {
    if (z.name.empty()) z.name = "imported zone";
    z.synthetic = false;
    const ZoneId id = zones_.add(std::move(z));
    zone_source_.resize(zones_.size(), Source::Api);
    index_zone(id, now_ms_);
    LogEntry e;
    e.kind = LogKind::ZoneCreated; e.t_ms = now_ms_; e.zone = id; e.actor = by.id;
    e.detail = zones_.get(id)->name + " (GeoJSON import)";
    append_log(std::move(e));
    if (added) ++*added;
  }
  return Outcome::good();
}

// ─── History ────────────────────────────────────────────────────────────────

// "What did the system know at time t": the last point whose TIME is at or
// before t AND that the server had RECEIVED by t. The two differ only for
// points synced late from an offline device -- they happened earlier than the
// server learned of them, and a historical query must not show knowledge the
// system did not yet have.
const TrailPoint* Platform::position_at(UserId u, Timestamp t) const {
  const Tracked* tr = tracked(u);
  if (!tr || tr->trail.empty()) return nullptr;
  auto it = std::upper_bound(tr->trail.begin(), tr->trail.end(), t,
                             [](Timestamp v, const TrailPoint& q) { return v < q.t_ms; });
  while (it != tr->trail.begin()) {
    --it;
    if (it->recorded_ms <= t) return &*it;
  }
  return nullptr;
}

alert::AlertState Platform::alert_state_at(AlertId a, Timestamp t, bool* existed) const {
  *existed = false;
  if (a >= alerts_.size()) return alert::AlertState::Open;
  alert::AlertState s = alert::AlertState::Open;
  for (const auto& [at, st] : alerts_[a].history) {
    if (at > t) break;
    *existed = true;
    s = st;
  }
  return s;
}

CaseState Platform::case_state_at(IncidentId i, Timestamp t, bool* existed) const {
  *existed = false;
  if (i >= cases_.size()) return CaseState::Open;
  CaseState s = CaseState::Open;
  for (const auto& [at, st] : cases_[i].history) {
    if (at > t) break;
    *existed = true;
    s = st;
  }
  return s;
}

// ─── Offline devices ────────────────────────────────────────────────────────

bool Platform::is_offline(UserId u) const { return u < devices_.size() && devices_[u] != nullptr; }

size_t Platform::device_pending(UserId u) const {
  return is_offline(u) ? devices_[u]->queue.pending() : 0;
}

Outcome Platform::go_offline(const Principal& by, UserId u, int64_t skew_ms) {
  User* usr = user_mut(u);
  if (!usr) return Outcome::fail(404, "no such user");
  if (!(by.system || by.id == u)) return Outcome::fail(403, "only the device's owner (or the simulator) can do this");
  if (!usr->sharing) return Outcome::fail(409, "no active tracking session");
  if (is_offline(u)) return Outcome::good(u);
  if (devices_.size() <= u) devices_.resize(size_t(u) + 1);
  auto d = std::make_unique<DeviceAgent>();
  d->user = u;
  d->queue = sync::OfflineQueue(DeviceId(u + 1));
  d->zones = zones_;
  // Ship the index as bytes, exactly as a handset would download it: build a
  // geohash index of the current zones here, serialise it, and let the device
  // deserialise its own copy. tests/fence/index_independence_test.cpp is why a
  // geohash on the device and an R-tree on the server give identical events.
  index::Geohash server_side;
  std::vector<std::pair<ZoneId, geo::Bbox>> items;
  for (ZoneId id : zones_.all_ids()) items.emplace_back(id, zones_.get(id)->shape.bbox());
  server_side.build(items);
  std::vector<uint8_t> bytes;
  server_side.serialize(bytes);
  if (!d->idx.deserialize(bytes)) d->idx.build(items);
  d->eval = std::make_unique<fence::Evaluator>(d->idx, d->zones, cfg_.eval);
  d->tourist = tracked_mut(u).tourist;    // state handed to the device
  d->skew_ms = skew_ms;
  d->offline_since = now_ms_;
  devices_[u] = std::move(d);
  usr->connected = false;
  return Outcome::good(u);
}

FixResult Platform::device_fix(UserId u, const FixInput& in) {
  FixResult res;
  if (!is_offline(u)) { res.status = FixStatus::Invalid; res.reason = "device is online"; return res; }
  DeviceAgent& d = *devices_[u];
  const Timestamp true_t = in.t_ms < 0 ? now_ms_ : in.t_ms;
  geo::UncertainPoint p;
  p.pos = {in.lat, in.lon};
  p.accuracy_m = in.accuracy_m;
  p.t_ms = true_t + d.skew_ms;          // the DEVICE's clock, which may be wrong
  if (!p.pos.valid() || !(in.accuracy_m > 0)) { res.status = FixStatus::Invalid; return res; }
  d.tourist.last_fix = p;
  track::Ping ping{};
  ping.fix = p;
  d.tourist.pings.push(ping);
  std::vector<fence::Event> evs;
  d.eval->evaluate(d.tourist, p.t_ms, evs);
  d.queue.append(encode_fix(p), p.t_ms);
  for (const auto& e : evs) d.queue.append(encode_event(e), p.t_ms);
  ++d.fixes;
  d.last_fix_true_ms = true_t;
  res.status = FixStatus::Accepted;
  res.events = evs.size();
  return res;
}

Outcome Platform::reconnect(const Principal& by, UserId u, size_t retries, SyncReport* rep) {
  SyncReport local;
  SyncReport& r = rep ? *rep : local;
  r = SyncReport{};
  if (!(by.system || by.id == u)) return Outcome::fail(403, "not allowed");
  if (!is_offline(u)) return Outcome::fail(409, "device is not offline");
  std::unique_ptr<DeviceAgent> d = std::move(devices_[u]);
  User& usr = users_[u];
  usr.connected = true;

  // On reconnect the device reports its clock; the server's estimate of the
  // skew is the difference. (Real links add half a round trip of error; the
  // simulated link has none, so the estimate here is exact.)
  const Timestamp device_now = now_ms_ + d->skew_ms;
  r.estimated_skew_ms = device_now - now_ms_;

  const std::vector<sync::StampedEvent> batch = d->queue.drain();
  r.uploaded = batch.size();
  std::vector<sync::StampedEvent> admitted;
  r.admitted = reconciler_.merge(batch, &admitted);
  for (size_t i = 0; i < retries; ++i) {
    const auto before = reconciler_.stats().duplicates_rejected;
    reconciler_.merge(batch);
    r.duplicates += size_t(reconciler_.stats().duplicates_rejected - before);
  }
  r.reordered = size_t(reconciler_.stats().reordered);

  LogEntry off;
  off.kind = LogKind::DeviceOffline; off.t_ms = d->offline_since; off.user = u; off.late = true;
  off.detail = "learned on reconnect";
  append_log(std::move(off));

  Tracked& tr = tracked_mut(u);
  const size_t alerts_before = alerts_.size();
  Timestamp last_t = tr.last_fix_ms;
  for (const auto& se : admitted) {
    util::Reader rd(se.payload);
    uint8_t kind = 0;
    if (!rd.u8(&kind)) continue;
    clock_.observe(se.stamp.counter);
    if (kind == kPayloadFix) {
      int64_t t = 0; double lat = 0, lon = 0, acc = 0;
      if (!rd.i64(&t) || !rd.f64(&lat) || !rd.f64(&lon) || !rd.f64(&acc)) continue;
      const Timestamp corrected = t - r.estimated_skew_ms;
      TrailPoint tp{corrected, lat, lon, acc, kTrailSynced, now_ms_};
      auto it = std::upper_bound(tr.trail.begin(), tr.trail.end(), corrected,
                                 [](Timestamp v, const TrailPoint& q) { return v < q.t_ms; });
      tr.trail.insert(it, tp);
      last_t = std::max(last_t, corrected);
      ++tr.fixes_sent;
    } else if (kind == kPayloadEvent) {
      uint8_t ek = 0, cont = 0; uint32_t zone = 0; int64_t t = 0; double depth = 0, acc = 0, eta = 0;
      if (!rd.u8(&ek) || !rd.u32(&zone) || !rd.i64(&t) || !rd.f64(&depth) || !rd.f64(&acc) ||
          !rd.f64(&eta) || !rd.u8(&cont) || ek > uint8_t(fence::EventKind::DwellExceeded))
        continue;
      fence::Event ev{};
      ev.kind = fence::EventKind(ek);
      ev.tourist = u; ev.zone = zone; ev.t_ms = t - r.estimated_skew_ms;
      ev.depth_m = depth; ev.accuracy_m = acc; ev.eta_s = eta;
      ev.containment = geo::Containment(cont > 2 ? 0 : cont);
      apply_events(u, {ev}, true, now_ms_, &se.stamp);
    }
  }
  r.alerts_raised = alerts_.size() - alerts_before;

  // Adopt the device's engine state so the next online fix continues from
  // where the device left off, with no duplicate transition.
  // Entry times are rebased onto the server clock so dwell limits keep counting
  // correctly. The hysteresis filters keep their device-clock pending times:
  // with a device clock BEHIND the server (the usual case) a pending transition
  // merely confirms sooner; a device clock AHEAD delays confirmation by at most
  // the skew. Documented in docs/final/LIMITATIONS_AND_FUTURE_SCOPE.md.
  tr.tourist = d->tourist;
  for (auto& st : tr.tourist.zone_states) st.entered_ms -= r.estimated_skew_ms;
  // The pings carry device-clock times; rebase them onto the server clock.
  {
    std::vector<track::Ping> ps;
    for (size_t i = tr.tourist.pings.size(); i-- > 0;) ps.push_back(tr.tourist.pings[i]);
    tr.tourist.pings.clear();
    for (auto& p : ps) { p.fix.t_ms -= r.estimated_skew_ms; tr.tourist.pings.push(p); }
    tr.tourist.last_fix.t_ms -= r.estimated_skew_ms;
  }
  tr.has_fix = true;
  tr.last_fix_ms = last_t;
  tr.last_seq = 0;

  if (tr.signal_alert != kNoId && tr.signal_alert < alerts_.size() &&
      alert_is_open(alerts_[tr.signal_alert].a.state)) {
    alerts_[tr.signal_alert].a.note = "device reconnected";
    set_alert_state(alerts_[tr.signal_alert], alert::AlertState::Resolved, kNoId);
  }
  tr.signal_alert = kNoId;
  tr.anomaly = tr.anomaly_raw = track::AnomalyKind::None;
  tr.anomaly_raw_since = now_ms_;

  LogEntry rc;
  rc.kind = LogKind::DeviceReconnected; rc.t_ms = now_ms_; rc.user = u;
  rc.detail = "clock skew " + std::to_string(r.estimated_skew_ms / 1000) + " s";
  append_log(std::move(rc));
  LogEntry sm;
  sm.kind = LogKind::SyncMerged; sm.t_ms = now_ms_; sm.user = u;
  sm.detail = std::to_string(r.uploaded) + " uploaded, " + std::to_string(r.admitted) + " admitted, " +
              std::to_string(r.duplicates) + " duplicates rejected, " + std::to_string(r.alerts_raised) +
              " alerts raised late";
  append_log(std::move(sm));
  return Outcome::good(u);
}

}  // namespace safetrail::app
