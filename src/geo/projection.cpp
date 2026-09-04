#include "safetrail/geo/projection.hpp"

#include <cmath>

namespace safetrail::geo {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
// Must match geo::distance_m's radius exactly -- see m_per_deg_lat below.
constexpr double kEarthR = 6371008.8;

// Metres per degree, on the SAME Earth model geo::distance_m uses: a sphere of
// mean radius 6371008.8 m.
//
// This matters more than it looks. The obvious thing to write here is the WGS84
// ellipsoidal series (111132.92 - 559.82*cos(2p) + ... for latitude), which is a
// better model of the real Earth -- and which disagrees with this project's
// spherical haversine by about 0.37% at Shillong's latitude. Two internally
// consistent models, mixed, produce a 2.8 m discrepancy over a 750 m segment:
// comparable to the open-sky GPS accuracy the whole design is built around, and
// arising from nothing but a modelling mismatch between two files.
//
// So the plane is deliberately the first-order LINEARISATION of the metric the
// engine already uses, not an independently better one. The residual is then pure
// curvature error, which is what the error budget in the header describes and
// what tests/geo/projection_test.cpp measures. If distance_m ever moves to an
// ellipsoidal model, this must move with it.
double m_per_deg_lat(double /*lat_deg*/) {
  return kEarthR * kDeg2Rad;                       // ~111194.9 m
}
double m_per_deg_lon(double lat_deg) {
  return kEarthR * kDeg2Rad * std::cos(lat_deg * kDeg2Rad);
}

// Shortest signed difference between two longitudes, in degrees, normalised to
// (-180, 180]. Without this, a segment spanning the antimeridian is measured the
// long way round the planet. Shillong never triggers it; a latent 40,000 km error
// in a geometry primitive is still a bug.
double lon_delta_deg(double from, double to) {
  double d = to - from;
  while (d > 180.0) d -= 360.0;
  while (d <= -180.0) d += 360.0;
  return d;
}
}  // namespace

LocalPlane::LocalPlane(const LatLon& origin)
    : origin_(origin),
      m_per_deg_lat_(m_per_deg_lat(origin.lat)),
      m_per_deg_lon_(m_per_deg_lon(origin.lat)) {}

Meters LocalPlane::to_meters(const LatLon& p) const {
  return Meters{lon_delta_deg(origin_.lon, p.lon) * m_per_deg_lon_,
                (p.lat - origin_.lat) * m_per_deg_lat_};
}

LatLon LocalPlane::to_latlon(const Meters& m) const {
  const double lat = origin_.lat + m.y / m_per_deg_lat_;
  double lon = origin_.lon + (m_per_deg_lon_ != 0.0 ? m.x / m_per_deg_lon_ : 0.0);
  while (lon > 180.0) lon -= 360.0;
  while (lon <= -180.0) lon += 360.0;
  return {lat, lon};
}

double LocalPlane::distance_m(const Meters& a, const Meters& b) const {
  const double dx = a.x - b.x, dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

namespace {
// Shared core: project p onto [a,b] in a plane anchored at `a`, clamp to the
// segment, and report both the distance and the clamped parameter.
double seg_distance(const LatLon& p, const LatLon& a, const LatLon& b, double* t_out,
                    const LocalPlane** plane_out, LocalPlane* storage) {
  *storage = LocalPlane(a);
  if (plane_out) *plane_out = storage;
  const Meters pm = storage->to_meters(p);
  const Meters bm = storage->to_meters(b);
  const double len2 = bm.x * bm.x + bm.y * bm.y;
  if (len2 < 1e-12) {                       // degenerate segment: both ends coincide
    if (t_out) *t_out = 0.0;
    return std::sqrt(pm.x * pm.x + pm.y * pm.y);
  }
  double t = (pm.x * bm.x + pm.y * bm.y) / len2;
  t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  if (t_out) *t_out = t;
  const double dx = pm.x - t * bm.x, dy = pm.y - t * bm.y;
  return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

double point_segment_distance_m(const LatLon& p, const LatLon& a, const LatLon& b) {
  LocalPlane plane;
  return seg_distance(p, a, b, nullptr, nullptr, &plane);
}

double point_segment_distance_m(const LatLon& p, const LatLon& a, const LatLon& b,
                                LatLon* closest) {
  LocalPlane plane;
  double t = 0.0;
  const double d = seg_distance(p, a, b, &t, nullptr, &plane);
  if (closest) {
    const Meters bm = plane.to_meters(b);
    *closest = plane.to_latlon(Meters{t * bm.x, t * bm.y});
  }
  return d;
}

}  // namespace safetrail::geo
