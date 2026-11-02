// The demonstration scenarios, run end to end through the real platform: each
// must produce the story it claims to tell, and the same story every time.
//
// SAFETRAIL_VERBOSE=1 prints every log entry, which is how the scripted paths
// were checked against the real OpenStreetMap zones.
#include "safetrail/app/scenario.hpp"
#include "safetrail/app/world.hpp"
#include "test_harness.hpp"

#include <cstdio>
#include <cstdlib>

using namespace safetrail;
using app::LogKind;

namespace {

struct Harness {
  std::unique_ptr<app::Platform> world;
  std::unique_ptr<app::ScenarioRunner> runner;
  Harness() {
    runner = std::make_unique<app::ScenarioRunner>(&world, [this] {
      app::WorldOptions o;
      o.config.pbkdf2_iterations = 1000;
      std::vector<std::string> gen;
      o.staff = app::staff_from_environment(true, &gen);
      std::string err;
      world = app::build_world(o, &err);
      if (!world) { std::printf("world: %s\n", err.c_str()); std::exit(1); }
    });
  }
  bool run(const std::string& id) {
    std::string err;
    if (!runner->load(id, &err)) { std::printf("load %s: %s\n", id.c_str(), err.c_str()); return false; }
    while (runner->state() != app::RunState::Finished) runner->step(60);
    if (std::getenv("SAFETRAIL_VERBOSE")) {
      for (const auto& e : world->log()) {
        if (e.kind == LogKind::UserCreated || e.kind == LogKind::ResponderCreated) continue;
        const app::User* u = world->user(e.user);
        const fence::Zone* z = world->zones().get(e.zone);
        std::printf("%6lld %-20s %-24s %-26s %s%s\n", (long long)(e.t_ms / 1000), app::to_string(e.kind),
                    u ? u->display_name.c_str() : "-", z ? z->name.c_str() : "-", e.detail.c_str(),
                    e.late ? " [late]" : "");
      }
    }
    return true;
  }
  app::UserId uid(const std::string& name) const {
    const app::User* u = world->user_by_name(name);
    return u ? u->id : kNoId;
  }
  ZoneId zone(const std::string& name) const {
    for (ZoneId id : world->zones().all_ids()) if (world->zones().get(id)->name == name) return id;
    return kNoId;
  }
  size_t count(LogKind k, app::UserId u = kNoId, ZoneId z = kNoId) const {
    size_t n = 0;
    for (const auto& e : world->log())
      if (e.kind == k && (u == kNoId || e.user == u) && (z == kNoId || e.zone == z)) ++n;
    return n;
  }
  size_t alerts_of(alert::AlertKind k, app::UserId u) const {
    size_t n = 0;
    for (const auto& r : world->alerts()) if (r.a.kind == k && r.a.tourist == u) ++n;
    return n;
  }
  std::string digest() const {
    std::string d;
    for (const auto& e : world->log()) d += app::canonical(e) + "\n";
    return d;
  }
};

}  // namespace

