#include "safetrail/geo/containment.hpp"

#include <cmath>
#include <limits>

#include "safetrail/geo/projection.hpp"
#include "safetrail/geo/segment.hpp"

namespace safetrail::geo {

const char* to_string(Containment c) {
  switch (c) {
    case Containment::Inside: return "inside";
    case Containment::Outside: return "outside";
    case Containment::Uncertain: return "uncertain";
  }
  return "?";
}

// Is p on segment ab? Case 1 in GEOMETRY_EDGE_CASES.md -- we DEFINE on-boundary
// as inside, so this must be checked before the parity test. The predicate lives
// in geo/segment.hpp so that validation, the sweep, and this agree on where an
// edge is to the last bit; a local copy with its own epsilon is how two layers of
// one system end up disagreeing about containment.
static bool on_edge(const LatLon& a, const LatLon& b, const LatLon& p) {
  return point_on_segment(a, b, p);
}

// Crossing count for one ring, using the HALF-OPEN rule (y1 > py) != (y2 > py).
// That single comparison handles cases 2 and 3 -- ray through a vertex, and
// horizontal edges collinear with the ray -- because each vertex contributes to
// exactly one of its two edges and horizontal edges never qualify.
static int ring_crossings(const Ring& r, const LatLon& p, bool* on_boundary) {
  int crossings = 0;
  const size_t n = r.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    if (on_edge(r[j], r[i], p)) { *on_boundary = true; return 0; }
    if ((r[i].lat > p.lat) != (r[j].lat > p.lat)) {
      const double t = (p.lat - r[i].lat) / (r[j].lat - r[i].lat);
      if (p.lon < r[i].lon + t * (r[j].lon - r[i].lon)) ++crossings;
    }
  }
  return crossings;
}

bool contains(const Polygon& poly, const LatLon& p) {
  if (!poly.bbox().contains(p)) return false;         // cheap reject first
  bool on_boundary = false;
  int c = ring_crossings(poly.outer(), p, &on_boundary);
  if (on_boundary) return true;
  // Case 5: holes contribute to the SAME parity count. Counting them separately
  // and subtracting is the common bug -- it breaks on nested holes.
  for (const auto& h : poly.holes()) {
    c += ring_crossings(h, p, &on_boundary);
    if (on_boundary) return true;
  }
  return (c & 1) != 0;
}

// Independent second implementation. Kept permanently: it cross-validates ray
// casting on randomised input, and the two disagree exactly where the hard cases
// live -- which is how the hole-orientation bug below was found.
//
// Winding number is ORIENTATION-SENSITIVE, and that is the trap. The textbook
// rule requires holes to be wound opposite to the outer ring so their windings
// cancel; ray casting's parity rule does not care, because a crossing is a
// crossing whichever way the edge runs. So a file with a counter-clockwise hole
// inside a counter-clockwise outer ring gives w = 1 + 1 = 2 at the hole's centre,
// which is "non-zero", which reads as INSIDE -- while ray casting correctly says
// outside. Most GeoJSON producers get hole orientation wrong, so this is the
// common case, not the exotic one.
//
// The fix is the same normalisation signed_area() and centroid() already apply:
// force each hole to the opposite winding from the outer ring before summing. The
// two implementations then agree for every hole orientation, which is what
// tests/geo/polygon_holes_test.cpp asserts.
bool contains_winding(const Polygon& poly, const LatLon& p) {
  if (!poly.bbox().contains(p)) return false;
  auto wind = [&](const Ring& r) {
    int w = 0;
    const size_t n = r.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
      if (on_edge(r[j], r[i], p)) return 1000000;      // sentinel: on boundary
      if (r[i].lat <= p.lat) {
        if (r[j].lat > p.lat) {
          const double side = (r[j].lon - r[i].lon) * (p.lat - r[i].lat) -
                              (p.lon - r[i].lon) * (r[j].lat - r[i].lat);
          if (side > 0) ++w;
        }
      } else if (r[j].lat <= p.lat) {
        const double side = (r[j].lon - r[i].lon) * (p.lat - r[i].lat) -
                            (p.lon - r[i].lon) * (r[j].lat - r[i].lat);
        if (side < 0) --w;
      }
    }
    return w;
  };
  int w = wind(poly.outer());
  if (w == 1000000) return true;
  const double outer_sign = ring_signed_area(poly.outer()) < 0 ? -1.0 : 1.0;
  for (const auto& h : poly.holes()) {
    const int hw = wind(h);
    if (hw == 1000000) return true;
    // Same winding as the outer ring? Then this hole was authored "wrong" and its
    // contribution must be negated so it subtracts rather than adds.
    const double hole_sign = ring_signed_area(h) < 0 ? -1.0 : 1.0;
    w += (hole_sign == outer_sign) ? -hw : hw;
  }
  return w != 0;
}

