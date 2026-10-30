#pragma once
//
// The SafeTrail platform: the engine, made into a multi-user application.
//
// Everything below this file existed before it -- the evaluator, the indexes,
// the correlator, the cohesion monitor, dispatch, the persistent index, the
// Merkle log, Lamport clocks. What did not exist was anything that a PERSON
// could use: no accounts, no consent, no tracking session, no way for a fix to
// arrive from outside a simulation, no alert anyone could acknowledge, no route
// a responder could follow. This layer adds exactly that, and it does it by
// CALLING the engine rather than re-implementing any part of it.
//
// ── Isolation between users ──────────────────────────────────────────────────
//
// Each tracked user owns one track::Tourist, and that object holds every piece
// of per-(user, zone) state the engine has: hysteresis phase, confirmed
// containment, entry time, dwell and approach flags. Evaluator::evaluate() takes
// a single Tourist& and touches nothing else that is per-user, so two users in
// the same zone cannot share state by construction. tests/app/multi_user_test.cpp
// checks it anyway: two users entering the same zone independently, one leaving
// while the other stays, one stopping tracking while the other continues.
//
// ── Ordering ─────────────────────────────────────────────────────────────────
//
// The platform is single-writer: the HTTP server calls into it from one thread,
// so every mutation is serialised and the order of the event log is the order
// of the calls. Fixes for one user must be in time order, because hysteresis is
// a state machine over time; a fix older than the last accepted one is RECORDED
// in the user's trail (flagged late) but not evaluated, since evaluating it
// would replay the state machine backwards. Duplicates are dropped and counted.
//
// ── Time ─────────────────────────────────────────────────────────────────────
//
// The platform clock is milliseconds since the platform's own epoch. In demo
// mode the scenario runner advances it (so it can be paused, stepped and run
// faster than real time); in live mode the server advances it with the wall
// clock. Every timestamp the API returns is on this clock.
//
// ── Evidence ─────────────────────────────────────────────────────────────────
//
// Every log entry is appended to a Merkle log (evidence/merkle_log.hpp), so any
// entry can later be proven to be in the log with an O(log n) inclusion proof.
// Entries carry pseudonymous ids and zone ids, never coordinates: location
// trails live in a separate store that retention and erasure CAN delete, which
// an append-only evidence log by definition cannot.
//
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "safetrail/alert/alert.hpp"
#include "safetrail/alert/correlator.hpp"
#include "safetrail/alert/escalation.hpp"
#include "safetrail/dispatch/assigner.hpp"
#include "safetrail/dispatch/responder.hpp"
#include "safetrail/ds/hash_table.hpp"
#include "safetrail/evidence/merkle_log.hpp"
#include "safetrail/fence/evaluator.hpp"
#include "safetrail/fence/zone.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/group/cohesion.hpp"
#include "safetrail/index/spatial_index.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/sync/lamport.hpp"
#include "safetrail/track/anomaly.hpp"
#include "safetrail/track/tourist.hpp"

namespace safetrail::app {

using UserId = uint32_t;

// ─── Users ──────────────────────────────────────────────────────────────────

enum class Role : uint8_t { Admin, Operator, Responder, Tourist };
const char* to_string(Role r);
bool role_from(const std::string& s, Role* out);

// Where a record came from. Fixture and Scenario records are demonstration
// data and are labelled as such everywhere they are shown; Api records were
// created by a signed-in person. Production mode creates no Fixture records and
// refuses to run scenarios.
enum class Source : uint8_t { Api, Fixture, Scenario };
const char* to_string(Source s);

struct User {
  UserId      id = kNoId;
  std::string username;
  std::string display_name;
  Role        role = Role::Tourist;
  bool        active = true;
  Source      source = Source::Api;
  std::string password_hash;           // pbkdf2-sha256$...; never sent to a client
  Timestamp   created_ms = 0;

  // Tourist fields. Tracking needs BOTH: consent (a standing permission the
  // user grants or withdraws) and an open session (this trip, now). Withdrawing
  // consent ends the session.
  bool      consent = false;
  Timestamp consent_ms = 0;
  bool      sharing = false;
  uint32_t  sessions = 0;
  Timestamp session_started_ms = 0;
  Timestamp session_ended_ms = 0;
  GroupId   group = kNoId;
  bool      battery_aware = false;
  bool      connected = true;          // false while the device is offline

