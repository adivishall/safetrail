// The hysteresis state machine, transition by transition.
//
// It sits between the three-valued geometry verdict and the event stream, so
// every flap it lets through is a false alert and every transition it swallows
// is a missed one. Two real defects are pinned here:
//
//   * Uncertain bypassed the filter. From Outside, an Uncertain verdict was
//     reported at once and the next Outside verdict silently reset it, so a fix
//     alternating between 4 m and 35 m accuracy near a boundary emitted a new
//     ZoneUncertain on every alternation.
//   * An Uncertain verdict while an exit was pending was reported as Uncertain,
//     which the evaluator emits as a ZoneExit; the next inside fix re-entered.
//     An exit/enter flap pair straight through the filter.
#include "../test_harness.hpp"

#include <string>
#include <vector>

#include "safetrail/fence/evaluator.hpp"
#include "safetrail/fence/hysteresis.hpp"
#include "safetrail/index/quadtree.hpp"

using namespace safetrail;
using namespace safetrail::fence;
using geo::Containment;

namespace {

// One observation: the verdict and the signed distance it came from.
struct Obs { Containment raw; double sd; };
const Obs kDeepIn{Containment::Inside, -40.0};
const Obs kShallowIn{Containment::Inside, -5.0};
const Obs kUnsure{Containment::Uncertain, 10.0};
const Obs kNearOut{Containment::Outside, 12.0};     // outside, but inside the exit margin
const Obs kClearOut{Containment::Outside, 60.0};

// Feeds observations one second apart and records the reported state after each.
std::vector<Containment> feed(HysteresisState& h, const std::vector<Obs>& obs, int64_t& t,
                              const HysteresisConfig& cfg = {}) {
  std::vector<Containment> out;
  for (const Obs& o : obs) { out.push_back(h.update(o.raw, o.sd, t, cfg)); t += 1000; }
  return out;
}

// Number of changes in the reported state -- what the evaluator turns into events.
int changes(Containment start, const std::vector<Containment>& seq) {
  int n = 0;
  for (Containment c : seq) { if (c != start) ++n; start = c; }
  return n;
}

HysteresisState inside_state(int64_t& t) {
  HysteresisState h;
  feed(h, std::vector<Obs>(8, kDeepIn), t);             // 8 s deep inside: confirmed
  return h;
}

}  // namespace

