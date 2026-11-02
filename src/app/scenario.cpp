#include "safetrail/app/scenario.hpp"

#include <algorithm>
#include <cmath>

#include "safetrail/geo/containment.hpp"

namespace safetrail::app {

const char* to_string(RunState s) {
  switch (s) {
    case RunState::Idle: return "idle";
    case RunState::Paused: return "paused";
    case RunState::Running: return "running";
    case RunState::Finished: return "finished";
  }
  return "?";
}

// ─── Script ─────────────────────────────────────────────────────────────────

namespace {

struct Leg {
  geo::LatLon to;
  double speed_mps = 1.2;
  int64_t pause_ms = 0;     // wait this long on arrival
};

struct Actor {
  std::string username, display, group_key;
  geo::LatLon truth{};
  std::vector<Leg> legs;
  size_t leg = 0;
  int64_t pause_until = 0;
  bool battery_aware = false;
  UserId id = kNoId;
  sim::GpsDrift drift{};
  sim::Rng rng{1};
  int64_t last_sent = -1'000'000'000;
  uint64_t sent = 0, skipped = 0;
  int64_t skew_ms = 0;
};

enum class Act : uint8_t {
  CreateGroup, Join, ConsentAndShare, StopSharing, SetDwell, GoOffline, Reconnect,
  BatteryAware, Narrate, CreateZone,
};

struct Action {
  int64_t at = 0;
  Act kind = Act::Narrate;
  std::string a, b;          // actor / group key / zone name, and narration text
  int64_t n = 0;             // dwell seconds, skew ms, retry count
  geo::LatLon at_pos{};      // CreateZone: centre
  double radius_m = 0;       // CreateZone: circumradius
};

}  // namespace

struct ScenarioRunner::Script {
  std::vector<Actor> actors;
  std::vector<Action> actions;
  size_t next_action = 0;
  std::vector<std::pair<std::string, GroupId>> groups;
  int64_t duration_ms = 0;
  sim::GpsErrorModel gps{};

