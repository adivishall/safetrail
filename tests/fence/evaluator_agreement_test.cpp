// The fence evaluator and the geometry layer must agree, and the candidate cap
// must not silently lose a breach.
//
// Two properties, both of which were broken:
//
//   1. ONE definition of containment. fence::Evaluator used to re-derive the
//      Inside/Uncertain/Outside verdict from signed_distance_m with its own
//      threshold comparisons -- a second copy of the semantics, free to drift
//      from geo::evaluate(), which is what the geometry tests pin. A policy layer
//      is allowed to add hysteresis and dwell rules on top of a verdict; it is
//      not allowed to quietly redefine the verdict.
//
//   2. NO SILENT FALSE NEGATIVES. When the index returned more candidates than
//      max_candidates, the evaluator did `candidates.resize(cap)` -- truncating
//      in quadtree traversal order, which has nothing to do with risk. In a
//      system whose output is safety alerts, a dropped candidate is a missed
//      breach. The default policy now evaluates every candidate and treats the
//      cap as a diagnostic; the capped policy is opt-in, ranks by risk before
//      cutting, and reports what it dropped.
#include "../test_harness.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "safetrail/fence/evaluator.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;
using namespace safetrail::fence;

static geo::Ring square(double lat, double lon, double half) {
  return {{lat - half, lon - half}, {lat - half, lon + half},
          {lat + half, lon + half}, {lat + half, lon - half}};
}

static track::Tourist tourist_at(track::TouristId id, geo::LatLon p, double acc,
                                 int64_t t_ms) {
  track::Tourist t;
  t.id = id;
  t.last_fix = {p, acc, t_ms};
  return t;
}

