#pragma once
//
// Positions, in two deliberately incompatible types.
//
// LatLon and Meters are separate types with no implicit conversion because
// mixing them is the single most common bug in geospatial code. Degrees are not
// a unit of distance, and 0.001 degrees of longitude is a different distance in
// Shillong than it is in Chennai. The compiler should stop you.
//
#include <cstdint>

namespace safetrail::geo {

// ─── WGS84 geographic coordinate ────────────────────────────────────────────
struct LatLon {
  double lat = 0.0;   // degrees, [-90, 90]
  double lon = 0.0;   // degrees, [-180, 180]

  bool valid() const;
};

// ─── Projected planar coordinate, metres ────────────────────────────────────
// Local ENU (east-north-up) tangent plane. Valid only near its origin — good to
// well under a metre of error across a district, which is far below GPS noise,
// so the flat-earth approximation is free for our purposes. See projection.hpp.
struct Meters {
  double x = 0.0;   // east
  double y = 0.0;   // north
};

// ─── Position with measurement uncertainty ──────────────────────────────────
//
// GAP 1. Every existing implementation of this problem treats a GPS fix as an
// exact point and asks a yes/no containment question. Reality: 3-5 m accuracy in
// open sky, 20-50 m under multipath in steep or dense terrain. A tourist 10 m
// outside a restricted zone with a 30 m accuracy radius is not outside it, and
// reporting "outside" is a fabrication.
//
// So a position is a disc, not a point, and containment is three-valued.
// See containment.hpp.
//
struct UncertainPoint {
  LatLon  pos;
  double  accuracy_m = 0.0;   // 68% confidence radius, as reported by the fix
  int64_t t_ms       = 0;     // device clock, milliseconds since epoch

  // A fix with accuracy worse than this is not worth evaluating; it will produce
  // Uncertain against every zone it is near and only generate noise.
  static constexpr double UNUSABLE_ACCURACY_M = 150.0;

  bool usable() const { return accuracy_m > 0.0 && accuracy_m < UNUSABLE_ACCURACY_M; }
};

// ─── Free functions ─────────────────────────────────────────────────────────
// Great-circle distance. See haversine.hpp for the implementation and for why
// we do not use the equirectangular approximation at these latitudes.
double distance_m(const LatLon& a, const LatLon& b);

// Initial bearing a→b, degrees clockwise from true north.
double bearing_deg(const LatLon& a, const LatLon& b);

// Project a point d metres from `from` along `bearing`. Used by the mobility
// models and by trajectory extrapolation in the predictive alerting path.
LatLon offset(const LatLon& from, double bearing_deg, double distance_m);

}  // namespace safetrail::geo
