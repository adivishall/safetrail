// Every case in docs/GEOMETRY_EDGE_CASES.md. These are the tests that matter --
// ray casting is fifteen lines and wrong in at least three ways by default.
#include "../test_harness.hpp"
#include "safetrail/geo/containment.hpp"

using namespace safetrail::geo;

static Polygon square() {
  return Polygon(Ring{{0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {1.0, 0.0}});
}
// C-shape: concave, so any convexity assumption breaks.
static Polygon cshape() {
  return Polygon(Ring{{0,0},{0,3},{1,3},{1,1},{2,1},{2,3},{3,3},{3,0}});
}

int main() {
  Polygon sq = square();

  // basics
  t::ok(contains(sq, {0.5, 0.5}), "centre is inside");
  t::ok(!contains(sq, {1.5, 0.5}), "point right of square is outside");
  t::ok(!contains(sq, {-0.5, 0.5}), "point left of square is outside");

  // CASE 1: point exactly on an edge -- we DEFINE this as inside
  t::ok(contains(sq, {0.0, 0.5}), "case 1: point on bottom edge counts inside");
  t::ok(contains(sq, {0.5, 1.0}), "case 1: point on right edge counts inside");

  // CASE 2: ray passes exactly through a vertex. The half-open rule must count
  // each vertex for exactly one of its two edges, or parity inverts.
  t::ok(contains(sq, {0.0, 0.0}), "case 2: corner vertex counts inside");
  t::ok(!contains(sq, {2.0, 1.0}), "case 2: ray through vertex from outside stays outside");

  // CASE 3: horizontal edge collinear with the ray
  t::ok(!contains(sq, {1.0, 2.0}), "case 3: collinear with top edge, outside");

  // CASE 4: concave polygon -- the notch must read as outside
  Polygon c = cshape();
  t::ok(contains(c, {0.5, 0.5}), "case 4: concave left arm inside");
  t::ok(contains(c, {2.5, 0.5}), "case 4: concave right arm inside");
  t::ok(contains(c, {0.5, 1.5}), "case 4: concave base inside");
  t::ok(contains(c, {2.5, 1.5}), "case 4: top arm inside (lat 2.5 is above the notch)");
  t::ok(!contains(c, {1.5, 2.5}), "case 4: the notch (lat 1.5, lon 2.5) is OUTSIDE");

  // CASE 5: hole -- crossings inside a hole must flip parity
  Polygon withhole(Ring{{0,0},{0,10},{10,10},{10,0}});
  withhole.add_hole(Ring{{4,4},{4,6},{6,6},{6,4}});
  t::ok(contains(withhole, {1.0, 1.0}), "case 5: inside outer, outside hole");
  t::ok(!contains(withhole, {5.0, 5.0}), "case 5: inside the HOLE is outside");
  t::ok(contains(withhole, {9.0, 9.0}), "case 5: far corner still inside");

  // CASE 6/8: degenerate and self-intersecting geometry must be rejected
  t::ok(Polygon(Ring{{0,0},{1,1}}).validate() == Polygon::Validity::TooFewVertices,
        "case 8: two vertices rejected");
  t::ok(Polygon(Ring{{0,0},{0,1},{0,2}}).validate() == Polygon::Validity::ZeroArea,
        "case 8: collinear ring has zero area");
  t::ok(Polygon(Ring{{0,0},{2,2},{0,2},{2,0}}).validate() == Polygon::Validity::SelfIntersecting,
        "case 6: bowtie detected as self-intersecting");
  t::ok(square().validate() == Polygon::Validity::Ok, "valid square passes validation");
  t::ok(cshape().validate() == Polygon::Validity::Ok, "valid concave C passes validation");

  // signed distance sign convention
  t::ok(signed_distance_m(sq, {0.5, 0.5}) < 0, "signed distance negative inside");
  t::ok(signed_distance_m(sq, {5.0, 0.5}) > 0, "signed distance positive outside");

  // GAP 1: three-valued containment
  UncertainPoint deep{{0.5, 0.5}, 5.0, 0};
  t::ok(evaluate(sq, deep) == Containment::Inside, "gap1: deep inside with small radius = Inside");
  UncertainPoint edgey{{0.0, 0.5}, 5000.0, 0};
  t::ok(evaluate(sq, edgey) == Containment::Uncertain,
        "gap1: on boundary with large radius = Uncertain");
  UncertainPoint far{{50.0, 50.0}, 5.0, 0};
  t::ok(evaluate(sq, far) == Containment::Outside, "gap1: far away = Outside");

  return t::report("geo/ray_casting");
}
