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

  {
    // Hole TOUCHING the outer boundary, sharing an edge. Policy: refused. The
    // tangent point pinches the region to zero width, and neither containment
    // implementation has a defensible answer there -- see the policy note in
    // geo/polygon.hpp. This is the case a vertex-only test waves through.
    Polygon p(square(0, 0, 1.0));
    p.add_hole({{-0.5, 0.5}, {-0.5, 1.0}, {0.5, 1.0}, {0.5, 0.5}});   // right edge on x=1
    t::ok(p.validate() == V::HoleCrossesOuter,
          "a hole flush against the outer boundary is rejected");
  }
  {
    // Touching at a single vertex only -- the weakest form of contact, and the
    // one an edge-crossing test can most easily miss. Same policy, same verdict.
    Polygon p(square(0, 0, 1.0));
    p.add_hole({{1.0, 0.0}, {0.5, 0.4}, {0.5, -0.4}});   // apex ON the north edge
    t::ok(p.validate() == V::HoleCrossesOuter,
          "a hole touching the outer boundary at one vertex is rejected");
  }
  {
    // Two holes touching each other at a vertex: same policy.
    Polygon p(square(0, 0, 2.0));
    p.add_hole({{0.0, 0.0}, {0.0, 0.5}, {0.5, 0.5}});
    p.add_hole({{0.0, 0.0}, {0.0, -0.5}, {-0.5, -0.5}});
    t::ok(p.validate() == V::HolesOverlap, "two holes meeting at a vertex are rejected");
  }
  {
    // ...and the near-miss must be accepted, or the policy is just "reject
    // everything". Pull the second hole 1e-6 deg (~11 cm) clear.
    Polygon p(square(0, 0, 2.0));
    p.add_hole({{0.0, 0.0}, {0.0, 0.5}, {0.5, 0.5}});
    p.add_hole({{-1e-6, -1e-6}, {-1e-6, -0.5}, {-0.5, -0.5}});
    t::ok(p.validate() == V::Ok, "two holes that come close but do not touch are accepted");
  }

  // ── a CONCAVE outer ring with a legitimate hole ────────────────────────────
  //
  // The positive counterpart to the C-with-a-bar case above: the same awkward
  // outer ring, with a hole that really is inside it. If the edge-crossing test
  // were the naive "any contact at all", this would be rejected and the rule
  // would be useless on exactly the shapes it was written for.
  {
    Ring c = {{-2, -2}, {-2, 2}, {-1, 2}, {-1, -1}, {1, -1}, {1, 2}, {2, 2}, {2, -2}};
    Polygon p(c);
    p.add_hole(square(-1.5, 0.0, 0.3));      // inside the southern arm of the C
    t::ok(p.validate() == V::Ok, "a concave outer ring with a hole inside it is valid");
    t::ok(!contains(p, {-1.5, 0.0}), "the hole is a hole");
    t::ok(contains(p, {-1.7, 1.5}), "and the rest of the arm is still inside");
    // The hole must be subtracted from the region area, concavity or not.
    const double outer_area = std::fabs(p.outer_signed_area());
    t::near(std::fabs(p.signed_area()), outer_area - 0.36, 1e-9,
            "the hole is subtracted from a concave outer ring too");
  }

  // ── a hole big enough to go through the sweep line ─────────────────────────
  //
  // validate() dispatches self-intersection detection to Shamos-Hoey at
  // kSweepThresholdVertices. Holes go through the same predicate as the outer
  // ring, so a hole above that size must be judged by the sweep and reach the
  // same verdict the pairwise scan would.
  {
    const size_t n = kSweepThresholdVertices * 4;   // comfortably over the threshold
    Ring big;
    for (size_t i = 0; i < n; ++i) {
      const double a = 6.283185307179586 * double(i) / double(n);
      big.push_back({0.4 * std::sin(a), 0.4 * std::cos(a)});
    }
    Polygon good(square(0, 0, 1.0));
    good.add_hole(big);
    t::ok(good.validate() == V::Ok, "a 224-vertex circular hole is valid");
    t::ok(!ring_self_intersects(big) && !ring_self_intersects_pairwise(big),
          "both self-intersection implementations agree it is simple");

    Ring broken = big;
    std::swap(broken[1], broken[n / 2]);      // introduce a crossing
    Polygon bad(square(0, 0, 1.0));
    bad.add_hole(broken);
    t::ok(bad.validate() == V::HoleSelfIntersecting,
          "a large self-intersecting hole is caught by the sweep");
    t::ok(ring_self_intersects_pairwise(broken),
          "and the O(V^2) oracle agrees, so the dispatch did not change the answer");
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
