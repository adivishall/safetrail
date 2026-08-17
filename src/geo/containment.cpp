#include "safetrail/geo/containment.hpp"
#include <cmath>
#include <limits>

namespace safetrail::geo {

const char* to_string(Containment c) {
  switch (c) {
    case Containment::Inside: return "inside";
    case Containment::Outside: return "outside";
    case Containment::Uncertain: return "uncertain";
  }
  return "?";
}

static constexpr double kEps = 1e-12;

// Is p on segment ab? Case 1 in GEOMETRY_EDGE_CASES.md -- we DEFINE on-boundary
// as inside, so this must be checked before the parity test.
static bool on_edge(const LatLon& a, const LatLon& b, const LatLon& p) {
  const double cross = (b.lon - a.lon) * (p.lat - a.lat) - (b.lat - a.lat) * (p.lon - a.lon);
  if (std::fabs(cross) > 1e-11) return false;
  return p.lon >= std::fmin(a.lon, b.lon) - kEps && p.lon <= std::fmax(a.lon, b.lon) + kEps &&
         p.lat >= std::fmin(a.lat, b.lat) - kEps && p.lat <= std::fmax(a.lat, b.lat) + kEps;
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
// live. Winding number is orientation-sensitive, hence fabs at the end.
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
  for (const auto& h : poly.holes()) {
    const int hw = wind(h);
    if (hw == 1000000) return true;
    w += hw;
  }
  return w != 0;
}

// Distance from p to segment ab, in metres. Projects onto the segment and clamps
// to the endpoints -- the endpoint case is a distinct edge case worth testing.
static double dist_to_seg_m(const LatLon& a, const LatLon& b, const LatLon& p) {
  const double dlat = b.lat - a.lat, dlon = b.lon - a.lon;
  const double len2 = dlat * dlat + dlon * dlon;
  if (len2 < 1e-24) return distance_m(p, a);          // degenerate segment
  double t = ((p.lat - a.lat) * dlat + (p.lon - a.lon) * dlon) / len2;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return distance_m(p, {a.lat + t * dlat, a.lon + t * dlon});
}

static double nearest_edge_dist_m(const Polygon& poly, const LatLon& p, LatLon* where) {
  double best = std::numeric_limits<double>::infinity();
  auto scan = [&](const Ring& r) {
    const size_t n = r.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
      const double d = dist_to_seg_m(r[j], r[i], p);
      if (d < best) {
        best = d;
        if (where) {
          const double dlat = r[i].lat - r[j].lat, dlon = r[i].lon - r[j].lon;
          const double len2 = dlat * dlat + dlon * dlon;
          double t = len2 < 1e-24 ? 0.0
                   : ((p.lat - r[j].lat) * dlat + (p.lon - r[j].lon) * dlon) / len2;
          t = t < 0 ? 0 : (t > 1 ? 1 : t);
          *where = {r[j].lat + t * dlat, r[j].lon + t * dlon};
        }
      }
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
Containment evaluate(const Polygon& poly, const UncertainPoint& p) {
  // Fast reject: if the disc cannot even reach the bbox, it is outside.
  if (poly.bbox().min_distance_m(p.pos) > p.accuracy_m) return Containment::Outside;
  const double sd = signed_distance_m(poly, p.pos);
  const double r = p.accuracy_m;
  if (sd < -r) return Containment::Inside;    // fully inside, disc clears boundary
  if (sd > r) return Containment::Outside;    // fully outside
  return Containment::Uncertain;              // disc straddles the boundary
}

}  // namespace safetrail::geo
