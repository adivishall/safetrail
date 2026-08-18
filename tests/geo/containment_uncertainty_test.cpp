// GAP 1: three-valued containment under GPS uncertainty. The disc, not the point.
#include "../test_harness.hpp"
#include "safetrail/geo/containment.hpp"
using namespace safetrail::geo;

int main() {
  // 1km-ish square around origin (degrees; ~111m per 0.001 lat)
  Polygon sq(Ring{{0.0,0.0},{0.0,0.01},{0.01,0.01},{0.01,0.0}});

  // deep inside, tight accuracy -> Inside
  t::ok(evaluate(sq, {{0.005,0.005}, 5.0, 0}) == Containment::Inside,
        "deep inside, 5m accuracy = Inside");
  // far outside -> Outside
  t::ok(evaluate(sq, {{0.05,0.05}, 5.0, 0}) == Containment::Outside,
        "far away, 5m accuracy = Outside");
  // near the boundary with a big disc that straddles it -> Uncertain
  t::ok(evaluate(sq, {{0.005,0.0}, 200.0, 0}) == Containment::Uncertain,
        "on the edge with 200m disc = Uncertain");
  // just inside but disc large enough to cross out -> Uncertain
  t::ok(evaluate(sq, {{0.005,0.0009}, 200.0, 0}) == Containment::Uncertain,
        "just inside, disc crosses boundary = Uncertain");
  // deep inside, but a huge disc still can't reach any edge -> Inside
  t::ok(evaluate(sq, {{0.005,0.005}, 50.0, 0}) == Containment::Inside,
        "centre with 50m disc, edges >500m away = Inside");

  // signed distance sign convention underpins all of the above
  t::ok(signed_distance_m(sq, {0.005,0.005}) < 0, "signed distance negative inside");
  t::ok(signed_distance_m(sq, {0.05,0.05})   > 0, "signed distance positive outside");
  // monotonic: deeper inside is more negative
  t::ok(signed_distance_m(sq,{0.005,0.005}) < signed_distance_m(sq,{0.005,0.0005}),
        "centre is deeper (more negative) than near-edge");

  // an unusable fix (accuracy worse than the cap) is flagged by usable()
  t::ok(!UncertainPoint{{0.005,0.005}, 999.0, 0}.usable(), "999m fix is unusable");
  t::ok( UncertainPoint{{0.005,0.005}, 35.0, 0}.usable(),  "35m fix is usable");

  return t::report("geo/containment_uncertainty");
}