// Distance from p to the nearest polygon edge, in metres.
//
// The projection parameter t is computed in LOCAL METRES, not in degrees. This
// was the bug: at Shillong a degree of longitude is 100 km and a degree of
// latitude is 111 km, so degree-space arithmetic silently stretches the east-west
// axis by 11%. That skew does not affect the final distance much when the nearest
// point is an endpoint, but it lands t in the wrong place along a slanted edge,
// which moves the reported nearest boundary point and biases signed_distance_m --
// the number three separate features (uncertainty resolution, time-to-boundary,
// adaptive sampling) all key off. geo/projection.hpp does the conversion, and
// documents the error budget: ~12 mm at 10 km, i.e. ~300x below GPS noise.
static double nearest_edge_dist_m(const Polygon& poly, const LatLon& p, LatLon* where) {
  double best = std::numeric_limits<double>::infinity();
  auto scan = [&](const Ring& r) {
    const size_t n = r.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
      LatLon w{};
      const double d = where ? point_segment_distance_m(p, r[j], r[i], &w)
                             : point_segment_distance_m(p, r[j], r[i]);
      if (d < best) { best = d; if (where) *where = w; }
    }
  };
  scan(poly.outer());
  for (const auto& h : poly.holes()) scan(h);
  return best;
}

// Negative inside, positive outside. Used by three separate features: uncertainty
// resolution (GAP 1), time-to-boundary prediction (GAP 2), and adaptive sampling
// (GAP 7). Worth the O(V) cost because it is shared.
double signed_distance_m(const Polygon& poly, const LatLon& p) {
  const double d = nearest_edge_dist_m(poly, p, nullptr);
  return contains(poly, p) ? -d : d;
}

LatLon nearest_boundary_point(const Polygon& poly, const LatLon& p) {
  LatLon w{};
  nearest_edge_dist_m(poly, p, &w);
  return w;
}

// GAP 1. The whole project pivots here. Resolve to a definite answer only when
// the entire uncertainty disc falls on one side of the boundary.
//
// One rule, one place. classify() is the whole of the semantics; both public
// overloads and every caller in the engine route through it, so there is exactly
// one definition of where Inside stops and Uncertain begins.
static Containment classify(double signed_dist_m, double accuracy_m) {
  if (signed_dist_m < -accuracy_m) return Containment::Inside;   // disc clears the boundary
  if (signed_dist_m >  accuracy_m) return Containment::Outside;
  return Containment::Uncertain;                                 // disc straddles it
}

Containment evaluate(const Polygon& poly, const UncertainPoint& p) {
  // Fast reject: if the disc cannot even reach the bbox, it is outside. This is
  // consistent with the full test rather than an approximation of it -- the bbox
  // contains the polygon, so a point further from the box than its accuracy
  // radius is further than that from every edge, and classify() would say
  // Outside too.
  if (poly.bbox().min_distance_m(p.pos) > p.accuracy_m) return Containment::Outside;
  return classify(signed_distance_m(poly, p.pos), p.accuracy_m);
}

Containment evaluate(const Polygon& poly, const UncertainPoint& p,
                     double* out_signed_distance_m) {
  const double sd = signed_distance_m(poly, p.pos);
  if (out_signed_distance_m) *out_signed_distance_m = sd;
  return classify(sd, p.accuracy_m);
}

}  // namespace safetrail::geo
