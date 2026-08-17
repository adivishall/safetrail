#include "safetrail/geo/bbox.hpp"
#include <cmath>
#include <limits>

namespace safetrail::geo {

Bbox Bbox::empty() {
  const double inf = std::numeric_limits<double>::infinity();
  return {inf, inf, -inf, -inf};
}

// Latitude degrees are ~constant in metres; longitude degrees shrink by cos(lat).
// Ignoring that cosine is the classic bug that makes east-west queries too narrow.
Bbox Bbox::around(const LatLon& c, double radius_m) {
  const double dlat = radius_m / 110574.0;
  const double coslat = std::cos(c.lat * 3.14159265358979323846 / 180.0);
  const double dlon = radius_m / (111320.0 * (std::fabs(coslat) < 1e-9 ? 1e-9 : coslat));
  return {c.lat - dlat, c.lon - dlon, c.lat + dlat, c.lon + dlon};
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
