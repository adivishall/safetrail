#include "safetrail/geo/segment.hpp"

#include <cmath>

namespace safetrail::geo {

namespace {
// Degrees. 1e-14 deg is ~1 nanometre on the ground -- far below any coordinate
// this engine will ever see, so it separates "genuinely collinear" from
// floating-point residue without swallowing real geometry.
constexpr double kOrientEps = 1e-14;
// Looser, for the "is this point ON the edge" test: a point produced by an
// independent computation (a GPS fix, an interpolated vertex) lands within
// rounding of the line, not exactly on it.
constexpr double kCollinearEps = 1e-11;
constexpr double kExtentEps = 1e-12;

bool within_extent(const LatLon& a, const LatLon& b, const LatLon& p, double eps) {
  return std::fmin(a.lon, b.lon) - eps <= p.lon && p.lon <= std::fmax(a.lon, b.lon) + eps &&
         std::fmin(a.lat, b.lat) - eps <= p.lat && p.lat <= std::fmax(a.lat, b.lat) + eps;
}
}  // namespace

int orientation(const LatLon& a, const LatLon& b, const LatLon& c) {
  const double v = (b.lon - a.lon) * (c.lat - a.lat) - (b.lat - a.lat) * (c.lon - a.lon);
  return v > kOrientEps ? 1 : (v < -kOrientEps ? -1 : 0);
}

bool point_on_segment(const LatLon& a, const LatLon& b, const LatLon& p) {
  const double cross = (b.lon - a.lon) * (p.lat - a.lat) - (b.lat - a.lat) * (p.lon - a.lon);
  if (std::fabs(cross) > kCollinearEps) return false;
  return within_extent(a, b, p, kExtentEps);
}

bool segments_intersect(const LatLon& a, const LatLon& b,
                        const LatLon& c, const LatLon& d) {
  const int o1 = orientation(a, b, c), o2 = orientation(a, b, d);
  const int o3 = orientation(c, d, a), o4 = orientation(c, d, b);
  if (o1 != o2 && o3 != o4) return true;                 // proper crossing
  // Collinear / touching cases: an endpoint of one lying on the other.
  if (o1 == 0 && within_extent(a, b, c, kOrientEps)) return true;
  if (o2 == 0 && within_extent(a, b, d, kOrientEps)) return true;
  if (o3 == 0 && within_extent(c, d, a, kOrientEps)) return true;
  if (o4 == 0 && within_extent(c, d, b, kOrientEps)) return true;
  return false;
}

bool segments_properly_cross(const LatLon& a, const LatLon& b,
                             const LatLon& c, const LatLon& d) {
  const int o1 = orientation(a, b, c), o2 = orientation(a, b, d);
  const int o3 = orientation(c, d, a), o4 = orientation(c, d, b);
  // All four must be strictly non-zero: a zero means an endpoint lies on the
  // other segment's line, which is a touch, not a transversal crossing.
  if (o1 == 0 || o2 == 0 || o3 == 0 || o4 == 0) return false;
  return o1 != o2 && o3 != o4;
}

}  // namespace safetrail::geo