  // Responder accounts: which unit they staff.
  ResponderId unit = kNoId;
};

// ─── Location ingestion ─────────────────────────────────────────────────────

struct FixInput {
  double    lat = 0, lon = 0;
  double    accuracy_m = 10;
  Timestamp t_ms = -1;                 // -1 = now
  uint64_t  seq = 0;                   // optional device sequence number, 0 = none
};

enum class FixStatus : uint8_t {
  Accepted, Duplicate, OutOfOrder, Invalid, NotTracking, Offline, UnknownUser
};
const char* to_string(FixStatus s);

struct FixResult {
  FixStatus   status = FixStatus::Invalid;
  std::string reason;
  size_t      events = 0;              // transitions this fix produced
  bool        jump = false;            // implied speed above the jump threshold
};

enum TrailFlag : uint8_t { kTrailLate = 1, kTrailJump = 2, kTrailSynced = 4, kTrailNoisy = 8 };

// Bitemporal: t_ms is when the fix was TAKEN, recorded_ms when the server
// RECEIVED it. They differ for fixes synced late from an offline device, and a
// historical query ("what did the system know at 10:05?") must use both.
struct TrailPoint {
  Timestamp t_ms = 0;
  double    lat = 0, lon = 0, accuracy_m = 0;
  uint8_t   flags = 0;
  Timestamp recorded_ms = 0;
};

// Per-user engine state. One per tourist account, created on first session.
struct Tracked {
  track::Tourist tourist;
  bool      has_fix = false;
  Timestamp last_fix_ms = 0;
  uint64_t  last_seq = 0;
  uint64_t  accepted = 0, duplicates = 0, out_of_order = 0, invalid = 0, jumps = 0;
  std::vector<TrailPoint> trail;       // time-ordered; binary-searched for history

  // Anomaly confirmation, time-based (the simulator counts ticks; fixes here
  // arrive at whatever rate the device sends them).
  track::AnomalyKind anomaly = track::AnomalyKind::None;
  track::AnomalyKind anomaly_raw = track::AnomalyKind::None;
  Timestamp anomaly_raw_since = 0;
  AlertId   signal_alert = kNoId;      // open SignalLost alert, resolved on contact

  // Battery accounting: fixes the device sent, against the fixes a fixed 1 Hz
  // policy would have sent over the same session time.
  uint64_t  fixes_sent = 0;
  Timestamp session_time_ms = 0;
};

// ─── Event log and evidence ─────────────────────────────────────────────────

enum class LogKind : uint8_t {
  ZoneEnter, ZoneExit, ZoneUncertain, ZoneApproaching, DwellExceeded,
  AlertRaised, AlertAcknowledged, AlertEscalated, AlertResolved, AlertDismissed,
  IncidentOpened, IncidentUpdated, IncidentAcknowledged, IncidentAssigned, IncidentResolved,
  SessionStarted, SessionStopped, ConsentGranted, ConsentWithdrawn, HistoryErased,
  UserCreated, UserUpdated, GroupCreated, GroupChanged, GroupSeparated, GroupRejoined,
  ZoneCreated, ZoneUpdated, ZoneDeleted, ZoneValidity,
  ResponderCreated, ResponderUpdated, DeviceOffline, DeviceReconnected, SyncMerged,
};
const char* to_string(LogKind k);

struct LogEntry {
  uint64_t          seq = 0;
  sync::LamportClock stamp{};          // device 0 = server; devices are 1 + user id
  Timestamp         t_ms = 0;          // when it happened (platform clock)
  Timestamp         recorded_ms = 0;   // when the server learned of it
  LogKind           kind = LogKind::ZoneEnter;
  UserId            user = kNoId;      // subject
  UserId            actor = kNoId;     // who caused it; kNoId = the system
  ZoneId            zone = kNoId;
  AlertId           alert = kNoId;
  IncidentId        incident = kNoId;
  GroupId           group = kNoId;
  ResponderId       responder = kNoId;
  bool              late = false;      // learned after the fact (offline sync)
  std::string       detail;
  uint64_t          evidence_index = 0;
};

// The canonical byte string an entry is hashed as. Coordinates never appear.
std::string canonical(const LogEntry& e);

struct AccessRecord {
  Timestamp   t_ms = 0;
  UserId      actor = kNoId;
  UserId      subject = kNoId;
  std::string what;                    // "live", "trail", "history", ...
  bool        allowed = false;
};

// ─── Alerts and incidents ───────────────────────────────────────────────────

struct AlertRecord {
  alert::Alert a;
  GroupId      group = kNoId;
  uint8_t      original_severity = 1;
  bool         escalated = false;
  bool         redacted = false;       // position erased at the user's request
  bool         late = false;           // raised from an offline-synced event
  UserId       acted_by = kNoId;
  std::vector<std::pair<Timestamp, alert::AlertState>> history;
  Source       source = Source::Api;
};

enum class CaseState : uint8_t { Open, Acknowledged, Dispatched, Resolved };
const char* to_string(CaseState s);

struct Route {
  bool   found = false;
  double distance_m = 0;
  std::vector<geo::LatLon> points;     // along the road graph
  size_t astar_expanded = 0;           // nodes A* settled for this route
  size_t dijkstra_expanded = 0;        // nodes Dijkstra settled for the same pair
  graph::NodeId from = graph::kNoNode, to = graph::kNoNode;
};

struct IncidentCase {
  IncidentId  id = kNoId;
  CaseState   state = CaseState::Open;
  ResponderId recommended = kNoId;     // nearest available by road distance
  double      recommended_m = 0;
  ResponderId responder = kNoId;       // assigned
  Route       route;
  Timestamp   acknowledged_ms = 0, dispatched_ms = 0, resolved_ms = 0;
  UserId      acknowledged_by = kNoId, resolved_by = kNoId;
  std::string resolution;
  std::vector<std::pair<Timestamp, CaseState>> history;
};

// ─── Groups ─────────────────────────────────────────────────────────────────

struct Straggler {
  UserId    user = kNoId;
  Timestamp since_ms = 0;              // first observed separated
  double    separation_m = 0;
  bool      alerted = false;           // separation outlasted confirm_ms
};

struct GroupRecord {
  GroupId     id = kNoId;
  std::string name;
  std::vector<UserId> members;
  group::CohesionConfig cfg{};
  Source      source = Source::Api;
  Timestamp   created_ms = 0;