  Actor* actor(const std::string& key) {
    for (auto& a : actors) if (a.username == key) return &a;
    return nullptr;
  }
  GroupId group(const std::string& key) const {
    for (const auto& g : groups) if (g.first == key) return g.second;
    return kNoId;
  }
};

namespace {

using Script = ScenarioRunner::Script;

const fence::Zone* zone_named(const Platform& pf, const std::string& name) {
  for (ZoneId id : pf.zones().all_ids()) {
    const fence::Zone* z = pf.zones().get(id);
    if (z && z->name == name) return z;
  }
  return nullptr;
}

// The interior point of `z` nearest to `from` that lies at least 20 m inside
// the boundary, else the deepest point found: a 120 x 120 grid over the zone's
// box, each sample tested with the engine's own signed distance. A centroid is
// not good enough -- the centroid of a concave river polygon lies on dry land
// -- and most real OSM hazard polygons are thin: the rivers here are at most
// 25-40 m deep, which is why "uncertain" is the common honest answer near them.
geo::LatLon inside_near(const fence::Zone& z, geo::LatLon from) {
  const geo::Bbox b = z.shape.bbox();
  geo::LatLon best = z.shape.centroid(), deepest = best;
  double best_d = 1e18, deepest_sd = 0;
  for (int i = 0; i <= 119; ++i)
    for (int j = 0; j <= 119; ++j) {
      const geo::LatLon p{b.min_lat + (b.max_lat - b.min_lat) * i / 119.0,
                          b.min_lon + (b.max_lon - b.min_lon) * j / 119.0};
      const double sd = geo::signed_distance_m(z.shape, p);
      if (sd < deepest_sd) { deepest_sd = sd; deepest = p; }
      if (sd > -20.0) continue;
      const double d = geo::distance_m(p, from);
      if (d < best_d) { best_d = d; best = p; }
    }
  return best_d < 1e18 ? best : deepest;
}

void add_actor(Script& s, const std::string& user, const std::string& display,
               const std::string& group, geo::LatLon start, std::vector<Leg> legs, uint64_t seed) {
  Actor a;
  a.username = user;
  a.display = "SIM \xC2\xB7 " + display;
  a.group_key = group;
  a.truth = start;
  a.legs = std::move(legs);
  a.rng = sim::Rng(seed);
  s.actors.push_back(std::move(a));
}

void act(Script& s, int64_t at_s, Act k, std::string a = {}, std::string b = {}, int64_t n = 0) {
  Action x;
  x.at = at_s * 1000;
  x.kind = k;
  x.a = std::move(a);
  x.b = std::move(b);
  x.n = n;
  s.actions.push_back(std::move(x));
}

void say(Script& s, int64_t at_s, const std::string& text) { act(s, at_s, Act::Narrate, {}, text); }

// Opening steps shared by every group: create it, add members, opt in.
void enrol(Script& s, int64_t t0, const std::string& gkey, const std::string& gname,
           const std::vector<std::string>& members, bool narrate) {
  act(s, t0, Act::CreateGroup, gkey, gname);
  if (narrate) say(s, t0, "Operator creates the group \"" + gname + "\".");
  for (const auto& m : members) act(s, t0 + 3, Act::Join, m, gkey);
  if (narrate) say(s, t0 + 3, std::to_string(members.size()) + " simulated tourists join " + gname + ".");
  for (const auto& m : members) act(s, t0 + 8, Act::ConsentAndShare, m);
  if (narrate) say(s, t0 + 8, "Each tourist gives consent and starts sharing; their markers appear on the map.");
}

// ── A: normal movement ─────────────────────────────────────────────────────
void build_normal(Script& s, const Platform& pf, int64_t t0, bool narrate) {
  const fence::Zone* h = zone_named(pf, "Sonapani Waterfall Cliff");
  const geo::LatLon base = geo::offset(h->shape.centroid(), 175.0, 1250.0);
  const geo::LatLon w1 = geo::offset(base, 95.0, 380.0);
  const geo::LatLon w2 = geo::offset(w1, 10.0, 260.0);
  std::vector<std::string> names;
  const char* who[] = {"Anika", "Bilal", "Chitra", "Dev", "Esha"};
  for (int i = 0; i < 5; ++i) {
    const std::string u = "sim-a" + std::to_string(i + 1);
    const geo::LatLon st = geo::offset(base, 72.0 * i, 12.0);
    add_actor(s, u, std::string(who[i]) + " (walk)", "A", st,
              {{st, 0.0, (t0 + 15) * 1000},
               {geo::offset(w1, 72.0 * i, 10.0), 1.15 + 0.03 * i, 30'000},
               {geo::offset(w2, 72.0 * i, 10.0), 1.15 + 0.03 * i, 30'000},
               {geo::offset(base, 72.0 * i, 12.0), 1.15 + 0.03 * i, 0}},
              101 + uint64_t(i));
    names.push_back(u);
  }
  enrol(s, t0, "A", "Heritage Walk", names, narrate);
  if (narrate) say(s, t0 + 60, "Normal movement: the group walks a loop well clear of any hazard. Expect no alerts.");
}

// ── B: restricted-zone entry ───────────────────────────────────────────────
//
// The OpenStreetMap "Sonapani Waterfall Cliff" polygon is a sliver: its
// deepest interior point is about 10 m from the edge, so with a 5 m GPS fix and
// the 15 m entry margin no one can ever be CONFIRMED inside it -- the engine is
// right to refuse. The story therefore starts with the administrator
// registering a temporary rockfall closure on the trail below the cliff, a
// polygon a person can actually stand inside. It is named "SIM ..." and
// created with Source::Scenario, like every other scripted record.
void build_entry(Script& s, const Platform& pf, int64_t t0, bool narrate) {
  const fence::Zone* h = zone_named(pf, "Sonapani Waterfall Cliff");
  const geo::LatLon zc = geo::offset(h->shape.centroid(), 185.0, 330.0);
  const geo::LatLon start = geo::offset(zc, 185.0, 400.0);
  const geo::LatLon view = geo::offset(zc, 185.0, 190.0);
  const geo::LatLon in = geo::offset(zc, 185.0, 15.0);
  {
    Action z;
    z.at = (t0 + 12) * 1000;
    z.kind = Act::CreateZone;
    z.a = "SIM \xC2\xB7 Rockfall closure (Sonapani trail)";
    z.n = 120;
    z.at_pos = zc;
    z.radius_m = 85.0;
    s.actions.push_back(z);
  }
  std::vector<std::string> names;
  // Ravi walks in and stays past the dwell limit.
  add_actor(s, "sim-b1", "Ravi (viewpoint)", "B", start,
            {{start, 0.0, (t0 + 15) * 1000}, {view, 1.25, 20'000}, {in, 1.0, 210'000}, {view, 1.1, 0}}, 201);
  // Meera walks in later, briefly: a second user in the same zone, independently.
  add_actor(s, "sim-b2", "Meera (viewpoint)", "B", geo::offset(start, 90, 15),
            {{geo::offset(start, 90, 15), 0.0, (t0 + 15) * 1000},
             {geo::offset(view, 90, 15), 1.25, 260'000}, {geo::offset(zc, 185.0, 45.0), 1.2, 0},
             {geo::offset(view, 90, 15), 1.2, 0}}, 202);
  add_actor(s, "sim-b3", "Joseph (viewpoint)", "B", geo::offset(start, 270, 15),
            {{geo::offset(start, 270, 15), 0.0, (t0 + 15) * 1000}, {geo::offset(view, 270, 20), 1.25, 0}}, 203);
  add_actor(s, "sim-b4", "Lin (viewpoint)", "B", geo::offset(start, 0, 15),
            {{geo::offset(start, 0, 15), 0.0, (t0 + 15) * 1000}, {geo::offset(view, 200, 25), 1.25, 0}}, 204);
  for (int i = 1; i <= 4; ++i) names.push_back("sim-b" + std::to_string(i));
  enrol(s, t0, "B", "Waterfall Viewpoint", names, narrate);
  if (narrate) {
    say(s, t0 + 12, "Administrator registers a temporary rockfall closure below the cliff: restricted, severity 5, 2-minute dwell limit.");
    say(s, t0 + 200, "Ravi leaves the viewpoint toward the closure: watch for ZoneApproaching, then ZoneUncertain at the edge.");
    say(s, t0 + 300, "Hysteresis confirms the entry: ZoneEnter, a severity-5 breach alert, and an incident with a recommended responder.");
    say(s, t0 + 430, "Ravi has been inside longer than the limit: DwellExceeded joins the same incident.");
    say(s, t0 + 470, "Meera enters the same zone on her own: her state is independent of Ravi's.");
    say(s, t0 + 620, "Both walk out: ZoneExit for each. Acknowledge, assign and resolve the incident from Incidents.");
  }
}

// ── C: group separation and dispatch ───────────────────────────────────────
void build_separation(Script& s, const Platform& pf, int64_t t0, bool narrate) {
  const fence::Zone* h = zone_named(pf, "Sonapani Waterfall Cliff");
  const geo::LatLon r0 = geo::offset(h->shape.centroid(), 150.0, 1550.0);
  const geo::LatLon r1 = geo::offset(r0, 80.0, 1500.0);
  std::vector<std::string> names;
  const char* who[] = {"Kiran", "Laila", "Mohan", "Nisha", "Omar", "Priya"};
  for (int i = 0; i < 6; ++i) {
    const std::string u = "sim-c" + std::to_string(i + 1);
    const geo::LatLon st = geo::offset(r0, 60.0 * i, 15.0);
    std::vector<Leg> legs = {{st, 0.0, (t0 + 15) * 1000}};
    if (i == 3)   // Nisha is hurt three minutes in and stays where she fell
      legs.push_back({geo::offset(geo::offset(r0, 80.0, 230.0), 60.0 * i, 10.0), 1.3, 3'600'000});
    else
      legs.push_back({geo::offset(r1, 60.0 * i, 15.0), 1.3, 0});
    add_actor(s, u, std::string(who[i]) + " (ridge)", "C", st, std::move(legs), 301 + uint64_t(i));
    names.push_back(u);
  }
  enrol(s, t0, "C", "Ridge Hikers", names, narrate);
  if (narrate) {
    say(s, t0 + 195, "Nisha stops moving (simulated injury). The rest of the group walks on.");
    say(s, t0 + 290, "Stationary anomaly confirmed: alert and incident; the nearest responder by ROAD distance is recommended.");
    say(s, t0 + 500, "The group's proximity graph splits: Nisha is in a different connected component (union-find).");
    say(s, t0 + 560, "Separation outlasts the 60 s confirmation: a Straggler alert joins Nisha's incident.");
    say(s, t0 + 600, "Assign the responder: the route is A* over the OpenStreetMap road graph.");
  }
}

// ── D: offline device and synchronisation ──────────────────────────────────
void build_offline(Script& s, const Platform& pf, int64_t t0, bool narrate) {
  const fence::Zone* h = zone_named(pf, "Sonapani Waterfall Cliff");
  const fence::Zone* w = zone_named(pf, "Deep Water 4");
  const geo::LatLon probe = geo::offset(h->shape.centroid(), 310.0, 300.0);
  const geo::LatLon in = inside_near(*w, probe);
  const geo::LatLon start = geo::offset(in, geo::bearing_deg(in, probe), 330.0);
  add_actor(s, "sim-d1", "Tenzin (solo trek)", "", start,
            {{start, 0.0, (t0 + 15) * 1000}, {in, 1.3, 150'000}, {start, 1.3, 0},
             {geo::offset(start, geo::bearing_deg(in, start), 600.0), 0.8, 0}}, 401);
  act(s, t0 + 8, Act::ConsentAndShare, "sim-d1");
  act(s, t0 + 120, Act::GoOffline, "sim-d1", {}, 7 * 60'000);
  act(s, t0 + 720, Act::Reconnect, "sim-d1", {}, 1);
  if (narrate) {
    say(s, t0 + 8, "Tenzin, trekking alone, opts in and starts sharing.");
    say(s, t0 + 120, "Tenzin's phone loses network in the forest, just before he fords the river (Deep Water 4, caution, severity 4). It keeps evaluating zones ON THE DEVICE, from an index shipped to it as bytes, and queues events with Lamport stamps. Its clock is 7 minutes fast.");
    say(s, t0 + 245, "The server has heard nothing for 2 minutes: SignalLost. It does not pretend to know where Tenzin is.");
    say(s, t0 + 720, "Reconnect: the queue uploads (and is retried once, as a flaky link would). The reconciler admits each event once, orders by Lamport stamp, corrects the clock skew, and raises the late breach alert.");
  }
}

}  // namespace

// ─── Catalogue ──────────────────────────────────────────────────────────────

std::vector<ScenarioInfo> ScenarioRunner::catalogue() {
  return {
      {"walkthrough", "Complete walkthrough",
       "All four stories at once: normal walk, restricted-zone entry, group separation with dispatch, and an offline trekker who reconnects. The live demo.",
       900'000},
      {"normal", "A. Normal movement", "A group of five walks a loop clear of every hazard. No alerts are expected.", 600'000},
      {"entry", "B. Restricted-zone entry",
       "One tourist approaches and enters a restricted cliff zone and overstays its dwell limit; a second enters independently.",
       780'000},
      {"separation", "C. Group separation and dispatch",
       "A hiker is injured and left behind; stationary and straggler alerts form one incident; nearest responder, A* route.",
       780'000},
      {"offline", "D. Offline device and sync",
       "A trekker's phone loses network, evaluates zones locally, queues Lamport-stamped events, and reconciles on reconnect.",
       840'000},
  };
}

// ─── Runner ─────────────────────────────────────────────────────────────────

ScenarioRunner::ScenarioRunner(std::unique_ptr<Platform>* world, std::function<void()> rebuild)
    : world_(world), rebuild_(std::move(rebuild)) {}

ScenarioRunner::~ScenarioRunner() = default;

bool ScenarioRunner::load(const std::string& id, std::string* err, bool fresh) {
  bool known = false;
  for (const auto& c : catalogue()) if (c.id == id) { known = true; duration_ms_ = c.duration_ms; }
  if (!known) { if (err) *err = "unknown scenario"; return false; }
  if (!fresh) rebuild_();
  Platform& pf = **world_;
  if (!zone_named(pf, "Sonapani Waterfall Cliff") || !zone_named(pf, "Deep Water 4")) {
    if (err) *err = "scenarios need the Shillong zone set (data/zones/shillong_osm.geojson)";
    return false;
  }
  script_ = std::make_unique<Script>();
  Script& s = *script_;
  s.gps.open_sky_m = 5.0;
  s.gps.multipath_m = 18.0;
  s.gps.multipath_probability = 0.15;
  s.gps.dropout_probability = 0.0;
  s.gps.correlation = 0.9;
  if (id == "normal") build_normal(s, pf, 0, true);
  else if (id == "entry") build_entry(s, pf, 0, true);
  else if (id == "separation") build_separation(s, pf, 0, true);
  else if (id == "offline") build_offline(s, pf, 0, true);
  else {
    say(s, 0, "Steps 1-5: an operator creates tourist groups, simulated tourists join, opt in and start sharing.");
    build_normal(s, pf, 0, false);
    build_separation(s, pf, 5, false);
    build_entry(s, pf, 20, false);
    build_offline(s, pf, 30, false);
    say(s, 40, "Step 6: every fix goes through the evaluator against 38 OpenStreetMap hazard zones; each user's state is separate.");
    say(s, 32, "The administrator registers a temporary rockfall closure (restricted, severity 5) below Sonapani falls.");
    say(s, 220, "Steps 7-8: Ravi leaves the viewpoint toward the closure - watch for ZoneApproaching.");
    say(s, 215, "Nisha (Ridge Hikers) stops moving: a simulated injury.");
    say(s, 320, "Steps 9-11: ZoneEnter is confirmed by hysteresis; a severity-5 breach opens an incident with a recommended responder.");
    say(s, 275, "Tenzin's phone (solo trek) has had no network for 2 minutes: SignalLost. The server only knows the last contact.");
    say(s, 310, "Stationary anomaly for Nisha: a second incident, a different nearest responder.");
    say(s, 450, "Dwell limit exceeded inside the closure: the alert joins Ravi's incident (correlation).");
    say(s, 540, "Step 12: Nisha's group has split (union-find over the proximity graph); her separation outlasts 60 s - Straggler.");
    say(s, 600, "Steps 13-16: open Incidents - acknowledge, assign the recommended responder (A* road route), then resolve.");
    say(s, 750, "Tenzin reconnects: the offline queue is reconciled by Lamport order, the duplicate retry is rejected, late alerts appear.");
    say(s, 820, "Step 17: open Investigation, pick a past time, and see what the system knew then; verify an evidence proof.");
  }
  std::stable_sort(s.actions.begin(), s.actions.end(),
                   [](const Action& a, const Action& b) { return a.at < b.at; });
  // Create every simulated user up front (no password: they cannot sign in).
  const Principal sys = Principal::sys();
  for (auto& a : s.actors) {
    a.id = pf.create_user(sys, a.username, a.display, Role::Tourist, "", Source::Scenario).id;
  }
  narration_.clear();
  for (const auto& x : s.actions)
    if (x.kind == Act::Narrate) narration_.push_back({x.at, x.b, false});
  sync_reports_.clear();
  id_ = id;
  state_ = RunState::Paused;
  accum_ms_ = 0;
  return true;
}

void ScenarioRunner::start() { if (script_) state_ = RunState::Running; }
void ScenarioRunner::pause() { if (state_ == RunState::Running) state_ = RunState::Paused; }
void ScenarioRunner::resume() { if (state_ == RunState::Paused) state_ = RunState::Running; }

void ScenarioRunner::reset() {
  if (id_.empty()) return;
  std::string err;
  const double keep = speed_;
  load(id_, &err);
  speed_ = keep;
}

void ScenarioRunner::set_speed(double x) {
  if (!std::isfinite(x)) return;
  speed_ = std::min(60.0, std::max(0.25, x));
}

void ScenarioRunner::step(int ticks) {
  if (!script_ || state_ == RunState::Running) return;
  for (int i = 0; i < ticks && state_ != RunState::Finished; ++i) tick_once();
}

int ScenarioRunner::advance(double real_seconds) {
  if (!script_ || state_ != RunState::Running) return 0;
  accum_ms_ += real_seconds * 1000.0 * speed_;
  int n = 0;
  // Bounded per call, so a stalled loop cannot try to replay an hour at once.
  while (accum_ms_ >= 1000.0 && n < 240 && state_ == RunState::Running) {
    accum_ms_ -= 1000.0;
    tick_once();
    ++n;
  }
  if (accum_ms_ > 5000.0) accum_ms_ = 0;
  return n;
}

void ScenarioRunner::tick_once() {
  Script& s = *script_;
  Platform& pf = **world_;
  const Timestamp t = pf.now() + 1000;
  const Principal sys = Principal::sys();

  // 1. Timed actions due by t.
  while (s.next_action < s.actions.size() && s.actions[s.next_action].at <= t) {
    const Action& x = s.actions[s.next_action++];
    Actor* a = s.actor(x.a);
    switch (x.kind) {
      case Act::CreateGroup:
        s.groups.emplace_back(x.a, pf.create_group(sys, x.b, Source::Scenario).id);
        break;
      case Act::Join:
        if (a) pf.add_member(sys, s.group(x.b), a->id);
        break;
      case Act::ConsentAndShare:
        if (a) { pf.set_consent(sys, a->id, true); pf.start_sharing(sys, a->id); }
        break;
      case Act::StopSharing:
        if (a) pf.stop_sharing(sys, a->id);
        break;
      case Act::SetDwell:
        if (const fence::Zone* z = zone_named(pf, x.b)) pf.set_zone_dwell(sys, z->id, x.n);
        break;
      case Act::GoOffline:
        if (a) { a->skew_ms = x.n; pf.go_offline(sys, a->id, x.n); }
        break;
      case Act::Reconnect:
        if (a) {
          Platform::SyncReport rep;
          if (pf.reconnect(sys, a->id, size_t(x.n), &rep).ok())
            sync_reports_.push_back(a->display + ": " + std::to_string(rep.uploaded) + " events uploaded, " +
                                    std::to_string(rep.admitted) + " admitted, " +
                                    std::to_string(rep.duplicates) + " duplicates rejected on retry, " +
                                    std::to_string(rep.reordered) + " out of wall-clock order, clock skew " +
                                    std::to_string(rep.estimated_skew_ms / 1000) + " s corrected, " +
                                    std::to_string(rep.alerts_raised) + " alerts raised late");
        }
        break;
      case Act::BatteryAware:
        if (a) { a->battery_aware = x.n != 0; pf.set_battery_aware(sys, a->id, x.n != 0); }
        break;
      case Act::CreateZone: {
        Platform::ZoneInput zi;
        zi.name = x.a;
        zi.kind = fence::ZoneKind::Restricted;
        zi.severity = 5;
        zi.max_dwell_s = x.n;
        for (int k = 0; k < 7; ++k)
          zi.ring.push_back(geo::offset(x.at_pos, 360.0 * k / 7.0 + 10.0, x.radius_m * (k % 2 ? 0.85 : 1.0)));
        pf.create_zone(sys, zi, Source::Scenario);
        break;
      }
      case Act::Narrate:
        for (auto& n : narration_) if (n.at_ms == x.at && n.text == x.b) n.done = true;
        break;
    }
  }

  // 2. Move every actor and send its fix.
  for (auto& a : s.actors) {
    if (a.leg < a.legs.size() && t >= a.pause_until) {
      const Leg& L = a.legs[a.leg];
      const double d = geo::distance_m(a.truth, L.to);
      const double stepm = L.speed_mps * 1.0;
      if (d <= stepm || L.speed_mps <= 0.0) {
        a.truth = L.speed_mps <= 0.0 ? a.truth : L.to;
        a.pause_until = L.speed_mps <= 0.0 ? L.pause_ms : t + L.pause_ms;
        ++a.leg;
      } else {
        a.truth = geo::offset(a.truth, geo::bearing_deg(a.truth, L.to), stepm);
      }
    }
    const User* u = pf.user(a.id);
    // The drift and the random stream advance every tick whether or not a fix
    // is sent, so turning the battery policy on changes WHICH fixes are sent,
    // never the noise on the ones that are.
    const geo::UncertainPoint fx = sim::apply_gps_error(a.truth, t, s.gps, a.drift, a.rng);
    if (!u || !u->sharing) continue;
    FixInput f;
    f.lat = fx.pos.lat; f.lon = fx.pos.lon; f.accuracy_m = fx.accuracy_m; f.t_ms = t;
    if (pf.is_offline(a.id)) { pf.device_fix(a.id, f); ++a.sent; continue; }
    if (a.battery_aware) {
      const Tracked* tr = pf.tracked(a.id);
      const int64_t interval = tr && tr->has_fix ? tr->tourist.sampler.current_interval_ms() : 0;
      if (t - a.last_sent < interval) { ++a.skipped; continue; }
    }
    pf.ingest_fix(sys, a.id, f);
    a.last_sent = t;
    ++a.sent;
  }

  // 3. Everything time-driven.
  pf.tick(t);
  if (t >= duration_ms_) state_ = RunState::Finished;
}

std::vector<ScenarioRunner::DeviceView> ScenarioRunner::devices() const {
  std::vector<DeviceView> out;
  if (!script_) return out;
  const Platform& pf = **world_;
  for (const auto& a : script_->actors) {
    DeviceView v;
    v.user = a.id;
    v.name = a.display;
    v.offline = pf.is_offline(a.id);
    v.queued = pf.device_pending(a.id);
    v.clock_skew_ms = v.offline ? a.skew_ms : 0;
    v.battery_aware = a.battery_aware;
    v.fixes_sent = a.sent;
    v.fixes_skipped = a.skipped;
    out.push_back(v);
  }
  return out;
}

}  // namespace safetrail::app
