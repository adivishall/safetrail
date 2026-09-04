// Polygons with holes: validation and metrics.
//
// A hole is not decoration -- an exempt village inside a restricted forest block
// is a hole, and both the containment answer and the reported area depend on
// getting it right. Two things were wrong before this file existed:
//
//   1. validate() checked the outer ring and nothing else. A hole could
//      self-intersect, sit outside the outer ring, cross its boundary, or overlap
//      another hole, and the zone loaded clean. Ray casting's parity rule assumes
//      none of those, so containment answers were arbitrary in exactly the way a
//      self-intersecting ring makes them arbitrary -- which the code already
//      refused to accept for the outer ring.
//   2. signed_area(), centroid() and perimeter_m() ignored holes entirely, so a
//      ring-shaped zone reported the area of a disc and a centroid sitting in the
//      middle of the hole, i.e. outside the zone.
#include "../test_harness.hpp"

#include <cmath>

#include "safetrail/geo/containment.hpp"
#include "safetrail/geo/polygon.hpp"

using namespace safetrail::geo;
using V = Polygon::Validity;

// Axis-aligned square, counter-clockwise, centred at (lat, lon).
static Ring square(double lat, double lon, double half) {
  return {{lat - half, lon - half}, {lat - half, lon + half},
          {lat + half, lon + half}, {lat + half, lon - half}};
}