  // Observed state, refreshed every cohesion pass.
  size_t      tracked_members = 0;
  size_t      components = 0;
  bool        fragmented = false;
  Timestamp   fragmented_since = 0;
  geo::LatLon centre{};
  bool        has_centre = false;
  std::vector<Straggler> stragglers;
  std::unique_ptr<group::CohesionMonitor> monitor;
};

// ─── Responders ─────────────────────────────────────────────────────────────

struct ResponderInfo {
  std::string unit_type;               // "medical", "police", "rescue", ...
  Source      source = Source::Api;
  UserId      account = kNoId;
  IncidentId  assigned = kNoId;
  Timestamp   position_ms = 0;         // when the position was last set
};

// ─── Offline device ─────────────────────────────────────────────────────────
//
// A tourist's phone with no network. It holds its own copy of the zones and a
// spatial index shipped to it as BYTES (Geohash::serialize -- the same format a
// real handset would download), evaluates fixes locally with its own Evaluator,
// and appends what it sees to an OfflineQueue stamped with its Lamport clock and
// its own (possibly wrong) wall clock. The server learns NOTHING of this until
// reconnect(): while the device is offline the operator sees only "last contact
// N minutes ago", because that is all the server knows.
struct DeviceAgent;

// ─── Configuration ──────────────────────────────────────────────────────────

struct PlatformConfig {
  fence::EvaluatorConfig eval{};
  // Alerts within 200 m and 10 minutes of each other form one incident. The
  // engine's default window is 60 s, tuned for a crowd breaching at once; an
  // operations board also wants "the same person, same place, a few minutes
  // later" (a breach, then its dwell overrun) on ONE card.
  alert::CorrelationConfig correlation{200.0, 600'000, 2, false};
  int64_t   warning_ttl_ms = 600'000;           // approaching/uncertain warnings expire
  int64_t   escalation_timeout_ms = 120'000;   // unacknowledged alerts escalate
  int64_t   cohesion_interval_ms = 10'000;
  int64_t   trail_retention_ms = 6 * 3600'000;  // movement history kept this long
  size_t    trail_max_points = 20'000;
  double    jump_speed_mps = 70.0;              // faster than this between fixes = jump
  int64_t   max_future_skew_ms = 60'000;        // fixes from further ahead are refused
  int64_t   anomaly_confirm_ms = 30'000;
  int64_t   stationary_confirm_ms = 90'000;        // on top of the 50 s detection window
  uint8_t   warning_min_severity = 4;              // approaching/uncertain alerts below this: log only
  double    approach_alert_eta_s = 180.0;
  uint8_t   incident_min_breach_severity = 3;   // breaches below this stay alerts
  uint32_t  pbkdf2_iterations = 120'000;
  bool      auto_dispatch = false;              // assign the recommended responder at once
  size_t    max_users = 5000, max_zones = 20000, max_zone_vertices = 4096;
};

struct Principal {
  UserId id = kNoId;
  Role   role = Role::Tourist;
  bool   system = false;               // the scenario runner / server itself
  static Principal sys() { Principal p; p.system = true; p.role = Role::Admin; return p; }
};

// Result of a command. `code` is an HTTP-style status so the API layer can pass
// it through: 200 ok, 400 bad input, 403 not allowed, 404 unknown, 409 conflict.
struct Outcome {
  int code = 200;
  std::string error;
  uint32_t id = kNoId;
  bool ok() const { return code >= 200 && code < 300; }
  static Outcome good(uint32_t i = kNoId) { Outcome o; o.id = i; return o; }
  static Outcome fail(int c, std::string e) { Outcome o; o.code = c; o.error = std::move(e); return o; }
};

// ─── The platform ───────────────────────────────────────────────────────────

class Platform {
 public:
  explicit Platform(PlatformConfig cfg = {});
  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  const PlatformConfig& config() const { return cfg_; }
  PlatformConfig& mutable_config() { return cfg_; }