int main() {
  // ── A: nothing happens, and nothing should ────────────────────────────────
  {
    Harness h;
    t::ok(h.run("normal"), "normal scenario runs");
    size_t serious = 0;
    for (const auto& r : h.world->alerts())
      if (r.a.kind == alert::AlertKind::ZoneBreach && r.a.severity >= 3) ++serious;
    t::ok(serious == 0, "no serious breach on a walk clear of every hazard");
    t::ok(h.world->correlator().stats().incidents_opened == 0, "no incident");
    t::ok(h.count(LogKind::SessionStarted) == 5, "five tracking sessions");
    t::ok(h.world->stats().fixes_accepted > 2500, "every member sent fixes throughout");
    const auto* g = h.world->group_rec(0);
    t::ok(g && !g->fragmented && g->stragglers.empty(), "the group stayed together");
  }

  // ── B: restricted-zone entry ──────────────────────────────────────────────
  {
    Harness h;
    t::ok(h.run("entry"), "entry scenario runs");
    const ZoneId cliff = h.zone("SIM \xC2\xB7 Rockfall closure (Sonapani trail)");
    t::ok(cliff != kNoId, "the administrator registered the closure zone");
    const auto ravi = h.uid("sim-b1"), meera = h.uid("sim-b2"), joseph = h.uid("sim-b3"), lin = h.uid("sim-b4");
    t::ok(h.count(LogKind::ZoneApproaching, ravi, cliff) >= 1, "Ravi is warned before he crosses");
    t::ok(h.count(LogKind::ZoneEnter, ravi, cliff) == 1, "exactly one confirmed entry for Ravi");
    t::ok(h.count(LogKind::DwellExceeded, ravi, cliff) == 1, "Ravi overstays the 2-minute limit");
    t::ok(h.count(LogKind::ZoneExit, ravi, cliff) == 1, "and leaves");
    t::ok(h.count(LogKind::ZoneEnter, meera, cliff) == 1, "Meera enters independently");
    t::ok(h.count(LogKind::ZoneExit, meera, cliff) == 1, "and leaves");
    t::ok(h.count(LogKind::DwellExceeded, meera, cliff) == 0, "Meera's short visit does not overstay");
    t::ok(h.count(LogKind::ZoneEnter, joseph, cliff) + h.count(LogKind::ZoneEnter, lin, cliff) == 0,
          "the two who stayed at the viewpoint never enter");
    const app::IncidentCase* c = nullptr;
    for (const auto& r : h.world->alerts())
      if (r.a.tourist == ravi && r.a.kind == alert::AlertKind::ZoneBreach) c = h.world->incident_case(r.a.incident);
    t::ok(c != nullptr, "Ravi's breach opened an incident");
    t::ok(c && c->recommended != kNoId && c->recommended_m > 0, "with a recommended responder by road distance");
    size_t ravi_alerts_in_case = 0;
    if (c)
      for (AlertId a : h.world->correlator().get(c->id)->alerts)
        if (h.world->alerts()[a].a.tourist == ravi) ++ravi_alerts_in_case;
    t::ok(ravi_alerts_in_case >= 2, "breach and dwell overrun are one incident, not two");
    Timestamp ravi_in = 0;
    for (const auto& e : h.world->log())
      if (e.kind == LogKind::ZoneEnter && e.user == ravi && e.zone == cliff) ravi_in = e.t_ms;
    size_t open_warnings = 0;
    for (const auto& r : h.world->alerts())
      if ((r.a.kind == alert::AlertKind::ZoneApproaching || r.a.kind == alert::AlertKind::ZoneUncertain) &&
          r.a.tourist == ravi && r.a.zone == cliff && r.a.raised_ms <= ravi_in &&
          r.a.state == alert::AlertState::Open)
        ++open_warnings;
    t::ok(open_warnings == 0, "warnings that forecast Ravi's entry were settled by it");
    // The operator's side of the story.
    const app::User* op = h.world->user_by_name("operator");
    app::Principal p; p.id = op->id; p.role = app::Role::Operator;
    t::ok(h.world->acknowledge_incident(p, c->id).ok(), "operator acknowledges");
    t::ok(h.world->assign_incident(p, c->id).ok(), "operator assigns the recommended responder");
    const app::IncidentCase* c2 = h.world->incident_case(c->id);
    t::ok(c2->route.found && c2->route.points.size() >= 2, "an A* route over the road graph");
    t::ok(c2->route.astar_expanded <= c2->route.dijkstra_expanded,
          "A* settles no more junctions than Dijkstra for the same pair");
    t::ok(!h.world->responders()[c2->responder].available, "the responder is now busy");
    t::ok(h.world->resolve_incident(p, c->id, "escorted back to the viewpoint").ok(), "resolved");
    t::ok(h.world->responders()[c2->responder].available, "and the responder is free again");
    for (AlertId a : h.world->correlator().get(c->id)->alerts)
      t::ok(h.world->alerts()[a].a.state == alert::AlertState::Resolved, "incident alerts resolved with it");
  }

  // ── C: separation, one incident, dispatch ─────────────────────────────────
  {
    Harness h;
    t::ok(h.run("separation"), "separation scenario runs");
    const auto nisha = h.uid("sim-c4");
    t::ok(h.alerts_of(alert::AlertKind::Stationary, nisha) == 1, "Nisha's stillness is detected");
    t::ok(h.alerts_of(alert::AlertKind::Straggler, nisha) == 1, "she is reported separated from the group");
    size_t other_stragglers = 0;
    for (const auto& r : h.world->alerts())
      if (r.a.kind == alert::AlertKind::Straggler && r.a.tourist != nisha) ++other_stragglers;
    t::ok(other_stragglers == 0, "nobody else is reported separated");
    IncidentId st_inc = kNoId, sg_inc = kNoId;
    Timestamp st_t = 0, sg_t = 0;
    for (const auto& r : h.world->alerts()) {
      if (r.a.tourist != nisha) continue;
      if (r.a.kind == alert::AlertKind::Stationary) { st_inc = r.a.incident; st_t = r.a.raised_ms; }
      if (r.a.kind == alert::AlertKind::Straggler) { sg_inc = r.a.incident; sg_t = r.a.raised_ms; }
    }
    t::ok(st_inc != kNoId && st_inc == sg_inc, "both alerts are one incident");
    const auto* g = h.world->group_rec(0);
    t::ok(g && g->fragmented && g->components >= 2, "the group is observed in two or more components");
    t::ok(h.count(LogKind::GroupSeparated) >= 1, "the split is logged");
    t::ok(sg_t - st_t > 60'000, "separation was confirmed over time, not on first sight");
  }

  // ── D: offline device and synchronisation ─────────────────────────────────
  {
    Harness h;
    t::ok(h.run("offline"), "offline scenario runs");
    const auto tenzin = h.uid("sim-d1");
    const ZoneId water = h.zone("Deep Water 4");
    t::ok(h.alerts_of(alert::AlertKind::SignalLost, tenzin) == 1, "the server notices the silence");
    bool lost_resolved = false;
    for (const auto& r : h.world->alerts())
      if (r.a.kind == alert::AlertKind::SignalLost && r.a.state == alert::AlertState::Resolved) lost_resolved = true;
    t::ok(lost_resolved, "SignalLost resolves on reconnect");
    size_t late_enter = 0, live_enter = 0;
    for (const auto& e : h.world->log())
      if (e.kind == LogKind::ZoneEnter && e.user == tenzin && e.zone == water) (e.late ? late_enter : live_enter)++;
    t::ok(late_enter == 1 && live_enter == 0, "the entry, made offline, arrives late and exactly once");
    const auto& rs = h.world->reconciler().stats();
    t::ok(rs.merged > 0 && rs.duplicates_rejected == rs.merged, "the retried batch is rejected event for event");
    t::ok(h.runner->sync_reports().size() == 1, "one sync report");
    // Bitemporal history: during the offline period the server did not know
    // where Tenzin was, even though it knows now.
    const Timestamp mid = 400'000;
    const app::TrailPoint* then = h.world->position_at(tenzin, mid);
    t::ok(then && then->t_ms <= 125'000, "at t=400 s the system's latest knowledge is the last pre-offline fix");
    const auto& trail = h.world->tracked(tenzin)->trail;
    bool synced_present = false;
    for (const auto& p : trail) if ((p.flags & app::kTrailSynced) && p.t_ms > 300'000 && p.t_ms < 500'000) synced_present = true;
    t::ok(synced_present, "...while the trail now holds the synced fixes from that period");
    // Lamport: every synced zone event carries the device's stamp.
    bool device_stamps = true;
    for (const auto& e : h.world->log())
      if (e.late && e.kind == LogKind::ZoneEnter && e.stamp.device != tenzin + 1) device_stamps = false;
    t::ok(device_stamps, "synced events keep the device's Lamport stamp");
  }

  // ── Determinism ───────────────────────────────────────────────────────────
  {
    Harness a, b;
    a.run("entry");
    b.run("entry");
    t::ok(a.digest() == b.digest(), "the same scenario yields a byte-identical event log");
    t::ok(a.world->evidence().root() == b.world->evidence().root(), "and the same evidence root");
  }

  // ── The full walkthrough runs to completion ───────────────────────────────
  {
    Harness h;
    t::ok(h.run("walkthrough"), "walkthrough runs");
    t::ok(h.world->correlator().stats().incidents_opened >= 3, "it produces several incidents");
    t::ok(h.runner->narration().back().done, "every narration step was reached");
  }
  return t::report("app/scenario");
}
