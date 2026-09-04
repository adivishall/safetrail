#include "safetrail/geo/point.hpp"
#include <cmath>

namespace safetrail::geo {

static constexpr double kEarthR = 6371008.8;   // mean radius, metres
static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
static constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;

bool LatLon::valid() const {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 &&
         !std::isnan(lat) && !std::isnan(lon);
}

// Haversine. We do not use the equirectangular approximation: at these
// latitudes over multi-km distances it is off by enough to matter when the
// hysteresis margins are 15-25 m.
//
// Longitude wraparound: this form needs no explicit normalisation of the
// longitude delta, and it is worth saying why rather than leaving it to luck.
// The delta enters only as sin(dl/2)^2 and, in bearing_deg, as sin(dl)/cos(dl).
// Those are periodic with period 2*pi, and sin(dl/2)^2 is invariant under
// dl -> dl +/- 2*pi, so 179.9 -> -179.9 (raw delta -359.8 deg) evaluates
// identically to the true +0.2 deg crossing. offset() is the one place that DOES
// need normalisation, because it produces a longitude rather than consuming one;
// see below. tests/geo/wraparound_test.cpp pins all three.
double distance_m(const LatLon& a, const LatLon& b) {
  const double p1 = a.lat * kDeg2Rad, p2 = b.lat * kDeg2Rad;
  const double dp = (b.lat - a.lat) * kDeg2Rad;
  const double dl = (b.lon - a.lon) * kDeg2Rad;
  const double s = std::sin(dp / 2) * std::sin(dp / 2) +
                   std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
  return 2.0 * kEarthR * std::asin(std::sqrt(s < 0 ? 0 : (s > 1 ? 1 : s)));
}

double bearing_deg(const LatLon& a, const LatLon& b) {
  const double p1 = a.lat * kDeg2Rad, p2 = b.lat * kDeg2Rad;
  const double dl = (b.lon - a.lon) * kDeg2Rad;
  const double y = std::sin(dl) * std::cos(p2);
  const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
  double d = std::atan2(y, x) * kRad2Deg;
  return d < 0 ? d + 360.0 : d;
}

// Normalise a longitude in degrees into (-180, 180]. offset() is the only
// producer of a longitude in this file, so it is the only place that needs it:
// l1 is already in (-180, 180] and atan2 returns (-pi, pi], so their sum can land
// anywhere in (-360, 360] and must be folded back onto the branch every other
// function in the engine assumes.
static double normalize_lon(double lon_deg) {
  while (lon_deg > 180.0) lon_deg -= 360.0;
  while (lon_deg <= -180.0) lon_deg += 360.0;
  return lon_deg;
}

LatLon offset(const LatLon& from, double bearing, double dist) {
  const double br = bearing * kDeg2Rad, p1 = from.lat * kDeg2Rad;
  const double l1 = from.lon * kDeg2Rad, ad = dist / kEarthR;
  const double p2 = std::asin(std::sin(p1) * std::cos(ad) +
                              std::cos(p1) * std::sin(ad) * std::cos(br));
  const double l2 = l1 + std::atan2(std::sin(br) * std::sin(ad) * std::cos(p1),
                                    std::cos(ad) - std::sin(p1) * std::sin(p2));
  return {p2 * kRad2Deg, normalize_lon(l2 * kRad2Deg)};
}

}  // namespace safetrail::geo
