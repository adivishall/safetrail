// Multi-user tracking: isolation between users, consent and sessions, and the
// ingestion boundary (duplicates, out-of-order, jumps, invalid and refused fixes).
//
// The isolation claim in include/safetrail/app/platform.hpp is "by
// construction" -- each user owns their Tourist and so their per-zone state.
// These tests check it from the outside anyway, because "by construction" is a
// claim about the code as it is today and a test is a claim about every
// future version of it.
#include "safetrail/app/platform.hpp"
#include "test_harness.hpp"

#include <algorithm>

using namespace safetrail;
using app::FixStatus;

namespace {

app::Principal as(app::UserId id, app::Role r) {
  app::Principal p;
  p.id = id;
  p.role = r;
  return p;
}

// A 222 m x 200 m square around (25.581, 91.881).
app::Platform::ZoneInput square(const std::string& name, fence::ZoneKind kind, int sev) {
  app::Platform::ZoneInput z;
  z.name = name;
  z.kind = kind;
  z.severity = sev;
  z.ring = {{25.580, 91.880}, {25.580, 91.882}, {25.582, 91.882}, {25.582, 91.880}};
  return z;
}

const geo::LatLon kInside{25.581, 91.881};
const geo::LatLon kOutside{25.5765, 91.881};   // ~390 m south of the square

app::FixInput at(geo::LatLon p, int64_t t_ms, double acc = 5.0) {
  app::FixInput f;
  f.lat = p.lat; f.lon = p.lon; f.accuracy_m = acc; f.t_ms = t_ms;
  return f;
}

size_t count_log(const app::Platform& pf, app::LogKind k, app::UserId u) {
  size_t n = 0;
  for (const auto& e : pf.log()) if (e.kind == k && e.user == u) ++n;
  return n;
}

struct World {
  app::PlatformConfig cfg;
  std::unique_ptr<app::Platform> pf;
  app::UserId admin = 0, op = 0, a = 0, b = 0, c = 0;
  ZoneId zone = 0;

  World() {
    cfg.pbkdf2_iterations = 1000;          // fast tests; production cost is the default
    pf = std::make_unique<app::Platform>(cfg);
    const auto sys = app::Principal::sys();
    admin = pf->create_user(sys, "admin", "Admin", app::Role::Admin, "admin-password-1").id;
    op = pf->create_user(sys, "op", "Operator", app::Role::Operator, "operator-pass-1").id;
    a = pf->create_user(sys, "alice", "Alice", app::Role::Tourist, "alice-password").id;
    b = pf->create_user(sys, "bob", "Bob", app::Role::Tourist, "bob-password-1").id;
    c = pf->create_user(sys, "chen", "Chen", app::Role::Tourist, "chen-password-1").id;
    zone = pf->create_zone(as(admin, app::Role::Admin), square("Cliff", fence::ZoneKind::Restricted, 5)).id;
    for (app::UserId u : {a, b, c}) {
      pf->set_consent(as(u, app::Role::Tourist), u, true);
      pf->start_sharing(as(u, app::Role::Tourist), u);
    }
  }
  app::FixResult fix(app::UserId u, geo::LatLon p, int64_t t, double acc = 5.0) {
    return pf->ingest_fix(as(u, app::Role::Tourist), u, at(p, t, acc));
  }
  geo::Containment state(app::UserId u) const {
    const auto* tr = pf->tracked(u);
    const auto* st = tr ? tr->tourist.peek_state(zone) : nullptr;
    return st ? st->confirmed : geo::Containment::Outside;
  }
};

}  // namespace