  // ── Clock ────────────────────────────────────────────────────────────────
  Timestamp now() const { return now_ms_; }
  // Advance the clock and run everything time-driven: anomaly detection,
  // cohesion, escalation, retention. Never moves backwards.
  void tick(Timestamp now_ms);

  // ── World ────────────────────────────────────────────────────────────────
  bool load_zones_file(const std::string& path, std::string* err);
  bool load_roads_file(const std::string& path, std::string* err);
  void use_grid_roads(int rows, int cols);   // synthetic fallback

  // ── Users ────────────────────────────────────────────────────────────────
  Outcome create_user(const Principal& by, const std::string& username,
                      const std::string& display_name, Role role,
                      const std::string& password, Source src = Source::Api);
  Outcome set_active(const Principal& by, UserId u, bool active);
  Outcome set_display_name(const Principal& by, UserId u, const std::string& name);
  const User* user(UserId u) const;
  const User* user_by_name(const std::string& username) const;
  const std::vector<User>& users() const { return users_; }
  // Password check for login. Constant-time comparison inside; false for an
  // unknown or inactive user without revealing which.
  const User* authenticate(const std::string& username, const std::string& password) const;
  Outcome set_password(const Principal& by, UserId u, const std::string& password);

  // Consent and sessions. Only the user themself (or the system acting for a
  // simulated user) may grant consent or start sharing: an operator cannot
  // switch on someone else's tracking.
  Outcome set_consent(const Principal& by, UserId u, bool consent);
  Outcome start_sharing(const Principal& by, UserId u);
  Outcome stop_sharing(const Principal& by, UserId u);
  Outcome set_battery_aware(const Principal& by, UserId u, bool on);
  Outcome erase_history(const Principal& by, UserId u);

  FixResult ingest_fix(const Principal& by, UserId u, const FixInput& fix);

  const Tracked* tracked(UserId u) const;
  // Who may see subject's location. Every positive answer for someone other
  // than the subject is written to the access audit log by the API layer.
  bool may_view_location(const Principal& by, UserId subject) const;
  void audit_access(const Principal& by, UserId subject, const std::string& what, bool allowed);
  const std::vector<AccessRecord>& access_log() const { return access_; }
  const evidence::MerkleLog& access_evidence() const { return access_merkle_; }

  // Summary of a user's position against the zones: confirmed containment per
  // zone with open state, and the nearest zones by box distance.
  struct ZoneStatus { ZoneId zone; geo::Containment state; double distance_m; };
  std::vector<ZoneStatus> zone_status(UserId u, size_t nearest = 3) const;
  std::string status_label(UserId u) const;   // "inside", "uncertain", "approaching", "clear", ...