int main() {
  // ── The basic Schmitt trigger ──────────────────────────────────────────────
  {
    int64_t t = 0;
    HysteresisState h;
    const auto s = feed(h, {kDeepIn, kDeepIn, kDeepIn, kDeepIn, kDeepIn, kDeepIn}, t);
    t::ok(s[1] == Containment::Outside && s.back() == Containment::Inside,
          "entry needs confirm_samples deep fixes and the minimum dwell");
    const auto e = feed(h, {kClearOut, kClearOut, kClearOut}, t);
    t::ok(e[0] == Containment::Inside && e[1] == Containment::Inside &&
              e[2] == Containment::Outside,
          "exit needs confirm_samples clearly-outside fixes");
  }
  {
    int64_t t = 0;
    HysteresisState h = inside_state(t);
    const auto s = feed(h, {kNearOut, kShallowIn, kNearOut, kNearOut, kShallowIn}, t);
    t::ok(changes(Containment::Inside, s) == 0,
          "drift inside the dead band between the margins never leaves Inside");
  }

  // ── Regression: Uncertain during a pending exit is not an exit ─────────────
  {
    int64_t t = 0;
    HysteresisState h = inside_state(t);
    const auto s = feed(h, {kClearOut, kUnsure, kDeepIn, kDeepIn}, t);
    t::ok(changes(Containment::Inside, s) == 0,
          "Inside -> pending exit -> Uncertain -> inside reports no change at all "
          "(it used to report Uncertain, i.e. a ZoneExit, then re-enter)");
    t::ok(h.phase() == HysteresisState::Phase::Inside, "and the pending exit was cancelled");
  }
  {
    int64_t t = 0;
    HysteresisState h = inside_state(t);
    const auto s = feed(h, std::vector<Obs>(20, kUnsure), t);
    t::ok(changes(Containment::Inside, s) == 0, "a long run of Uncertain never ends Inside");
  }

  // ── Regression: the Uncertain band is debounced ────────────────────────────
  {
    // Standing 10-12 m outside a boundary while accuracy alternates 35 m / 4 m:
    // the verdict alternates Uncertain / Outside every fix.
    int64_t t = 0;
    HysteresisState h;
    std::vector<Obs> obs;
    for (int i = 0; i < 60; ++i) obs.push_back(i % 2 ? kNearOut : kUnsure);
    const auto s = feed(h, obs, t);
    int uncertain_reports = 0;
    Containment prev = Containment::Outside;
    for (Containment c : s) { if (c == Containment::Uncertain && prev != c) ++uncertain_reports; prev = c; }
    t::ok(uncertain_reports == 1, "60 alternating fixes near a boundary report Uncertain once, "
                                  "not " + std::to_string(uncertain_reports) + " times");
  }
  {
    int64_t t = 0;
    HysteresisState h;
    feed(h, {kUnsure}, t);
    const auto s = feed(h, {kClearOut, kClearOut, kClearOut}, t);
    t::ok(s[0] == Containment::Uncertain && s[1] == Containment::Uncertain &&
              s[2] == Containment::Outside,
          "Uncertain clears to Outside only after confirm_samples clearly-outside fixes");
    const auto s2 = feed(h, {kNearOut, kNearOut, kNearOut}, t);
    t::ok(changes(Containment::Outside, s2) == 0, "and once Outside, near fixes stay Outside");
  }
  {
    int64_t t = 0;
    HysteresisState h;
    feed(h, {kUnsure}, t);
    const auto s = feed(h, {kClearOut, kNearOut, kClearOut, kClearOut}, t);
    t::ok(s.back() == Containment::Uncertain,
          "a near fix restarts the clearing run: the three must be consecutive");
  }
  {
    int64_t t = 0;
    HysteresisState h;
    feed(h, {kUnsure}, t);
    const auto s = feed(h, std::vector<Obs>(8, kDeepIn), t);
    t::ok(s[0] == Containment::Uncertain && s.back() == Containment::Inside &&
              changes(Containment::Uncertain, s) == 1,
          "Uncertain -> deep inside confirms straight to Inside, with no Outside in between");
  }
  {
    int64_t t = 0;
    HysteresisState h;
    feed(h, {kDeepIn, kDeepIn}, t);                       // entry pending from Outside
    const auto s = feed(h, {kUnsure}, t);
    t::ok(s[0] == Containment::Uncertain && h.phase() == HysteresisState::Phase::EnteringPending,
          "an Uncertain fix during a pending entry is reported, and interrupts the run");
    const auto s2 = feed(h, {kDeepIn, kDeepIn, kDeepIn}, t);
    t::ok(s2[0] == Containment::Uncertain && s2[1] == Containment::Uncertain &&
              s2[2] == Containment::Inside,
          "the entry then needs confirm_samples fresh agreeing fixes, but keeps its dwell clock");
    HysteresisState g;
    int64_t t2 = 0;
    feed(g, {kDeepIn, kDeepIn, kUnsure}, t2);
    const auto s3 = feed(g, {kNearOut}, t2);
    t::ok(s3[0] == Containment::Uncertain && g.phase() == HysteresisState::Phase::Ambiguous &&
              g.suppressed_last_update(),
          "an interrupted entry that then falls back reverts to Uncertain, as a suppressed flap");
  }

  // ── Through the evaluator: the event count on the alternating-accuracy walk ─
  {
    ZoneStore zones;
    Zone z;
    z.shape = geo::Polygon({{25.50, 91.80}, {25.50, 91.82}, {25.52, 91.82}, {25.52, 91.80}});
    zones.add(z);
    index::Quadtree qt;
    qt.build({{0, zones.get(0)->shape.bbox()}});
    Evaluator ev(qt, zones);
    track::Tourist tr;
    tr.id = 1;
    std::vector<Event> out;
    // 12 m south of the southern edge, accuracy alternating 35 m / 4 m.
    const geo::LatLon p{25.50 - 12.0 / 111195.0, 91.81};
    for (int i = 0; i < 300; ++i) {
      tr.last_fix = {p, (i % 2) ? 4.0 : 35.0, int64_t(i) * 1000};
      ev.evaluate(tr, int64_t(i) * 1000, out);
    }
    size_t uncertain = 0;
    for (const auto& e : out) uncertain += e.kind == EventKind::ZoneUncertain;
    t::ok(uncertain == 1, "five minutes of alternating accuracy near a zone: " +
                              std::to_string(uncertain) + " ZoneUncertain event(s), expected 1");
  }

  return t::report("fence/hysteresis");
}