int main() {
  // ── Consent, sessions and who may switch tracking on ──────────────────────
  {
    World w;
    auto sys = app::Principal::sys();
    const app::UserId d = w.pf->create_user(sys, "dana", "Dana", app::Role::Tourist, "dana-password").id;
    t::ok(!w.pf->start_sharing(as(d, app::Role::Tourist), d).ok(), "no session without consent");
    t::ok(w.pf->set_consent(as(w.op, app::Role::Operator), d, true).code == 403,
          "an operator cannot give consent on a user's behalf");
    t::ok(w.pf->set_consent(as(w.admin, app::Role::Admin), d, true).code == 403,
          "nor can an administrator");
    t::ok(w.pf->set_consent(as(d, app::Role::Tourist), d, true).ok(), "the user can");
    t::ok(w.pf->start_sharing(as(w.op, app::Role::Operator), d).code == 403,
          "an operator cannot start someone's tracking");
    auto r = w.fix(d, kOutside, 1000);
    t::ok(r.status == FixStatus::NotTracking, "a fix before the session starts is refused");
    t::ok(w.pf->tracked(d)->trail.empty(), "...and not stored anywhere");
    t::ok(w.pf->start_sharing(as(d, app::Role::Tourist), d).ok(), "user starts sharing");
    t::ok(w.fix(d, kOutside, 1000).status == FixStatus::Accepted, "now fixes are accepted");
    auto spoof = w.pf->ingest_fix(as(w.a, app::Role::Tourist), d, at(kOutside, 2000));
    t::ok(spoof.status == FixStatus::NotTracking, "one user cannot post another user's fix");
    t::ok(w.pf->set_consent(as(d, app::Role::Tourist), d, false).ok(), "consent withdrawn");
    t::ok(!w.pf->user(d)->sharing, "withdrawing consent ends the session");
    t::ok(w.fix(d, kOutside, 3000).status == FixStatus::NotTracking, "and fixes are refused again");
    t::ok(count_log(*w.pf, app::LogKind::ConsentWithdrawn, d) == 1, "withdrawal is logged");
  }

  // ── Two users enter the same zone independently ───────────────────────────
  {
    World w;
    for (int s = 1; s <= 3; ++s) { w.fix(w.a, kOutside, s * 1000); w.fix(w.b, kOutside, s * 1000); }
    // Alice walks in; Bob stays outside.
    for (int s = 4; s <= 15; ++s) { w.fix(w.a, kInside, s * 1000); w.fix(w.b, kOutside, s * 1000); }
    t::ok(w.state(w.a) == geo::Containment::Inside, "Alice is inside");
    t::ok(w.state(w.b) == geo::Containment::Outside, "Bob, outside the whole time, is not");
    t::ok(count_log(*w.pf, app::LogKind::ZoneEnter, w.a) == 1, "one enter event for Alice");
    t::ok(count_log(*w.pf, app::LogKind::ZoneEnter, w.b) == 0, "none for Bob");
    // Now Bob enters too, later and on his own schedule.
    for (int s = 16; s <= 30; ++s) { w.fix(w.a, kInside, s * 1000); w.fix(w.b, kInside, s * 1000); }
    t::ok(w.state(w.b) == geo::Containment::Inside, "Bob is now inside");
    t::ok(count_log(*w.pf, app::LogKind::ZoneEnter, w.a) == 1, "Bob's entry did not re-fire Alice's");
    t::ok(count_log(*w.pf, app::LogKind::ZoneEnter, w.b) == 1, "one enter for Bob");
    t::ok(w.pf->users_inside(w.zone) == 2, "zone reports two users inside");

    // One leaves while the other remains.
    for (int s = 31; s <= 45; ++s) { w.fix(w.a, kOutside, s * 1000); w.fix(w.b, kInside, s * 1000); }
    t::ok(w.state(w.a) == geo::Containment::Outside, "Alice has left");
    t::ok(w.state(w.b) == geo::Containment::Inside, "Bob, still inside, is unaffected");
    t::ok(count_log(*w.pf, app::LogKind::ZoneExit, w.a) == 1, "exit logged for Alice");
    t::ok(count_log(*w.pf, app::LogKind::ZoneExit, w.b) == 0, "no exit for Bob");
    t::ok(w.pf->users_inside(w.zone) == 1, "one user inside");

    // A user stops tracking: their state goes, the other user's does not.
    t::ok(w.pf->stop_sharing(as(w.b, app::Role::Tourist), w.b).ok(), "Bob stops sharing");
    t::ok(w.pf->tracked(w.b)->tourist.zone_states.empty(), "Bob's zone state is discarded");
    t::ok(count_log(*w.pf, app::LogKind::ZoneExit, w.b) == 0,
          "...without inventing an exit the system never observed");
    t::ok(w.fix(w.b, kInside, 46000).status == FixStatus::NotTracking, "Bob's fixes are refused");
    t::ok(w.fix(w.a, kOutside, 46000).status == FixStatus::Accepted, "Alice is still tracked");
    t::ok(w.pf->users_inside(w.zone) == 0, "nobody inside");

    // Alerts belong to the right person.
    size_t alice_breach = 0, bob_breach = 0;
    for (const auto& r : w.pf->alerts())
      if (r.a.kind == alert::AlertKind::ZoneBreach) (r.a.tourist == w.a ? alice_breach : bob_breach)++;
    t::ok(alice_breach == 1 && bob_breach == 1, "one breach alert each, attributed to each");
  }

  // ── The ingestion boundary ────────────────────────────────────────────────
  {
    World w;
    t::ok(w.fix(w.c, kOutside, 1000).status == FixStatus::Accepted, "first fix");
    t::ok(w.fix(w.c, kOutside, 1000).status == FixStatus::Duplicate, "identical fix is a duplicate");
    auto seqd = w.pf->ingest_fix(as(w.c, app::Role::Tourist), w.c, [] {
      app::FixInput f = at(kOutside, 2000); f.seq = 7; return f; }());
    t::ok(seqd.status == FixStatus::Accepted, "sequence-numbered fix accepted");
    auto again = w.pf->ingest_fix(as(w.c, app::Role::Tourist), w.c, [] {
      app::FixInput f = at({25.5766, 91.881}, 2500); f.seq = 7; return f; }());
    t::ok(again.status == FixStatus::Duplicate, "same sequence number is a duplicate even if moved");
    t::ok(w.fix(w.c, kOutside, 3000).status == FixStatus::Accepted, "next fix");
    auto late = w.fix(w.c, {25.5764, 91.881}, 1500);
    t::ok(late.status == FixStatus::OutOfOrder, "an older fix is out of order");
    const auto* tr = w.pf->tracked(w.c);
    bool late_kept = false;
    for (const auto& p : tr->trail) if (p.t_ms == 1500 && (p.flags & app::kTrailLate)) late_kept = true;
    t::ok(late_kept, "...recorded in the trail, flagged late");
    bool sorted = std::is_sorted(tr->trail.begin(), tr->trail.end(),
                                 [](const app::TrailPoint& x, const app::TrailPoint& y) { return x.t_ms < y.t_ms; });
    t::ok(sorted, "the trail stays in time order (binary search depends on it)");
    t::ok(tr->tourist.last_fix.t_ms == 3000, "...but not evaluated: the engine's clock did not rewind");

    t::ok(w.fix(w.c, {95.0, 91.0}, 4000).status == FixStatus::Invalid, "latitude 95 is invalid");
    t::ok(w.fix(w.c, kOutside, 4000, -3).status == FixStatus::Invalid, "negative accuracy is invalid");
    t::ok(w.fix(w.c, kOutside, 4000, 1e9).status == FixStatus::Invalid, "absurd accuracy is invalid");
    w.pf->tick(5000);
    t::ok(w.fix(w.c, kOutside, 5000 + 3600'000).status == FixStatus::Invalid, "an hour in the future is refused");

    // A GPS jump: 4.5 km in one second.
    auto jump = w.fix(w.c, {25.62, 91.881}, 6000);
    t::ok(jump.status == FixStatus::Accepted && jump.jump, "a jump is accepted but flagged");
    t::ok((w.pf->tracked(w.c)->trail.back().flags & app::kTrailJump) != 0, "and marked in the trail");

    // Inaccurate fixes go through the engine's usability gate, not dropped here.
    auto noisy = w.fix(w.c, {25.62, 91.881}, 7000, 400);
    t::ok(noisy.status == FixStatus::Accepted, "a 400 m fix is accepted at the boundary");
    t::ok((w.pf->tracked(w.c)->trail.back().flags & app::kTrailNoisy) != 0, "and marked too noisy to use");
    const auto& st = w.pf->stats();
    t::ok(st.fixes_duplicate == 2 && st.fixes_out_of_order == 1 && st.jumps >= 1, "counters agree");
  }

  // ── Missing fixes: signal lost, then contact restored ─────────────────────
  {
    World w;
    for (int s = 1; s <= 5; ++s) w.fix(w.a, kOutside, s * 1000);
    for (int64_t t = 6000; t <= 200'000; t += 1000) w.pf->tick(t);   // no fixes for ~3 minutes
    size_t lost = 0;
    for (const auto& r : w.pf->alerts())
      if (r.a.kind == alert::AlertKind::SignalLost && r.a.tourist == w.a) ++lost;
    t::ok(lost == 1, "one SignalLost alert after two minutes of silence");
    t::ok(w.pf->status_label(w.a) == "no contact", "status shows no contact");
    t::ok(w.fix(w.a, kOutside, 201'000).status == FixStatus::Accepted, "a fix arrives");
    bool resolved = false;
    for (const auto& r : w.pf->alerts())
      if (r.a.kind == alert::AlertKind::SignalLost && r.a.state == alert::AlertState::Resolved) resolved = true;
    t::ok(resolved, "the SignalLost alert resolves itself on contact");
  }

  // ── Location access and erasure ───────────────────────────────────────────
  {
    World w;
    for (int s = 1; s <= 5; ++s) w.fix(w.a, kOutside, s * 1000);
    t::ok(w.pf->may_view_location(as(w.op, app::Role::Operator), w.a), "operators may see tracked users");
    t::ok(!w.pf->may_view_location(as(w.b, app::Role::Tourist), w.a), "a tourist may not see another");
    t::ok(w.pf->may_view_location(as(w.a, app::Role::Tourist), w.a), "but may see themself");
    const auto sys = app::Principal::sys();
    const app::UserId rsp = w.pf->create_user(sys, "medic", "Medic", app::Role::Responder, "medic-password").id;
    t::ok(!w.pf->may_view_location(as(rsp, app::Role::Responder), w.a),
          "a responder sees nobody without an assignment");
    w.pf->audit_access(as(w.op, app::Role::Operator), w.a, "trail", true);
    t::ok(w.pf->access_log().size() == 1 && w.pf->access_evidence().size() == 1,
          "an operator's read is audited, in a tamper-evident log");
    t::ok(w.pf->erase_history(as(w.b, app::Role::Tourist), w.a).code == 403, "others cannot erase a history");
    t::ok(w.pf->erase_history(as(w.a, app::Role::Tourist), w.a).ok(), "the user can");
    t::ok(w.pf->tracked(w.a)->trail.empty(), "the trail is gone");
    t::ok(count_log(*w.pf, app::LogKind::HistoryErased, w.a) == 1, "and the erasure itself is on record");
  }

  // ── Evidence: every log entry is provably in the Merkle log ───────────────
  {
    World w;
    for (int s = 1; s <= 20; ++s) w.fix(w.a, s < 6 ? kOutside : kInside, s * 1000);
    const auto& lg = w.pf->log();
    t::ok(lg.size() == w.pf->evidence().size(), "one evidence leaf per log entry");
    const auto root = w.pf->evidence().root();
    bool all = true;
    for (const auto& e : lg) {
      const std::string c = app::canonical(e);
      const auto leaf = evidence::leaf_hash(reinterpret_cast<const uint8_t*>(c.data()), c.size());
      if (!w.pf->evidence().prove(e.evidence_index).verify(leaf, root)) all = false;
    }
    t::ok(all, "every entry's inclusion proof verifies against the root");
    app::LogEntry forged = lg.back();
    forged.detail += " (edited)";
    const std::string c = app::canonical(forged);
    const auto leaf = evidence::leaf_hash(reinterpret_cast<const uint8_t*>(c.data()), c.size());
    t::ok(!w.pf->evidence().prove(forged.evidence_index).verify(leaf, root), "an edited entry does not");
    bool no_coords = true;
    for (const auto& e : lg) if (app::canonical(e).find("25.58") != std::string::npos) no_coords = false;
    t::ok(no_coords, "no coordinates are written into the evidence log");
  }
  return t::report("app/multi_user");
}