  // ── Groups ───────────────────────────────────────────────────────────────
  Outcome create_group(const Principal& by, const std::string& name, Source src = Source::Api);
  Outcome add_member(const Principal& by, GroupId g, UserId u);
  Outcome remove_member(const Principal& by, GroupId g, UserId u);
  Outcome set_group_thresholds(const Principal& by, GroupId g, double proximity_m,
                               double straggler_m, int64_t confirm_ms);
  const GroupRecord* group_rec(GroupId g) const;
  const std::vector<GroupRecord>& groups() const { return groups_; }
  void run_cohesion();                 // tick() calls this on its interval

  // ── Zones ────────────────────────────────────────────────────────────────
  struct ZoneInput {
    std::string name;
    fence::ZoneKind kind = fence::ZoneKind::Caution;
    int severity = 3;
    std::vector<geo::LatLon> ring;     // outer ring, unclosed
    int64_t max_dwell_s = 0;
    Timestamp active_from = 0, active_to = kForever;
  };
  Outcome create_zone(const Principal& by, const ZoneInput& in, Source src = Source::Api);
  Outcome update_zone(const Principal& by, ZoneId z, const ZoneInput& in);
  Outcome delete_zone(const Principal& by, ZoneId z);
  Outcome set_zone_validity(const Principal& by, ZoneId z, Timestamp from, Timestamp to);
  Outcome set_zone_dwell(const Principal& by, ZoneId z, int64_t max_dwell_s);
  // All-or-nothing: every feature is validated before any is added.
  Outcome import_geojson(const Principal& by, const std::string& text, size_t* added);
  const fence::ZoneStore& zones() const { return zones_; }
  const index::SpatialIndex& spatial_index() const { return *index_; }
  const index::VersionedIndex& versioned() const { return *vindex_; }
  Source zone_source(ZoneId z) const;
  size_t users_inside(ZoneId z) const;

  // ── Alerts ───────────────────────────────────────────────────────────────
  Outcome acknowledge_alert(const Principal& by, AlertId a);
  Outcome resolve_alert(const Principal& by, AlertId a, bool dismiss);
  Outcome raise_sos(const Principal& by, UserId u);
  const std::vector<AlertRecord>& alerts() const { return alerts_; }
  // Open alerts in triage order -- built through alert::TriageQueue (a binary
  // max-heap) on triage_score: severity, age, isolation from the nearest responder.
  struct Triaged { AlertId id; double priority; };
  std::vector<Triaged> triage_order() const;
  double nearest_responder_m(const geo::LatLon& p) const;

  // ── Incidents ────────────────────────────────────────────────────────────
  const alert::Correlator& correlator() const { return *corr_; }
  const IncidentCase* incident_case(IncidentId i) const;
  const std::vector<IncidentCase>& cases() const { return cases_; }
  Outcome acknowledge_incident(const Principal& by, IncidentId i);
  // responder = kNoId -> the recommended one.
  Outcome assign_incident(const Principal& by, IncidentId i, ResponderId r = kNoId);
  Outcome resolve_incident(const Principal& by, IncidentId i, const std::string& note);

  // Greedy vs Hungarian over every open, unassigned incident and every
  // available responder. apply=true assigns the optimal plan.
  struct PlanSummary {
    dispatch::Plan greedy, optimal;
    std::vector<IncidentId> incidents;
    std::vector<ResponderId> responders;
  };
  PlanSummary plan_dispatch(const Principal& by, bool apply);

  // ── Responders ───────────────────────────────────────────────────────────
  Outcome create_responder(const Principal& by, const std::string& name, const std::string& type,
                           geo::LatLon pos, Source src = Source::Api, UserId account = kNoId);
  Outcome set_responder_available(const Principal& by, ResponderId r, bool available);
  Outcome set_responder_position(const Principal& by, ResponderId r, geo::LatLon pos);
  const dispatch::ResponderPool& responders() const { return pool_; }
  const ResponderInfo* responder_info(ResponderId r) const;
  const graph::RoadGraph& roads() const { return roads_; }
  Route route_between(geo::LatLon from, geo::LatLon to) const;

  // ── History ──────────────────────────────────────────────────────────────
  const std::vector<LogEntry>& log() const { return log_; }
  const evidence::MerkleLog& evidence() const { return merkle_; }
  // Last trail point at or before t (binary search). nullptr if none retained.
  const TrailPoint* position_at(UserId u, Timestamp t) const;
  alert::AlertState alert_state_at(AlertId a, Timestamp t, bool* existed) const;
  CaseState case_state_at(IncidentId i, Timestamp t, bool* existed) const;

