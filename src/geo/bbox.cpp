#include "safetrail/geo/bbox.hpp"
#include <cmath>
#include <limits>

namespace safetrail::geo {

Bbox Bbox::empty() {
  const double inf = std::numeric_limits<double>::infinity();
  return {inf, inf, -inf, -inf};
}

// The bounding box of the spherical cap {p : distance_m(c, p) <= radius_m}.
//
// This box is the index's FILTER, and the contract of filter-then-refine is that
// the filter is conservative with respect to the refinement: every point the
// exact test could accept must be inside the box. So the box is derived on the
// same sphere distance_m() measures on (kEarthR in point.cpp), not from a pair
// of "metres per degree" constants.
//
// It used to be radius/110574 in latitude and radius/(111320 cos lat) in
// longitude -- WGS84 metres-per-degree figures, on a different earth model from
// the haversine distance the geometry uses. Latitude came out slightly too wide
// (harmless), longitude 0.11% too NARROW: a zone whose edge sat 99.9% of the
// query radius due east of a fix was dropped by the index while the exact test
// would have called it Uncertain. tests/geo/bbox_around_test.cpp samples points
// inside the disc and asserts every one lands in the box.
//
// The longitude half-width is not radius/(R cos lat) either: a great circle
// bulges poleward, so the easternmost point within distance r is slightly off
// the parallel and the exact half-width is asin(sin(r/R) / cos(lat)). When the
// cap reaches a pole, every longitude is inside it.
Bbox Bbox::around(const LatLon& c, double radius_m) {
  constexpr double kEarthR = 6371008.8;                 // must match point.cpp
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kDeg = 180.0 / kPi;
  // A relative pad of 1e-9 absorbs rounding in asin/sin/cos; at a 10 km query it
  // is 10 micrometres, far below anything that could change a candidate set.
  const double r = radius_m > 0.0 ? radius_m : 0.0;
  const double delta = (r / kEarthR) * (1.0 + 1e-9);   // angular radius, radians
  const double phi = c.lat / kDeg;

  const double min_lat = c.lat - delta * kDeg, max_lat = c.lat + delta * kDeg;
  if (max_lat >= 90.0 || min_lat <= -90.0 || std::cos(phi) <= std::sin(delta))
    return {min_lat < -90.0 ? -90.0 : min_lat, -180.0, max_lat > 90.0 ? 90.0 : max_lat, 180.0};

  const double dlon = std::asin(std::sin(delta) / std::cos(phi)) * kDeg * (1.0 + 1e-9);
  return {min_lat, c.lon - dlon, max_lat, c.lon + dlon};
}

Bbox Bbox::of(const LatLon* pts, size_t n) {
  Bbox b = empty();
  for (size_t i = 0; i < n; ++i) b.expand(pts[i]);
  return b;
}

double Bbox::enlargement_to_include(const Bbox& o) const {
  Bbox u = *this;
  u.expand(o);
  return u.area() - area();
}

// Distance from p to the box, 0 if inside. Used to order nearest() candidates
// and as a cheap lower bound before the O(V) polygon distance.
double Bbox::min_distance_m(const LatLon& p) const {
  const double clat = p.lat < min_lat ? min_lat : (p.lat > max_lat ? max_lat : p.lat);
  const double clon = p.lon < min_lon ? min_lon : (p.lon > max_lon ? max_lon : p.lon);
  if (clat == p.lat && clon == p.lon) return 0.0;
  return distance_m(p, {clat, clon});
}

}  // namespace safetrail::geo
