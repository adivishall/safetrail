#pragma once
// Local tangent-plane projection: WGS84 degrees <-> local metres.
//
// Why this exists. Several places in the engine want to do PLANAR geometry --
// project a point onto a segment, interpolate along an edge, compare two
// distances. Doing that arithmetic directly in degrees is wrong in a specific,
// quantifiable way: a degree of latitude is ~111 km everywhere, but a degree of
// longitude is 111 km * cos(lat), so at Shillong (25.57 N) it is ~100 km. Treating
// the two axes as interchangeable stretches the east-west axis by 1/cos(lat) =
// 1.108, which skews every projection parameter t computed in degree space.
//
// The fix is not a geodesic library. It is to pick an origin, convert to a local
// east-north plane in metres, do the planar work there, and convert back.
//
// ── Error budget, measured ───────────────────────────────────────────────────
//
// The plane is the first-order linearisation of the same spherical metric
// geo::distance_m uses (same radius, same model -- see the .cpp for why mixing
// Earth models is a 2.8 m bug waiting to happen). The residual is therefore pure
// curvature error, and it depends on ONE thing: how far the points are from the
// plane's origin, not on how far they are from each other.
//
// Measured (tests/geo/projection_test.cpp prints these every run), the error
// follows err ~ 6.4e-8 * r^2 metres, where r is the greatest distance from the
// anchor. Quadruple the radius, sixteen times the error:
//
//   points within  200 m of the anchor  ->  ~2.6 mm worst case
//   points within    2 km of the anchor  ->  ~26 cm
//   points within   25 km of the anchor  ->  ~40 m
//
// The last row is the important one, and it is why the API takes an origin
// instead of exposing one global district plane: a plane anchored 25 km away is
// not good enough for anything. Anchored AT the geometry it is used on -- which
// is what point_segment_distance_m does, per segment, so r is the segment's own
// length -- the error is the first row: millimetres, roughly a thousand times
// below the 4 m open-sky GPS accuracy this project models and far below the
// 15-25 m hysteresis margins that consume it.
//
// So: haversine (geo::distance_m) remains the authority for DISTANCE. This
// header is for the planar arithmetic in between -- where the alternative is
// degree-space arithmetic that is wrong by 11%, not by 12 mm.
#include "safetrail/geo/point.hpp"

namespace safetrail::geo {

// An east-north tangent plane anchored at `origin`. Cheap to construct (one
// cosine), trivially copyable, and const once built -- so a caller can hoist it
// out of a loop over a polygon's edges.
class LocalPlane {
 public:
  LocalPlane() = default;
  explicit LocalPlane(const LatLon& origin);

  const LatLon& origin() const { return origin_; }

  Meters to_meters(const LatLon& p) const;
  LatLon to_latlon(const Meters& m) const;

  // Distance in the plane. Accurate to the budget above -- which is a statement
  // about distance from the ORIGIN, not about the separation being measured.
  double distance_m(const Meters& a, const Meters& b) const;

 private:
  LatLon origin_{};
  double m_per_deg_lat_ = 111132.0;
  double m_per_deg_lon_ = 111320.0;
};

// Distance from p to segment [a, b], in metres, computed in a local plane
// anchored at `a`. This is the primitive the geometry layer needs: projecting
// onto a segment requires a parameter t, and computing t in degree space biases
// it toward the latitude axis by 1/cos(lat) -- 11% at Shillong.
//
// Anchoring per segment rather than per polygon is deliberate: it keeps every
// evaluation in the first row of the error budget above, whatever the polygon's
// overall extent.
double point_segment_distance_m(const LatLon& p, const LatLon& a, const LatLon& b);

// Same, but also reports the closest point on the segment.
double point_segment_distance_m(const LatLon& p, const LatLon& a, const LatLon& b,
                                LatLon* closest);

}  // namespace safetrail::geo