  // ── Offline devices ──────────────────────────────────────────────────────
  Outcome go_offline(const Principal& by, UserId u, int64_t device_clock_skew_ms);
  // A fix taken by the phone while it has no network: evaluated on the device.
  FixResult device_fix(UserId u, const FixInput& fix);
  // The device reconnects and uploads its queue. `retries` re-sends the same
  // batch that many extra times, as a flaky link would; the reconciler must
  // admit each event once.
  struct SyncReport {
    size_t uploaded = 0, admitted = 0, duplicates = 0, reordered = 0;
    int64_t estimated_skew_ms = 0;
    size_t alerts_raised = 0;
  };
  Outcome reconnect(const Principal& by, UserId u, size_t retries, SyncReport* report);
  bool is_offline(UserId u) const;
  size_t device_pending(UserId u) const;          // the DEVICE's view, for the device panel
  const sync::Reconciler& reconciler() const { return reconciler_; }

  // ── Counters ─────────────────────────────────────────────────────────────
  struct Stats {
    uint64_t fixes_accepted = 0, fixes_duplicate = 0, fixes_out_of_order = 0,
             fixes_invalid = 0, fixes_refused = 0, jumps = 0;
    uint64_t transitions = 0, alerts = 0, escalations = 0;
    double   eval_us_total = 0;        // wall time inside Evaluator::evaluate
    uint64_t eval_calls = 0;
    double   cohesion_us_total = 0;
    uint64_t cohesion_passes = 0;
  };
  const Stats& stats() const { return stats_; }
  fence::Evaluator::Counters eval_counters() const { return eval_->counters(); }

 private:
  PlatformConfig cfg_;
  Timestamp now_ms_ = 0;

  std::vector<User> users_;
  ds::HashMap<std::string, UserId> by_name_;     // username -> id, open addressing
  std::vector<std::unique_ptr<Tracked>> tracked_; // indexed by UserId, null for staff

  fence::ZoneStore zones_;
  std::vector<Source> zone_source_;
  std::unique_ptr<index::SpatialIndex> index_;
  std::unique_ptr<index::VersionedIndex> vindex_;
  std::unique_ptr<fence::Evaluator> eval_;

  std::unique_ptr<alert::Correlator> corr_;
  std::unique_ptr<alert::EscalationTracker> escalation_;
  std::vector<AlertRecord> alerts_;
  std::vector<IncidentCase> cases_;

  std::vector<GroupRecord> groups_;
  Timestamp next_cohesion_ms_ = 0;
  Timestamp next_retention_ms_ = 0;

  graph::RoadGraph roads_;
  dispatch::ResponderPool pool_;
  std::vector<ResponderInfo> rinfo_;

  std::vector<LogEntry> log_;
  evidence::MerkleLog merkle_;
  sync::LamportClock clock_{};
  std::vector<AccessRecord> access_;
  evidence::MerkleLog access_merkle_;

  std::vector<std::unique_ptr<DeviceAgent>> devices_;   // indexed by UserId
  sync::Reconciler reconciler_;
  Stats stats_{};

  // internals
  User* user_mut(UserId u);
  Tracked& tracked_mut(UserId u);
  LogEntry& append_log(LogEntry e);
  AlertId raise_alert(alert::Alert a, GroupId g, bool late, Source src);
  void set_alert_state(AlertRecord& r, alert::AlertState s, UserId by);
  void correlate(const std::vector<AlertId>& fresh);
  void after_incident_touched(IncidentId id);
  void recommend(IncidentCase& c);
  void apply_events(UserId u, const std::vector<fence::Event>& evs, bool late, Timestamp recorded,
                    const sync::LamportClock* device_stamp = nullptr);
  geo::LatLon position_near(const Tracked& tr, Timestamp t) const;
  void evaluate_fix(UserId u, Tracked& tr, const geo::UncertainPoint& fix, std::vector<fence::Event>& out);
  void detect_anomalies(Timestamp now);
  void escalate(Timestamp now);
  void expire_warnings(Timestamp now);
  void settle_warnings(UserId u, ZoneId z, const char* why);
  void enforce_retention(Timestamp now);
  void rebuild_monitor(GroupRecord& g);
  void index_zone(ZoneId id, Timestamp at);
  Outcome validate_zone(const ZoneInput& in, fence::Zone* out) const;
  bool is_staff(const Principal& by) const;
  void free_responder(ResponderId r);
};

}  // namespace safetrail::app