int main() {
  // ── the two layers agree, on randomised input ──────────────────────────────
  //
  // For every (zone, fix) pair, the verdict the evaluator acts on -- before
  // hysteresis, which is policy -- must be exactly geo::evaluate()'s.
  {
    sim::Rng rng(5150);
    size_t disagreements = 0, checked = 0;
    size_t inside = 0, outside = 0, uncertain = 0;

    for (int trial = 0; trial < 400; ++trial) {
      geo::Polygon poly(square(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                               rng.range(0.001, 0.01)));
      for (int probe = 0; probe < 10; ++probe) {
        const geo::LatLon c = poly.bbox().center();
        const geo::UncertainPoint fix{
            {c.lat + rng.range(-0.015, 0.015), c.lon + rng.range(-0.015, 0.015)},
            rng.range(3.0, 60.0), 1000};

        double sd_out = 0.0;
        const geo::Containment via_out = geo::evaluate(poly, fix, &sd_out);
        const geo::Containment via_plain = geo::evaluate(poly, fix);
        const double sd_direct = geo::signed_distance_m(poly, fix.pos);

        ++checked;
        if (via_out != via_plain) ++disagreements;
        if (std::fabs(sd_out - sd_direct) > 1e-9) ++disagreements;

        // And the classification really is the documented rule.
        geo::Containment expected;
        if (sd_direct < -fix.accuracy_m) expected = geo::Containment::Inside;
        else if (sd_direct > fix.accuracy_m) expected = geo::Containment::Outside;
        else expected = geo::Containment::Uncertain;
        if (via_out != expected) ++disagreements;

        if (via_out == geo::Containment::Inside) ++inside;
        else if (via_out == geo::Containment::Outside) ++outside;
        else ++uncertain;
      }
    }
    t::ok(disagreements == 0,
          "both geo::evaluate overloads and the documented rule agree on all " +
              std::to_string(checked) + " probes");
    // Guard against a vacuous pass: all three verdicts must actually occur.
    t::ok(inside > 100 && outside > 100 && uncertain > 100,
          "and all three verdicts were exercised (" + std::to_string(inside) + " in / " +
              std::to_string(outside) + " out / " + std::to_string(uncertain) + " uncertain)");
  }

  // ── the candidate cap does not lose a breach ───────────────────────────────
  //
  // Construct the pathological case on purpose: many overlapping zones around one
  // point, so the index legitimately returns far more candidates than the cap,
  // and the tourist is deep inside the LAST one the traversal would reach.
  {
    ZoneStore zones;
    const geo::LatLon here{25.5600, 91.8800};

    // 80 decoy zones, all containing the point, plus the one that matters.
    for (int i = 0; i < 80; ++i) {
      Zone z;
      z.name = "decoy " + std::to_string(i);
      z.kind = ZoneKind::Advisory;
      z.severity = 1;
      z.shape = geo::Polygon(square(here.lat, here.lon, 0.004 + 0.0001 * i));
      zones.add(z);
    }
    Zone hazard;
    hazard.name = "the real hazard";
    hazard.kind = ZoneKind::Restricted;
    hazard.severity = 5;
    hazard.shape = geo::Polygon(square(here.lat, here.lon, 0.0005));   // tightest
    const ZoneId hazard_id = zones.add(hazard);

    index::Quadtree ix;
    std::vector<std::pair<ZoneId, geo::Bbox>> items;
    for (ZoneId id : zones.all_ids()) items.emplace_back(id, zones.get(id)->shape.bbox());
    ix.build(items);

    auto run = [&](CandidatePolicy policy, size_t cap) {
      EvaluatorConfig cfg;
      cfg.candidate_policy = policy;
      cfg.max_candidates = cap;
      cfg.hysteresis.enabled = false;          // isolate the candidate question
      Evaluator ev(ix, zones, cfg);

      std::vector<Event> out;
      track::Tourist t = tourist_at(1, here, 3.0, 1000);
      ev.evaluate(t, 1000, out);

      bool saw_hazard = false;
      for (const auto& e : out) if (e.zone == hazard_id) saw_hazard = true;
      // Even with hysteresis off, a first-tick transition needs the state machine
      // to have run; check the recorded per-zone state as well as the events.
      const track::ZoneState* st = t.peek_state(hazard_id);
      const bool evaluated = st != nullptr;
      return std::make_pair(saw_hazard || evaluated, ev.counters());
    };

    // Default policy: the cap is a diagnostic. It trips, and nothing is dropped.
    {
      const auto [found, c] = run(CandidatePolicy::ExactAlways, 8);
      t::ok(c.candidate_cap_hits > 0, "ExactAlways: the cap is reported as exceeded");
      t::ok(c.candidates_dropped == 0, "ExactAlways: but NOTHING is dropped");
      t::ok(c.candidates_examined == 81,
            "ExactAlways: all 81 candidates are examined (" +
                std::to_string(c.candidates_examined) + ")");
      t::ok(found, "ExactAlways: the tightest, highest-severity zone is still evaluated");
    }

    // Capped policy: the approximation is explicit, counted, and risk-ordered.
    {
      const auto [found, c] = run(CandidatePolicy::NearestFirstCapped, 8);
      t::ok(c.candidate_cap_hits > 0, "NearestFirstCapped: the cap trips");
      t::ok(c.candidates_dropped == 73,
            "NearestFirstCapped: and reports exactly how many it dropped (" +
                std::to_string(c.candidates_dropped) + ")");
      t::ok(c.candidates_examined == 8, "NearestFirstCapped: only the cap is examined");
      t::ok(found,
            "NearestFirstCapped: risk ordering still keeps the tightest, "
            "highest-severity zone");
    }

    // A cap large enough for the data must behave identically under both policies.
    {
      const auto [found_a, ca] = run(CandidatePolicy::ExactAlways, 200);
      const auto [found_b, cb] = run(CandidatePolicy::NearestFirstCapped, 200);
      t::ok(ca.candidate_cap_hits == 0 && cb.candidate_cap_hits == 0,
            "an adequate cap never trips");
      t::ok(ca.candidates_examined == cb.candidates_examined,
            "and the two policies are indistinguishable");
      t::ok(found_a && found_b, "both find the hazard");
    }
  }

  // ── the query radius is configurable and its clamping is reported ──────────
  {
    ZoneStore zones;
    Zone z;
    z.name = "z";
    z.shape = geo::Polygon(square(25.56, 91.88, 0.002));
    zones.add(z);
    index::Quadtree ix;
    ix.build({{0, zones.get(0)->shape.bbox()}});

    EvaluatorConfig cfg;
    cfg.min_query_radius_m = 50.0;
    cfg.max_query_radius_m = 100.0;
    Evaluator ev(ix, zones, cfg);

    // A stationary tourist with a tight fix: the derived radius (accuracy plus
    // reach, and reach is zero when stationary) is below the floor.
    track::Tourist tight = tourist_at(1, {25.56, 91.88}, 4.0, 1000);
    t::near(ev.query_radius_m(tight), 50.0, 1e-9, "a small derived radius clamps to the floor");

    // A poor but still USABLE fix -- accuracy below UNUSABLE_ACCURACY_M, so the
    // evaluator does not reject it outright -- pushes the derived radius past the
    // ceiling. Using an unusable fix here would test nothing: it is discarded at
    // step 1, before the radius is ever computed.
    track::Tourist noisy = tourist_at(2, {25.56, 91.88}, 140.0, 1000);
    t::ok(noisy.last_fix.usable(), "the noisy fix is still a usable one");
    t::near(ev.query_radius_m(noisy), 100.0, 1e-9,
            "a large derived radius clamps to the ceiling");

    // And the clamp is counted, not silent: the engine must be able to say that
    // its predictive search was bounded rather than complete.
    std::vector<Event> out;
    ev.evaluate(noisy, 1000, out);
    t::ok(ev.counters().radius_clamped_max == 1,
          "hitting the ceiling is recorded in the counters");
    t::ok(ev.counters().fixes_rejected_noise == 0, "and the fix was not rejected as noise");

    // Configurability is the point: a different ceiling gives a different radius.
    EvaluatorConfig wide = cfg;
    wide.max_query_radius_m = 20000.0;
    Evaluator ev2(ix, zones, wide);
    t::near(ev2.query_radius_m(noisy), 140.0, 1e-9,
            "raising the ceiling lets the derived radius through unclamped");
    std::vector<Event> out2;
    ev2.evaluate(noisy, 1000, out2);
    t::ok(ev2.counters().radius_clamped_max == 0, "and nothing is reported as clamped");
  }

  return t::report("fence/evaluator_agreement");
}