int main() {
  // ── a well-formed polygon with a hole ──────────────────────────────────────
  {
    Polygon p(square(0, 0, 1.0));
    p.add_hole(square(0, 0, 0.5));
    t::ok(p.validate() == V::Ok, "square with a centred square hole is valid");

    // Region area = 4 (outer 2x2) - 1 (hole 1x1) = 3.
    t::near(std::fabs(p.signed_area()), 3.0, 1e-9, "area subtracts the hole");
    t::near(std::fabs(p.outer_signed_area()), 4.0, 1e-9,
            "outer_signed_area() still reports the ring alone");
    t::ok(p.vertex_count() == 8, "vertex count includes the hole's vertices");

    // Perimeter includes the hole boundary: crossing it takes you out of the zone.
    const double outer_only = p.outer_perimeter_m();
    t::ok(p.perimeter_m() > outer_only, "perimeter includes the hole boundary");
    t::near(p.perimeter_m(), outer_only * 1.5, outer_only * 0.02,
            "and by the right amount: a half-size hole adds half the perimeter");

    // A symmetric hole leaves the centroid where it was.
    const LatLon c = p.centroid();
    t::near(c.lat, 0.0, 1e-9, "symmetric hole leaves the centroid centred (lat)");
    t::near(c.lon, 0.0, 1e-9, "symmetric hole leaves the centroid centred (lon)");
  }

  // ── the centroid actually moves, and lands inside the zone ─────────────────
  //
  // The case that exposes an outer-ring-only centroid: punch the hole off-centre
  // and the true centroid shifts away from it.
  {
    Polygon p(square(0, 0, 1.0));
    p.add_hole(square(0, 0.5, 0.4));            // hole to the east
    const LatLon c = p.centroid();
    t::ok(c.lon < -0.01, "an eastern hole pushes the centroid west");
    t::ok(contains(p, c), "and the centroid still lies inside the zone");
  }

  // A ring-shaped zone: outer-ring centroid would sit in the hole, i.e. outside.
  {
    Polygon donut(square(0, 0, 1.0));
    donut.add_hole(square(0, 0, 0.9));
    t::ok(!contains(donut, {0.0, 0.0}), "the middle of a donut is NOT inside it");
    t::near(std::fabs(donut.signed_area()), 4.0 - 3.24, 1e-9, "thin ring area");
  }

  // ── hole winding is normalised away ────────────────────────────────────────
  //
  // Most GeoJSON producers get hole orientation wrong. Area must not depend on it.
  {
    Ring ccw_hole = square(0, 0, 0.5);
    Ring cw_hole(ccw_hole.rbegin(), ccw_hole.rend());

    Polygon a(square(0, 0, 1.0)); a.add_hole(ccw_hole);
    Polygon b(square(0, 0, 1.0)); b.add_hole(cw_hole);
    t::near(std::fabs(a.signed_area()), std::fabs(b.signed_area()), 1e-12,
            "area is the same whichever way the hole is wound");
    t::near(a.centroid().lat, b.centroid().lat, 1e-12, "so is the centroid (lat)");
    t::near(a.centroid().lon, b.centroid().lon, 1e-12, "so is the centroid (lon)");
    t::ok(b.validate() == V::Ok, "a clockwise hole is still valid geometry");
  }

  // Outer ring winding flips the SIGN but not the magnitude.
  {
    Ring ccw = square(0, 0, 1.0);
    Ring cw(ccw.rbegin(), ccw.rend());
    Polygon a(ccw); a.add_hole(square(0, 0, 0.5));
    Polygon b(cw);  b.add_hole(square(0, 0, 0.5));
    t::ok(a.signed_area() > 0 && b.signed_area() < 0,
          "the sign still encodes the outer ring's winding direction");
    t::near(std::fabs(a.signed_area()), std::fabs(b.signed_area()), 1e-12,
            "and the magnitude is the enclosed area either way");
  }

  // ── adversarial holes: each must be REJECTED ───────────────────────────────
  {
    // Hole entirely outside the outer ring.
    Polygon p(square(0, 0, 1.0));
    p.add_hole(square(5, 5, 0.5));
    t::ok(p.validate() == V::HoleOutsideOuter, "a hole outside the outer ring is rejected");
  }
  {
    // Hole straddling the outer boundary: some vertices in, some out.
    Polygon p(square(0, 0, 1.0));
    p.add_hole(square(0, 1.0, 0.5));
    t::ok(p.validate() == V::HoleOutsideOuter, "a hole hanging over the edge is rejected");
  }
  {
    // The subtle one: every hole VERTEX is inside a concave outer ring, but a
    // hole EDGE exits through the notch. Vertex testing alone passes this.
    //
    // Outer ring: a C opening to the east.
    Ring c = {{-2, -2}, {-2, 2}, {-1, 2}, {-1, -1}, {1, -1}, {1, 2}, {2, 2}, {2, -2}};
    Polygon p(c);
    // Both endpoints sit in the arms of the C; the segment between them crosses
    // the notch, which is outside.
    Ring bar = {{-1.5, 1.0}, {1.5, 1.0}, {1.5, 1.5}, {-1.5, 1.5}};
    p.add_hole(bar);
    const V v = p.validate();
    t::ok(v == V::HoleCrossesOuter || v == V::HoleOutsideOuter,
          std::string("a hole whose EDGE leaves a concave outer ring is rejected (got ") +
              Polygon::to_string(v) + ")");
  }
  {
    // Two overlapping holes: their intersection would flip parity twice and read
    // as inside the zone, and their areas would be double-subtracted.
    Polygon p(square(0, 0, 2.0));
    p.add_hole(square(0, 0, 0.5));
    p.add_hole(square(0, 0.4, 0.5));
    t::ok(p.validate() == V::HolesOverlap, "overlapping holes are rejected");
  }
  {
    // One hole nested entirely inside another: edge-disjoint, so an
    // edge-intersection test alone misses it.
    Polygon p(square(0, 0, 2.0));
    p.add_hole(square(0, 0, 1.0));
    p.add_hole(square(0, 0, 0.5));
    t::ok(p.validate() == V::HolesOverlap, "a hole nested inside another is rejected");
  }
  {
    // Self-intersecting hole (a bowtie).
    Polygon p(square(0, 0, 2.0));
    p.add_hole({{-0.5, -0.5}, {0.5, 0.5}, {0.5, -0.5}, {-0.5, 0.5}});
    t::ok(p.validate() == V::HoleSelfIntersecting, "a self-intersecting hole is rejected");
  }
  {
    Polygon p(square(0, 0, 2.0));
    p.add_hole({{0, 0}, {0.5, 0}});
    t::ok(p.validate() == V::HoleTooFewVertices, "a two-vertex hole is rejected");
  }
  {
    Polygon p(square(0, 0, 2.0));
    p.add_hole({{0, 0}, {0.5, 0}, {1.0, 0}});     // collinear: zero area
    t::ok(p.validate() == V::HoleZeroArea, "a degenerate zero-area hole is rejected");
  }

  // Two disjoint holes side by side are fine -- the rejections above must not be
  // over-eager.
  {
    Polygon p(square(0, 0, 2.0));
    p.add_hole(square(0, -1.0, 0.4));
    p.add_hole(square(0, 1.0, 0.4));
    t::ok(p.validate() == V::Ok, "two disjoint holes are accepted");
    t::near(std::fabs(p.signed_area()), 16.0 - 0.64 - 0.64, 1e-9,
            "both holes are subtracted");
  }

  // ── containment still agrees with itself around holes ──────────────────────
  {
    Polygon p(square(0, 0, 1.0));
    p.add_hole(square(0, 0, 0.5));
    struct Case { LatLon p; bool inside; const char* what; };
    const Case cases[] = {
        {{0.0, 0.0}, false, "hole centre is outside"},
        {{0.75, 0.0}, true, "between hole and outer edge is inside"},
        {{0.0, 0.75}, true, "same, on the other axis"},
        {{1.5, 0.0}, false, "beyond the outer ring is outside"},
        {{0.5, 0.0}, true, "exactly on the hole boundary counts as inside"},
        {{1.0, 0.0}, true, "exactly on the outer boundary counts as inside"},
    };
    for (const auto& c : cases) {
      t::ok(contains(p, c.p) == c.inside, std::string("ray casting: ") + c.what);
      t::ok(contains_winding(p, c.p) == c.inside,
            std::string("winding number: ") + c.what);
    }
  }

  return t::report("geo/polygon_holes");
}
